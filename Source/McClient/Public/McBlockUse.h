// What using a block does for a survival player on the server: a crafting table, a chest or a furnace opens its
// window. Chests share one container between everyone who has it open (kept in step with the chunk's block entity,
// which is what is saved), and furnaces are kept here, burning with nobody looking, and saved with their chunk as
// block entities. Breaking either drops what is inside. Each dimension has its own world actor; blocks are keyed by
// the dimension they are in.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "McBlockUse.generated.h"

class AMcPlayerPawn;
class AMcWorldActor;
class FMcContainer;
class FMcFurnace;
struct FMcItemStack;
struct FMcWindow;

UCLASS()
class UMcBlockUseSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMcBlockUseSubsystem, STATGROUP_Tickables); }
	virtual void Tick(float DeltaSeconds) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }

	// Opens the window for what is at Pos, if it is one of those; true if it was. Server only.
	bool Use(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos);

	// Whether the window the player has open may stay open: the block is still what it was, and they are close.
	static bool IsWindowValid(const AMcPlayerPawn& Player, const AMcWorldActor& WorldActor, const FMcWindow& Window);

	// The block at Pos is about to go (or has): what its chest or furnace holds is dropped there and forgotten. Does
	// nothing for any other block.
	void ReleaseBlock(AMcWorldActor& WorldActor, const FIntVector& Pos);

	// How many furnaces are being kept (for tests and logs).
	int32 NumFurnaces() const { return Furnaces.Num(); }

	// The furnace at Pos, loaded from its chunk's block entity if it is not being kept yet; null if there is none and
	// bCreate is false. Also what a test uses to look at one.
	TSharedPtr<FMcFurnace> GetFurnace(AMcWorldActor& WorldActor, const FIntVector& Pos, bool bCreate);
	// Writes every kept furnace of the world into its chunk (done at each save; a test does it by hand).
	void SaveFurnaces(AMcWorldActor& WorldActor);
	// Forgets the kept furnaces and chests, as when the server restarts (a test uses it).
	void ForgetAll();

	// What a chest or furnace holds, as the slots to drop; used by ReleaseBlock and testable on its own.
	static void GetContents(const AMcWorldActor& WorldActor, const FIntVector& Pos, TArray<FMcItemStack>& Out);

private:
	struct FKey
	{
		FName Dimension;
		FIntVector Pos;
		bool operator==(const FKey& Other) const { return Dimension == Other.Dimension && Pos == Other.Pos; }
		friend uint32 GetTypeHash(const FKey& Key) { return HashCombine(GetTypeHash(Key.Dimension), GetTypeHash(Key.Pos)); }
	};
	struct FFurnaceEntry
	{
		TWeakObjectPtr<AMcWorldActor> World;
		TSharedPtr<FMcFurnace> Furnace;
		TArray<uint8> Saved;
	};
	struct FChestEntry
	{
		TWeakObjectPtr<AMcWorldActor> World;
		TSharedPtr<FMcContainer> Container;
	};

	static FKey MakeKey(const AMcWorldActor& WorldActor, const FIntVector& Pos);
	TSharedPtr<FMcContainer> GetChest(AMcWorldActor& WorldActor, const FIntVector& Pos, int32 NumSlots);
	void WriteChest(AMcWorldActor& WorldActor, const FIntVector& Pos);
	void WriteFurnace(const FKey& Key, FFurnaceEntry& Entry);
	void Sweep();
	void WatchWorld(AMcWorldActor& WorldActor);

	TMap<FKey, FChestEntry> Chests;
	TMap<FKey, FFurnaceEntry> Furnaces;
	TSet<TWeakObjectPtr<AMcWorldActor>> Watched;
	float Accumulator = 0.0f;
	int32 CleanTimer = 0;
};
