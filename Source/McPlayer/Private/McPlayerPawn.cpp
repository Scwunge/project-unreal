#include "McPlayerPawn.h"
#include "McLeash.h"
#include "McMob.h"
#include "McMount.h"
#include "McBanner.h"
#include "McShulker.h"
#include "McCache.h"
#include "McResourcePacks.h"
#include "McRenderTextures.h"
#include "McBannerEntity.h"
#include "McBed.h"
#include "McPlayer.h"
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
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "McItemRegistry.h"
#include "McJukebox.h"
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
#include "McArrow.h"
#include "McThrown.h"
#include "McParticleComponent.h"
#include "McBlockHighlight.h"
#include "McHand.h"
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
	constexpr float GlidingEyeHeight = 0.4f;
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
	// The hand is drawn with this scale, which squeezes its depth toward the camera.
	Camera->bEnableFirstPersonScale = true;
	Camera->FirstPersonScale = 0.1f;
#if !UE_BUILD_SHIPPING
	// Development: -McFov=<degrees> (the reference client in spectator mode shoots at about 80, not the setting).
	float DevFov = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("-McFov="), DevFov) && DevFov > 10.0f)
	{
		Camera->SetFieldOfView(DevFov);
	}
#endif
	Camera->SetRelativeLocation(FVector(0.0, 0.0, StandingEyeHeight * McCoords::UnitsPerBlock));

	Data = CreateDefaultSubobject<UMcPlayerDataComponent>(TEXT("PlayerData"));

	// The owning machine moves the player and tells the server; everyone else sees the server's copy.
	bReplicates = true;
	SetReplicatingMovement(true);

#if MC_WITH_RENDER
	// The body other players see. It stays hidden on the machine that plays this pawn.
	Model = CreateDefaultSubobject<UMcPlayerModelComponent>(TEXT("Body"));
	Model->SetupAttachment(RootComponent);
	Hand = CreateDefaultSubobject<UMcHandComponent>(TEXT("Hand"));
	Hand->SetupAttachment(Camera);
	Highlight = CreateDefaultSubobject<UMcBlockHighlightComponent>(TEXT("Highlight"));
	Highlight->SetupAttachment(RootComponent);
	Highlight->SetAbsolute(true, true, true);
#endif
}

namespace
{
	// How long a food takes to eat: 1.6 seconds unless it says otherwise.
	float GetEatSeconds(const FMcItemStack& Held)
	{
		if (const TSharedPtr<FJsonValue> Consumable = Held.Get(TEXT("minecraft:consumable")))
		{
			const TSharedPtr<FJsonObject>* Object = nullptr;
			double Value = 0.0;
			if (Consumable->TryGetObject(Object) && (*Object)->TryGetNumberField(TEXT("consume_seconds"), Value))
			{
				return static_cast<float>(Value);
			}
		}
		return 1.6f;
	}
}

void AMcPlayerPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AMcPlayerPawn, McLookYaw, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AMcPlayerPawn, McLookPitch, COND_SkipOwner);
	DOREPLIFETIME_CONDITION(AMcPlayerPawn, bSneakPose, COND_SkipOwner);
	DOREPLIFETIME(AMcPlayerPawn, RidingId);
	DOREPLIFETIME(AMcPlayerPawn, MountHealth);
	DOREPLIFETIME(AMcPlayerPawn, MountMaxHealth);
	DOREPLIFETIME(AMcPlayerPawn, MountJump);
	DOREPLIFETIME(AMcPlayerPawn, bSleeping);
	DOREPLIFETIME(AMcPlayerPawn, SleepTicks);
	DOREPLIFETIME(AMcPlayerPawn, DeathScore);
	DOREPLIFETIME_CONDITION(AMcPlayerPawn, DigStage, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AMcPlayerPawn, DigBlockPos, COND_OwnerOnly);
	DOREPLIFETIME(AMcPlayerPawn, bRodCast);
	DOREPLIFETIME_CONDITION(AMcPlayerPawn, bGlidePose, COND_SkipOwner);
	DOREPLIFETIME(AMcPlayerPawn, bWearsElytra);
	DOREPLIFETIME(AMcPlayerPawn, WornHead);
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
	// Lying down, the eyes are just above the mattress.
	return bSleeping ? 0.2f : bGliding ? GlidingEyeHeight : bSneaking ? SneakingEyeHeight : StandingEyeHeight;
}

