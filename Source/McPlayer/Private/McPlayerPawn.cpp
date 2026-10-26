#include "McPlayerPawn.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

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
}

void AMcPlayerPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

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

	ShowPosition();
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
