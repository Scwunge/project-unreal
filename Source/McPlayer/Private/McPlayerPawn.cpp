#include "McPlayerPawn.h"
#include "McBanner.h"
#include "McShulker.h"
#include "McCache.h"
#include "McBannerEntity.h"
#include "McBlockBreak.h"
#include "McBlockPlacement.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McEffects.h"
#include "McBlockTags.h"
#include "McEntityManager.h"
#include "McVehicle.h"
#include "McPhysics.h"
#include "McRaycast.h"
#include "McItemRegistry.h"
#include "McPlayerData.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "EngineUtils.h"
#include "McBlockWorld.h"
#include "McCombat.h"
#include "McXpOrb.h"
#include "Dom/JsonObject.h"
#include "McEntityActor.h"
#include "McChunk.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"

#if MC_WITH_RENDER
#include "McParticleComponent.h"
#include "McPlayerModel.h"
#endif

namespace
{
	constexpr float SecondsPerTick = 0.05f;

	// The block a body standing at Feet is on (a little below the feet, so a slab or a path counts), by name.
	FName GetBlockNameBelow(const FMcBlockWorld& World, const FVector& Feet)
	{
		const FMcStateId State = World.GetBlock(FIntVector(FMath::FloorToInt32(Feet.X), FMath::FloorToInt32(Feet.Y - 0.2), FMath::FloorToInt32(Feet.Z)));
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		return Registry.IsValidState(State) ? Registry.GetBlockOf(State).Name : NAME_None;
	}
	constexpr float StandingEyeHeight = 1.62f;
	constexpr float SneakingEyeHeight = 1.27f;
	constexpr float StandingHeight = 1.8f;
	constexpr float SneakingHeight = 1.5f;
	// A second press of Space within this many ticks toggles flying.
	constexpr int32 DoubleTapTicks = 7;
	// Never run more than this many ticks in one frame, so a long hitch doesn't freeze the game catching up.
	constexpr int32 MaxTicksPerFrame = 10;
	// Holding a button repeats: breaking every 5 ticks (creative), placing every 4.
	constexpr float BreakRepeatSeconds = 0.25f;
	constexpr float PlaceRepeatSeconds = 0.2f;
}

AMcPlayerPawn::AMcPlayerPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	// The actor sits at the player's feet; the camera at eye height above them.
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Feet"));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(RootComponent);
	Camera->bUsePawnControlRotation = true;
	// Minecraft's default Field of View setting is 70 degrees, measured vertically; DefaultEngine.ini sets
	// the local player to keep the vertical angle fixed so this matches.
	Camera->SetFieldOfView(70.0f);
	Camera->SetRelativeLocation(FVector(0.0, 0.0, StandingEyeHeight * McCoords::UnitsPerBlock));

	Data = CreateDefaultSubobject<UMcPlayerDataComponent>(TEXT("PlayerData"));

	// The owning machine moves the player and tells the server; everyone else sees the server's copy.
	bReplicates = true;
	SetReplicatingMovement(true);

#if MC_WITH_RENDER
	// The body other players see. It stays hidden on the machine that plays this pawn.
	Model = CreateDefaultSubobject<UMcPlayerModelComponent>(TEXT("Body"));
	Model->SetupAttachment(RootComponent);
#endif
}

void AMcPlayerPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMcPlayerPawn, McLookYaw, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AMcPlayerPawn, McLookPitch, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AMcPlayerPawn, bSneakPose, COND_SkipOwner);
	DOREPLIFETIME(AMcPlayerPawn, RidingId);
}

void AMcPlayerPawn::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && Data)
	{
		// Survival unless the server was started with -McCreative.
		Data->bCreative = FParse::Param(FCommandLine::Get(), TEXT("McCreative"));
		if (Data->bCreative)
		{
			Data->FillCreativeHotbar();
		}
	}
}

void AMcPlayerPawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	const FRotator Start = GetActorRotation();
	Yaw = Start.Yaw;
	Pitch = Start.Pitch;

	if (APlayerController* Player = Cast<APlayerController>(NewController))
	{
		Player->SetInputMode(FInputModeGameOnly());
		Player->bShowMouseCursor = false;
	}
}

void AMcPlayerPawn::SetView(const FVector& EyePos, float McYaw, float McPitch)
{
	// Minecraft's yaw 0 looks along +z (Unreal +Y, yaw 90) and grows clockwise seen from above, as Unreal's
	// does, so it is a fixed offset. Pitch is flipped.
	Yaw = FMath::Fmod(McYaw + 90.0f, 360.0f);
	Pitch = FMath::Clamp(-McPitch, -89.9f, 89.9f);
	bFlying = true;
	bSneaking = false;
	Body.Height = StandingHeight;
	SetActorLocation(McCoords::BlockToUnreal(EyePos - FVector(0.0, StandingEyeHeight, 0.0)));
	if (AController* PawnController = GetController())
	{
		PawnController->SetControlRotation(FRotator(Pitch, Yaw, 0.0f));
	}
}

float AMcPlayerPawn::GetEyeHeight() const
{
	return bSneaking ? SneakingEyeHeight : StandingEyeHeight;
}

void AMcPlayerPawn::GameTick(const FMcMoveInput& Input)
{
	PreviousPosition = Body.Position;
	PreviousEyeHeight = GetEyeHeight();

	// Sitting in a minecart or boat: the vehicle carries the body (the server moves it); the keys steer it.
	if (RidingId != 0)
	{
		bSprinting = false;
		if (HasAuthority())
		{
			RideForward = Input.Forward;
			RideStrafe = Input.Strafe;
			if (Input.bSneak)
			{
				ServerDismount();
			}
		}
		else
		{
			ServerRideInput(Input.Forward, Input.Strafe, Yaw);
			if (Input.bSneak)
			{
				ServerWantDismount();
			}
		}
		return;
	}

	// Wait for the ground to arrive before moving.
	const FIntPoint Chunk(McCoords::BlockToChunk(FMath::FloorToInt32(Body.Position.X)), McCoords::BlockToChunk(FMath::FloorToInt32(Body.Position.Z)));
	if (!BlockWorld || !BlockWorld->HasChunk(Chunk))
	{
		return;
	}

	// Space tapped twice quickly starts or stops flying; landing stops it too.
	if (Input.bJump && !bJumpWasDown && bMayFly)
	{
		if (FlyToggleTicks > 0)
		{
			bFlying = !bFlying;
			FlyToggleTicks = 0;
		}
		else
		{
			FlyToggleTicks = DoubleTapTicks;
		}
	}
	bJumpWasDown = Input.bJump;
	FlyToggleTicks = FMath::Max(0, FlyToggleTicks - 1);

	// Sneaking shrinks the hitbox; standing up again needs the room.
	const bool bWantSneak = Input.bSneak && !bFlying;
	if (bWantSneak != bSneaking)
	{
		FMcBody Resized = Body;
		Resized.Height = bWantSneak ? SneakingHeight : StandingHeight;
		if (bWantSneak || !McPhysics::Collides(*BlockWorld, Resized.GetBox()))
		{
			bSneaking = bWantSneak;
			Body.Height = Resized.Height;
		}
	}

	// Sprinting lasts while moving forward, until stopping or running into a wall.
	if (Input.bSprint && Input.Forward > 0.0f && !bSneaking)
	{
		bSprinting = true;
	}
	else if (Input.Forward <= 0.0f || bSneaking || Body.bHorizontalCollision)
	{
		bSprinting = false;
	}

	FMcMoveInput Move = Input;
	Move.bSprint = bSprinting;
	Move.bFlying = bFlying;
	Move.bSneak = bFlying ? Input.bSneak : bSneaking;
	if (Data)
	{
		Move.Speed *= McEffects::GetSpeedFactor(Data->Stats);
		Move.JumpBonus = McEffects::GetJumpBonus(Data->Stats);
	}
	McPhysics::TickPlayer(*BlockWorld, Body, Move);
	if (bFlying && Body.bOnGround)
	{
		bFlying = false;
	}

	// Like Minecraft, the client moves its own player and the server takes its word for now; checks come with
	// survival (M7).
	bServerPositionKnown = true;
	if (!HasAuthority())
	{
		ServerMove(Body.Position, Yaw, Pitch, (bSneaking ? 1 : 0) | (Body.bOnGround ? 2 : 0) | (bSprinting ? 4 : 0) | (bFlying ? 8 : 0));
	}
}