void AMcPlayerPawn::GameTick(const FMcMoveInput& Input)
{
	PreviousPosition = Body.Position;
	PreviousEyeHeight = GetEyeHeight();
	if (Data)
	{
		Data->Stats.MountHealth = RidingId != 0 ? MountHealth : 0.0f;
		Data->Stats.MountMaxHealth = RidingId != 0 ? MountMaxHealth : 0.0f;
		Data->Stats.MountJump = RidingId != 0 ? MountJump : -1.0f;
	}

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
			ServerRideInput(Input.Forward, Input.Strafe, Yaw, Input.bJump);
			if (Input.bSneak)
			{
				ServerWantDismount();
			}
		}
		return;
	}

	// In bed: the server holds the body at the pillow; sneaking or jumping gets up.
	if (bSleeping)
	{
		bSprinting = false;
		if (Input.bSneak || Input.bJump)
		{
			if (HasAuthority())
			{
				ServerWakeUp();
			}
			else
			{
				ServerWantWake();
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
	const bool bJumpPressed = Input.bJump && !bJumpWasDown;
	if (bJumpPressed && bMayFly)
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
	if (bWantSneak != bSneaking && !bGliding)
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
	// An opening shulker box's lid moves what is in its way.
	if (LidPush)
	{
		const FVector Push = LidPush(Body.GetBox(), LastLidTick);
		if (!Push.IsZero())
		{
			McPhysics::Move(*BlockWorld, Body, Push, false);
		}
	}
	// Elytra glide in place of walking.
	if (!TickGlide(Move, bJumpPressed))
	{
		McPhysics::TickPlayer(*BlockWorld, Body, Move);
	}
	if (bFlying && Body.bOnGround)
	{
		bFlying = false;
	}

	// Like Minecraft, the client moves its own player and the server takes its word for now; checks come with
	// survival (M7).
	bServerPositionKnown = true;
	if (!HasAuthority())
	{
		ServerMove(Body.Position, Yaw, Pitch, (bSneaking ? 1 : 0) | (Body.bOnGround ? 2 : 0) | (bSprinting ? 4 : 0) | (bFlying ? 8 : 0) | (bGliding ? 16 : 0));
	}
	else
	{
		bServerGliding = bGliding;
	}
}

void AMcPlayerPawn::ServerMove_Implementation(FVector FeetPos, float NewYaw, float NewPitch, uint8 Flags)
{
	if (FeetPos.ContainsNaN() || !FMath::IsFinite(NewYaw) || !FMath::IsFinite(NewPitch) || bServerAwaitingTeleport || RidingId != 0 || bSleeping)
	{
		return;
	}
	Body.Position = FeetPos;
	Body.bOnGround = (Flags & 2) != 0;
	bServerSprint = (Flags & 4) != 0;
	bServerFlying = (Flags & 8) != 0;
	bServerGliding = (Flags & 16) != 0;
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
		Figure->SetShown(!IsLocallyControlled() || bShowSelf || ViewMode != 0);
		Figure->SetLook(McLookYaw, McLookPitch, bSneakPose, bGlidePose);
		Figure->SetWings(bWearsElytra);
		Figure->SetWornHead(WornHead);
	}
	if (UMcHandComponent* Arm = Hand.Get())
	{
		// Only the player at the keyboard sees their own hand, and not while dead.
		Arm->SetShown(IsLocallyControlled() && !IsDead() && !bSleeping && ViewMode == 0 && !bHudHidden);
		const FMcItemStack& Held = Data->GetSelected();
		// A bow being drawn shows the picture of how far.
		FName HeldName = Held.IsEmpty() ? NAME_None : Held.Item;
		if (bBowLocal && !Held.IsEmpty())
		{
			HeldName = FName(*FString::Printf(TEXT("minecraft:bow_pulling_%d"), BowSecondsLocal >= 0.9f ? 2 : BowSecondsLocal >= 0.65f ? 1 : 0));
		}
		Arm->SetHeld(HeldName, Held.Count);
		Arm->SetEating(bEatingLocal && !Held.IsEmpty(), EatSecondsLocal);
		Arm->SetDrawing(bBowLocal && !Held.IsEmpty(), BowSecondsLocal);
	}
#endif
	TickTotem(DeltaSeconds);

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
	if (Player && Player->IsLocalController())
	{
		HandleFunctionKeys(*Player);
	}
	FMcMoveInput Input;
	if (Player && Player->IsLocalController())
	{
		// While a window is open the mouse and keys belong to it: no looking, walking or hotbar keys.
		if (!bBlocked)
		{
			// Look. Kept here rather than in the controller's input scaling so the feel doesn't depend on ini settings.
			float MouseX = 0.0f, MouseY = 0.0f;
			Player->GetInputMouseDelta(MouseX, MouseY);
			// In bed the view stays along the mattress.
			if (!bSleeping)
			{
				Yaw = FMath::Fmod(Yaw + MouseX * MouseSensitivity, 360.0f);
				Pitch = FMath::Clamp(Pitch + MouseY * MouseSensitivity, -89.9f, 89.9f);
				Player->SetControlRotation(FRotator(Pitch, Yaw, 0.0f));
			}

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
	bGlidePose = bGliding;

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
	// A hit rolls the view for the rest of its hurt time.
	float HurtTicksShown = Data ? static_cast<float>(Data->Stats.HurtTime) - Alpha : 0.0f;
	float HurtSideShown = HurtSide;
#if !UE_BUILD_SHIPPING
	// -McHurtHold=N shows the roll of N hurt ticks left from the left, for screenshots against the reference client's.
	static const int32 HurtHold = [] { int32 Value = 0; FParse::Value(FCommandLine::Get(), TEXT("-McHurtHold="), Value); return Value; }();
	if (HurtHold > 0)
	{
		HurtTicksShown = static_cast<float>(HurtHold);
		HurtSideShown = 1.0f;
	}
#endif
	if (HurtTicksShown > 0.0f && HurtSideShown != 0.0f)
	{
		Camera->ClearAdditiveOffset();	// the engine multiplies each add into the last
		Camera->AddAdditiveOffset(FTransform(FRotator(0.0, 0.0, -HurtSideShown * McCombat::GetHurtTilt(HurtTicksShown))), 0.0f);
	}
	else
	{
		Camera->ClearAdditiveOffset();
	}
	// F5: from behind the player, or from the front looking back, four blocks off or as far as the blocks let it.
	Camera->bUsePawnControlRotation = true;
	if (ViewMode != 0 && BlockWorld)
	{
		const double ViewYaw = FMath::DegreesToRadians(static_cast<double>(Yaw - 90.0f));
		const double ViewPitch = FMath::DegreesToRadians(static_cast<double>(-Pitch));
		const FVector Look(-FMath::Sin(ViewYaw) * FMath::Cos(ViewPitch), -FMath::Sin(ViewPitch), FMath::Cos(ViewYaw) * FMath::Cos(ViewPitch));
		const FVector EyeBlock = McCoords::UnrealToBlock(GetActorLocation() + FVector(0.0, 0.0, EyeHeight * McCoords::UnitsPerBlock));
		const FVector Away = ViewMode == 1 ? -Look : Look;
		const TOptional<FMcBlockHit> Blocked = McRaycast::Trace(*BlockWorld, EyeBlock, Away, 4.0);
		const double Distance = Blocked.IsSet() ? FMath::Max(0.0, FVector::Dist(EyeBlock, Blocked->Point) - 0.2) : 4.0;
		Camera->bUsePawnControlRotation = false;
		Camera->SetWorldLocation(McCoords::BlockToUnreal(EyeBlock + Away * Distance));
		Camera->SetWorldRotation(ViewMode == 1 ? FRotator(Pitch, Yaw, 0.0f) : FRotator(-Pitch, Yaw + 180.0f, 0.0f));
	}
#if !UE_BUILD_SHIPPING
	// Development: -McSelfShot=<blocks> puts the camera that far in front of the player, looking back at the player
	// model, for a picture of the skin. -McSelfShotYaw=degrees swings the camera round the player from there (180 looks
	// at their back), and -McSelfShotRise=blocks lifts it, looking down at them.
	float SelfShot = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("-McSelfShot="), SelfShot) && SelfShot > 0.0f)
	{
		float Around = 0.0f, Rise = 0.0f;
		FParse::Value(FCommandLine::Get(), TEXT("-McSelfShotYaw="), Around);
		FParse::Value(FCommandLine::Get(), TEXT("-McSelfShotRise="), Rise);
		Camera->bUsePawnControlRotation = false;
		const FRotator Look(0.0f, Yaw + Around, 0.0f);
		const FVector Eye = GetActorLocation() + FVector(0.0, 0.0, EyeHeight * McCoords::UnitsPerBlock * 0.9);
		const FVector Offset = Look.Vector() * SelfShot * McCoords::UnitsPerBlock + FVector(0.0, 0.0, Rise * McCoords::UnitsPerBlock);
		Camera->SetWorldLocation(Eye + Offset);
		Camera->SetWorldRotation((-Offset).Rotation());
	}
#endif

	UpdateTarget();
#if MC_WITH_RENDER
	if (UMcBlockHighlightComponent* Marks = Highlight.Get())
	{
		// The aimed block's outline (not while a window is open or in a spectator's view) and the dug block's cracks.
		TOptional<FIntVector> Aimed;
		TArray<FBox> Shape;
		if (Player && Player->IsLocalController() && !bBlocked && Target.IsSet())
		{
			Aimed = Target->Block;
			for (const FBox3f& Box : FMcBlockShapes::Get().GetCollision(Target->State))
			{
				Shape.Add(FBox(FVector(Box.Min), FVector(Box.Max)));
			}
			if (Shape.Num() == 0)
			{
				Shape.Add(FBox(FVector(0.2, 0.0, 0.2), FVector(0.8, 0.6, 0.8)));
			}
		}
		Marks->SetTarget(Aimed, Shape, Target.IsSet() ? FVector::Dist(McCoords::UnrealToBlock(Camera->GetComponentLocation()), FVector(Target->Point)) : 4.0);
		Marks->SetDig(DigBlockPos, Player && Player->IsLocalController() ? DigStage : -1);
	}
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
	// Food being eaten throws bits of itself from the mouth, on the beat of the munching.
	if (Player && Player->IsLocalController() && bEatingLocal)
	{
		const FMcItemStack& Eaten = Data->GetSelected();
		if (!Eaten.IsEmpty() && Eaten.Item != EatCrumbItem)
		{
			EatCrumbItem = Eaten.Item;
			EatCrumbColours.Reset();
			TArray<FColor> Pixels;
			int32 Width = 0, Height = 0;
			if (McRenderTextures::LoadPngPixels(McCache::Asset(TEXT("textures/item/") + McCache::PathOf(Eaten.Item.ToString()) + TEXT(".png")), Pixels, Width, Height))
			{
				for (const FColor& Pixel : Pixels)
				{
					if (Pixel.A > 128)
					{
						EatCrumbColours.Add(FColor(Pixel.R, Pixel.G, Pixel.B));
					}
				}
			}
		}
		UMcParticleComponent* Particles = UMcParticleComponent::FindFor(BlockWorld);
		for (int32 Tick = 0; Tick < FMath::Min(Ticks, MaxTicksPerFrame); ++Tick)
		{
			if (++EatCrumbTicks > 6 && EatCrumbTicks % 4 == 0 && Particles)
			{
				const FVector Eye = McCoords::UnrealToBlock(Camera->GetComponentLocation());
				Particles->ItemEaten(Eye, McCoords::UnrealToBlock(Camera->GetForwardVector()).GetSafeNormal(), EatCrumbColours);
			}
		}
	}
	else
	{
		EatCrumbTicks = 0;
	}
#endif
	BreakCooldown -= DeltaSeconds;
	PlaceCooldown -= DeltaSeconds;
	if (Player && Player->IsLocalController())
	{
		if (bBlocked || bSleeping)
		{
			StopDigging();
			bEatingLocal = false;
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

	// The screen goes dark in about five seconds of sleep, and comes back in a second or so after.
	SleepFade = FMath::Clamp(SleepFade + (bSleeping ? DeltaSeconds / 5.0f : -DeltaSeconds), 0.0f, 1.0f);

	ShowPosition();
}

void AMcPlayerPawn::HandleFunctionKeys(APlayerController& Player)
{
	if (Player.WasInputKeyJustPressed(EKeys::F1))
	{
		bHudHidden = !bHudHidden;
	}
	// F3 alone shows the debug text, on its release; F3 with another key is a combination: T reloads the resource packs.
	if (Player.IsInputKeyDown(EKeys::F3) && Player.WasInputKeyJustPressed(EKeys::T))
	{
		McPacks::RequestReload();
		bF3Combo = true;
	}
	if (Player.WasInputKeyJustReleased(EKeys::F3))
	{
		bDebugShown = bDebugShown != !bF3Combo;
		bF3Combo = false;
	}
	if (Player.WasInputKeyJustPressed(EKeys::F5))
	{
		ViewMode = (ViewMode + 1) % 3;
	}
	if (Player.WasInputKeyJustPressed(EKeys::F2))
	{
		const FString Name = FDateTime::Now().ToString(TEXT("%Y-%m-%d_%H.%M.%S")) + TEXT(".png");
		FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("Screenshots") / Name, false, false);
		UMcPlayerDataComponent::AddChatLine(TEXT("Saved screenshot as ") + Name, 0xFFFFFF);
	}
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
	const FVector Eye = McCoords::UnrealToBlock(GetActorLocation() + FVector(0.0, 0.0, GetEyeHeight() * McCoords::UnitsPerBlock));
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
	// Q throws one of what is in hand, Ctrl+Q all of it.
	if (Player.WasInputKeyJustPressed(EKeys::Q))
	{
		ServerDropSelected(Player.IsInputKeyDown(EKeys::LeftControl) || Player.IsInputKeyDown(EKeys::RightControl));
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
	bEatingLocal = bEating;
	EatSecondsLocal = bHoldingFood ? GetEatSeconds(Held) : 1.6f;
	// A bow in hand with an arrow to shoot (or creative) is drawn by holding the use button.
	const bool bDraw = !bEating && !Held.IsEmpty() && Held.Item == TEXT("minecraft:bow") && Player.IsInputKeyDown(EKeys::RightMouseButton)
		&& (bCreative || Data->Inventory->CountItem(TEXT("minecraft:arrow")) > 0);
	bBowLocal = bDraw;
	BowSecondsLocal = bDraw ? BowSecondsLocal + GetWorld()->GetDeltaSeconds() : 0.0f;
	// The view closes in as the bow draws: up to 15% of the field of view over the first second, as the game's.
	if (bDraw)
	{
		if (BowBaseFov <= 0.0f)
		{
			BowBaseFov = Camera->FieldOfView;
		}
		Camera->SetFieldOfView(BowBaseFov * McCombat::GetBowFovScale(BowSecondsLocal));
	}
	else if (BowBaseFov > 0.0f)
	{
		Camera->SetFieldOfView(BowBaseFov);
		BowBaseFov = 0.0f;
	}
	const bool bUsingNow = bEating || bDraw;
	if (bUsingNow != bUsingSent)
	{
		bUsingSent = bUsingNow;
		ServerSetUsing(bUsingNow);
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

	// Using the stick in hand while riding a pig or strider speeds it up.
	if (RidingId != 0 && Player.WasInputKeyJustPressed(EKeys::RightMouseButton))
	{
		ServerRideBoost();
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
			if (!FMcVehicle::IsVehicleType(It->Kind))
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

	// A right click on a trader in front of the block being looked at asks it to trade (not while sneaking).
	if (Player.WasInputKeyJustPressed(EKeys::RightMouseButton) && !bSneaking)
	{
		const double McYaw = FMath::DegreesToRadians(static_cast<double>(Yaw - 90.0f));
		const double McPitch = FMath::DegreesToRadians(static_cast<double>(-Pitch));
		const FVector Look(-FMath::Sin(McYaw) * FMath::Cos(McPitch), -FMath::Sin(McPitch), FMath::Cos(McYaw) * FMath::Cos(McPitch));
		const FVector Eye = McCoords::UnrealToBlock(Camera->GetComponentLocation());
		constexpr double UseReach = 3.0;
		const double BlockDistance = Target.IsSet() ? FVector::Dist(Eye, Target->Point) : UseReach;
		for (TActorIterator<AMcEntityActor> It(GetWorld()); It; ++It)
		{
			if (It->IsHidden() || (It->Kind != TEXT("minecraft:villager") && It->Kind != TEXT("minecraft:wandering_trader") && It->Kind != TEXT("minecraft:zombie_villager")))
			{
				continue;
			}
			const FVector Feet = McCoords::UnrealToBlock(It->GetActorLocation());
			const FBox Box(Feet - FVector(It->Size.X * 0.5, 0.0, It->Size.X * 0.5), Feet + FVector(It->Size.X * 0.5, It->Size.Y, It->Size.X * 0.5));
			FVector HitPoint, HitNormal;
			float Time = 0.0f;
			if (FMath::LineExtentBoxIntersection(Box, Eye, Eye + Look * UseReach, FVector::ZeroVector, HitPoint, HitNormal, Time) && Time * UseReach < BlockDistance)
			{
				ServerUseMob(Look);
				return;
			}
		}
	}

	// A snowball, egg or ender pearl is thrown wherever the player looks, block or not.
	if (Player.WasInputKeyJustPressed(EKeys::RightMouseButton) && !Held.IsEmpty() && !bEating)
	{
		FMcThrownEntity::EKind ThrownKind;
		if (FMcThrownEntity::FindKind(Held.Item, ThrownKind))
		{
			ServerThrow();
			return;
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

	// A fishing rod casts or reels in, and a firework rocket pulls a glider along: at nothing, or (below) at a block that
	// doesn't open or move.
	const bool bUsedInAir = Held.Item == TEXT("minecraft:fishing_rod") || (bGliding && Held.Item == TEXT("minecraft:firework_rocket"));
	if (!Target.IsSet())
	{
		if (bUsedInAir && Player.WasInputKeyJustPressed(EKeys::RightMouseButton))
		{
			ServerUseItem();
		}
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
	static const TSet<FName> Usable = { TEXT("minecraft:crafting_table"), TEXT("minecraft:chest"), TEXT("minecraft:trapped_chest"), TEXT("minecraft:ender_chest"), TEXT("minecraft:barrel"),
		TEXT("minecraft:furnace"), TEXT("minecraft:blast_furnace"), TEXT("minecraft:smoker"), TEXT("minecraft:lever"), TEXT("minecraft:enchanting_table"),
		TEXT("minecraft:stonecutter"), TEXT("minecraft:smithing_table"), TEXT("minecraft:grindstone"), TEXT("minecraft:anvil"), TEXT("minecraft:chipped_anvil"),
		TEXT("minecraft:damaged_anvil"), TEXT("minecraft:brewing_stand"), TEXT("minecraft:hopper"), TEXT("minecraft:dispenser"), TEXT("minecraft:dropper") };
	const FName Clicked = Registry.GetBlockOf(Hit.State).Name;
	const FString ClickedName = Clicked.ToString();
	// A sign opens its editor, or takes the dye, ink or wax in hand.
	// A shulker box opens and a bell rings. A lectern with a book opens it, and an empty one takes a book and quill or
	// written book held (anything else is placed against it).
	const bool bLectern = Clicked == TEXT("minecraft:lectern") && (Registry.GetPropertyValue(Hit.State, TEXT("has_book")) == TEXT("true")
		|| Held.Item == TEXT("minecraft:writable_book") || Held.Item == TEXT("minecraft:written_book"));
	const bool bMoves = ClickedName.EndsWith(TEXT("_button")) || ClickedName.EndsWith(TEXT("_fence_gate")) || ClickedName.EndsWith(TEXT("_sign")) || McBed::IsBed(ClickedName)
		|| ((ClickedName.EndsWith(TEXT("_door")) || ClickedName.EndsWith(TEXT("_trapdoor"))) && !ClickedName.Contains(TEXT("iron")))
		|| ClickedName.EndsWith(TEXT("shulker_box")) || Clicked == TEXT("minecraft:bell") || bLectern;
	// A jukebox takes a disc, or gives the one in it back; with anything else in hand and none in it, a block goes beside it.
	const bool bJukebox = Clicked == TEXT("minecraft:jukebox")
		&& (Registry.GetPropertyValue(Hit.State, TEXT("has_record")) == TEXT("true") || McJukebox::IsDisc(Held.Item));
	if (bPlaceClick && (Usable.Contains(Clicked) || bMoves || bJukebox) && !(bSneaking && !Held.IsEmpty()))
	{
		ServerUseBlock(Hit.Block);
		return;
	}
	if (bUsedInAir)
	{
		if (bPlaceClick)
		{
			ServerUseItem();
		}
		return;
	}
	// A bucket of water or lava pours into the block clicked (if it can be replaced) or the one beside it.
	if (bPlaceClick && (Held.Item == TEXT("minecraft:water_bucket") || Held.Item == TEXT("minecraft:lava_bucket")))
	{
		ServerUseItemOn(Hit.Block, Hit.Face);
		return;
	}
	// A boat goes on water (the fluid the crosshair would pass through), a minecart on a rail.
	if (bPlaceClick && FMcVehicle::IsVehicleItem(Held.Item) && !FMcVehicle::CreateFromItem(Held.Item)->RunsOnRails())
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
	if (bPlaceClick && !Held.IsEmpty() && (Held.Item == TEXT("minecraft:flint_and_steel") || FMcVehicle::IsVehicleItem(Held.Item)
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

float AMcPlayerPawn::ServerHurt(float Amount, McSurvival::EDamage Kind, const FVector* From, bool bKnock)
{
	if (!HasAuthority() || !Data || Data->bCreative || bServerDead)
	{
		return 0.0f;
	}
	const float Taken = McSurvival::Hurt(Data->Stats, Amount, Kind, Data->Armor.Get(), &GameRules);
	if (Taken > 0.0f && bSleeping)
	{
		ServerWakeUp();
	}
	if (Taken > 0.0f)
	{
		const float Side = From ? McCombat::GetHurtSide(McLookYaw, From->X - Body.Position.X, From->Z - Body.Position.Z, true) : McCombat::GetHurtSide(McLookYaw, 0.0, 0.0, false);
		if (IsLocallyControlled())
		{
			HurtSide = Side;
		}
		else
		{
			ClientHurtSide(Side);
		}
	}
	if (Taken > 0.0f && From && bKnock && Data->Stats.Health > 0.0f)
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

void AMcPlayerPawn::ServerBlast(float Amount, const FVector& Push, const FVector& Center)
{
	if (!HasAuthority() || !Data || Push.ContainsNaN())
	{
		return;
	}
	// ServerHurt leaves creative players alone; they are not pushed either.
	if (Data->bCreative)
	{
		return;
	}
	ServerHurt(Amount, McSurvival::EDamage::Explosion, &Center, false);
	if (IsLocallyControlled())
	{
		AddVelocity(Push);
	}
	else
	{
		ClientAddVelocity(Push);
	}
}

void AMcPlayerPawn::AddVelocity(const FVector& Push)
{
	Body.Velocity += Push;
}

void AMcPlayerPawn::ClientAddVelocity_Implementation(FVector Push)
{
	AddVelocity(Push);
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

void AMcPlayerPawn::ClientHurtSide_Implementation(float Side)
{
	HurtSide = Side;
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
	if (FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr)
	{
		// A full swing sounds strong (or a critical one), a rushed one weak.
		Entities->PlaySound(FName(bCritical && Strength > 0.9f ? "entity.player.attack.crit" : Strength > 0.9f ? "entity.player.attack.strong" : "entity.player.attack.weak"), Body.Position + FVector(0.0, 1.0, 0.0));
	}
	if (!Data->bCreative)
	{
		McSurvival::AddExhaustion(Data->Stats, McSurvival::AttackExhaustion);
		const int32 Wear = McCombat::GetWear(Held);
		if (Wear > 0 && Held.HurtAndBreak(Wear))
		{
			Held.Clear();
			ServerItemBroke();
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

void AMcPlayerPawn::ServerThrow_Implementation()
{
	FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr;
	if (!Entities || !Data || bServerDead)
	{
		return;
	}
	FMcThrownEntity::EKind Kind;
	const FMcItemStack& Held = Data->GetSelected();
	if (Held.IsEmpty() || !FMcThrownEntity::FindKind(Held.Item, Kind))
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Kind == FMcThrownEntity::EKind::EnderPearl)
	{
		if (Now < PearlReadyTime)
		{
			return;
		}
		PearlReadyTime = Now + 1.0;
	}
	const double McYaw = FMath::DegreesToRadians(static_cast<double>(McLookYaw));
	const double McPitch = FMath::DegreesToRadians(static_cast<double>(McLookPitch));
	const FVector Look(-FMath::Sin(McYaw) * FMath::Cos(McPitch), -FMath::Sin(McPitch), FMath::Cos(McYaw) * FMath::Cos(McPitch));
	TUniquePtr<FMcThrownEntity> Thrown = MakeUnique<FMcThrownEntity>(Kind);
	// TODO: the launch point's offset from the eye and the aim's spread, measured from the reference client (none until then).
	Thrown->Body.Position = Body.Position + FVector(0.0, GetEyeHeight(), 0.0);
	// 1.5 blocks a tick along the look, with the game's inaccuracy, and the thrower's own speed on top.
	Thrown->Body.Velocity = Look * FMcThrownEntity::Speed
		+ FVector(Body.Velocity.X, Body.bOnGround ? 0.0 : Body.Velocity.Y, Body.Velocity.Z);
	if (Kind == FMcThrownEntity::EKind::EnderPearl)
	{
		TWeakObjectPtr<AMcPlayerPawn> Thrower(this);
		Thrown->OnPearlLanded = [Thrower](const FVector& At)
		{
			if (AMcPlayerPawn* Pawn = Thrower.Get())
			{
				if (Pawn->bServerDead)
				{
					return;
				}
				// The thrower arrives where it landed, hurt by 5 (a creative player isn't), with the teleport's sound.
				Pawn->ServerTeleport(At, Pawn->McLookYaw);
				Pawn->ServerHurt(5.0f, McSurvival::EDamage::Fall, nullptr);
				if (const FMcEntityManager* Sounds = Pawn->GetEntities ? Pawn->GetEntities() : nullptr)
				{
					Sounds->PlaySound(FName("entity.player.teleport"), At, 1.0f, 1.0f);
				}
			}
		};
	}
	Entities->Add(MoveTemp(Thrown));
	static const FName Sounds[3] = { FName("entity.snowball.throw"), FName("entity.egg.throw"), FName("entity.ender_pearl.throw") };
	Entities->PlaySound(Sounds[static_cast<int32>(Kind)], Body.Position + FVector(0.0, 1.0, 0.0), 1.0f, 1.0f /* TODO: volume and pitch measured from the reference client */);
	if (!Data->bCreative)
	{
		FMcItemStack& Stack = Data->Inventory->GetMutable(Data->SelectedSlot);
		if (--Stack.Count <= 0)
		{
			Stack.Clear();
		}
	}
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
		if (Held.Item == TEXT("minecraft:ender_eye") || Held.Item == TEXT("minecraft:fire_charge") || FMcVehicle::IsVehicleItem(Held.Item))
		{
			if (--Held.Count <= 0)
			{
				Held.Clear();
			}
		}
		else if (Held.HurtAndBreak(1))
		{
			Held.Clear();
			ServerItemBroke();
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
	if (Data)
	{
		// vanilla shot (ref_ride_horse.png): the action bar tells how to get off.
		Data->ServerOverlay(TEXT("Press Left Shift to dismount"), false);
	}
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
	if (Found && Found->Mount.IsValid() && BlockWorld)
	{
		McMount::ClearPlayer(*Found, GetUniqueID());
	}
	else if (!Found || !Found->IsVehicle() || !BlockWorld)
	{
		return;
	}
	else
	{
		static_cast<FMcVehicle*>(Found)->ClearRider(GetUniqueID());
	}
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
	if (Found && Found->Mount.IsValid() && !Found->bRemoved)
	{
		// A horse, donkey or mule: the keys steer it, and a rider it has bucked off (or lost to someone else) is out.
		if (!McMount::HasPlayer(*Found, GetUniqueID()))
		{
			ServerDismount();
			return;
		}
		if (Found->Mount->RiderId == GetUniqueID())
		{
			McMount::SetRiderInput(*Found, RideForward, RideStrafe, McLookYaw, bRideJump, Data ? Data->Inventory->Get(Data->SelectedSlot).Item : NAME_None);
		}
		if (Found->IsLiving())
		{
			const FMcMob& Ridden = static_cast<const FMcMob&>(*Found);
			MountHealth = Ridden.Health;
			MountMaxHealth = Ridden.MaxHealth;
			MountJump = McMount::IsRideable(*Found) && !McMount::IsStickMount(*Found) ? McMount::ChargeScale(Found->Mount->JumpHoldTicks) : -1.0f;
		}
		const FVector MountSeat = McMount::GetSeatFor(*Found, GetUniqueID());
		Body.Position = MountSeat;
		Body.Velocity = FVector::ZeroVector;
		Body.bOnGround = true;
		ServerPreviousPosition = MountSeat;
		ServerFall = McSurvival::FFallTracker();
		if (!IsLocallyControlled())
		{
			LastLocation = McCoords::BlockToUnreal(MountSeat);
			SetActorLocation(LastLocation);
			ClientRideSeat(MountSeat);
		}
		return;
	}
	if (!Found || !Found->IsVehicle() || Found->bRemoved)
	{
		RidingId = 0;
		return;
	}
	FMcVehicle* Vehicle = static_cast<FMcVehicle*>(Found);
	if (Vehicle->ConsumeEject(GetUniqueID()))
	{
		ServerDismount();
		return;
	}
	if (!Vehicle->SetRider(RideForward, RideStrafe, McLookYaw, GetUniqueID()))
	{
		// Someone else took the last seat.
		RidingId = 0;
		return;
	}
	const FVector Seat = Vehicle->GetSeat(Vehicle->GetSeatIndex(GetUniqueID()));
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
		FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr;
		FMcEntity* Found = Entities ? Entities->Find(Id) : nullptr;
		FMcVehicle* Vehicle = Found && Found->IsVehicle() ? static_cast<FMcVehicle*>(Found) : nullptr;
		if (Vehicle && Entities && !Data->Inventory->Get(Data->SelectedSlot).IsEmpty() && Data->Inventory->Get(Data->SelectedSlot).Item == TEXT("minecraft:lead")
			&& McLeash::Attach(*Vehicle, Body.Position))
		{
			// A lead ties a boat to the player.
			FMcItemStack& Lead = Data->Inventory->GetMutable(Data->SelectedSlot);
			if (!Data->bCreative && --Lead.Count <= 0)
			{
				Lead.Clear();
			}
		}
		else if (Vehicle && Entities && Vehicle->bLeashed && Vehicle->LeashKnot == 0)
		{
			McLeash::Detach(*Entities, *Vehicle, !Data->bCreative);
		}
		else if (Vehicle && Vehicle->OpensOnUse(bSneakPose))
		{
			// A chest cart or boat opens like a chest instead of taking a rider.
			const FIntVector Where(FMath::FloorToInt32(Vehicle->Body.Position.X), FMath::FloorToInt32(Vehicle->Body.Position.Y), FMath::FloorToInt32(Vehicle->Body.Position.Z));
			Data->OpenBlockWindow(Vehicle->GetInventorySlots() == 5 ? EMcWindowKind::Hopper : EMcWindowKind::Chest, Where, Vehicle->GetInventoryTitle(), Vehicle->GetInventory(), nullptr);
			Data->MarkWindowOnEntity(Vehicle->Id);
		}
		else if (Vehicle && !Vehicle->CanRide())
		{
			// Fuel for a furnace cart, a flame for a TNT cart.
			FMcItemStack& Held = Data->Inventory->GetMutable(Data->SelectedSlot);
			if (!Held.IsEmpty() && Vehicle->UseItem(Held.Item, Body.Position) == FMcVehicle::EUse::Consumed && !Data->bCreative && --Held.Count <= 0)
			{
				Held.Clear();
			}
		}
		else if (Vehicle && Vehicle->HasFreeSeat())
		{
			ServerRide(Id);
		}
	}
}

void AMcPlayerPawn::ServerRideInput_Implementation(float Forward, float Strafe, float LookYaw, bool bJump)
{
	bRideJump = bJump;
	if (!FMath::IsFinite(Forward) || !FMath::IsFinite(Strafe) || !FMath::IsFinite(LookYaw))
	{
		return;
	}
	RideForward = FMath::Clamp(Forward, -1.0f, 1.0f);
	RideStrafe = FMath::Clamp(Strafe, -1.0f, 1.0f);
	McLookYaw = LookYaw - 90.0f;
}

void AMcPlayerPawn::ServerRideBoost_Implementation()
{
	FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr;
	FMcEntity* Found = Entities && RidingId != 0 ? Entities->Find(RidingId) : nullptr;
	if (Found && Found->Mount.IsValid() && Found->Mount->RiderId == GetUniqueID())
	{
		if (McMount::Boost(*Found) && Data && !Data->bCreative)
		{
			// The stick wears 7 a use (25 for the carrot, 100 for the fungus); worn out, it turns into a fishing rod.
			FMcItemStack& Stick = Data->Inventory->GetMutable(Data->SelectedSlot);
			const bool bFungus = Stick.Item == TEXT("minecraft:warped_fungus_on_a_stick");
			double Worn = 0.0;
			if (const TSharedPtr<FJsonValue> Old = Stick.Get(TEXT("minecraft:damage")))
			{
				Old->TryGetNumber(Worn);
			}
			Worn += 7.0;
			if (Worn >= (bFungus ? 100.0 : 25.0))
			{
				Stick = FMcItemStack(FName(TEXT("minecraft:fishing_rod")), 1);
			}
			else
			{
				Stick.Set(TEXT("minecraft:damage"), MakeShared<FJsonValueNumber>(Worn));
			}
		}
	}
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

void AMcPlayerPawn::ServerStartSleep(const FIntVector& Head)
{
	if (!HasAuthority() || bServerDead || !BlockWorld || bSleeping)
	{
		return;
	}
	const TOptional<float> SleepYaw = McBed::GetSleepYaw(*BlockWorld, Head);
	if (!SleepYaw.IsSet())
	{
		return;
	}
	if (RidingId != 0)
	{
		ServerDismount();
	}
	bSleeping = true;
	SleepTicks = 0;
	UE_LOG(LogMcPlayer, Display, TEXT("Went to bed at %s"), *Head.ToString());
	bSleptThroughSent = false;
	SleepBed = Head;
	bSneaking = false;
	Body.Height = StandingHeight;
	ServerTeleport(McBed::GetSleepPosition(Head), SleepYaw.GetValue());
	if (IsLocallyControlled())
	{
		Pitch = 0.0f;
	}
}

void AMcPlayerPawn::ServerWakeUp()
{
	if (!HasAuthority() || !bSleeping)
	{
		return;
	}
	bSleeping = false;
	SleepTicks = 0;
	UE_LOG(LogMcPlayer, Display, TEXT("Got up from the bed at %s"), *SleepBed.ToString());
	FVector Feet = FVector(SleepBed.X + 0.5, SleepBed.Y + 1.0, SleepBed.Z + 0.5);
	if (BlockWorld)
	{
		if (const TOptional<FVector> Stand = McBed::FindStandPosition(*BlockWorld, SleepBed))
		{
			Feet = Stand.GetValue();
		}
	}
	ServerTeleport(Feet, McLookYaw);
}

void AMcPlayerPawn::ServerDropSelected_Implementation(bool bWholeStack)
{
	FMcEntityManager* Manager = GetEntities ? GetEntities() : nullptr;
	if (!Manager || bServerDead || bSleeping || !Data || Data->HasWindow() || RidingId != 0)
	{
		return;
	}
	const int32 Slot = FMath::Clamp(Data->SelectedSlot, 0, UMcPlayerDataComponent::InventorySize - 1);
	FMcItemStack Held = Data->Inventory->Get(Slot);
	if (Held.IsEmpty())
	{
		return;
	}
	FMcItemStack Thrown = Held;
	Thrown.Count = bWholeStack ? Held.Count : 1;
	Held.Count -= Thrown.Count;
	if (Held.Count <= 0)
	{
		Held.Clear();
	}
	Data->Inventory->Set(Slot, Held);

	// From just under the eyes, 0.3 blocks a tick along the look and a little up, with the pickup held off for two seconds.
	const double McYaw = FMath::DegreesToRadians(static_cast<double>(McLookYaw));
	const double McPitch = FMath::DegreesToRadians(static_cast<double>(McLookPitch));
	const FVector Look(-FMath::Sin(McYaw) * FMath::Cos(McPitch), -FMath::Sin(McPitch), FMath::Cos(McYaw) * FMath::Cos(McPitch));
	FMcItemEntity& Item = Manager->DropItem(Body.Position + FVector(0.0, GetEyeHeight() - 0.3, 0.0), Thrown, 40);
	Item.Body.Velocity = Look * 0.3 + FVector(0.0, 0.1, 0.0);
	UE_LOG(LogMcPlayer, Display, TEXT("Threw %d of %s"), Thrown.Count, *Thrown.Item.ToString());
}

void AMcPlayerPawn::ServerWantWake_Implementation()
{
	ServerWakeUp();
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
	// A blow that would kill may be cheated by a totem in hand.
	ServerTryTotem();
	ServerHurtSound();
	if (bSleeping)
	{
		// Up if the bed is gone; after a hundred ticks the controller sees whether everyone is in bed and skips the night.
		if (!BlockWorld || !McBed::GetHead(*BlockWorld, SleepBed).IsSet())
		{
			ServerWakeUp();
		}
		else if (++SleepTicks >= McBed::TicksToSleep && !bSleptThroughSent && OnSleptThrough)
		{
			OnSleptThrough(*this);
		}
	}
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

		const int32 FireBefore = Stats.OnFireTicks;
		if (!Data->bCreative)
		{
			if (bTeleported)
			{
				ServerFall.Distance = 0.0f;
			}
			else
			{
				// A glide that isn't diving fast doesn't build up a fall.
				const bool bGlideHolds = bServerGliding && Step.Y > -0.5;
				const bool bBreaksFall = bFlyingNow || bGlideHolds || Environment.bFeetInWater || Environment.bClimbing || Environment.bInCobweb;
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
		if (Entities)
		{
			ServerTickFishing(*Entities);
		}
		ServerTickElytra();

		// Footsteps, on the ground and not sneaking (sneaking is quiet), at the step sound of the block underfoot.
		const bool bSneakNow = bLocal ? bSneaking : bSneakPose;
		if (!bTeleported && Body.bOnGround && !bFlyingNow && !bSneakNow && ServerSteps.Walk(FVector2D(Step.X, Step.Z).Size()) && Entities)
		{
			Entities->PlaySound(McSound::GetBlockSound(GetBlockNameBelow(*BlockWorld, Feet), McSound::EBlockSound::Step, Feet));
		}
		// Water: a splash going in (louder and higher the faster), swimming sounds on the way, and a fire put out.
		if (Entities && !bTeleported && !bFlyingNow)
		{
			const bool bInWater = Environment.bFeetInWater;
			const float Speed = FMath::Clamp(FMath::Sqrt(static_cast<float>(Step.X * Step.X * 0.2 + Step.Y * Step.Y + Step.Z * Step.Z * 0.2)) * 0.2f, 0.12f, 1.0f);
			const float Wobble = 1.0f + (FMath::FRand() - FMath::FRand()) * 0.4f;
			if (bInWater && !bServerWasInWater)
			{
				Entities->PlaySound(FName(Speed < 0.25f ? "entity.player.splash" : "entity.player.splash.high_speed"), Feet, Speed, Wobble);
				ServerSwum = 0.0;
			}
			else if (bInWater && !Body.bOnGround)
			{
				ServerSwum += Step.Size();
				if (ServerSwum > 1.6)
				{
					ServerSwum = 0.0;
					Entities->PlaySound(FName("entity.player.swim"), Feet, 0.3f, Wobble);
				}
			}
			if (FireBefore > 0 && Stats.OnFireTicks == 0 && (bInWater || Environment.bInLava))
			{
				Entities->PlaySound(FName("entity.generic.extinguish_fire"), Feet, 0.7f, 1.6f + (FMath::FRand() - FMath::FRand()) * 0.4f);
			}
			bServerWasInWater = bInWater;
		}
		// Going into a bubble column: its inside sound, once (0.7 at the player, from the bubble2 and bubble3 vanilla runs).
		if (Entities && Body.bInBubbleColumn && !bServerWasInBubble)
		{
			Entities->PlaySound(FName(Body.bBubbleDown ? "block.bubble_column.whirlpool_inside" : "block.bubble_column.upwards_inside"), Feet, 0.7f, 1.0f);
		}
		bServerWasInBubble = Body.bInBubbleColumn;
		ServerPreviousPosition = Feet;
		bServerWasOnGround = Body.bOnGround;

		if (Entities && Stats.Health > 0.0f)
		{
			const FVector Ears = Body.Position + FVector(0.0, 0.5, 0.0);
			const int32 LevelBefore = Stats.XpLevel;
			if (McSurvival::CollectItems(*Entities, Body.GetBox(), *Data->Inventory) > 0)
			{
				Entities->PlaySound(FName("entity.item.pickup"), Ears, 0.2f, (FMath::FRand() - FMath::FRand()) * 1.4f + 2.0f);
			}
			if (McSurvival::CollectArrows(*Entities, Body.GetBox(), *Data->Inventory) > 0)
			{
				Entities->PlaySound(FName("entity.item.pickup"), Ears, 0.2f, (FMath::FRand() - FMath::FRand()) * 1.4f + 2.0f);
			}
			if (McSurvival::CollectXp(*Entities, Body.GetBox(), Stats) > 0)
			{
				Entities->PlaySound(FName("entity.experience_orb.pickup"), Ears, 0.1f, 0.5f * ((FMath::FRand() - FMath::FRand()) * 0.7f + 1.8f));
			}
			if (Stats.XpLevel > LevelBefore)
			{
				// Every fifth level rings louder.
				Entities->PlaySound(FName("entity.player.levelup"), Ears, Stats.XpLevel % 5 == 0 ? 1.0f : 0.75f, 1.0f);
			}
		}

		// Eating takes a while with the food held: 1.6 seconds unless the food says otherwise.
		FMcFoodData Food;
		const FMcItemStack& Held = Data->GetSelected();
		if (bServerUsing && !Held.IsEmpty() && Held.Item == FName(TEXT("minecraft:ominous_bottle")))
		{
			// An ominous bottle is drunk in 1.6 seconds and gives Bad Omen for 100 minutes, a level above its amplifier.
			if (++EatTicks >= 32)
			{
				EatTicks = 0;
				int32 Level = 0;
				if (const TSharedPtr<FJsonValue> Amplifier = Held.Get(TEXT("minecraft:ominous_bottle_amplifier")))
				{
					Level = FMath::Clamp(FMath::RoundToInt32(Amplifier->AsNumber()), 0, 4);
				}
				McEffects::Add(Stats, TEXT("bad_omen"), Level, 120000);
				if (!Data->bCreative)
				{
					FMcItemStack& Slot = Data->Inventory->GetMutable(Data->SelectedSlot);
					if (--Slot.Count <= 0)
					{
						Slot = FMcItemStack(FName(TEXT("minecraft:glass_bottle")), 1);
					}
					else
					{
						const FMcItemStack Left = Data->Inventory->AddItem(FMcItemStack(FName(TEXT("minecraft:glass_bottle")), 1));
						if (!Left.IsEmpty() && Entities)
						{
							Entities->DropItem(Feet + FVector(0.0, 1.0, 0.0), Left);
						}
					}
				}
			}
		}
		else if (bServerUsing && !Data->bCreative && !Held.IsEmpty() && Items.GetFood(Held.Item, Food) && McSurvival::CanEat(Stats, Food))
		{
			const float Seconds = GetEatSeconds(Held);
			const int32 MealTicks = FMath::Max(1, FMath::RoundToInt32(Seconds * 20.0f));
			++EatTicks;
			if (Entities && EatTicks < MealTicks && EatTicks > 6 && EatTicks % 4 == 0)
			{
				// Munching, every fourth tick after the first few.
				Entities->PlaySound(FName("entity.generic.eat"), Body.Position + FVector(0.0, GetEyeHeight(), 0.0), 0.5f, FMath::FRand() * 0.1f + 0.9f);
			}
			if (EatTicks >= MealTicks)
			{
				EatTicks = 0;
				if (Entities)
				{
					Entities->PlaySound(FName("entity.player.burp"), Body.Position + FVector(0.0, GetEyeHeight(), 0.0), 0.5f, FMath::FRand() * 0.1f + 0.9f);
				}
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

		// The bow: the draw counts while the use button is held with it in hand; letting go shoots.
		{
			const FMcItemStack& InHand = Data->GetSelected();
			const bool bBowHeld = !InHand.IsEmpty() && InHand.Item == TEXT("minecraft:bow");
			const bool bCanDraw = Data->bCreative || Data->Inventory->CountItem(TEXT("minecraft:arrow")) > 0;
			if (bServerUsing && bBowHeld && bCanDraw && (BowTicks == 0 || BowSlot == Data->SelectedSlot))
			{
				if (BowTicks == 0)
				{
					BowSlot = Data->SelectedSlot;
				}
				++BowTicks;
			}
			else if (BowTicks > 0)
			{
				// Let go (or the bow went out of the hand): a draw that is still in the bow's slot shoots.
				const bool bStillBow = bBowHeld && BowSlot == Data->SelectedSlot && bCanDraw;
				const int32 Drawn = BowTicks;
				BowTicks = 0;
				if (bStillBow && !bServerUsing)
				{
					ServerShootBow(Drawn);
				}
			}
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

	DigStage = DigPos.IsSet() ? FMath::Clamp(FMath::FloorToInt(DigProgress * 10.0f), 0, 9) : -1;
	DigBlockPos = DigPos.IsSet() ? DigPos.GetValue() : FIntVector::ZeroValue;

	// A blow that would kill may be cheated by a totem in hand.
	ServerTryTotem();
	ServerHurtSound();
	if (Stats.Health <= 0.0f)
	{
		ServerDie();
		return;
	}
	Data->ServerTick();
}

void AMcPlayerPawn::ServerShootBow(int32 DrawTicks)
{
	FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr;
	if (!Entities || !Data)
	{
		return;
	}
	// The draw's power: (t * t + t * 2) / 3 for t seconds, up to 1; under a tenth is not a shot.
	const float Power = McCombat::GetBowPower(DrawTicks);
	if (Power < 0.1f)
	{
		return;
	}
	const double McYaw = FMath::DegreesToRadians(static_cast<double>(McLookYaw));
	const double McPitch = FMath::DegreesToRadians(static_cast<double>(McLookPitch));
	const FVector Look(-FMath::Sin(McYaw) * FMath::Cos(McPitch), -FMath::Sin(McPitch), FMath::Cos(McYaw) * FMath::Cos(McPitch));
	// Three blocks a tick at full draw, aimed with the measured spread, from 0.1 block under the eye.
	TUniquePtr<FMcArrowEntity> Arrow = MakeUnique<FMcArrowEntity>();
	Arrow->Body.Position = Body.Position + FVector(0.0, GetEyeHeight() - McCombat::GetBowLaunchDrop(), 0.0);
	Arrow->Body.Velocity = McCombat::GetBowAim(Look, DropRandom) * (Power * 3.0);
	Arrow->PlayerBase = 2.0f;
	Arrow->bCritical = Power >= 1.0f;
	Arrow->bPickup = !Data->bCreative;
	Entities->Add(MoveTemp(Arrow));
	Entities->PlaySound(FName("entity.arrow.shoot"), Body.Position + FVector(0.0, 1.0, 0.0), 1.0f, McCombat::GetBowShootPitch(DropRandom.FRand(), Power));
	if (!Data->bCreative)
	{
		Data->Inventory->RemoveItem(TEXT("minecraft:arrow"), 1);
		FMcItemStack& Bow = Data->Inventory->GetMutable(Data->SelectedSlot);
		if (Bow.HurtAndBreak(1))
		{
			Bow.Clear();
			ServerItemBroke();
		}
	}
}

void AMcPlayerPawn::ServerItemBroke()
{
	if (const FMcEntityManager* Sounds = GetEntities ? GetEntities() : nullptr)
	{
		Sounds->PlaySound(FName("entity.item.break"), Body.Position + FVector(0.0, GetEyeHeight(), 0.0), 0.8f, 0.8f + FMath::FRand() * 0.4f);
	}
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
			// What hurt picks the voice: drowning gasps, burning crackles, the rest is the plain grunt.
			const McSurvival::EDamage Kind = static_cast<McSurvival::EDamage>(Data->Stats.LastHurtKind);
			const bool bBurn = Kind == McSurvival::EDamage::InFire || Kind == McSurvival::EDamage::OnFire || Kind == McSurvival::EDamage::Lava;
			Sounds->PlaySound(Health <= 0.0f ? FName(TEXT("entity.player.death"))
				: Kind == McSurvival::EDamage::Drown ? FName(TEXT("entity.player.hurt_drown"))
				: bBurn ? FName(TEXT("entity.player.hurt_on_fire"))
				: FName(TEXT("entity.player.hurt")), Body.Position, 1.0f, VoicePitch);
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

	// A decorated pot hit with a tool that breaks pots cracks, and drops its sherds instead of itself (OnBreakDrops).
	if (Registry.GetBlockOf(State).Name == TEXT("minecraft:decorated_pot") && !Tool.IsEmpty()
		&& FMcItemRegistry::Get().HasTag(Tool.Item, TEXT("minecraft:breaks_decorated_pots")))
	{
		LootState = Registry.FindState(TEXT("minecraft:decorated_pot"), {{TEXT("cracked"), TEXT("true")}}).Get(LootState);
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
	// Shears take a tripwire away disarmed, so the hooks do not feel it go (reference client, rs_trip3: survival, shears
	// held, the hooks did nothing; without shears they powered for 9 ticks).
	if (Registry.GetBlockOf(State).Name == TEXT("minecraft:tripwire") && !Tool.IsEmpty() && Tool.Item == TEXT("minecraft:shears"))
	{
		TMap<FName, FName> Disarmed;
		for (const TCHAR* Property : { TEXT("attached"), TEXT("east"), TEXT("north"), TEXT("powered"), TEXT("south"), TEXT("west") })
		{
			Disarmed.Add(Property, Registry.GetPropertyValue(State, Property));
		}
		Disarmed.Add(TEXT("disarmed"), TEXT("true"));
		ServerSetBlock(Pos, Registry.FindState(TEXT("minecraft:tripwire"), Disarmed).Get(State));
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
		ServerItemBroke();
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
	DeathScore = McSurvival::GetTotalXp(Data->Stats);
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
		if (OnPunchBlock)
		{
			OnPunchBlock(*this, Pos);
		}
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
		if (OnPlaced)
		{
			OnPlaced(Change.Pos);
		}
		AttachBanner(Change.Pos, Held);
	}
	if (!Data->bCreative && --Held.Count <= 0)
	{
		Held.Clear();
	}
}

bool AMcPlayerPawn::CarriesPlacedData(FName Item)
{
	// A sign carries nothing, but whoever places it is to write on it.
	return Item.ToString().EndsWith(TEXT("_banner")) || McShulker::IsBox(Item) || Item.ToString().EndsWith(TEXT("_sign"));
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
	if (Banner.Item.ToString().EndsWith(TEXT("_sign")))
	{
		// Once the sign is there: a standing or wall one for a sign, a hanging one for a hanging sign.
		if (!BlockWorld || !McBlockBreak::IsBlockOfItem(BlockWorld->GetBlock(Pos), Banner.Item))
		{
			return false;
		}
		if (OnPlacedSign)
		{
			OnPlacedSign(*this, Pos);
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

void AMcPlayerPawn::ServerUseMob_Implementation(FVector Look)
{
	if (!OnUseMob || bServerDead || !Data || Look.ContainsNaN() || Look.IsNearlyZero())
	{
		return;
	}
	OnUseMob(*this, Body.Position + FVector(0.0, GetEyeHeight(), 0.0), Look.GetSafeNormal(), 3.0);
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
