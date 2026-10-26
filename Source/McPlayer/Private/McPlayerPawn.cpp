#include "McPlayerPawn.h"
#include "McBlockPlacement.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "EngineUtils.h"
#include "McBlockWorld.h"
#include "McEntityActor.h"
#include "McChunk.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"

#if MC_WITH_RENDER
#include "McPlayerModel.h"
#endif

namespace
{
	constexpr float SecondsPerTick = 0.05f;
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
	McPhysics::TickPlayer(*BlockWorld, Body, Move);
	if (bFlying && Body.bOnGround)
	{
		bFlying = false;
	}

	// Like Minecraft, the client moves its own player and the server takes its word for now; checks come with
	// survival (M7).
	if (!HasAuthority())
	{
		ServerMove(Body.Position, Yaw, Pitch, bSneaking);
	}
}

void AMcPlayerPawn::ServerMove_Implementation(FVector FeetPos, float NewYaw, float NewPitch, bool bNewSneaking)
{
	Body.Position = FeetPos;
	LastLocation = McCoords::BlockToUnreal(FeetPos);
	SetActorLocationAndRotation(LastLocation, FRotator(0.0f, NewYaw, 0.0f));
	McLookYaw = NewYaw - 90.0f;
	McLookPitch = -NewPitch;
	bSneakPose = bNewSneaking;
}

void AMcPlayerPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

#if MC_WITH_RENDER
	if (UMcPlayerModelComponent* Figure = Model.Get())
	{
		Figure->SetShown(!IsLocallyControlled());
		Figure->SetLook(McLookYaw, McLookPitch, bSneakPose);
	}
#endif

	if (!IsLocallyControlled())
	{
		return;
	}

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

	UpdateTarget();
	BreakCooldown -= DeltaSeconds;
	PlaceCooldown -= DeltaSeconds;
	if (Player && Player->IsLocalController())
	{
		UseBlocks(*Player);
	}

	ShowPosition();
}

void AMcPlayerPawn::FillDefaultHotbar()
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	static const TCHAR* const Blocks[HotbarSize] = {
		TEXT("minecraft:grass_block"), TEXT("minecraft:dirt"), TEXT("minecraft:stone"), TEXT("minecraft:cobblestone"),
		TEXT("minecraft:oak_planks"), TEXT("minecraft:oak_log"), TEXT("minecraft:glass"), TEXT("minecraft:torch"),
		TEXT("minecraft:oak_stairs"),
	};
	Hotbar.SetNumZeroed(HotbarSize);
	for (int32 Slot = 0; Slot < HotbarSize; ++Slot)
	{
		Hotbar[Slot] = Registry.FindDefaultState(Blocks[Slot]).Get(0);
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
	const FVector Eye = McCoords::UnrealToBlock(Camera->GetComponentLocation());
	Target = McRaycast::Trace(*BlockWorld, Eye, Look, bMayFly ? McRaycast::CreativeReach : McRaycast::SurvivalReach);
}

void AMcPlayerPawn::UseBlocks(APlayerController& Player)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (Hotbar.Num() != HotbarSize && Registry.IsLoaded())
	{
		FillDefaultHotbar();
	}

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
		SelectedSlot = (SelectedSlot + 1) % HotbarSize;
	}
	if (Player.WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		SelectedSlot = (SelectedSlot + HotbarSize - 1) % HotbarSize;
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
			if (It->Kind != TEXT("minecraft:item") && FMath::LineExtentBoxIntersection(Box, Eye, Eye + Look * AttackReach, FVector::ZeroVector, HitPoint, HitNormal, Time)
				&& Time * AttackReach < BlockDistance)
			{
				AttackHandler(Look);
				BreakCooldown = BreakRepeatSeconds;
				return;
			}
		}
	}

	if (!Target.IsSet() || !SetBlockHandler || Hotbar.Num() != HotbarSize)
	{
		return;
	}
	const FMcBlockHit Hit = Target.GetValue();

	// Pick block: the targeted block into the hotbar, or select it if it's already there.
	if (Player.WasInputKeyJustPressed(EKeys::MiddleMouseButton))
	{
		FString Name = Registry.GetBlockOf(Hit.State).Name.ToString();
		Name.ReplaceInline(TEXT("wall_torch"), TEXT("torch"));
		const FMcStateId Picked = Registry.FindDefaultState(FName(*Name)).Get(Hit.State);
		const int32 Existing = Hotbar.IndexOfByPredicate([&](FMcStateId State) { return Registry.GetBlockOf(State).Name == Registry.GetBlockOf(Picked).Name && State != 0; });
		if (Existing != INDEX_NONE)
		{
			SelectedSlot = Existing;
		}
		else
		{
			Hotbar[SelectedSlot] = Picked;
		}
	}

	// Break: at once on a click, then again every few ticks while held.
	const bool bBreakClick = Player.WasInputKeyJustPressed(EKeys::LeftMouseButton);
	if (bBreakClick || (Player.IsInputKeyDown(EKeys::LeftMouseButton) && BreakCooldown <= 0.0f))
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

	// Place: the same, every 4 ticks while held.
	const bool bPlaceClick = Player.WasInputKeyJustPressed(EKeys::RightMouseButton);
	if ((bPlaceClick || (Player.IsInputKeyDown(EKeys::RightMouseButton) && PlaceCooldown <= 0.0f)) && Hotbar[SelectedSlot] != 0)
	{
		PlaceCooldown = PlaceRepeatSeconds;
		const TArray<FMcPlacedBlock> Placed = McBlockPlacement::GetPlacement(*BlockWorld, Hotbar[SelectedSlot], Hit, Yaw - 90.0f, -Pitch);
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
		for (const FMcPlacedBlock& Block : Placed)
		{
			SetBlockHandler(Block.Pos, Block.State);
		}
	}
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
