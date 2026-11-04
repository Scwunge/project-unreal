// Placed chests and shulker boxes. A chest's block has no model of its own (its block model is only a particle picture, so the chunk
// meshes leave it out): the game draws it from the chest's entity picture, with a lid that opens. This finds the
// chests in the loaded chunks and stands an entity model (McEntityModel's "mc:chest/...") in each: the chest, trapped
// chest, ender chest and copper chests, single or half of a double, turned to their facing. Shulker boxes are the same
// (McEntityModel's "mc:shulker_box/..."), turned so the lid faces the way the box does, the lid rising and turning as
// the box opens. A bell's body is one too ("mc:bell"; its frame is the block model), swinging when it is rung, and so
// are the books ("mc:book"): lying open on a lectern that has one, and floating over an enchanting table, turning to
// the nearest player and opening, its pages flipping, while one is near. Mob heads are too ("mc:head/..."), and
// decorated pots and conduits (the shell, or while it is awake the turning cage with its eye on the player).

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "McCore.h"
#include "McChestComponent.generated.h"

class FMcBlockWorld;
class UMcEntityModelComponent;

// An enchanting table's book, stepped 20 times a second as vanilla's is (every rule here measured from vanilla's
// per-tick state, E:/mc-reference/scripts/table_book_log.txt and table_book_run2.txt). Rot is the way its pages face
// (radians, Minecraft's atan2(dz, dx): 0 east, half pi south); Open runs 0 to 1; Flip is the loose pages' turn, a whole
// number at rest, springing towards FlipT.
struct MCRENDER_API FMcTableBook
{
	int32 Time = 0;
	float Rot = 0.0f, ORot = 0.0f, TRot = 0.0f;
	float Open = 0.0f, OOpen = 0.0f;
	float Flip = 0.0f, OFlip = 0.0f, FlipT = 0.0f, FlipA = 0.0f;

	// One tick. bNear: a player's feet are within 3 blocks of the table's middle; ToPlayer is the way to them (x, z).
	void Tick(bool bNear, const FVector2D& ToPlayer, FRandomStream& Random);
	// How far over loose page 0 or 1 is, from lying on the right half (0) to lying on the left (1), at a flip.
	static float PageTurn(float Flip, int32 Page);
};

// The pieces of food lying on one campfire's grill, by spot.
USTRUCT()
struct FMcGrillFood
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMcEntityModelComponent>> Spots;
};

