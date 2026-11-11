// What using a block does for a survival player on the server: a crafting table, a chest or a furnace opens its
// window. Chests share one container between everyone who has it open (kept in step with the chunk's block entity,
// which is what is saved), and furnaces are kept here, burning with nobody looking, and saved with their chunk as
// block entities. Breaking either drops what is inside. Hoppers are kept here too, moving items between them. Each dimension has its own world actor; blocks are keyed by
// the dimension they are in.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "McBlockEntity.h"
#include "McHopper.h"
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

	// Opens the window for what is at Pos, if it is one of those, or works a lever, button, door, trapdoor or gate;
	// true if it did. Server only.
	bool Use(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos);

private:
	// A click on a jukebox: puts the held disc in, or takes the one in it out. True if it did either.
	bool UseJukebox(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos);
	// Brings a jukebox's block state and music in line with its slot, after a hand or a hopper changed it: a disc newly
	// in starts its song, one taken out silences it.
	void SyncJukebox(AMcWorldActor& WorldActor, const FIntVector& Pos);
	void StopJukebox(AMcWorldActor& WorldActor, const FIntVector& Pos);
	void TickJukeboxes();

	// A click on a bed: sets the respawn point, and puts the player to sleep if it is night and no monsters are near.
	void UseBed(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos);

