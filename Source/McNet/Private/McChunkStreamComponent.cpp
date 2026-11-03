#include "McChunkStreamComponent.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McChunkCodec.h"
#include "McNet.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// Well under the size Unreal sends as one bunch.
	constexpr int32 PieceSize = 16 * 1024;
	// Block changes sent in one go; more wait for the next tick.
	constexpr int32 MaxChangesPerMessage = 4096;
}

UMcChunkStreamComponent::UMcChunkStreamComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UMcChunkStreamComponent::SetBlockWorld(FMcBlockWorld* InWorld)
{
	Unbind();
	BlockWorld = InWorld;
	// Whether this controller's player is remote can still change while it logs in, so the server always
	// listens; the handlers only act for chunks it has sent.
	const AActor* Owner = GetOwner();
	if (BlockWorld && Owner && Owner->HasAuthority())
	{
		BlockChangedHandle = BlockWorld->OnBlockChanged.AddUObject(this, &UMcChunkStreamComponent::HandleBlockChanged);
		BlockEntityHandle = BlockWorld->OnBlockEntityChanged.AddUObject(this, &UMcChunkStreamComponent::HandleBlockEntityChanged);
		ChunkRemovedHandle = BlockWorld->OnChunkRemoved.AddUObject(this, &UMcChunkStreamComponent::HandleChunkRemoved);
	}
	if (BlockWorld && Owner && !Owner->HasAuthority())
	{
		ServerReady();
	}
}

void UMcChunkStreamComponent::SwitchWorld(FMcBlockWorld* NewWorld, const FString& DimensionId)
{
	SetBlockWorld(NewWorld);
	Sent.Reset();
	PendingChanges.Reset();
	PendingEntities.Reset();
	PiecesInFlight = 0;
	if (IsServerForRemotePlayer())
	{
		// Nothing goes out until the client has a world for it.
		bClientReady = false;
		ClientSwitchWorld(DimensionId);
	}
}

void UMcChunkStreamComponent::ClientSwitchWorld_Implementation(const FString& DimensionId)
{
	Arriving.Reset();
	Unbind();
	BlockWorld = nullptr;
	if (OnWorldSwitched)
	{
		OnWorldSwitched(DimensionId);
	}
}

void UMcChunkStreamComponent::ServerReady_Implementation()
{
	bClientReady = true;
}

void UMcChunkStreamComponent::Unbind()
{
	if (BlockWorld)
	{
		BlockWorld->OnBlockChanged.Remove(BlockChangedHandle);
		BlockWorld->OnBlockEntityChanged.Remove(BlockEntityHandle);
		BlockWorld->OnChunkRemoved.Remove(ChunkRemovedHandle);
	}
	BlockChangedHandle.Reset();
	BlockEntityHandle.Reset();
	ChunkRemovedHandle.Reset();
}

void UMcChunkStreamComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Unbind();
	BlockWorld = nullptr;
	Super::EndPlay(Reason);
}

bool UMcChunkStreamComponent::IsServerForRemotePlayer() const
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	return Controller && Controller->HasAuthority() && !Controller->IsLocalController();
}

FIntPoint UMcChunkStreamComponent::GetPlayerChunk(bool& bOutHasPlayer) const
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	bOutHasPlayer = Pawn != nullptr;
	if (!Pawn)
	{
		return FIntPoint::ZeroValue;
	}
	const FVector Block = McCoords::UnrealToBlock(Pawn->GetActorLocation());
	return FIntPoint(McCoords::BlockToChunk(FMath::FloorToInt32(Block.X)), McCoords::BlockToChunk(FMath::FloorToInt32(Block.Z)));
}

void UMcChunkStreamComponent::RequestSetBlock(const FIntVector& Pos, FMcStateId State)
{
	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		OnSetBlockRequest.ExecuteIfBound(Pos, State);
	}
	else
	{
		ServerSetBlock(Pos, State);
	}
}

void UMcChunkStreamComponent::RequestAttack(const FVector& Look)
{
	const AActor* Owner = GetOwner();
	if (Owner && Owner->HasAuthority())
	{
		if (OnAttackRequest)
		{
			OnAttackRequest(Look);
		}
	}
	else
	{
		ServerAttack(Look);
	}
}

void UMcChunkStreamComponent::ServerAttack_Implementation(FVector_NetQuantizeNormal Look)
{
	if (OnAttackRequest)
	{
		OnAttackRequest(FVector(Look));
	}
}

void UMcChunkStreamComponent::ServerSetBlock_Implementation(FIntVector Pos, uint16 State)
{
	OnSetBlockRequest.ExecuteIfBound(Pos, State);
}

void UMcChunkStreamComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!BlockWorld || !IsServerForRemotePlayer() || !bClientReady)
	{
		return;
	}

	// Changes first, so they never arrive after a chunk sent later that already has them.
	for (int32 Start = 0; Start < PendingChanges.Num(); Start += MaxChangesPerMessage)
	{
		ClientBlockChanges(TArray<FMcNetBlockChange>(PendingChanges.GetData() + Start, FMath::Min(MaxChangesPerMessage, PendingChanges.Num() - Start)));
	}
	PendingChanges.Reset();
	// Then block entities, after the blocks they belong to (a sign's text after the sign).
	for (const FIntVector& Pos : PendingEntities)
	{
		const FMcChunk* Chunk = BlockWorld->FindChunk(FIntPoint(McCoords::BlockToChunk(Pos.X), McCoords::BlockToChunk(Pos.Z)));
		const FMcBlockEntity* Entity = Chunk ? Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15) : nullptr;
		TArray<uint8> Bytes;
		if (Entity)
		{
			FMemoryWriter Writer(Bytes);
			FMcBlockEntity Copy = *Entity;
			Copy.Serialize(Writer);
		}
		ClientBlockEntity(Pos, Bytes);
	}
	PendingEntities.Reset();

	SendChunks();
}

void UMcChunkStreamComponent::SendChunks()
{
	bool bHasPlayer = false;
	const FIntPoint Centre = GetPlayerChunk(bHasPlayer);
	if (!bHasPlayer)
	{
		return;
	}

	// Chunks gone out of range: the client drops them. A margin stops churn along the edge.
	const int32 KeepRadius = SendRadius + 1;
	TArray<FIntPoint> Far;
	for (const FIntPoint& Pos : Sent)
	{
		if (FMath::Abs(Pos.X - Centre.X) > KeepRadius || FMath::Abs(Pos.Y - Centre.Y) > KeepRadius)
		{
			Far.Add(Pos);
		}
	}
	for (const FIntPoint& Pos : Far)
	{
		Sent.Remove(Pos);
		ClientUnloadChunk(Pos);
	}

	if (PiecesInFlight >= MaxPiecesInFlight)
	{
		return;
	}

	// Loaded chunks in range not sent yet, nearest first.
	TArray<TPair<int32, FIntPoint>> Wanted;
	for (int32 DZ = -SendRadius; DZ <= SendRadius; ++DZ)
	{
		for (int32 DX = -SendRadius; DX <= SendRadius; ++DX)
		{
			const FIntPoint Pos = Centre + FIntPoint(DX, DZ);
			if (!Sent.Contains(Pos) && BlockWorld->HasChunk(Pos))
			{
				Wanted.Add({ DX * DX + DZ * DZ, Pos });
			}
		}
	}
	Wanted.Sort([](const TPair<int32, FIntPoint>& A, const TPair<int32, FIntPoint>& B) { return A.Key < B.Key; });

	for (const TPair<int32, FIntPoint>& Entry : Wanted)
	{
		if (PiecesInFlight >= MaxPiecesInFlight)
		{
			break;
		}
		const TArray<uint8> Bytes = McChunkCodec::Encode(*BlockWorld->FindChunk(Entry.Value));
		const int32 NumPieces = FMath::Max(1, FMath::DivideAndRoundUp(Bytes.Num(), PieceSize));
		if (NumPieces > 255)
		{
			UE_LOG(LogMcNet, Error, TEXT("Chunk %d %d is too big to send (%d bytes)"), Entry.Value.X, Entry.Value.Y, Bytes.Num());
			continue;
		}
		for (int32 Piece = 0; Piece < NumPieces; ++Piece)
		{
			const int32 Start = Piece * PieceSize;
			ClientChunkPiece(Entry.Value, static_cast<uint8>(Piece), static_cast<uint8>(NumPieces),
				TArray<uint8>(Bytes.GetData() + Start, FMath::Min(PieceSize, Bytes.Num() - Start)));
		}
		PiecesInFlight += NumPieces;
		Sent.Add(Entry.Value);
		if (GetOpenBoxes)
		{
			TArray<FIntVector> Boxes;
			TArray<int32> Openers;
			GetOpenBoxes(Entry.Value, Boxes, Openers);
			if (!Boxes.IsEmpty())
			{
				ClientOpenBoxes(Boxes, Openers);
			}
		}
	}
}

