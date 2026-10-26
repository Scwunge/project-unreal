#include "McFreeCameraPawn.h"
#include "McChunk.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

AMcFreeCameraPawn::AMcFreeCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->bUsePawnControlRotation = true;
	// Minecraft's default Field of View setting is 70 degrees, measured vertically; DefaultEngine.ini sets
	// the local player to keep the vertical angle fixed so this matches.
	Camera->SetFieldOfView(70.0f);
	RootComponent = Camera;
}

void AMcFreeCameraPawn::PossessedBy(AController* NewController)
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

void AMcFreeCameraPawn::SetView(const FVector& BlockPos, float McYaw, float McPitch)
{
	// Minecraft's yaw 0 looks along +z (Unreal +Y, yaw 90) and grows clockwise seen from above, towards -x
	// (Unreal -X, yaw 180). Unreal's yaw also grows that way, so it is a fixed offset. Pitch is flipped.
	Yaw = FMath::Fmod(McYaw + 90.0f, 360.0f);
	Pitch = FMath::Clamp(-McPitch, -89.9f, 89.9f);
	Velocity = FVector::ZeroVector;
	SetActorLocation(McCoords::BlockToUnreal(BlockPos));
	if (AController* PawnController = GetController())
	{
		PawnController->SetControlRotation(FRotator(Pitch, Yaw, 0.0f));
	}
}

void AMcFreeCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APlayerController* Player = Cast<APlayerController>(GetController());
	if (!Player || !Player->IsLocalController())
	{
		return;
	}

	// Look. Kept here rather than in the controller's input scaling so the feel doesn't depend on ini settings.
	float MouseX = 0.0f, MouseY = 0.0f;
	Player->GetInputMouseDelta(MouseX, MouseY);
	Yaw = FMath::Fmod(Yaw + MouseX * MouseSensitivity, 360.0f);
	Pitch = FMath::Clamp(Pitch + MouseY * MouseSensitivity, -89.9f, 89.9f);
	Player->SetControlRotation(FRotator(Pitch, Yaw, 0.0f));

	if (Player->WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		SpeedScale = FMath::Min(SpeedScale * 1.25f, 10.0f);
	}
	if (Player->WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		SpeedScale = FMath::Max(SpeedScale / 1.25f, 0.1f);
	}

	// Move. Like flying in Minecraft, forward ignores pitch and up and down are separate keys.
	const FVector Forward = FRotator(0.0f, Yaw, 0.0f).Vector();
	const FVector Right = FVector::CrossProduct(FVector::UpVector, Forward);
	FVector Wish = FVector::ZeroVector;
	Wish += Forward * ((Player->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f) - (Player->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f));
	Wish += Right * ((Player->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f) - (Player->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f));
	Wish += FVector::UpVector * ((Player->IsInputKeyDown(EKeys::SpaceBar) ? 1.0f : 0.0f) - (Player->IsInputKeyDown(EKeys::LeftShift) ? 1.0f : 0.0f));
	Wish = Wish.GetClampedToMaxSize(1.0f);

	const bool bSprint = Player->IsInputKeyDown(EKeys::LeftControl);
	const float Speed = FlySpeed * SpeedScale * (bSprint ? SprintMultiplier : 1.0f) * McCoords::UnitsPerBlock;
	Velocity = FMath::VInterpTo(Velocity, Wish * Speed, DeltaSeconds, 10.0f);
	SetActorLocation(GetActorLocation() + Velocity * DeltaSeconds);

	ShowPosition();
}

void AMcFreeCameraPawn::ShowPosition() const
{
#if !UE_BUILD_SHIPPING
	if (GEngine)
	{
		const FVector Block = McCoords::UnrealToBlock(GetActorLocation());
		const FIntVector BlockPos(FMath::FloorToInt32(Block.X), FMath::FloorToInt32(Block.Y), FMath::FloorToInt32(Block.Z));
		GEngine->AddOnScreenDebugMessage(static_cast<uint64>(GetUniqueID()), 0.0f, FColor::White, FString::Printf(
			TEXT("XYZ: %.3f / %.3f / %.3f\nBlock: %d %d %d\nChunk: %d %d (in %d %d %d)\nSpeed x%.2f"),
			Block.X, Block.Y, Block.Z,
			BlockPos.X, BlockPos.Y, BlockPos.Z,
			McCoords::BlockToChunk(BlockPos.X), McCoords::BlockToChunk(BlockPos.Z),
			McCoords::BlockToLocal(BlockPos.X), McCoords::BlockToLocal(BlockPos.Y), McCoords::BlockToLocal(BlockPos.Z),
			SpeedScale));
	}
#endif
}
