// The bodies of mobs and the pictures of dropped items. Each mob kind is a short list of parts, each part a few
// boxes cut from the mob's texture (minecraft.wiki, "Model"), posed by what the mob is doing: a head that turns,
// legs that swing as it walks. Everything but the component at the end is plain maths.
//
// Model space is the same as the player's: x east, y up, z south, in pixels (16 to a block) above the feet, and a
// mob faces south (+z) when its yaw is 0. The mob's own right side is -x.

#pragma once

#include "CoreMinimal.h"
#include "McBanner.h"
#include "McCore.h"
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
		// A boat's paddles, which row while the boat has a rider pushing it.
		PaddleLeft,
		PaddleRight,
		// A chest's lid, which swings up about its back edge as the chest opens.
		ChestLid,
		// A shulker box's lid, which rises and turns as the box opens.
		ShulkerLid,
		// A bell's body, which swings about the top when it is rung.
		BellBody,
		// A book's halves (cover and pages), which close about the spine, and a loose page (Index 0 or 1) that flips
		// from the right half to the left.
		BookLeft,
		BookRight,
		BookPage,
		// An active conduit's cage, turning slowly about its middle, and its eye, which looks at the player (HeadYaw and
		// HeadPitch, unclamped).
		ConduitCage,
		ConduitEye,
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
		// Drawn at full brightness whatever the light (a spider's or an enderman's eyes).
		bool bGlow = false;
		FVector Pivot = FVector::ZeroVector;
		// Its boxes shrunk about the pivot (a chest at three quarters in a minecart).
		float Scale = 1.0f;
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
		// A layer cut from a picture of another size (a chest's on a minecart's), by layer; empty or 0 for TextureSize.
		TArray<FIntPoint> LayerTextureSizes;
		// A block drawn in the model from its own model and pictures (a furnace in a minecart): the state, where its
		// middle sits (pixels from the model's origin) and how big (1 is a whole block). Empty for none.
		FString Block;
		FVector BlockOffset = FVector::ZeroVector;
		float BlockScale = 0.75f;
		// A texture file for each layer, under textures/entity/.
		TArray<FString> Textures;
		TArray<FPart> Parts;
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
		// The layer whose parts are not drawn but mark where water is left out (a boat's hull, under its gunwales), or
		// INDEX_NONE. It is after the textures' layers.
		int32 MaskLayer = INDEX_NONE;
	};

	// The custom stencil value a hull's mask writes; the translucent materials (tools/make_materials.py) leave out the
	// water behind it. Nothing else uses it.
	constexpr int32 HullStencil = 77;

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
		// A boat's paddles: how far round the stroke each is (radians) and whether it is rowing.
		float PaddleLeft = 0.0f;
		float PaddleRight = 0.0f;
		bool bPaddleLeft = false;
		bool bPaddleRight = false;
		// How far a chest's or shulker box's lid is open, 0 shut to 1 open.
		float Lid = 0.0f;
		// A rung bell's swing, radians: tipped about x (positive takes its bottom north) and about z (east).
		float SwingPitch = 0.0f;
		float SwingRoll = 0.0f;
		// A book: how far open (0 shut, 1 lying flat), and how far over each loose page is (0 on the right, 1 the left).
		float BookOpen = 0.0f;
		float BookFlip[2] = { 0.0f, 0.0f };
		// Drawn flat white, unshaded (a creeper's flash, with the white silhouette picture).
		bool bFlash = false;
	};

	// A block state's quads in the pose of Model.Block: Flash draws it white from a picture's opaque corner (UV 0.2, 0.6
	// of whatever picture the material has). UV1 gets the texture array layer for each vertex.
	MCRENDER_API void BuildBlockMesh(const FModel& Model, FMcStateId State, float BodyYaw, bool bFlash, McPlayerModel::FMeshData& Out, TArray<FVector2D>& UV1);

	// The mesh of one layer of the model in the pose.
	MCRENDER_API void BuildMesh(const FModel& Model, const FAnim& Anim, int32 Layer, McPlayerModel::FMeshData& Out);

	// A placed banner (McBanner): its pole and crossbar (bFlag false) or its flag (true), facing Minecraft yaw BodyYaw, at
	// the size the game draws it. The flag is cut from whichever 64 square picture is laid over it.
	MCRENDER_API void BuildBannerMesh(bool bWall, bool bFlag, float BodyYaw, McPlayerModel::FMeshData& Out);

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

	// What to draw. A mob kind with no model yet draws a magenta box the size of its hitbox, and HasModel says it has
	// none. For "minecraft:item" the item and its count say what picture to show.
	void SetKind(FName InKind, FName InItem, int32 InCount);
	bool HasModel() const;
	// Where the mob looks (Minecraft degrees, body and head) and the count that goes up when it is hurt.
	void SetLook(float InBodyYaw, float InHeadYaw, float InHeadPitch);
	void SetHurtCount(int32 InHurtCount);
	// The hitbox the entity has, for a model that grows with it (a slime's size).
	void SetHitboxWidth(float InWidth);
	void SetHitboxHeight(float InHeight) { HitboxHeight = InHeight; }
	// A creeper's fuse, 0 (not lit) to 1 (about to go): it swells, shakes and flashes.
	void SetSwelling(float InSwelling);
	// A chest's lid opens over a few ticks while this is set, and shuts again after.
	void SetOpen(bool bInOpen) { bOpen = bInOpen; }
	// A bell's swing (see FAnim), radians.
	void SetSwing(float Pitch, float Roll) { Anim.SwingPitch = Pitch; Anim.SwingRoll = Roll; }
	// A book's pose (see FAnim).
	void SetBook(float Open, float FlipA, float FlipB) { Anim.BookOpen = Open; Anim.BookFlip[0] = FlipA; Anim.BookFlip[1] = FlipB; }
	// A fishing bobber: the rod tip its line runs from (block coordinates), unset if the angler isn't to be seen.
	void SetFishingLine(const TOptional<FVector>& InTip) { RodTip = InTip; }
	// Draws the model this many times its size (a head worn on a player's).
	void SetModelScale(float InScale) { ModelScale = InScale; }
	FName GetKind() const { return Kind; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UMaterialInstanceDynamic* GetMaterial(int32 Layer);
	void Rebuild();
	// The block in the model, as a section after the layers.
	void TickBlock(int32 Section);
	// The magenta box for a kind with no model.
	void TickStandIn();
	// The picture a layer is cut from.
	FString GetTexturePath(int32 Layer) const;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
	// The look (McRenderMode) the materials were made for.
	uint32 MaterialGeneration = 0;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> TranslucentMaterial;
	// The block in the model (see FModel::Block): its material over the block pictures, and over the white one.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BlockMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> FlashMaterial;
	FString BlockText;
	int32 BlockVertices = 0;
	// The model's mask layer (FModel::MaskLayer), drawn only into custom depth.
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> Mask;

	FName Kind;
	FName Item;
	int32 Count = 0;
	const McEntityModel::FModel* Model = nullptr;
	// For an item: its picture's V extent, once it is loaded.
	float ItemVMax = 1.0f;
	bool bItemFailed = false;
	bool bIsItem = false;
	bool bIsArrow = false;
	// A fishing bobber: its picture facing the camera, and the line from the rod tip, made again each frame.
	bool bIsBobber = false;
	TOptional<FVector> RodTip;
	float ModelScale = 1.0f;
	void BuildBobber(McPlayerModel::FMeshData& Out) const;
	// A placed banner: its pictures are the base and then one for each pattern, each tinted by its dye.
	bool bIsBanner = false;
	bool bBannerWall = false;
	FString BannerBase;
	TArray<McBanner::FLayer> BannerLayers;
	float HitboxWidth = 0.0f;
	float HitboxHeight = 0.0f;
	// The hitbox the magenta box was last made for.
	FVector2D StandInSize = FVector2D::ZeroVector;
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
	// A boat's paddles' strokes, advanced a tick at a time while they row (bit 0 the left, bit 1 the right in Count).
	float PaddlePhase[2] = { 0.0f, 0.0f };
	// A chest's lid: whether it is to be open, and how far it is (a tenth more each tick), now and the tick before.
	bool bOpen = false;
	float Lid = 0.0f;
	float PreviousLid = 0.0f;
	FVector2D LastPosition = FVector2D::ZeroVector;
	bool bHasPosition = false;
	float TickAccumulator = 0.0f;
	float Walking = 0.0f;
	int32 NumSections = 0;
	bool bDirty = true;
};
