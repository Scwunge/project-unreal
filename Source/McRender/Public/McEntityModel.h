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
		// A flat picture rather than a box (a held item): it lies in the part's y-z plane at Min.X, Size.Y by Size.Z, the whole
		// of its layer's picture on it with the top towards -y and the right towards +z. Drawn from both sides.
		bool bFlat = false;
		// With bFlat: lying in the part's x-y plane instead, centred on (Min.X + Size.X / 2, Min.Y + Size.Y / 2) at Min.Z and Size.Y
		// across, turned 45 degrees so an item icon's barrel (its diagonal) points along -y: a crossbow held out flat.
		bool bFlatTop = false;
	};

	struct FPart
	{
		ERole Role = ERole::Static;
		int32 Index = 0;
		// Which texture it is cut from: 0 the mob, 1 its second layer (a sheep's wool).
		int32 Layer = 0;
		// Drawn at full brightness whatever the light (a spider's or an enderman's eyes).
		bool bGlow = false;
		FVector Pivot = FVector::ZeroVector;
		// The rest pose, radians: tip forward, turn, lean sideways.
		float RestPitch = 0.0f;
		float RestYaw = 0.0f;
		float RestRoll = 0.0f;
		TArray<FBox> Boxes;
		// The poses (bit per FAnim::Pose) it is drawn in.
		uint32 ShowPoses = 0xFFFFFFFFu;
	};

	struct FModel
	{
		FName Kind;
		FIntPoint TextureSize = FIntPoint(64, 32);
		// A texture file for each layer, under textures/entity/.
		TArray<FString> Textures;
		TArray<FPart> Parts;
		// The pictures are laid over each other into one (a villager's biome type, profession and level over its base) and the
		// whole model is drawn from that, as layer 0.
		bool bComposite = false;
		// Looks the kind comes in (a horse's coat and markings, a wolf's breed): each the pictures for the layers. One is picked by
		// the mob's look seed. Empty for a kind with one look.
		TArray<TArray<FString>> Variants;
		// Zombies hold their arms out in front.
		bool bArmsForward = false;
		// Layers (bit per layer) drawn see-through, so a slime's outer shell shows what is inside.
		uint32 TranslucentLayers = 0;
		// Layers drawn at full brightness (the eyes).
		uint32 GlowLayers = 0;
		// Most a leg or arm swings either way, radians; 0 for no limit (an enderman strides stiffly).
		float MaxLimbSwing = 0.0f;
		// The hitbox width the model is drawn for, blocks; the model grows with a hitbox that is wider (slimes). 0 for fixed.
		float BaseWidth = 0.0f;
		// How an illager holds its arms when armed: 1 a crossbow, 2 an axe, 3 casting (0 for no such arms).
		int32 IllagerStyle = 0;
		// The wings beat all the time (a vex).
		bool bFlutter = false;
		// The whole model drawn at this size about the feet (the villagers and illagers are 15/16: their root sits 1.5 pixels low in the game).
		float Scale = 1.0f;
		// Layers (bit per layer) multiplied by the anim's tint (a wolf's collar, cut white and dyed).
		uint32 TintedLayers = 0;
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
		// Drawn flat white, unshaded (a creeper's flash, with the white silhouette picture).
		bool bFlash = false;
		// A young one: half the size about its feet, the head half as big again.
		bool bBaby = false;
		// A golem holding out its poppy: the right arm is raised.
		bool bOffering = false;
		// What the arms are doing: 0 idle (an illager's folded), 1 armed or attacking, 2 charging a crossbow, 3 casting, 4 drinking.
		int32 Pose = 0;
		FColor Tint = FColor::White;
	};
	constexpr float BabyScale = 0.5f;
	constexpr float BabyHeadScale = 1.5f;

	// The pictures a villager (or zombie villager) is drawn from, by layer: the base, the biome type's hat, the
	// profession's clothes and the level's badge (the last only for a profession that has one). Under textures/entity/.
	MCRENDER_API TArray<FString> GetVillagerTextures(bool bZombie, FName Variant, FName Profession, int32 Level);

	// The colour of the dye with that index (0 white .. 15 black, in the game's order), for a collar.
	MCRENDER_API FColor GetDyeColor(int32 Index);

	// The mesh of one layer of the model in the pose.
	MCRENDER_API void BuildMesh(const FModel& Model, const FAnim& Anim, int32 Layer, McPlayerModel::FMeshData& Out);

	// A flat picture of an item standing up, two-sided, spinning and bobbing: Count copies stacked a little apart.
	// Width is in blocks. The picture's V runs 0..VMax (an animated item's strip is cut to its first frame).
	MCRENDER_API void BuildItemMesh(float Age, int32 Count, float Width, float VMax, McPlayerModel::FMeshData& Out);

	// How many pictures an item entity shows for a count: 1, 2 above one, 3 above 16, 4 above 32, 5 above 48.
	MCRENDER_API int32 GetItemCopies(int32 Count);

	// An arrow in flight, two crossed shafts and a fletching, pointing the way it flies (Minecraft yaw, and pitch with
	// positive down). Its picture is the 32 square projectiles/arrow.png. Centred half a hitbox above the feet.
	MCRENDER_API void BuildArrowMesh(float BodyYaw, float Pitch, McPlayerModel::FMeshData& Out);

	// How big a creeper looks as its fuse burns: swelling runs 0 to 1 over the fuse. Wider and a little taller, and
	// shaking as it gets near (minecraft.wiki: "Creeper"). X is across, Y up.
	MCRENDER_API FVector2D GetSwellScale(float Swelling);
	// Whether a swelling creeper is flashing white now: every other tenth of the fuse.
	MCRENDER_API bool IsSwellFlash(float Swelling);

	// A slime's squish (minecraft.wiki: "Slime"): it stretches up as it jumps and flattens as it lands, easing back
	// between. Fed how far the slime rose (blocks) each tick, since that is all a client sees of its hops.
	struct FSquish
	{
		float Target = 0.0f;
		float Value = 0.0f;
		float Previous = 0.0f;
		float LastRise = 0.0f;

		MCRENDER_API void Tick(float Rise);
		// The scale for a slime Size times the smallest (1, 2 or 4), Alpha of the way into the next tick: X across,
		// Y up. Bigger slimes squish less.
		MCRENDER_API FVector2D GetScale(float Alpha, float Size) const;
	};
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
	// The hitbox the entity has, for a model that grows with it (a slime's size).
	void SetHitboxWidth(float InWidth);
	// A creeper's fuse, 0 (not lit) to 1 (about to go): it swells, shakes and flashes.
	void SetSwelling(float InSwelling);
	// A villager's biome type, profession and level choose the pictures its layers are cut from. Does nothing for
	// other kinds.
	void SetOffering(bool bInOffering) { bOfferingLook = bInOffering; }
	void SetPose(int32 InPose);
	void SetBaby(bool bInBaby) { if (bInBaby != bBabyLook) { bBabyLook = bInBaby; bDirty = true; } }
	void SetVillagerLook(FName InVariant, FName InProfession, int32 InLevel, bool bInBaby);
	// Picks the look of a kind that comes in several (a horse's coat): the index into its Variants, McMobLook's.
	void SetVariantSeed(int32 InSeed);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UMaterialInstanceDynamic* GetMaterial(int32 Layer);
	void Rebuild();
	// The picture a layer is cut from.
	FString GetTexturePath(int32 Layer) const;
	// The pictures of a kind that comes in looks: the variant the seed picks, and for a wolf its tame or angry version.
	void ApplyVariant();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
	// The look (McRenderMode) the materials were made for.
	uint32 MaterialGeneration = 0;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TranslucentMaterial;

	FName Kind;
	FName Item;
	int32 Count = 0;
	// The pictures that stand in for the model's own, for the layers they name (a villager's look).
	TArray<FString> TextureOverrides;
	FName LookKey;
	int32 VariantSeed = INT32_MIN;
	const McEntityModel::FModel* Model = nullptr;
	// For an item: its picture's V extent, once it is loaded.
	float ItemVMax = 1.0f;
	bool bItemFailed = false;
	bool bIsItem = false;
	bool bIsArrow = false;
	float HitboxWidth = 0.0f;
	bool bBabyLook = false;
	bool bOfferingLook = false;
	int32 PoseLook = 0;
	// A wolf's look as the textures were last chosen: 0 wild, 1 angry, 2 tame.
	int32 WolfTextureState = 0;
	// The fuse as told, and as shown (it eases towards it).
	float TargetSwelling = 0.0f;
	float Swelling = 0.0f;
	bool bFlashing = false;
	// A slime's hop, from its height each tick.
	McEntityModel::FSquish Squish;
	double LastY = 0.0;
	bool bHasY = false;

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
