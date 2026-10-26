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

	UPROPERTY(Transient)
	TObjectPtr<AMcWorldActor> WorldActor;
};
