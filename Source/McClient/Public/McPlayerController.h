// One per player, on the server and on that player's own machine. It carries the chunk stream, and on the
// player's machine it sets up what they see (renderer, sky) once the world actor has arrived, and connects
// their pawn to the blocks.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "McPlayerController.generated.h"

class AMcWorldActor;
class AMcPlayerPawn;
class UActorComponent;
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

	// The dimension this player is in (null until they have joined a world).
	AMcWorldActor* GetWorldActor() const { return WorldActor; }
	// Server: the player is in another dimension's world now (a portal, a respawn). Their stream, pawn and, if they are on
	// this machine, what they see all follow; the pawn is moved by the caller.
	void EnterWorld(AMcWorldActor& NewWorld);

private:
	void ConnectToWorld();
	void SetUpLocalView();
	void SpawnEnvironment();
	// The renderer and sky of the world the player is in now.
	void AttachView();
	void DetachView();
	// Leaves the world the player was in (Old) for WorldActor on the machine that shows it.
	void SwitchLocalView(AMcWorldActor* Old);
	// A client was told the dimension changed: moves to that world once its actor is here.
	void ResolveWantedDimension();
	// Hides what belongs to the other dimensions, and mutes their sounds.
	void SyncDimensionView();
	void StartDevScreenshot();
	void HandleSetBlockRequest(const FIntVector& Pos, uint16 State);
	// Server: connects the pawn's survival rules to the world (blocks, items, windows, respawning).
	void HookServerPawn(AMcPlayerPawn& Hooked);

	UPROPERTY(Transient)
	TObjectPtr<AMcWorldActor> WorldActor;

	bool bStreamConnected = false;
	bool bViewSetUp = false;
	bool bEnvironmentSpawned = false;
	FString WantedDimension;
	float ViewSyncTimer = 0.0f;
	TWeakObjectPtr<UActorComponent> Renderer;
	TWeakObjectPtr<UActorComponent> Sky;
	float NetLogTimer = 0.0f;
};
