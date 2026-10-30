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
	void Clear(FIntPoint ChunkPos);
	void Unhook();

	FMcBlockWorld* World = nullptr;
	FDelegateHandle AddedHandle;
	FDelegateHandle RemovedHandle;
	FDelegateHandle ChangedHandle;
	// Whether a state is a chest, by state id.
	TBitArray<> IsChest;

	enum class EShape : uint8 { Chest, Shulker, Bell, LecternBook, TableBook, Head, Pot, Conduit };
	struct FChest
	{
		FMcStateId State = 0;
		EShape Shape = EShape::Chest;
		// An enchanting table's book: which way it faces (radians about up, 0 facing south), how far open, and its loose
		// pages, each flipping over and back.
		float BookYaw = 0.0f;
		float BookOpen = 0.0f;
		float BookFlip[2] = { 0.0f, 0.0f };
		float BookAge = 0.0f;
		// A conduit: whether it was awake when last looked at, and how long until it is looked at again.
		bool bAwake = false;
		float UntilCheck = 0.0f;
		FIntVector Partner = FIntVector::ZeroValue;
		// A bell: the side it was last struck on, and seconds since (negative for never).
		int32 StruckSide = 0;
		float SinceStruck = -1.0f;
	};
	TMap<FIntVector, FChest> Chests;
	TMap<FIntPoint, TArray<FIntVector>> ChestsByChunk;

	UPROPERTY(Transient)
	TMap<FIntVector, TObjectPtr<UMcEntityModelComponent>> Models;
};