void AMcPlayerPawn::ServerMove_Implementation(FVector FeetPos, float NewYaw, float NewPitch, uint8 Flags)
{
	if (FeetPos.ContainsNaN() || !FMath::IsFinite(NewYaw) || !FMath::IsFinite(NewPitch) || bServerAwaitingTeleport || RidingId != 0)
	{
		return;
	}
	Body.Position = FeetPos;
	Body.bOnGround = (Flags & 2) != 0;
	bServerSprint = (Flags & 4) != 0;
	bServerFlying = (Flags & 8) != 0;
	bServerPositionKnown = true;
	LastLocation = McCoords::BlockToUnreal(FeetPos);
	SetActorLocationAndRotation(LastLocation, FRotator(0.0f, NewYaw, 0.0f));
	McLookYaw = NewYaw - 90.0f;
	McLookPitch = -NewPitch;
	bSneakPose = (Flags & 1) != 0;
}

void AMcPlayerPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

#if MC_WITH_RENDER
	if (UMcPlayerModelComponent* Figure = Model.Get())
	{
		bool bShowSelf = false;
#if !UE_BUILD_SHIPPING
		float SelfShotCheck = 0.0f;
		bShowSelf = FParse::Value(FCommandLine::Get(), TEXT("-McSelfShot="), SelfShotCheck);
#endif
		Figure->SetShown(!IsLocallyControlled() || bShowSelf);
		Figure->SetLook(McLookYaw, McLookPitch, bSneakPose);
	}
#endif

	if (HasAuthority())
	{
		// The rules run 20 times a second whoever the player is.
		ServerAccumulator += DeltaSeconds;
		for (int32 Ticks = 0; ServerAccumulator >= SecondsPerTick && Ticks < MaxTicksPerFrame; ++Ticks)
		{
			ServerAccumulator -= SecondsPerTick;
			ServerGameTick();
			if (IsActorBeingDestroyed() || !IsValid(this))
			{
				return;
			}
		}
		ServerAccumulator = FMath::Min(ServerAccumulator, SecondsPerTick);
	}

	if (!IsLocallyControlled())
	{
		return;
	}
	// Creative mode flies; survival walks.
	bMayFly = Data && Data->bCreative;
	const bool bBlocked = Data && (Data->HasWindow() || Data->Stats.Health <= 0.0f);

	// Moved by someone else (spawning, a teleport, the dev camera): start from there.
	if (!GetActorLocation().Equals(LastLocation, 0.01))
	{
		Body.Position = McCoords::UnrealToBlock(GetActorLocation());
		Body.Velocity = FVector::ZeroVector;
		PreviousPosition = Body.Position;
	}

	APlayerController* Player = Cast<APlayerController>(GetController());
	FMcMoveInput Input;
	if (Player && Player->IsLocalController())
	{
		// While a window is open the mouse and keys belong to it: no looking, walking or hotbar keys.
		if (!bBlocked)
		{
			// Look. Kept here rather than in the controller's input scaling so the feel doesn't depend on ini settings.
			float MouseX = 0.0f, MouseY = 0.0f;
			Player->GetInputMouseDelta(MouseX, MouseY);
			Yaw = FMath::Fmod(Yaw + MouseX * MouseSensitivity, 360.0f);
			Pitch = FMath::Clamp(Pitch + MouseY * MouseSensitivity, -89.9f, 89.9f);
			Player->SetControlRotation(FRotator(Pitch, Yaw, 0.0f));

			auto Key = [Player](const FKey& K) { return Player->IsInputKeyDown(K) ? 1.0f : 0.0f; };
			Input.Forward = Key(EKeys::W) - Key(EKeys::S);
			Input.Strafe = Key(EKeys::A) - Key(EKeys::D);
			Input.bJump = Player->IsInputKeyDown(EKeys::SpaceBar);
			Input.bSneak = Player->IsInputKeyDown(EKeys::LeftShift);
			Input.bSprint = Player->IsInputKeyDown(EKeys::LeftControl);
		}
	}
	Input.Yaw = Yaw - 90.0f;
	McLookYaw = Yaw - 90.0f;
	McLookPitch = -Pitch;
	bSneakPose = bSneaking;

	TickAccumulator += DeltaSeconds;
	int32 Ticks = 0;
	while (TickAccumulator >= SecondsPerTick)
	{
		TickAccumulator -= SecondsPerTick;
		if (++Ticks <= MaxTicksPerFrame)
		{
			GameTick(Input);
		}
	}

	// Draw between the last two ticks.
	const float Alpha = TickAccumulator / SecondsPerTick;
	const FVector Feet = FMath::Lerp(PreviousPosition, Body.Position, static_cast<double>(Alpha));
	LastLocation = McCoords::BlockToUnreal(Feet);
	SetActorLocation(LastLocation);
	const float EyeHeight = FMath::Lerp(PreviousEyeHeight, GetEyeHeight(), Alpha);
	Camera->SetRelativeLocation(FVector(0.0, 0.0, EyeHeight * McCoords::UnitsPerBlock));
#if !UE_BUILD_SHIPPING
	// Development: -McSelfShot=<blocks> puts the camera that far in front of the player, looking back at the player
	// model, for a picture of the skin.
	float SelfShot = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("-McSelfShot="), SelfShot) && SelfShot > 0.0f)
	{
		Camera->bUsePawnControlRotation = false;
		const FRotator Look(0.0f, Yaw, 0.0f);
		const FVector Eye = GetActorLocation() + FVector(0.0, 0.0, EyeHeight * McCoords::UnitsPerBlock * 0.9);
		Camera->SetWorldLocation(Eye + Look.Vector() * SelfShot * McCoords::UnitsPerBlock);
		Camera->SetWorldRotation(FRotator(0.0, Yaw + 180.0, 0.0));
	}
#endif

	UpdateTarget();
#if MC_WITH_RENDER
	// The block being dug chips, every tick (survival; creative breaks at once).
	if (Ticks > 0 && DigSent.IsSet() && Target.IsSet() && Target->Block == DigSent.GetValue())
	{
		if (UMcParticleComponent* Particles = UMcParticleComponent::FindFor(BlockWorld))
		{
			static const FIntVector Faces[6] = { FIntVector(0, -1, 0), FIntVector(0, 1, 0), FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0) };
			for (int32 Tick = 0; Tick < FMath::Min(Ticks, MaxTicksPerFrame); ++Tick)
			{
				Particles->BlockHit(Target->Block, Faces[FMath::Clamp(Target->Face, 0, 5)]);
			}
		}
	}
#endif
	BreakCooldown -= DeltaSeconds;
	PlaceCooldown -= DeltaSeconds;
	if (Player && Player->IsLocalController())
	{
		if (bBlocked)
		{
			StopDigging();
			if (bUsingSent)
			{
				bUsingSent = false;
				ServerSetUsing(false);
			}
		}
		else
		{
			UseBlocks(*Player);
		}
	}

	ShowPosition();
}

