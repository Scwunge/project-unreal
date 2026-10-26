// M1's game mode: one flat overworld, a free camera, and (outside the dedicated server) the greybox renderer.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "McGameMode.generated.h"

class AMcWorldActor;

UCLASS()
class AMcGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMcGameMode();

	// Blocks above the surface that players start at.
	UPROPERTY(EditAnywhere, Category = "Mc")
	int32 SpawnHeightAboveSurface = 10;

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;

private:
	FTransform GetSpawnTransform() const;
	void SpawnEnvironment();

	// Development: -McCam=x,y,z,yaw,pitch puts the camera at a Minecraft position and angle; -McShot=<png>
	// saves a screenshot after -McShotDelay seconds (default 5) and quits. Used for side-by-side checks.
	void StartDevCapture();
	// Development: -McScene=<name> places tools/scenes/<name>.json and returns its camera.
	void LoadDevScene(const FString& Name, FString& OutCamera);

	UPROPERTY(Transient)
	TObjectPtr<AMcWorldActor> WorldActor;
};
