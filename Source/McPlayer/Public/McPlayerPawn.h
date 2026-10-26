// The player: walks, sprints, sneaks and jumps with Minecraft's movement, and flies like creative mode after a
// double tap of Space. Movement runs at 20 ticks a second; the camera is drawn between the last two ticks.
// WASD moves, Space jumps, Shift sneaks, Ctrl sprints.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "McPhysics.h"
#include "McPlayerPawn.generated.h"

class FMcBlockWorld;
class UCameraComponent;

UCLASS()
class MCPLAYER_API AMcPlayerPawn : public APawn
{
	GENERATED_BODY()

public:
	AMcPlayerPawn();

	// Degrees per mouse count.
	UPROPERTY(EditAnywhere, Category = "Mc")
	float MouseSensitivity = 0.15f;

	// Whether a double tap of Space starts and stops flying, as in creative mode.
	UPROPERTY(EditAnywhere, Category = "Mc")
	bool bMayFly = true;

	UPROPERTY(VisibleAnywhere, Category = "Mc")
	TObjectPtr<UCameraComponent> Camera;

	// The blocks to move through. Until it is set, or while the chunk underfoot isn't loaded, the player
	// stays where it is.
	void SetBlockWorld(const FMcBlockWorld* InWorld) { BlockWorld = InWorld; }

	// Puts the eyes at a Minecraft position (x, y, z in blocks), looking along Minecraft's yaw and pitch
	// (yaw 0 faces south, pitch 90 looks straight down), and starts flying so the view stays put.
	void SetView(const FVector& EyePos, float McYaw, float McPitch);

	bool IsFlying() const { return bFlying; }
	const FMcBody& GetBody() const { return Body; }

protected:
	virtual void PossessedBy(AController* NewController) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void GameTick(const FMcMoveInput& Input);
	float GetEyeHeight() const;
	void ShowPosition() const;

	const FMcBlockWorld* BlockWorld = nullptr;
	FMcBody Body;
	FVector PreviousPosition = FVector::ZeroVector;
	float PreviousEyeHeight = 1.62f;
	// Where Tick last put the actor; anything else moving it (spawning, teleports) is taken as a teleport.
	FVector LastLocation = FVector(TNumericLimits<double>::Max());
	bool bFlying = false;
	bool bSneaking = false;
	bool bSprinting = false;
	// Ticks left in which a second Space press toggles flying.
	int32 FlyToggleTicks = 0;
	bool bJumpWasDown = false;
	// Time not yet used up by whole ticks.
	float TickAccumulator = 0.0f;
	float Yaw = 0.0f;
	float Pitch = 0.0f;
};
