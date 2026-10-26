// Streams blocks to one remote player. Lives on that player's controller: on the server it sends the chunks
// around the player (nearest first, a few in flight at a time) and the block changes inside them; on the
// client it builds them into the client's own block world. Players on the host share the server's world and
// get nothing sent. Edits go the other way: the client asks, the server decides.
//
// Unreal replication isn't made for this much data, so it all goes as reliable RPCs, with chunks cut into
// pieces small enough for one bunch and acknowledged so the reliable buffer never overflows.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "McCore.h"
#include "McChunkStreamComponent.generated.h"

class FMcBlockWorld;

USTRUCT()
struct FMcNetBlockChange
{
	GENERATED_BODY()

	UPROPERTY()
	FIntVector Pos = FIntVector::ZeroValue;

	UPROPERTY()
	uint16 State = 0;
};

DECLARE_DELEGATE_TwoParams(FMcOnSetBlockRequest, const FIntVector& /*Pos*/, FMcStateId /*State*/);

UCLASS()
class MCNET_API UMcChunkStreamComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMcChunkStreamComponent();

	// Chunks within this many of the player's chunk are sent; one more than the view distance, as the
	// renderer meshes a chunk only when all its neighbours are there.
	UPROPERTY(EditAnywhere, Category = "Mc")
	int32 SendRadius = 13;

	// Chunk pieces sent but not yet acknowledged.
	UPROPERTY(EditAnywhere, Category = "Mc")
	int32 MaxPiecesInFlight = 32;

	// Server: the world to send from. Client: the world to build into.
	void SetBlockWorld(FMcBlockWorld* InWorld);

	// Server: what to do when the client asks for a block change (checks, then applies it).
	FMcOnSetBlockRequest OnSetBlockRequest;

	// Server: what to do when the client hits at something (the direction it looked, Minecraft axes).
	TFunction<void(const FVector& Look)> OnAttackRequest;
	// Either side: the player swings at whatever is in front of them.
	void RequestAttack(const FVector& Look);

	// Either side: changes a block for this player. On the server (and for host players) it goes straight
	// to OnSetBlockRequest; on a client it is sent to the server.
	void RequestSetBlock(const FIntVector& Pos, FMcStateId State);

	int32 NumChunksSent() const { return Sent.Num(); }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UFUNCTION(Client, Reliable)
	void ClientChunkPiece(FIntPoint Pos, uint8 Piece, uint8 NumPieces, const TArray<uint8>& Bytes);

	UFUNCTION(Client, Reliable)
	void ClientUnloadChunk(FIntPoint Pos);

	UFUNCTION(Client, Reliable)
	void ClientBlockChanges(const TArray<FMcNetBlockChange>& Changes);

	UFUNCTION(Server, Reliable)
	void ServerAckPieces(int32 Count);

	// The client has a world to build chunks into; until then anything sent would be lost.
	UFUNCTION(Server, Reliable)
	void ServerReady();

	UFUNCTION(Server, Reliable)
	void ServerSetBlock(FIntVector Pos, uint16 State);

	UFUNCTION(Server, Reliable)
	void ServerAttack(FVector_NetQuantizeNormal Look);

private:
	bool IsServerForRemotePlayer() const;
	FIntPoint GetPlayerChunk(bool& bOutHasPlayer) const;
	void SendChunks();
	void HandleBlockChanged(const FIntVector& Pos, FMcStateId State);
	void HandleChunkRemoved(FIntPoint Pos);
	void Unbind();

	FMcBlockWorld* BlockWorld = nullptr;
	FDelegateHandle BlockChangedHandle;
	FDelegateHandle ChunkRemovedHandle;

	// Server side.
	bool bClientReady = false;
	TSet<FIntPoint> Sent;
	TArray<FMcNetBlockChange> PendingChanges;
	int32 PiecesInFlight = 0;

	// Client side: pieces of chunks still arriving.
	struct FPartialChunk
	{
		TArray<TArray<uint8>> Pieces;
		int32 Received = 0;
	};
	TMap<FIntPoint, FPartialChunk> Arriving;
};