void UMcChunkStreamComponent::HandleBlockChanged(const FIntVector& Pos, FMcStateId State)
{
	// Only chunks the client has; any other gets the change when it is sent whole.
	if (Sent.Contains(FIntPoint(McCoords::BlockToChunk(Pos.X), McCoords::BlockToChunk(Pos.Z))))
	{
		PendingChanges.Add({ Pos, State });
	}
}

void UMcChunkStreamComponent::HandleBlockEntityChanged(const FIntVector& Pos)
{
	// As with blocks: a chunk not sent yet carries the entity when it goes.
	if (Sent.Contains(FIntPoint(McCoords::BlockToChunk(Pos.X), McCoords::BlockToChunk(Pos.Z))))
	{
		PendingEntities.AddUnique(Pos);
	}
}

void UMcChunkStreamComponent::HandleChunkRemoved(FIntPoint Pos)
{
	if (Sent.Remove(Pos) > 0)
	{
		ClientUnloadChunk(Pos);
	}
}

void UMcChunkStreamComponent::ServerAckPieces_Implementation(int32 Count)
{
	PiecesInFlight = FMath::Max(0, PiecesInFlight - Count);
}

void UMcChunkStreamComponent::ClientChunkPiece_Implementation(FIntPoint Pos, uint8 Piece, uint8 NumPieces, const TArray<uint8>& Bytes)
{
	ServerAckPieces(1);
	if (NumPieces == 0 || Piece >= NumPieces)
	{
		return;
	}
	FPartialChunk& Partial = Arriving.FindOrAdd(Pos);
	if (Partial.Pieces.Num() != NumPieces)
	{
		Partial.Pieces.Reset();
		Partial.Pieces.SetNum(NumPieces);
		Partial.Received = 0;
	}
	if (Partial.Pieces[Piece].IsEmpty())
	{
		++Partial.Received;
	}
	Partial.Pieces[Piece] = Bytes;
	if (Partial.Received < NumPieces)
	{
		return;
	}

	TArray<uint8> Whole;
	for (const TArray<uint8>& Each : Partial.Pieces)
	{
		Whole.Append(Each);
	}
	Arriving.Remove(Pos);
	if (!BlockWorld)
	{
		return;
	}
	TUniquePtr<FMcChunk> Chunk = BlockWorld->MakeChunk(Pos);
	if (!McChunkCodec::Decode(Whole, *Chunk))
	{
		UE_LOG(LogMcNet, Error, TEXT("Chunk %d %d arrived damaged"), Pos.X, Pos.Y);
		return;
	}
	BlockWorld->AddChunk(MoveTemp(Chunk));
}

void UMcChunkStreamComponent::ClientUnloadChunk_Implementation(FIntPoint Pos)
{
	Arriving.Remove(Pos);
	if (BlockWorld)
	{
		BlockWorld->RemoveChunk(Pos);
	}
}

void UMcChunkStreamComponent::ClientBlockChanges_Implementation(const TArray<FMcNetBlockChange>& Changes)
{
	if (!BlockWorld)
	{
		return;
	}
	UE_LOG(LogMcNet, Verbose, TEXT("%d block changes from the server"), Changes.Num());
	for (const FMcNetBlockChange& Change : Changes)
	{
		BlockWorld->SetBlock(Change.Pos, Change.State);
	}
}

void UMcChunkStreamComponent::ClientOpenBoxes_Implementation(const TArray<FIntVector>& Boxes, const TArray<int32>& Openers)
{
	for (int32 Index = 0; Index < Boxes.Num() && Index < Openers.Num(); ++Index)
	{
		if (OnBoxOpen)
		{
			OnBoxOpen(Boxes[Index], Openers[Index]);
		}
	}
}

void UMcChunkStreamComponent::ClientBlockEntity_Implementation(FIntVector Pos, const TArray<uint8>& Bytes)
{
	FMcChunk* Chunk = BlockWorld ? BlockWorld->FindChunk(FIntPoint(McCoords::BlockToChunk(Pos.X), McCoords::BlockToChunk(Pos.Z))) : nullptr;
	if (!Chunk)
	{
		return;
	}
	if (Bytes.IsEmpty())
	{
		Chunk->RemoveBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
	}
	else
	{
		FMcBlockEntity Entity;
		FMemoryReader Reader(Bytes);
		if (!Entity.Serialize(Reader))
		{
			UE_LOG(LogMcNet, Error, TEXT("Block entity at %d %d %d arrived damaged"), Pos.X, Pos.Y, Pos.Z);
			return;
		}
		Chunk->SetBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15, MoveTemp(Entity));
	}
	BlockWorld->OnBlockEntityChanged.Broadcast(Pos);
}