void AMcPlayerPawn::UpdateTarget()
{
	Target.Reset();
	if (!BlockWorld)
	{
		return;
	}
	// Minecraft's look vector: yaw 0 is +z, 90 is -x; positive pitch looks down.
	const double McYaw = FMath::DegreesToRadians(static_cast<double>(Yaw - 90.0f));
	const double McPitch = FMath::DegreesToRadians(static_cast<double>(-Pitch));
	const FVector Look(-FMath::Sin(McYaw) * FMath::Cos(McPitch), -FMath::Sin(McPitch), FMath::Cos(McYaw) * FMath::Cos(McPitch));
	const FVector Eye = McCoords::UnrealToBlock(Camera->GetComponentLocation());
	Target = McRaycast::Trace(*BlockWorld, Eye, Look, bMayFly ? McRaycast::CreativeReach : McRaycast::SurvivalReach);
}

void AMcPlayerPawn::StopDigging()
{
	if (DigSent.IsSet())
	{
		DigSent.Reset();
		ServerDig(FIntVector::ZeroValue, false);
	}
}

void AMcPlayerPawn::UseBlocks(APlayerController& Player)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!Data)
	{
		return;
	}
	const bool bCreative = Data->bCreative;

	// Choose a slot: number keys, or the wheel (down moves right, as in Minecraft).
	static const FKey NumberKeys[HotbarSize] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 Slot = 0; Slot < HotbarSize; ++Slot)
	{
		if (Player.WasInputKeyJustPressed(NumberKeys[Slot]))
		{
			SelectedSlot = Slot;
		}
	}
	if (Player.WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		SelectedSlot = UMcPlayerDataComponent::Scroll(SelectedSlot, 1);
	}
	if (Player.WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		SelectedSlot = UMcPlayerDataComponent::Scroll(SelectedSlot, -1);
	}
	if (SelectedSlot != LastSelectedSent)
	{
		LastSelectedSent = SelectedSlot;
		Data->SelectSlot(SelectedSlot);
	}

	// Food in hand is eaten by holding the use button, when there is room for it.
	const FMcItemStack& Held = Data->GetSelected();
	FMcFoodData Food;
	const bool bHoldingFood = !Held.IsEmpty() && FMcItemRegistry::Get().GetFood(Held.Item, Food);
	const bool bEating = bHoldingFood && !bCreative && Player.IsInputKeyDown(EKeys::RightMouseButton) && McSurvival::CanEat(Data->Stats, Food);
	if (bEating != bUsingSent)
	{
		bUsingSent = bEating;
		ServerSetUsing(bEating);
	}

	// A click on a mob in front of the block being looked at hits the mob instead.
	if (AttackHandler && Player.WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		const double McYaw = FMath::DegreesToRadians(static_cast<double>(Yaw - 90.0f));
		const double McPitch = FMath::DegreesToRadians(static_cast<double>(-Pitch));
		const FVector Look(-FMath::Sin(McYaw) * FMath::Cos(McPitch), -FMath::Sin(McPitch), FMath::Cos(McYaw) * FMath::Cos(McPitch));
		const FVector Eye = McCoords::UnrealToBlock(Camera->GetComponentLocation());
		constexpr double AttackReach = 3.0;
		const double BlockDistance = Target.IsSet() ? FVector::Dist(Eye, Target->Point) : AttackReach;
		for (TActorIterator<AMcEntityActor> It(GetWorld()); It; ++It)
		{
			const FVector Feet = McCoords::UnrealToBlock(It->GetActorLocation());
			const FBox Box(Feet - FVector(It->Size.X * 0.5, 0.0, It->Size.X * 0.5), Feet + FVector(It->Size.X * 0.5, It->Size.Y, It->Size.X * 0.5));
			FVector HitPoint, HitNormal;
			float Time = 0.0f;
			if (!It->IsHidden() && It->Kind != TEXT("minecraft:item") && FMath::LineExtentBoxIntersection(Box, Eye, Eye + Look * AttackReach, FVector::ZeroVector, HitPoint, HitNormal, Time)
				&& Time * AttackReach < BlockDistance)
			{
				AttackHandler(Look);
				BreakCooldown = BreakRepeatSeconds;
				StopDigging();
				return;
			}
		}
	}

	// A click on a minecart or boat in front of the block being looked at gets in.
	if (RidingId == 0 && Player.WasInputKeyJustPressed(EKeys::RightMouseButton))
	{
		const double McYaw = FMath::DegreesToRadians(static_cast<double>(Yaw - 90.0f));
		const double McPitch = FMath::DegreesToRadians(static_cast<double>(-Pitch));
		const FVector Look(-FMath::Sin(McYaw) * FMath::Cos(McPitch), -FMath::Sin(McPitch), FMath::Cos(McYaw) * FMath::Cos(McPitch));
		const FVector Eye = McCoords::UnrealToBlock(Camera->GetComponentLocation());
		constexpr double RideReach = 3.0;
		const double BlockDistance = Target.IsSet() ? FVector::Dist(Eye, Target->Point) : RideReach;
		for (TActorIterator<AMcEntityActor> It(GetWorld()); It; ++It)
		{
			if (It->Kind != FMcMinecartEntity::StaticType() && It->Kind != FMcBoatEntity::StaticType())
			{
				continue;
			}
			const FVector Feet = McCoords::UnrealToBlock(It->GetActorLocation());
			const FBox Box(Feet - FVector(It->Size.X * 0.5, 0.0, It->Size.X * 0.5), Feet + FVector(It->Size.X * 0.5, It->Size.Y, It->Size.X * 0.5));
			FVector HitPoint, HitNormal;
			float Time = 0.0f;
			if (!It->IsHidden() && FMath::LineExtentBoxIntersection(Box, Eye, Eye + Look * RideReach, FVector::ZeroVector, HitPoint, HitNormal, Time) && Time * RideReach < BlockDistance)
			{
				ServerTryRide(Look);
				return;
			}
		}
	}

	// An empty bucket takes the water or lava source it is aimed at, even past the blocks the crosshair would stop on.
	if (Player.WasInputKeyJustPressed(EKeys::RightMouseButton) && Data->Inventory->Get(SelectedSlot).Item == TEXT("minecraft:bucket"))
	{
		const double McYaw = FMath::DegreesToRadians(static_cast<double>(Yaw - 90.0f));
		const double McPitch = FMath::DegreesToRadians(static_cast<double>(-Pitch));
		const FVector Look(-FMath::Sin(McYaw) * FMath::Cos(McPitch), -FMath::Sin(McPitch), FMath::Cos(McYaw) * FMath::Cos(McPitch));
		const TOptional<FMcBlockHit> Fluid = McRaycast::Trace(*BlockWorld, McCoords::UnrealToBlock(Camera->GetComponentLocation()), Look,
			bMayFly ? McRaycast::CreativeReach : McRaycast::SurvivalReach, true);
		if (Fluid.IsSet() && McRaycast::IsFluidSource(Fluid->State))
		{
			ServerUseItemOn(Fluid->Block, Fluid->Face);
			return;
		}
	}

	if (!Target.IsSet())
	{
		StopDigging();
		return;
	}
	const FMcBlockHit Hit = Target.GetValue();

	// Pick block, in creative: the targeted block into the hotbar, or select it if it's already there.
	if (bCreative && Player.WasInputKeyJustPressed(EKeys::MiddleMouseButton))
	{
		const FName Picked = McBlockBreak::GetItemForBlock(Hit.State);
		if (!Picked.IsNone())
		{
			int32 Existing = INDEX_NONE;
			for (int32 Slot = 0; Slot < HotbarSize; ++Slot)
			{
				if (Data->Inventory->Get(Slot).Item == Picked)
				{
					Existing = Slot;
					break;
				}
			}
			if (Existing != INDEX_NONE)
			{
				SelectedSlot = Existing;
				LastSelectedSent = Existing;
				Data->SelectSlot(Existing);
			}
			else
			{
				ServerPickItem(Picked);
			}
		}
	}

	// Break. In creative: at once on a click, then again every few ticks while held. In survival the server digs, as
	// long as the button stays down on the block.
	const bool bBreakHeld = Player.IsInputKeyDown(EKeys::LeftMouseButton);
	if (!bCreative)
	{
		if (!bBreakHeld)
		{
			StopDigging();
		}
		else if (!DigSent.IsSet() || DigSent.GetValue() != Hit.Block)
		{
			DigSent = Hit.Block;
			ServerDig(Hit.Block, true);
		}
		if (bBreakHeld)
		{
			return;
		}
	}
	else if (SetBlockHandler && (Player.WasInputKeyJustPressed(EKeys::LeftMouseButton) || (bBreakHeld && BreakCooldown <= 0.0f)))
	{
		const FMcStateId Air = Registry.FindDefaultState(TEXT("minecraft:air")).Get(0);
		// Doors and beds go as a whole: the other half with them.
		const FName Half = Registry.GetPropertyValue(Hit.State, TEXT("half"));
		const FIntVector Other = Half == TEXT("upper") ? Hit.Block - FIntVector(0, 1, 0) : Half == TEXT("lower") ? Hit.Block + FIntVector(0, 1, 0) : Hit.Block;
		if (Other != Hit.Block && Registry.GetBlockOf(BlockWorld->GetBlock(Other)).Name == Registry.GetBlockOf(Hit.State).Name)
		{
			SetBlockHandler(Other, Air);
		}
		SetBlockHandler(Hit.Block, Air);
		BreakCooldown = BreakRepeatSeconds;
		return;
	}

	// Use, or place: the same, every 4 ticks while held.
	const bool bPlaceClick = Player.WasInputKeyJustPressed(EKeys::RightMouseButton);
	if (!bPlaceClick && !(Player.IsInputKeyDown(EKeys::RightMouseButton) && PlaceCooldown <= 0.0f))
	{
		return;
	}
	// A crafting table, chest or furnace opens, and a lever, button, door, trapdoor or gate moves, rather than takes a
	// block, unless the player sneaks with something in hand. Iron doors and trapdoors don't move for a hand.
	static const TSet<FName> Usable = { TEXT("minecraft:crafting_table"), TEXT("minecraft:chest"), TEXT("minecraft:trapped_chest"), TEXT("minecraft:barrel"),
		TEXT("minecraft:furnace"), TEXT("minecraft:blast_furnace"), TEXT("minecraft:smoker"), TEXT("minecraft:lever"), TEXT("minecraft:enchanting_table"),
		TEXT("minecraft:stonecutter"), TEXT("minecraft:smithing_table"), TEXT("minecraft:grindstone"), TEXT("minecraft:anvil"), TEXT("minecraft:chipped_anvil"),
		TEXT("minecraft:damaged_anvil"), TEXT("minecraft:brewing_stand"), TEXT("minecraft:hopper"), TEXT("minecraft:dispenser"), TEXT("minecraft:dropper") };
	const FName Clicked = Registry.GetBlockOf(Hit.State).Name;
	const FString ClickedName = Clicked.ToString();
	const bool bMoves = ClickedName.EndsWith(TEXT("_button")) || ClickedName.EndsWith(TEXT("_fence_gate"))
		|| ((ClickedName.EndsWith(TEXT("_door")) || ClickedName.EndsWith(TEXT("_trapdoor"))) && !ClickedName.Contains(TEXT("iron")));
	if (bPlaceClick && (Usable.Contains(Clicked) || bMoves) && !(bSneaking && !Held.IsEmpty()))
	{
		ServerUseBlock(Hit.Block);
		return;
	}
	// A bucket of water or lava pours into the block clicked (if it can be replaced) or the one beside it.
	if (bPlaceClick && (Held.Item == TEXT("minecraft:water_bucket") || Held.Item == TEXT("minecraft:lava_bucket")))
	{
		ServerUseItemOn(Hit.Block, Hit.Face);
		return;
	}
	// A boat goes on water (the fluid the crosshair would pass through), a minecart on a rail.
	if (bPlaceClick && (Held.Item.ToString().EndsWith(TEXT("_boat")) || Held.Item.ToString().EndsWith(TEXT("_raft"))))
	{
		const double McYaw = FMath::DegreesToRadians(static_cast<double>(Yaw - 90.0f));
		const double McPitch = FMath::DegreesToRadians(static_cast<double>(-Pitch));
		const FVector Look(-FMath::Sin(McYaw) * FMath::Cos(McPitch), -FMath::Sin(McPitch), FMath::Cos(McYaw) * FMath::Cos(McPitch));
		const TOptional<FMcBlockHit> Fluid = McRaycast::Trace(*BlockWorld, McCoords::UnrealToBlock(Camera->GetComponentLocation()), Look,
			bMayFly ? McRaycast::CreativeReach : McRaycast::SurvivalReach, true);
		if (Fluid.IsSet())
		{
			ServerUseItemOn(Fluid->Block, Fluid->Face);
			return;
		}
	}
	// Flint and steel lights the block clicked; an eye of ender goes in an end portal frame.
	if (bPlaceClick && !Held.IsEmpty() && (Held.Item == TEXT("minecraft:flint_and_steel") || Held.Item == TEXT("minecraft:minecart")
		|| (Held.Item == TEXT("minecraft:ender_eye") && Registry.GetBlockOf(Hit.State).Name == TEXT("minecraft:end_portal_frame"))))
	{
		ServerUseItemOn(Hit.Block, Hit.Face);
		return;
	}
	if (Held.IsEmpty() || bHoldingFood)
	{
		return;
	}
	const TOptional<FMcStateId> Item = McBlockBreak::GetBlockForItem(Held.Item);
	if (!Item)
	{
		return;
	}
	PlaceCooldown = PlaceRepeatSeconds;
	const TArray<FMcPlacedBlock> Placed = McBlockPlacement::GetPlacement(*BlockWorld, Item.GetValue(), Hit, Yaw - 90.0f, -Pitch);
	// Nothing goes where it would trap the player.
	const FBox Self = Body.GetBox().ExpandBy(-1e-4);
	for (const FMcPlacedBlock& Block : Placed)
	{
		for (const FBox3f& Local : FMcBlockShapes::Get().GetCollision(Block.State))
		{
			const FBox Box(FVector(Block.Pos) + FVector(Local.Min), FVector(Block.Pos) + FVector(Local.Max));
			if (Box.Intersect(Self))
			{
				return;
			}
		}
	}
	if (bCreative)
	{
		if (SetBlockHandler)
		{
			for (const FMcPlacedBlock& Block : Placed)
			{
				SetBlockHandler(Block.Pos, Block.State);
				if (CarriesPlacedData(Held.Item))
				{
					ServerAttachBanner(Block.Pos);
				}
			}
		}
		return;
	}
	TArray<FMcNetBlockChange> Request;
	for (const FMcPlacedBlock& Block : Placed)
	{
		FMcNetBlockChange& Change = Request.AddDefaulted_GetRef();
		Change.Pos = Block.Pos;
		Change.State = static_cast<uint16>(Block.State);
	}
	if (!Request.IsEmpty())
	{
		ServerPlace(Request);
	}
}

