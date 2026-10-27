// The bodies of mobs and the pictures of dropped items. Each mob kind is a short list of parts, each part a few
// boxes cut from the mob's texture (minecraft.wiki, "Model"), posed by what the mob is doing: a head that turns,
// legs that swing as it walks. Everything but the component at the end is plain maths.
//
// Model space is the same as the player's: x east, y up, z south, in pixels (16 to a block) above the feet, and a
// mob faces south (+z) when its yaw is 0. The mob's own right side is -x.

#pragma once

#include "CoreMinimal.h"
#include "McPlayerModel.h"
#include "ProceduralMeshComponent.h"
#include "McEntityModel.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2DArray;

namespace McEntityModel
{
	// What a part does in the walking animation.
	enum class ERole : uint8
	{
		Static,
		Head,
		// Four legs: front and back, right and left. Diagonal pairs swing together.
		LegFrontRight,
		LegFrontLeft,
		LegBackRight,
		LegBackLeft,
		// Two legs, and two arms that swing opposite them (or are held out in front).
		LegRight,
		LegLeft,
		ArmRight,
		ArmLeft,
		WingRight,
		WingLeft,
		// One of eight, Index 0..7: row = Index / 2 from the front, odd on the left.
		SpiderLeg,
	};

	struct FBox
	{
		FIntPoint UV;
		// Relative to the part's pivot, pixels.
		FVector Min;
		FIntVector Size;
		float Inflate = 0.0f;
		bool bMirror = false;
	};

	struct FPart
	{
		ERole Role = ERole::Static;
		int32 Index = 0;
		// Which texture it is cut from: 0 the mob, 1 its second layer (a sheep's wool).
		int32 Layer = 0;
		FVector Pivot = FVector::ZeroVector;
		// The rest pose, radians: tip forward, turn, lean sideways.
		float RestPitch = 0.0f;
		float RestYaw = 0.0f;
		float RestRoll = 0.0f;
		TArray<FBox> Boxes;
	};

	struct FModel
	{
		FName Kind;
		FIntPoint TextureSize = FIntPoint(64, 32);
		// A texture file for each layer, under textures/entity/.
		TArray<FString> Textures;
		TArray<FPart> Parts;
		// Zombies hold their arms out in front.
		bool bArmsForward = false;
	};

	// The model for a registry name like "minecraft:pig", null if the kind has none.
	MCRENDER_API const FModel* Find(FName Kind);

	struct FAnim
	{
		float BodyYaw = 0.0f;   // Minecraft degrees
		float HeadYaw = 0.0f;
		float HeadPitch = 0.0f; // positive looks down
		float WalkPos = 0.0f;
		float WalkAmount = 0.0f;
		float Age = 0.0f;       // seconds, for what idles
		float Hurt = 0.0f;      // 1 just hurt, down to 0
	};

	// The mesh of one layer of the model in the pose.
	MCRENDER_API void BuildMesh(const FModel& Model, const FAnim& Anim, int32 Layer, McPlayerModel::FMeshData& Out);

	// A flat picture of an item standing up, two-sided, spinning and bobbing: Count copies stacked a little apart.
	// Width is in blocks. The picture's V runs 0..VMax (an animated item's strip is cut to its first frame).
	MCRENDER_API void BuildItemMesh(float Age, int32 Count, float Width, float VMax, McPlayerModel::FMeshData& Out);

	// How many pictures an item entity shows for a count: 1, 2 above one, 3 above 16, 4 above 32, 5 above 48.
	MCRENDER_API int32 GetItemCopies(int32 Count);
}

// Draws one mob or dropped item. Put it on the entity's actor (feet at its origin).
UCLASS()
class MCRENDER_API UMcEntityModelComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UMcEntityModelComponent(const FObjectInitializer& ObjectInitializer);

	// What to draw. A mob kind with no model draws nothing, and HasModel says so. For "minecraft:item" the item
	// and its count say what picture to show.
	void SetKind(FName InKind, FName InItem, int32 InCount);
	bool HasModel() const;
	// Where the mob looks (Minecraft degrees, body and head) and the count that goes up when it is hurt.
	void SetLook(float InBodyYaw, float InHeadYaw, float InHeadPitch);
	void SetHurtCount(int32 InHurtCount);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UMaterialInstanceDynamic* GetMaterial(int32 Layer);
	void Rebuild();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	FName Kind;
	FName Item;
	int32 Count = 0;
	const McEntityModel::FModel* Model = nullptr;
	// For an item: its picture's V extent, once it is loaded.
	float ItemVMax = 1.0f;
	bool bItemFailed = false;
	bool bIsItem = false;

	McEntityModel::FAnim Anim;
	float TargetBodyYaw = 0.0f;
	float TargetHeadYaw = 0.0f;
	float TargetHeadPitch = 0.0f;
	bool bHasLook = false;
	int32 HurtCount = -1;
	FVector2D LastPosition = FVector2D::ZeroVector;
	bool bHasPosition = false;
	float TickAccumulator = 0.0f;
	float Walking = 0.0f;
	int32 NumSections = 0;
	bool bDirty = true;
};