public:

	// Whether the window the player has open may stay open: the block is still what it was, and they are close.
	static bool IsWindowValid(const AMcPlayerPawn& Player, const AMcWorldActor& WorldActor, const FMcWindow& Window);

	// The block at Pos is about to go (or has): what its chest or furnace holds is dropped there and forgotten. Does
	// nothing for any other block.
	void ReleaseBlock(AMcWorldActor& WorldActor, const FIntVector& Pos);

	// A sign was just placed at Pos by Player: it gets its block entity (no text yet) and the player the editor for its
	// front (minecraft.wiki, "Sign").
	void PlacedSign(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos);

	// A shulker box at Pos is being broken: what it holds goes into the box item among Drops (and is forgotten there).
	// A decorated pot's drop: the pot with its sides, or (cracked) the sides themselves.
	void PackPotDrops(AMcWorldActor& WorldActor, const FIntVector& Pos, TArray<FMcItemStack>& Drops);
	void PackBoxDrops(AMcWorldActor& WorldActor, const FIntVector& Pos, TArray<FMcItemStack>& Drops);
	// A shulker box item was placed at Pos: what it carried goes into the new box.
	void FillBox(AMcWorldActor& WorldActor, const FIntVector& Pos, const FMcItemStack& Box);

	// A jukebox at Pos: whether its record is playing (it plays for the song's length once put in, then keeps the disc).
	bool IsJukeboxPlaying(const AMcWorldActor& WorldActor, const FIntVector& Pos) const;

	// How many furnaces and brewing stands are being kept (for tests and logs).
	int32 NumFurnaces() const { return Machines.Num(); }
	// How many hoppers are at work.
	int32 NumHoppers() const { return Hoppers.Num(); }

	// A powered dispenser or dropper at Pos fires one item from a random slot (minecraft.wiki, "Dispenser",
	// "Dropper"). A dropper puts it into a container in front, or throws it out. A dispenser shoots an arrow, uses
	// bone meal on the block in front, or throws the item out. Called by the redstone; a test calls it directly.
	void Dispense(AMcWorldActor& WorldActor, const FIntVector& Pos);
	// A powered crafter at Pos crafts once from its nine slots; the product goes into the container it faces, or drops. False if no recipe fits.
	bool Craft(AMcWorldActor& WorldActor, const FIntVector& Pos);

	// The furnace at Pos, loaded from its chunk's block entity if it is not being kept yet; null if there is none and
	// bCreate is false. Also what a test uses to look at one.
	TSharedPtr<FMcFurnace> GetFurnace(AMcWorldActor& WorldActor, const FIntVector& Pos, bool bCreate);
	// The same for a brewing stand.
	TSharedPtr<class FMcBrewingStand> GetBrewingStand(AMcWorldActor& WorldActor, const FIntVector& Pos, bool bCreate);
	// Writes every kept furnace and brewing stand of the world into its chunk (done at each save; a test does it by hand).
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
	// A furnace or brewing stand being kept: it works whether or not anyone has it open.
	struct FMachineEntry
	{
		TWeakObjectPtr<AMcWorldActor> World;
		TSharedPtr<class FMcMachine> Machine;
		EMcBlockEntityKind Kind = EMcBlockEntityKind::Furnace;
		TArray<uint8> Saved;
	};
	struct FChestEntry
	{
		TWeakObjectPtr<AMcWorldActor> World;
		TSharedPtr<FMcContainer> Container;
	};
	// A hopper at work; its items are a chest entry like any other container's.
	struct FHopperEntry
	{
		TWeakObjectPtr<AMcWorldActor> World;
		int32 Cooldown = 0;
		// The order hoppers were set down in, which is the order they take their turn in each tick.
		int64 Order = 0;
	};

	static FKey MakeKey(const AMcWorldActor& WorldActor, const FIntVector& Pos);
	TSharedPtr<FMcContainer> GetChest(AMcWorldActor& WorldActor, const FIntVector& Pos, int32 NumSlots);
	void WriteChest(AMcWorldActor& WorldActor, const FIntVector& Pos);
	// The machine at Pos (loaded from its block entity, or made if bCreate), if the block there is one.
	FMachineEntry* GetMachine(AMcWorldActor& WorldActor, const FIntVector& Pos, bool bCreate);
	void WriteMachine(const FKey& Key, FMachineEntry& Entry);
	void Sweep();
	void WatchWorld(AMcWorldActor& WorldActor);
	// Starts the hoppers, furnaces and brewing stands in a chunk that has come in (and on each sweep, for the rest).
	void StartChunk(AMcWorldActor& WorldActor, FIntPoint ChunkPos);
	// One game tick of every hopper.
	void TickHoppers();
	// One game tick of the food on every lit campfire: it cooks, and a cooked piece drops as its result.
	void TickCampfires();
	// Tells each world which shulker boxes are open, and for how many (their lids follow on every machine).
	void TickShulkers();
	// Moves an item into the block the hopper faces, then one in from above; true if either moved.
	bool TickHopper(AMcWorldActor& WorldActor, const FIntVector& Pos, FMcStateId State);
	// What a hopper at the given face of the block at Pos may reach of its slots (none if it isn't a container).
	FMcSlotAccess GetAccess(AMcWorldActor& WorldActor, const FIntVector& Pos, McHopper::EFace Face);
	// After items moved in or out of the block at Pos: a chest or hopper is written back to its block entity.
	void ContentsChanged(AMcWorldActor& WorldActor, const FIntVector& Pos);
	// A player used the sign at Pos: what they hold is put on the side they face (a dye, glow ink, an ink sac, or
	// honeycomb on the whole sign), or else that side's editor opens. A waxed sign takes nothing.
	bool UseSign(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos);
	// Opens the editor on one side of the sign at Pos, and writes what comes back to the sign.
	void EditSign(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos, bool bBack);
	// The sign's block entity, made (with no text) if the chunk has none for it; null if Pos holds no sign.
	static FMcBlockEntity* FindSign(AMcWorldActor& WorldActor, const FIntVector& Pos);
	// After a sign's text, dye or wax changed: saved with its chunk, and the players who see it told.
	static void SignChanged(AMcWorldActor& WorldActor, const FIntVector& Pos);
	// A player used the lectern at Pos (minecraft.wiki, "Lectern"): an empty one takes the book and quill or written
	// book they hold; one with a book opens it to read. False for an empty lectern and anything else in hand.
	static bool UseLectern(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos);
	// A player used the campfire at Pos (minecraft.wiki, "Campfire"): the food in hand that a campfire cooks goes on
	// the first free of its four spots (one of it; creative keeps its own). False for anything else in hand or no free spot.
	static bool UseCampfire(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos);
	// A reader of the lectern at Pos pressed a button: the book opens at the menu's page for everyone reading it, the
	// lectern giving a 2-tick pulse, or the book is taken off into the reader's inventory.
	static void LecternButton(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos, class FMcLecternMenu& Menu);
	// The lectern's block entity, if Pos holds a lectern with a book on it.
	static FMcBlockEntity* FindLectern(AMcWorldActor& WorldActor, const FIntVector& Pos);

	TMap<FKey, FChestEntry> Chests;
	TMap<FKey, FMachineEntry> Machines;
	TMap<FKey, FHopperEntry> Hoppers;
	// A record playing, until its song ends.
	struct FJukeboxPlay
	{
		TWeakObjectPtr<AMcWorldActor> World;
		int32 TicksLeft = 0;
		int32 Elapsed = 0;
		FName Sound;
	};
	TMap<FKey, FJukeboxPlay> Jukeboxes;
	int64 NextHopperOrder = 0;
	TSet<TWeakObjectPtr<AMcWorldActor>> Watched;
	float Accumulator = 0.0f;
	int32 CleanTimer = 0;
};