// ---- The server's side ----

float AMcPlayerPawn::ServerHurt(float Amount, McSurvival::EDamage Kind, const FVector* From)
{
	if (!HasAuthority() || !Data || Data->bCreative || bServerDead)
	{
		return 0.0f;
	}
	const float Taken = McSurvival::Hurt(Data->Stats, Amount, Kind, Data->Armor.Get(), &GameRules);
	if (Taken > 0.0f && From && Data->Stats.Health > 0.0f)
	{
		const FVector Push = McCombat::GetKnockback(Body.Position, *From, FVector::ZeroVector);
		if (IsLocallyControlled())
		{
			ApplyKnockback(Push);
		}
		else
		{
			ClientKnockback(Push);
		}
	}
	return Taken;
}

void AMcPlayerPawn::ApplyKnockback(const FVector& Velocity)
{
	if (Velocity.ContainsNaN())
	{
		return;
	}
	Body.Velocity.X = Velocity.X;
	Body.Velocity.Y = Velocity.Y;
	Body.Velocity.Z = Velocity.Z;
	Body.bOnGround = false;
}

void AMcPlayerPawn::ClientKnockback_Implementation(FVector Velocity)
{
	ApplyKnockback(Velocity);
}

void AMcPlayerPawn::ServerSwing(const FVector& Look)
{
	if (!HasAuthority() || !Data || bServerDead || !MobAttack || Look.ContainsNaN() || Look.IsNearlyZero())
	{
		return;
	}
	FMcItemStack& Held = Data->Inventory->GetMutable(Data->SelectedSlot);
	const McCombat::FAttack Attack = McCombat::GetAttack(Held);
	const float Strength = McCombat::GetStrength(ServerAttackTicks, Attack.Speed);
	// Falling, and not in water or on a ladder, makes a critical hit.
	const bool bCritical = !Body.bOnGround && ServerFall.Distance > 0.0f && !Body.bInWater && !Body.bClimbing && !bServerFlying;
	const float Damage = McCombat::GetDamage(FMath::Max(0.0f, Attack.Damage + McEffects::GetAttackBonus(Data->Stats)), Strength, bCritical);
	const FVector Eye = Body.Position + FVector(0.0, GetEyeHeight(), 0.0);
	ServerAttackTicks = 0;
	if (!MobAttack(Eye, Look.GetSafeNormal(), 3.0, Damage, Data->bCreative))
	{
		return;
	}
	if (!Data->bCreative)
	{
		McSurvival::AddExhaustion(Data->Stats, McSurvival::AttackExhaustion);
		const int32 Wear = McCombat::GetWear(Held);
		if (Wear > 0 && Held.HurtAndBreak(Wear))
		{
			Held.Clear();
		}
	}
}

