// The text on placed signs. A sign's board is part of its block model (drawn with the chunk); its words are its block
// entity's (FMcSignSide), so this finds the signs in the loaded chunks and draws each one's lines on its board, front
// and back, in the game's font (textures/font/ascii.png): standing, wall, hanging and wall hanging signs, in their
// dye's colour. Glowing text is fully lit and, close up, outlined (minecraft.wiki, "Sign", "Glow Ink Sac").

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "McCore.h"
#include "McPlayerModel.h"
#include "McSignComponent.generated.h"

class FMcBlockWorld;
struct FMcBlockEntity;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UProceduralMeshComponent;

UCLASS(ClassGroup = (Mc))
class MCRENDER_API UMcSignComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMcSignComponent();

	// Null stops (and takes the text away). The world must outlive the component or be unset first.
	void SetWorld(FMcBlockWorld* InWorld);

	// Signs with text being drawn.
	int32 NumSigns() const { return Signs.Num(); }

	// The pieces of a sign's text, in Unreal units from the block's corner: the lines that aren't glowing, the ones
	// that are, and the glowing ones' outlines, black text's (seen from any distance) and the others' (only close up).
	// UV0 is over the font picture.
	struct FTextMesh
	{
		McPlayerModel::FMeshData Plain;
		McPlayerModel::FMeshData Glowing;
		McPlayerModel::FMeshData BlackOutline;
		McPlayerModel::FMeshData NearOutline;
	};
	static void BuildText(const FMcBlockEntity& Sign, FMcStateId State, FTextMesh& Out);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void Scan(FIntPoint ChunkPos);
	void Clear(FIntPoint ChunkPos);
	void EntityChanged(const FIntVector& Pos);
	void Remove(const FIntVector& Pos);
	void Unhook();
	void MakeMaterials();

	FMcBlockWorld* World = nullptr;
	FDelegateHandle AddedHandle;
	FDelegateHandle RemovedHandle;
	FDelegateHandle ChangedHandle;
	FDelegateHandle EntityHandle;

	struct FSign
	{
		FMcStateId State = 0;
		// The text, dye and glow drawn, to know when to draw again.
		uint32 Hash = 0;
		// Whether the near outline is showing.
		bool bNear = true;
	};
	TMap<FIntVector, FSign> Signs;
	TMap<FIntPoint, TArray<FIntVector>> SignsByChunk;

	UPROPERTY(Transient)
	TMap<FIntVector, TObjectPtr<UProceduralMeshComponent>> Meshes;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PlainMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> GlowMaterial;
	uint32 MaterialGeneration = MAX_uint32;
};
