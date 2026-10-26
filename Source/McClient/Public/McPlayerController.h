// One per player, on the server and on that player's own machine. It carries the chunk stream, and on the
// player's machine it sets up what they see (renderer, sky) once the world actor has arrived, and connects
// their pawn to the blocks.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "McPlayerController.generated.h"

class AMcWorldActor;
class UMcChunkStreamComponent;

UCLASS()
class AMcPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AMcPlayerController();

	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, Category = "Mc")
	TObjectPtr<UMcChunkStreamComponent> ChunkStream;

	// How far from a player's eyes the server accepts their edits, in blocks: reach plus some slack for lag.
	UPROPERTY(EditAnywhere, Category = "Mc")
	float MaxEditDistance = 8.0f;

private:
	void ConnectToWorld();
	void SetUpLocalView();
	void SpawnEnvironment();
	void StartDevScreenshot();
	void HandleSetBlockRequest(const FIntVector& Pos, uint16 State);

	UPROPERTY(Transient)
	TObjectPtr<AMcWorldActor> WorldActor;

	bool bStreamConnected = false;
	bool bViewSetUp = false;
	float NetLogTimer = 0.0f;
};