void AMcPlayerPawn::ServerDropXp(int32 Points)
{
	FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr;
	if (Entities && Points > 0)
	{
		FMcXpOrb::Spawn(*Entities, Body.Position + FVector(0.0, 0.5, 0.0), Points, DropRandom);
	}
}

void AMcPlayerPawn::ApplyTeleport(const FVector& Feet, float McYaw)
{
	Body.Position = Feet;
	Body.Velocity = FVector::ZeroVector;
	PreviousPosition = Feet;
	LastLocation = McCoords::BlockToUnreal(Feet);
	Yaw = FMath::Fmod(McYaw + 90.0f, 360.0f);
	McLookYaw = McYaw;
	SetActorLocation(LastLocation);
	if (AController* PawnController = GetController())
	{
		PawnController->SetControlRotation(FRotator(Pitch, Yaw, 0.0f));
	}
}

void AMcPlayerPawn::ServerTeleport(const FVector& Feet, float McYaw)
{
	if (!HasAuthority())
	{
		return;
	}
	ApplyTeleport(Feet, McYaw);
	ServerFall = McSurvival::FFallTracker();
	ServerPreviousPosition = Feet;
	if (!IsLocallyControlled())
	{
		bServerAwaitingTeleport = true;
		ClientTeleport(Feet, McYaw);
	}
}

void AMcPlayerPawn::ClientTeleport_Implementation(FVector Feet, float McYaw)
{
	ApplyTeleport(Feet, McYaw);
	ServerTeleported();
}

void AMcPlayerPawn::ServerTeleported_Implementation()
{
	bServerAwaitingTeleport = false;
}

void AMcPlayerPawn::ServerUseItemOn_Implementation(FIntVector Pos, int32 Face)
{
	if (!OnUseItemOn || bServerDead || !Data || Face < 0 || Face > 5)
	{
		return;
	}
	const FVector Eye = Body.Position + FVector(0.0, GetEyeHeight(), 0.0);
	if (FVector::Dist(Eye, FVector(Pos) + FVector(0.5)) > ServerReach)
	{
		return;
	}
	static const FIntVector Faces[6] = { FIntVector(0, -1, 0), FIntVector(0, 1, 0), FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0) };
	if (OnUseItemOn(*this, Pos, Pos + Faces[Face]) && !Data->bCreative)
	{
		FMcItemStack& Held = Data->Inventory->GetMutable(Data->SelectedSlot);
		if (Held.Item == TEXT("minecraft:ender_eye") || Held.Item == TEXT("minecraft:minecart") || Held.Item.ToString().EndsWith(TEXT("_boat")) || Held.Item.ToString().EndsWith(TEXT("_raft")))
		{
			if (--Held.Count <= 0)
			{
				Held.Clear();
			}
		}
		else if (Held.HurtAndBreak(1))
		{
			Held.Clear();
		}
	}
}

void AMcPlayerPawn::ServerRide(int32 VehicleId)
{
	if (!HasAuthority() || bServerDead)
	{
		return;
	}
	if (RidingId != 0)
	{
		ServerDismount();
	}
	RidingId = VehicleId;
	RideForward = 0.0f;
	RideStrafe = 0.0f;
	bSprinting = false;
	bServerSprint = false;
	ServerFall = McSurvival::FFallTracker();
	Body.Velocity = FVector::ZeroVector;
}

void AMcPlayerPawn::ServerDismount()
{
	if (!HasAuthority() || RidingId == 0)
	{
		return;
	}
	FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr;
	FMcEntity* Found = Entities ? Entities->Find(RidingId) : nullptr;
	RidingId = 0;
	if (!Found || !Found->IsVehicle() || !BlockWorld)
	{
		return;
	}
	static_cast<FMcVehicle*>(Found)->ClearRider();
	// Out beside it, on whichever side has room; failing that, on top.
	const FVector Centre = Found->Body.Position;
	const FVector Spots[] = { FVector(1.1, 0.0, 0.0), FVector(-1.1, 0.0, 0.0), FVector(0.0, 0.0, 1.1), FVector(0.0, 0.0, -1.1), FVector(0.0, 0.0, 0.0) };
	for (const FVector& Spot : Spots)
	{
		FMcBody Moved = Body;
		Moved.Position = Centre + Spot + FVector(0.0, 0.1, 0.0);
		if (!McPhysics::Collides(*BlockWorld, Moved.GetBox()))
		{
			ServerTeleport(Moved.Position, McLookYaw);
			break;
		}
	}
	ServerFall = McSurvival::FFallTracker();
}

