// Placed chests and shulker boxes. A chest's block has no model of its own (its block model is only a particle picture, so the chunk
// meshes leave it out): the game draws it from the chest's entity picture, with a lid that opens. This finds the
// chests in the loaded chunks and stands an entity model (McEntityModel's "mc:chest/...") in each: the chest, trapped
// chest, ender chest and copper chests, single or half of a double, turned to their facing. Shulker boxes are the same
// (McEntityModel's "mc:shulker_box/..."), turned so the lid faces the way the box does, the lid rising and turning as
// the box opens.

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

	// The model a chest state is drawn with ("mc:chest/normal_left"), the yaw it is turned by (Minecraft degrees) and
	// where its other half is, if it is half of a double; None for a state that isn't a chest. On Christmas days the
	// chest and trapped chest wear the Christmas picture.
	static FName GetChestModel(FMcStateId State, float& OutYaw, FIntVector& OutPartner, bool bChristmas = false);
	// A shulker box's model ("mc:shulker_box/shulker_red") and how it is turned so its lid faces the way the box does
	// (the model's up onto that side, about the block's middle); None for a state that isn't one.
	static FName GetShulkerModel(FMcStateId State, FQuat& OutRotation);

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

	struct FChest
	{
		FMcStateId State = 0;
		FIntVector Partner = FIntVector::ZeroValue;
	};
	TMap<FIntVector, FChest> Chests;
	TMap<FIntPoint, TArray<FIntVector>> ChestsByChunk;

	UPROPERTY(Transient)
	TMap<FIntVector, TObjectPtr<UMcEntityModelComponent>> Models;
};