UCLASS(ClassGroup = (Mc))
class MCRENDER_API UMcChestComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMcChestComponent();

	// Null stops (and takes the chests away). The world must outlive the component or be unset first.
	void SetWorld(FMcBlockWorld* InWorld);

	// Whether the chest at a block is open (someone has it open); asked each frame for each chest. Both halves of a
	// double chest open when either is.
	TFunction<bool(const FIntVector&)> IsOpen;

	int32 NumChests() const { return Chests.Num(); }

	// The bell at Pos was struck on Side (McBell's numbering): it swings away from that side for a while.
	void RingBell(const FIntVector& Pos, int32 Side);
	// How the bell at Pos is swung now (McEntityModel::FAnim's pitch and roll), zero if it isn't swinging.
	FVector2D GetBellSwing(const FIntVector& Pos) const;

	// The spawner at Pos started a new wait: the mob inside slows down and speeds up again.
	void SpawnerCycled(const FIntVector& Pos);
	// A mob's hitbox width and height (by its type), for the size of the one turning in a spawner.
	TFunction<FVector2D(FName)> GetMobSize;
	// The spin (Minecraft degrees) of the mob in the spawner at Pos, and the client's wait, or -1 for no spawner.
	float GetSpawnerSpin(const FIntVector& Pos) const { const FChest* Found = Chests.Find(Pos); return Found && Found->Shape == EShape::Spawner ? Found->Spin : -1.0f; }

	// The model a chest state is drawn with ("mc:chest/normal_left"), the yaw it is turned by (Minecraft degrees) and
	// where its other half is, if it is half of a double; None for a state that isn't a chest. On Christmas days the
	// chest and trapped chest wear the Christmas picture.
	static FName GetChestModel(FMcStateId State, float& OutYaw, FIntVector& OutPartner, bool bChristmas = false);
	// A shulker box's model ("mc:shulker_box/shulker_red") and how it is turned so its lid faces the way the box does
	// (the model's up onto that side, about the block's middle); None for a state that isn't one.
	static FName GetShulkerModel(FMcStateId State, FQuat& OutRotation);
	// Where the book on a lectern state lies, if it has one: the middle of its spine in block coordinates (from the
	// block's corner) and the Unreal rotation of the book model, lying open on the slanted top with its top up the
	// slope. False for a state that isn't a lectern with a book.
	static bool GetLecternBook(FMcStateId State, FVector& OutCentre, FQuat& OutRotation);
	// A mob head's model ("mc:head/zombie"), where the model's origin goes in the block (from its corner, in blocks)
	// and the Minecraft yaw it faces: on the floor in sixteen steps, on a wall out of it, set back against it and half
	// way up. None for a state that isn't a head.
	static FName GetHeadModel(FMcStateId State, FVector& OutCentre, float& OutYaw);
	// The Unreal rotation that takes the model's x, y and z (Minecraft's axes, as McEntityModel builds them) to the
	// given block directions.
	static FQuat RotationFromAxes(const FVector& X, const FVector& Y, const FVector& Z);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void Scan(FIntPoint ChunkPos);
	// A pot's sides came (or changed): its model is drawn with them.
	void EntityChanged(const FIntVector& Pos);
	// A pot's sides as its model's item: the four side items, back, left, right and front, comma separated.
	FName GetPotSides(const FIntVector& Pos) const;
	// Draws the food on the campfire at Pos (its block entity's Slots) lying on the four spots of its grill, and takes
	// away what is gone. Spots are at the corners 5/16 of a block out from the middle (fitted to a vanilla shot,
	// ref_campfood_side_north), slot 0 at the south-east of a north-facing campfire and the rest clockwise round it from
	// above; each facing turns them a quarter further clockwise.
	void UpdateGrill(const FIntVector& Pos, FMcStateId State);
	void DropGrill(const FIntVector& Pos);
	// The mob a spawner shows (its block entity's), or None.
	FName GetSpawnerMob(const FIntVector& Pos) const;
	void Clear(FIntPoint ChunkPos);
	void Unhook();

	FMcBlockWorld* World = nullptr;
	FDelegateHandle AddedHandle;
	FDelegateHandle EntityHandle;
	FDelegateHandle RemovedHandle;
	FDelegateHandle ChangedHandle;
	// Whether a state is a chest, by state id.
	TBitArray<> IsChest;

	enum class EShape : uint8 { Chest, Shulker, Bell, LecternBook, TableBook, Head, Pot, Conduit, Spawner, Campfire };
	struct FChest
	{
		FMcStateId State = 0;
		EShape Shape = EShape::Chest;
		// An enchanting table's book, and how far into its next tick the frame is (in ticks).
		FMcTableBook Book;
		float BookTickPart = 0.0f;
		FRandomStream BookRandom;
		// A conduit: whether it was awake when last looked at, and how long until it is looked at again.
		bool bAwake = false;
		bool bHunting = false;
		float UntilCheck = 0.0f;
		FIntVector Partner = FIntVector::ZeroValue;
		// A bell: the side it was last struck on, and seconds since (negative for never).
		int32 StruckSide = 0;
		float SinceStruck = -1.0f;
		// A spawner: the client's wait, the mob's spin (and the last tick's) and how far into its next tick the frame is.
		int32 SpawnerDelay = 0;
		float Spin = 0.0f;
		float OSpin = 0.0f;
		float SpinTickPart = 0.0f;
	};
	TMap<FIntVector, FChest> Chests;
	// Steps a spawner's spin and places its mob, for a frame DeltaTime long seen from Viewer's eyes.
	void TickSpawner(const FIntVector& Pos, FChest& Spawner, UMcEntityModelComponent& Model, const FVector& Viewer, float DeltaTime);
	TMap<FIntPoint, TArray<FIntVector>> ChestsByChunk;

	UPROPERTY(Transient)
	TMap<FIntVector, TObjectPtr<UMcEntityModelComponent>> Models;
	UPROPERTY(Transient)
	TMap<FIntVector, FMcGrillFood> Grills;
};