void AMcPlayerPawn::ServerTickRiding()
{
	FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr;
	FMcEntity* Found = Entities ? Entities->Find(RidingId) : nullptr;
	if (!Found || !Found->IsVehicle() || Found->bRemoved)
	{
		RidingId = 0;
		return;
	}
	FMcVehicle* Vehicle = static_cast<FMcVehicle*>(Found);
	if (Vehicle->ConsumeEject())
	{
		ServerDismount();
		return;
	}
	Vehicle->SetRider(RideForward, RideStrafe, McLookYaw);
	const FVector Seat = Vehicle->GetSeat();
	Body.Position = Seat;
	Body.Velocity = FVector::ZeroVector;
	Body.bOnGround = true;
	ServerPreviousPosition = Seat;
	ServerFall = McSurvival::FFallTracker();
	if (!IsLocallyControlled())
	{
		LastLocation = McCoords::BlockToUnreal(Seat);
		SetActorLocation(LastLocation);
		ClientRideSeat(Seat);
	}
}

void AMcPlayerPawn::ServerTryRide_Implementation(FVector Look)
{
	if (!HasAuthority() || bServerDead || RidingId != 0 || !FindVehicle || Look.ContainsNaN() || Look.IsNearlyZero())
	{
		return;
	}
	const FVector Eye = Body.Position + FVector(0.0, GetEyeHeight(), 0.0);
	if (const int32 Id = FindVehicle(Eye, Look.GetSafeNormal(), 3.5))
	{
		ServerRide(Id);
	}
}

void AMcPlayerPawn::ServerRideInput_Implementation(float Forward, float Strafe, float LookYaw)
{
	if (!FMath::IsFinite(Forward) || !FMath::IsFinite(Strafe) || !FMath::IsFinite(LookYaw))
	{
		return;
	}
	RideForward = FMath::Clamp(Forward, -1.0f, 1.0f);
	RideStrafe = FMath::Clamp(Strafe, -1.0f, 1.0f);
	McLookYaw = LookYaw - 90.0f;
}

void AMcPlayerPawn::ServerWantDismount_Implementation()
{
	ServerDismount();
}

void AMcPlayerPawn::ClientRideSeat_Implementation(FVector Feet)
{
	if (Feet.ContainsNaN())
	{
		return;
	}
	PreviousPosition = Body.Position;
	Body.Position = Feet;
	Body.Velocity = FVector::ZeroVector;
	Body.bOnGround = true;
}

void AMcPlayerPawn::ServerRespawn_Implementation()
{
	if (bServerDead && OnRespawnRequest)
	{
		OnRespawnRequest(*this);
	}
}

void AMcPlayerPawn::ServerGameTick()
{
	if (!Data)
	{
		return;
	}
	FMcPlayerStats& Stats = Data->Stats;
	++ServerAttackTicks;
	if (bServerDead)
	{
		return;
	}
	ServerHurtSound();
	if (PendingBannerTicks > 0 && (--PendingBannerTicks == 0 || AttachBanner(PendingBannerPos, PendingBanner)))
	{
		PendingBannerTicks = 0;
	}
	if (Stats.Health <= 0.0f)
	{
		ServerDie();
		return;
	}
	if (RidingId != 0)
	{
		ServerTickRiding();
	}

	const FMcItemRegistry& Items = FMcItemRegistry::Get();
	const FIntPoint Chunk(McCoords::BlockToChunk(FMath::FloorToInt32(Body.Position.X)), McCoords::BlockToChunk(FMath::FloorToInt32(Body.Position.Z)));
	if (bServerPositionKnown && BlockWorld && BlockWorld->HasChunk(Chunk))
	{
		const FVector Feet = Body.Position;
		const bool bLocal = IsLocallyControlled();
		const bool bFlyingNow = bLocal ? bFlying : bServerFlying;
		const bool bSprintNow = bLocal ? bSprinting : bServerSprint;
		const McSurvival::FEnvironment Environment = McSurvival::SampleEnvironment(*BlockWorld, Feet, GetEyeHeight());
		const FVector Step = Feet - ServerPreviousPosition;
		// A jump of more than 10 blocks in a tick is a teleport (spawning, the dev camera), not a fall.
		const bool bTeleported = Step.SizeSquared() > 100.0;

		if (!Data->bCreative)
		{
			if (bTeleported)
			{
				ServerFall.Distance = 0.0f;
			}
			else
			{
				const bool bBreaksFall = bFlyingNow || Environment.bFeetInWater || Environment.bClimbing || Environment.bInCobweb;
				const int32 FallDamage = ServerFall.Update(ServerPreviousPosition.Y, Feet.Y, Body.bOnGround, bBreaksFall);
				if (FallDamage > 0)
				{
					McSurvival::Hurt(Stats, static_cast<float>(FallDamage), McSurvival::EDamage::Fall, Data->Armor.Get(), &GameRules);
					// The landing: the player's own thud, and the block's fall sound.
					if (const FMcEntityManager* Sounds = GetEntities ? GetEntities() : nullptr)
					{
						Sounds->PlaySound(McSound::GetPlayerFallSound(FallDamage), Feet);
						Sounds->PlaySound(McSound::GetBlockSound(GetBlockNameBelow(*BlockWorld, Feet), McSound::EBlockSound::Fall, Feet));
					}
				}
				// What moving costs in hunger: sprinting on the ground, swimming and jumping.
				const float Moved = static_cast<float>(FVector2D(Step.X, Step.Z).Size());
				if (Environment.bFeetInWater || Environment.bHeadInWater)
				{
					McSurvival::AddExhaustion(Stats, McSurvival::SwimExhaustionPerBlock * Moved);
				}
				else if (bSprintNow && Body.bOnGround)
				{
					McSurvival::AddExhaustion(Stats, McSurvival::SprintExhaustionPerBlock * Moved);
				}
				if (bServerWasOnGround && !Body.bOnGround && Step.Y > 0.2)
				{
					McSurvival::AddExhaustion(Stats, bSprintNow ? McSurvival::SprintJumpExhaustion : McSurvival::JumpExhaustion);
				}
			}
			if (Feet.Y < BlockWorld->GetDimension().MinY - 64.0)
			{
				McSurvival::Hurt(Stats, 4.0f, McSurvival::EDamage::Void, Data->Armor.Get());
			}
			McSurvival::Tick(Stats, Environment, Difficulty, Data->Armor.Get(), &GameRules);
		}
		FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr;

		// Footsteps, on the ground and not sneaking (sneaking is quiet), at the step sound of the block underfoot.
		const bool bSneakNow = bLocal ? bSneaking : bSneakPose;
		if (!bTeleported && Body.bOnGround && !bFlyingNow && !bSneakNow && ServerSteps.Walk(FVector2D(Step.X, Step.Z).Size()) && Entities)
		{
			Entities->PlaySound(McSound::GetBlockSound(GetBlockNameBelow(*BlockWorld, Feet), McSound::EBlockSound::Step, Feet));
		}
		ServerPreviousPosition = Feet;
		bServerWasOnGround = Body.bOnGround;

		if (Entities && Stats.Health > 0.0f)
		{
			McSurvival::CollectItems(*Entities, Body.GetBox(), *Data->Inventory);
			McSurvival::CollectXp(*Entities, Body.GetBox(), Stats);
		}

		// Eating takes a while with the food held: 1.6 seconds unless the food says otherwise.
		FMcFoodData Food;
		const FMcItemStack& Held = Data->GetSelected();
		if (bServerUsing && !Data->bCreative && !Held.IsEmpty() && Items.GetFood(Held.Item, Food) && McSurvival::CanEat(Stats, Food))
		{
			float Seconds = 1.6f;
			if (const TSharedPtr<FJsonValue> Consumable = Held.Get(TEXT("minecraft:consumable")))
			{
				const TSharedPtr<FJsonObject>* Object = nullptr;
				double Value = 0.0;
				if (Consumable->TryGetObject(Object) && (*Object)->TryGetNumberField(TEXT("consume_seconds"), Value))
				{
					Seconds = static_cast<float>(Value);
				}
			}
			if (++EatTicks >= FMath::Max(1, FMath::RoundToInt32(Seconds * 20.0f)))
			{
				EatTicks = 0;
				McSurvival::Eat(Stats, Food);
				const FName Remainder = Items.GetUseRemainder(Held.Item);
				FMcItemStack& Slot = Data->Inventory->GetMutable(Data->SelectedSlot);
				if (--Slot.Count <= 0)
				{
					Slot.Clear();
				}
				if (!Remainder.IsNone())
				{
					if (Slot.IsEmpty())
					{
						Slot = FMcItemStack(Remainder, 1);
					}
					else
					{
						const FMcItemStack Left = Data->Inventory->AddItem(FMcItemStack(Remainder, 1));
						if (!Left.IsEmpty() && Entities)
						{
							Entities->DropItem(Feet + FVector(0.0, 1.0, 0.0), Left);
						}
					}
				}
			}
		}
		else
		{
			EatTicks = 0;
		}

		// Digging: the block gives way after the time its hardness and the tool say.
		if (DigPos.IsSet() && !Data->bCreative)
		{
			const FVector Eye = Feet + FVector(0.0, GetEyeHeight(), 0.0);
			const FMcStateId State = BlockWorld->GetBlock(DigPos.GetValue());
			if (FVector::Dist(Eye, FVector(DigPos.GetValue()) + FVector(0.5)) > ServerReach || FMcBlockRegistry::Get().IsAir(State) || !McBlockBreak::IsBreakable(State))
			{
				DigPos.Reset();
				DigProgress = 0.0f;
			}
			else
			{
				// Every 4 ticks of digging the block is hit, and sounds it.
				if (DigTicks++ % 4 == 0 && Entities)
				{
					Entities->PlaySound(McSound::GetBlockSound(FMcBlockRegistry::Get().GetBlockOf(State).Name, McSound::EBlockSound::Hit, FVector(DigPos.GetValue()) + FVector(0.5)));
				}
				DigProgress += McBlockBreak::GetProgressPerTick(Data->GetSelected(), State, Body.bOnGround, Environment.bHeadInWater, McEffects::GetDigFactor(Data->Stats));
				if (DigProgress >= 1.0f)
				{
					const FIntVector Broken = DigPos.GetValue();
					DigPos.Reset();
					DigProgress = 0.0f;
					ServerBreakBlock(Broken);
				}
			}
		}
	}

	ServerHurtSound();
	if (Stats.Health <= 0.0f)
	{
		ServerDie();
		return;
	}
	Data->ServerTick();
}

