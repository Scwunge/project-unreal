// M1's camera: flies like spectator mode. WASD to move, Space up, Shift down, Ctrl faster, the wheel sets speed.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "McFreeCameraPawn.generated.h"

class UCameraComponent;

UCLASS()
class MCPLAYER_API AMcFreeCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	AMcFreeCameraPawn();

	// Blocks per second. The wiki gives about 10.9 for creative flight and double that sprinting; to be
	// checked against measurements in 26.3 (PLAN.md, section 5).
	UPROPERTY(EditAnywhere, Category = "Mc")
	float FlySpeed = 10.92f;

	UPROPERTY(EditAnywhere, Category = "Mc")
	float SprintMultiplier = 2.0f;

	// Degrees per mouse count.
	UPROPERTY(EditAnywhere, Category = "Mc")
	float MouseSensitivity = 0.15f;

	UPROPERTY(VisibleAnywhere, Category = "Mc")
	TObjectPtr<UCameraComponent> Camera;

	// Puts the camera at a Minecraft position (x, y, z in blocks), looking along Minecraft's yaw and pitch:
	// yaw 0 faces south (+z) and turns towards west, pitch 90 looks straight down, as on the F3 screen.
	void SetView(const FVector& BlockPos, float McYaw, float McPitch);

protected:
	virtual void PossessedBy(AController* NewController) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ShowPosition() const;

	FVector Velocity = FVector::ZeroVector;
	float Yaw = 0.0f;
	float Pitch = 0.0f;
	// Changed by the mouse wheel, like spectator mode's speed.
	float SpeedScale = 1.0f;
};