void AMcPlayerPawn::ServerHurtSound()
{
	// Hurt by anything since the last look (a fall, a bite, an arrow, hunger): the player's hurt or death sound.
	const float Health = Data->Stats.Health;
	if (ServerLastHealth >= 0.0f && Health < ServerLastHealth)
	{
		if (const FMcEntityManager* Sounds = GetEntities ? GetEntities() : nullptr)
		{
			const float VoicePitch = McSound::GetVoicePitch(DropRandom.FRand(), DropRandom.FRand());
			Sounds->PlaySound(Health <= 0.0f ? FName(TEXT("entity.player.death")) : FName(TEXT("entity.player.hurt")), Body.Position, 1.0f, VoicePitch);
		}
	}
	ServerLastHealth = Health;
}

void AMcPlayerPawn::ServerBreakBlock(const FIntVector& Pos)
{
	if (!ServerSetBlock || !BlockWorld || !Data)
	{
		return;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId State = BlockWorld->GetBlock(Pos);
	if (OnBeforeBreak)
	{
		OnBeforeBreak(Pos);
	}
	FMcItemStack& Tool = Data->Inventory->GetMutable(Data->SelectedSlot);

	// Doors and beds go as a whole, and the lower half does the dropping.
	const FName Half = Registry.GetPropertyValue(State, TEXT("half"));
	FIntVector Other = Half == TEXT("upper") ? Pos - FIntVector(0, 1, 0) : Half == TEXT("lower") ? Pos + FIntVector(0, 1, 0) : Pos;
	FMcStateId LootState = State;
	if (Other != Pos && Registry.GetBlockOf(BlockWorld->GetBlock(Other)).Name == Registry.GetBlockOf(State).Name)
	{
		if (Half == TEXT("upper"))
		{
			LootState = BlockWorld->GetBlock(Other);
		}
	}
	else
	{
		Other = Pos;
	}

	TArray<FMcItemStack> Drops;
	McBlockBreak::GetDrops(LootState, Tool, DropRandom, Drops);
	if (OnBreakDrops)
	{
		OnBreakDrops(Pos, Drops);
	}
	// A banner drops with the patterns the placed flag holds.
	if (FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr)
	{
		if (const FMcBannerEntity* Flag = FMcBannerEntity::FindAt(*Entities, Pos))
		{
			FString Base;
			bool bWall;
			TArray<McBanner::FLayer> FlagLayers;
			if (McBanner::Decode(Flag->Layers, Base, bWall, FlagLayers) && !FlagLayers.IsEmpty())
			{
				for (FMcItemStack& Drop : Drops)
				{
					if (Drop.Item.ToString().EndsWith(TEXT("_banner")))
					{
						Drop.Set(TEXT("minecraft:banner_patterns"), McBanner::MakeComponent(FlagLayers));
					}
				}
			}
		}
	}
	const int32 Wear = McBlockBreak::GetToolWear(Tool, State);
	const FMcStateId Air = Registry.FindDefaultState(TEXT("minecraft:air")).Get(0);
	if (Other != Pos)
	{
		ServerSetBlock(Other, Air);
	}
	ServerSetBlock(Pos, Air);
	if (FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr)
	{
		for (const FMcItemStack& Drop : Drops)
		{
			if (GameRules.bDoTileDrops)
			{
				Entities->DropItem(FVector(Pos) + FVector(0.5), Drop);
			}
		}
		// Ores pay out experience, unless silk touch took the whole block.
		bool bSilk = false;
		if (const TSharedPtr<FJsonValue> Enchants = Tool.IsEmpty() ? nullptr : Tool.Get(TEXT("minecraft:enchantments")))
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			bSilk = Enchants->TryGetObject(Object) && (*Object)->HasField(TEXT("minecraft:silk_touch"));
		}
		const int32 Points = bSilk || !McBlockBreak::CanHarvest(Tool, State) ? 0 : McBlockBreak::GetXpDropped(State, DropRandom);
		if (Points > 0)
		{
			FMcXpOrb::Spawn(*Entities, FVector(Pos) + FVector(0.5), Points, DropRandom);
		}
	}
	if (Wear > 0 && Tool.HurtAndBreak(Wear))
	{
		Tool.Clear();
	}
	McSurvival::AddExhaustion(Data->Stats, McSurvival::BreakBlockExhaustion);
}

void AMcPlayerPawn::ServerDie()
{
	if (bServerDead || !Data)
	{
		return;
	}
	bServerDead = true;
	// What was on the cursor or in a grid goes back first, then everything is dropped where the player fell.
	Data->CloseWindow();
	// With keepInventory the inventory, armour and experience stay with the player.
	if (FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr; Entities && !GameRules.bKeepInventory)
	{
		const FVector At = Body.Position + FVector(0.0, 0.5, 0.0);
		const TSharedPtr<FMcContainer> Containers[3] = { Data->Inventory, Data->Armor, Data->Offhand };
		for (const TSharedPtr<FMcContainer>& Container : Containers)
		{
			for (int32 Slot = 0; Slot < Container->Num(); ++Slot)
			{
				if (!Container->Get(Slot).IsEmpty())
				{
					Entities->DropItem(At, Container->Get(Slot), 40);
				}
				Container->Set(Slot, FMcItemStack());
			}
		}
	}
	// The experience is lost in the death, but 7 a level (at most 100) drops as orbs.
	if (!GameRules.bKeepInventory)
	{
		ServerDropXp(McSurvival::GetDeathXp(Data->Stats));
		Data->Stats.XpLevel = 0;
		Data->Stats.XpProgress = 0.0f;
	}
	if (OnDied)
	{
		OnDied(*this);
	}
	// The owner is told they died (health 0) even though the ticks stop here.
	Data->ServerTick();
}

void AMcPlayerPawn::ServerDig_Implementation(FIntVector Pos, bool bStart)
{
	if (!bStart || !Data || Data->bCreative || bServerDead)
	{
		DigPos.Reset();
		DigProgress = 0.0f;
		return;
	}
	const FVector Eye = Body.Position + FVector(0.0, GetEyeHeight(), 0.0);
	if (FVector::Dist(Eye, FVector(Pos) + FVector(0.5)) > ServerReach)
	{
		return;
	}
	if (!DigPos.IsSet() || DigPos.GetValue() != Pos)
	{
		DigPos = Pos;
		DigProgress = 0.0f;
		DigTicks = 0;
	}
}

void AMcPlayerPawn::ServerPlace_Implementation(const TArray<FMcNetBlockChange>& Placed)
{
	if (!Data || bServerDead || Placed.IsEmpty() || Placed.Num() > 4 || !ServerSetBlock || !BlockWorld)
	{
		return;
	}
	FMcItemStack& Held = Data->Inventory->GetMutable(Data->SelectedSlot);
	if (Held.IsEmpty())
	{
		return;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	FMcBlockTags& BlockTagSet = FMcBlockTags::Get();
	BlockTagSet.EnsureLoaded();
	const FVector Eye = Body.Position + FVector(0.0, GetEyeHeight(), 0.0);
	const FBox Self = Body.GetBox().ExpandBy(-1e-4);
	for (const FMcNetBlockChange& Change : Placed)
	{
		const FMcStateId State = Change.State;
		if (!Registry.IsValidState(State) || !McBlockBreak::IsBlockOfItem(State, Held.Item) || FVector::Dist(Eye, FVector(Change.Pos) + FVector(0.5)) > ServerReach)
		{
			return;
		}
		// Only into air, or something that gives way (water, grass, snow).
		const FMcStateId Old = BlockWorld->GetBlock(Change.Pos);
		if (!Registry.IsAir(Old) && !BlockTagSet.Has(TEXT("minecraft:replaceable"), Old))
		{
			return;
		}
		for (const FBox3f& Local : FMcBlockShapes::Get().GetCollision(State))
		{
			const FBox Box(FVector(Change.Pos) + FVector(Local.Min), FVector(Change.Pos) + FVector(Local.Max));
			if (Box.Intersect(Self))
			{
				return;
			}
		}
	}
	for (const FMcNetBlockChange& Change : Placed)
	{
		ServerSetBlock(Change.Pos, Change.State);
		AttachBanner(Change.Pos, Held);
	}
	if (!Data->bCreative && --Held.Count <= 0)
	{
		Held.Clear();
	}
}

bool AMcPlayerPawn::CarriesPlacedData(FName Item)
{
	return Item.ToString().EndsWith(TEXT("_banner")) || McShulker::IsBox(Item);
}

void AMcPlayerPawn::PlaceHeldAt(const FIntVector& Pos)
{
	if (!Data || !BlockWorld)
	{
		return;
	}
	const FMcItemStack& Held = Data->Inventory->Get(Data->SelectedSlot);
	const TOptional<FMcStateId> State = McBlockBreak::GetBlockForItem(Held.Item);
	if (!State)
	{
		return;
	}
	if (Data->bCreative && SetBlockHandler)
	{
		SetBlockHandler(Pos, State.GetValue());
		if (CarriesPlacedData(Held.Item))
		{
			ServerAttachBanner(Pos);
		}
	}
	else
	{
		FMcNetBlockChange Change;
		Change.Pos = Pos;
		Change.State = static_cast<uint16>(State.GetValue());
		ServerPlace({ Change });
	}
}

bool AMcPlayerPawn::AttachBanner(const FIntVector& Pos, const FMcItemStack& Banner)
{
	if (McShulker::IsBox(Banner.Item))
	{
		// The box carries its things: they go into the block once it is there.
		if (!BlockWorld || FMcBlockRegistry::Get().GetBlockOf(BlockWorld->GetBlock(Pos)).Name != Banner.Item)
		{
			return false;
		}
		if (OnPlacedStack)
		{
			OnPlacedStack(Pos, Banner);
		}
		return true;
	}
	FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr;
	return Entities && !Banner.IsEmpty() && FMcBannerEntity::Stand(*Entities, Pos, McBanner::GetLayers(Banner)) != nullptr;
}

void AMcPlayerPawn::ServerAttachBanner_Implementation(FIntVector Pos)
{
	if (!Data || bServerDead)
	{
		return;
	}
	const FMcItemStack& Held = Data->Inventory->Get(Data->SelectedSlot);
	if (!Data->bCreative || !CarriesPlacedData(Held.Item))
	{
		return;
	}
	// The block comes by another channel: try now, then for a second.
	if (!AttachBanner(Pos, Held))
	{
		PendingBannerPos = Pos;
		PendingBanner = Held;
		PendingBannerTicks = 20;
	}
}

void AMcPlayerPawn::ServerUseBlock_Implementation(FIntVector Pos)
{
	if (!OnUseBlock || bServerDead || !Data)
	{
		return;
	}
	const FVector Eye = Body.Position + FVector(0.0, GetEyeHeight(), 0.0);
	if (FVector::Dist(Eye, FVector(Pos) + FVector(0.5)) > ServerReach)
	{
		return;
	}
	OnUseBlock(*this, Pos);
}

void AMcPlayerPawn::ServerSetUsing_Implementation(bool bUsing)
{
	bServerUsing = bUsing;
	if (!bUsing)
	{
		EatTicks = 0;
	}
}

void AMcPlayerPawn::ServerPickItem_Implementation(FName Item)
{
	if (!Data || !Data->bCreative || !FMcItemRegistry::Get().Exists(Item) || !McBlockBreak::GetBlockForItem(Item))
	{
		return;
	}
	Data->Inventory->Set(Data->SelectedSlot, FMcItemStack(Item, 64));
}

void AMcPlayerPawn::ShowPosition() const
{
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		const FVector Block = Body.Position;
		const FIntVector BlockPos(FMath::FloorToInt32(Block.X), FMath::FloorToInt32(Block.Y), FMath::FloorToInt32(Block.Z));
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.0f, FColor::White, FString::Printf(
			TEXT("XYZ: %.3f / %.3f / %.3f\nBlock: %d %d %d\nChunk: %d %d (in %d %d %d)\n%s%s%s%s"),
			Block.X, Block.Y, Block.Z,
			BlockPos.X, BlockPos.Y, BlockPos.Z,
			McCoords::BlockToChunk(BlockPos.X), McCoords::BlockToChunk(BlockPos.Z),
			McCoords::BlockToLocal(BlockPos.X), McCoords::BlockToLocal(BlockPos.Y), McCoords::BlockToLocal(BlockPos.Z),
			bFlying ? TEXT("Flying ") : TEXT(""), bSprinting ? TEXT("Sprinting ") : TEXT(""), bSneaking ? TEXT("Sneaking ") : TEXT(""),
			Body.bOnGround ? TEXT("On ground") : TEXT("")));
	}
#endif
}
