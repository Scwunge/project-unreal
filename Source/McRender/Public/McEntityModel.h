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
#include "McDecor.h"
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
		// The streaks round an awake conduit: a cube whose picture is a column of frames, one every 3 ticks.
		ConduitWind,
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
		// Turned about BoxPivot (in the part's own space) before the part's own pose is applied: a witch's hat bends in
		// four pieces, each on the last.
		FVector BoxPivot = FVector::ZeroVector;
		FQuat BoxRotation = FQuat::Identity;
		// A witch's nose turns in a slow circle about BoxPivot with the mob's age (see Build).
		bool bNoseWobble = false;
		// A camel's ear (1 right, 2 left): turned out about its pivot in a dash.
		uint8 CamelEar = 0;
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
		// How much its boxes are scaled about the pivot (a chest at three quarters in a minecart, a small armor stand's parts).
		float Scale = 1.0f;
		// The rest pose, radians: tip forward, turn, lean sideways.
		float RestPitch = 0.0f;
		float RestYaw = 0.0f;
		float RestRoll = 0.0f;
		TArray<FBox> Boxes;
		// The poses (bit per FAnim::Pose) it is drawn in.
		uint32 ShowPoses = 0xFFFFFFFFu;
		// A rabbit's part in its hop: 1 body, 2 head, 3 tail, 4 front legs, 5 hind legs (0 none).
		uint8 Hop = 0;
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
		// The pictures are laid over each other into one (a villager's biome type, profession and level over its base) and the
		// whole model is drawn from that, as layer 0.
		bool bComposite = false;
		// Looks the kind comes in (a horse's coat and markings, a wolf's breed): each the pictures for the layers. One is picked by
		// the mob's look seed. Empty for a kind with one look.
		TArray<TArray<FString>> Variants;
		// Zombies hold their arms out in front.
		bool bArmsForward = false;
		// A bowman's arms: raised straight out at its target (pose 1) with a bow.
		bool bBowAim = false;
		// Layers (bit per layer) drawn see-through, so a slime's outer shell shows what is inside.
		uint32 TranslucentLayers = 0;
		// Layers drawn at full brightness (the eyes).
		uint32 GlowLayers = 0;
		// Most a leg or arm swings either way, radians; 0 for no limit (an enderman strides stiffly).
		float MaxLimbSwing = 0.0f;
		// How far the legs swing per point of walk amount, radians (measured from the game's own model at a steady walk), the front legs'
		// own if they differ (negative for the same), and the arms'. The golem's swing is a triangle wave of 13 steps, not a cosine.
		float LegSwing = 1.4f;
		float FrontLegSwing = -1.0f;
		float ArmSwing = 1.0f;
		bool bTriangleWalk = false;
		// The hitbox width the model is drawn for, blocks; the model grows with a hitbox that is wider (slimes). 0 for fixed.
		float BaseWidth = 0.0f;
		// The layer whose parts are not drawn but mark where water is left out (a boat's hull, under its gunwales), or
		// INDEX_NONE. It is after the textures' layers.
		int32 MaskLayer = INDEX_NONE;
		// How an illager holds its arms when armed: 1 a crossbow, 2 an axe, 3 casting (0 for no such arms).
		int32 IllagerStyle = 0;
		// The wings beat all the time (a vex).
		bool bFlutter = false;
		// A camel: the stand-up, sit-down and dash drawn from the logged tables.
		bool bCamelAnim = false;
		// The whole model drawn at this size about the feet (the villagers and illagers are 15/16: their root sits 1.5 pixels low in the game).
		float Scale = 1.0f;
		// Wings that beat all the time (a bee's), and how far out a bird's rest when they are not beating (radians).
		bool bBeatWings = false;
		// The layer that draws the item in the right hand (the item's own picture, per mob), or -1 for a model that holds nothing.
		int32 HeldLayer = -1;
		// Hops (a rabbit): its parts follow the logged hop curve (Anim.HopPos) when it leaves the ground.
		bool bHops = false;
		float WingRest = 0.1203f;
		// This is a young one's own model (not the grown one's halved).
		bool bBabyModel = false;
		// Layers (bit per layer) multiplied by the anim's tint (a wolf's collar, cut white and dyed).
		uint32 TintedLayers = 0;
		// When set, the tinted layers use this colour instead of a dye (a potion's liquid).
		bool bFixedTint = false;
		FColor FixedTint = FColor::White;
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
		// Off the ground and moving up or down (a chicken beats its wings).
		bool bAirborne = false;
		// Ticks into a hop (with the fraction of the tick), or -1 when not hopping.
		float HopPos = -1.0f;
		// A young one: half the size about its feet, the head half as big again.
		bool bBaby = false;
		// A golem holding out its poppy: the right arm is raised.
		bool bOffering = false;
		// What the arms are doing: 0 idle (an illager's folded), 1 armed or attacking, 2 charging a crossbow, 3 casting, 4 drinking.
		int32 Pose = 0;
		// A camel's stand-up (the tick of its 52, or the sit-down run backwards) and its dash (the tick of 44), -1 for none.
		int32 SitStep = -1;
		int32 DashStep = -1;
		FColor Tint = FColor::White;
	};
	constexpr float BabyScale = 0.5f;
	constexpr float BabyHeadScale = 1.5f;

	// The pictures a villager (or zombie villager) is drawn from, by layer: the base, the biome type's hat, the
	// profession's clothes and the level's badge (the last only for a profession that has one). Under textures/entity/.
	MCRENDER_API TArray<FString> GetVillagerTextures(bool bZombie, FName Variant, FName Profession, int32 Level);

	// The colour of the dye with that index (0 white .. 15 black, in the game's order), for a collar.
	MCRENDER_API FColor GetDyeColor(int32 Index);

	// A block state's quads in the pose of Model.Block: Flash draws it white from a picture's opaque corner (UV 0.2, 0.6
	// of whatever picture the material has). UV1 gets the texture array layer for each vertex.
	MCRENDER_API void BuildBlockMesh(const FModel& Model, FMcStateId State, float BodyYaw, bool bFlash, McPlayerModel::FMeshData& Out, TArray<FVector2D>& UV1);

	// The mesh of one layer of the model in the pose.
	MCRENDER_API void BuildMesh(const FModel& Model, const FAnim& Anim, int32 Layer, McPlayerModel::FMeshData& Out);

	// A placed banner (McBanner): its pole and crossbar (bFlag false) or its flag (true), facing Minecraft yaw BodyYaw, at
	// the size the game draws it. The flag is cut from whichever 64 square picture is laid over it.
	// Tilt: how far the flag swings about its bar, in radians (see GetBannerTilt).
	MCRENDER_API void BuildBannerMesh(bool bWall, bool bFlag, float BodyYaw, McPlayerModel::FMeshData& Out, float Tilt = 0.0f);
	// The flag's swing at a time in ticks. Fitted to a vanilla run (banner3, 40 shots 4 ticks apart over a standing banner):
	// the foot of the flag moves on a 100 tick cycle, 0.033 rad either way of a rest 0.058 rad back; the phase is given
	// for the one position measured (a second banner 2 blocks away swung in step, so none is taken from x).
	MCRENDER_API float GetBannerTilt(double Ticks);

	// An item frame, painting or armor stand from an imported world (McDecor), at its entity's position: how many pictures
	// (layers) it is drawn from, and one layer's mesh. A painting's layer 0 is its picture (PictureBlocks across and up) and 1 the
	// plain back and edges; a frame's 0 is its wood, 1 its back plate and 2 its item (ItemVMax is how much of the item's
	// picture to show, an animated one being a column of frames).
	MCRENDER_API int32 GetDecorLayers(const McDecor::FDecor& Decor);
	MCRENDER_API void BuildDecorMesh(const McDecor::FDecor& Decor, int32 Layer, FVector2D PictureBlocks, float ItemVMax, McPlayerModel::FMeshData& Out);

	// A flat picture of an item standing up, two-sided, spinning and bobbing: Count copies stacked a little apart.
	// Width is in blocks. The picture's V runs 0..VMax (an animated item's strip is cut to its first frame).
	// An item's picture lying flat (a campfire's food), two-sided, Width blocks across, its middle at the origin and Height blocks up.
	MCRENDER_API void BuildFlatItemMesh(float Width, float Height, float VMax, McPlayerModel::FMeshData& Out);
	MCRENDER_API void BuildItemMesh(float Age, int32 Count, float Width, float VMax, McPlayerModel::FMeshData& Out);

	// A dropped item as the game shows it (fitted from same-camera shots of the game): Count copies of Unit (a mesh one block across with its middle at the origin,
	// thickness along x: a picture stood one pixel thick, or a cube), each Scale big, turning 1 radian a second and bobbing 0.096 of a block
	// (a sine of period 63 ticks) about a height Lift above the usual 0.26. Age is in seconds.
	MCRENDER_API float GetDroppedHeight(float Age, float Lift);

	// What a dropped item is made of: a picture cut out one pixel thick (Sides), a full block (Sides and Top, the cube's two pictures),
	// or a block that is not a full cube (stairs, slabs, fences) as its baked model (Sides, with LayerUV the block texture layer of
	// each vertex). Scale and Lift (blocks) place it as the model's ground display does. False only for a name with no picture and no block.
	struct FDroppedUnit
	{
		bool bCube = false;
		bool bModel = false;
		McPlayerModel::FMeshData Sides;
		McPlayerModel::FMeshData Top;
		TArray<FVector2D> LayerUV;
		float Scale = 0.5f;
		float Lift = 0.0f;
	};
	MCRENDER_API bool BuildDroppedUnit(FName Item, FDroppedUnit& Out);
	MCRENDER_API void BuildDroppedMesh(const McPlayerModel::FMeshData& Unit, float Age, int32 Count, float Scale, float Lift, McPlayerModel::FMeshData& Out);

	// A block of picture on every side, one block big, standing on the feet (a falling block). Layer 0 is every side (all six when
	// there is no separate top picture, otherwise all but the top), layer 1 the top alone.
	MCRENDER_API void BuildBlockMesh(bool bTopPicture, int32 Layer, McPlayerModel::FMeshData& Out);

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
	// The cube of a primed TNT, one third of it per picture: 0 the sides, 1 the top, 2 the bottom.
	MCRENDER_API void BuildTntMesh(int32 Layer, McPlayerModel::FMeshData& Out);

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
	// A villager's biome type, profession and level choose the pictures its layers are cut from. Does nothing for
	// other kinds.
	void SetOffering(bool bInOffering) { bOfferingLook = bInOffering; }
	// The item in the mob's right hand (None for nothing).
	void SetHeldItem(FName InItem);
	void SetPose(int32 InPose);
	// A young one of a kind that has a model of its own (registered as "<kind>_baby") is drawn with that; the others are the grown one's halved.
	void SetBaby(bool bInBaby);
	void SetVillagerLook(FName InVariant, FName InProfession, int32 InLevel, bool bInBaby);
	// Picks the look of a kind that comes in several (a horse's coat): the index into its Variants, McMobLook's.
	void SetVariantSeed(int32 InSeed);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	using UProceduralMeshComponent::GetMaterial;
	UMaterialInstanceDynamic* GetMaterial(int32 Layer);
	void Rebuild();
	// The block in the model, as a section after the layers.
	void TickBlock(int32 Section);
	// The magenta box for a kind with no model.
	void TickStandIn();
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
	// The pictures that stand in for the model's own, for the layers they name (a villager's look).
	TArray<FString> TextureOverrides;
	FName LookKey;
	int32 VariantSeed = INT32_MIN;
	const McEntityModel::FModel* Model = nullptr;
	// For an item: its picture's V extent, once it is loaded.
	float ItemVMax = 1.0f;
	bool bItemFailed = false;
	bool bIsItem = false;
	// An item lying flat, still ("mc:grillfood": the food on a campfire); also bIsItem.
	bool bIsGrillFood = false;
	// A dropped item (bIsItem, not thrown, not food on a grill): its picture stood thick, or its block as a small cube, spinning and bobbing.
	bool bIsDropped = false;
	bool bDropReady = false;
	bool bDropCube = false;
	bool bDropModel = false;
	TArray<FVector2D> DropLayerUV;
	float DropScale = 0.5f;
	float DropLift = 0.0f;
	McPlayerModel::FMeshData DropUnit;
	McPlayerModel::FMeshData DropTop;
	void PrepareDropped();
	bool bIsArrow = false;
	// A falling block: a cube with the block's picture, drawn smoothly between the 20 positions a second it is sent.
	bool bIsFalling = false;
	FVector FallingAt = FVector::ZeroVector;
	bool bHasFallingAt = false;
	// A snowball, egg or ender pearl in flight: its item's picture on two crossed squares.
	bool bIsThrown = false;
	// A dropped decorated pot ("minecraft:decorated_pot", or "=" and its sides after it): the pot's own model, small,
	// bobbing and turning as a dropped item does.
	bool bIsPotItem = false;
	FName PotSides;
	// A fishing bobber: its picture facing the camera, and the line from the rod tip, made again each frame.
	bool bIsBobber = false;
	TOptional<FVector> RodTip;
	float ModelScale = 1.0f;
	void BuildBobber(McPlayerModel::FMeshData& Out) const;
	// A placed banner: its pictures are the base and then one for each pattern, each tinted by its dye.
	bool bIsBanner = false;
	bool bIsTnt = false;
	// An item frame, painting or armor stand of an imported world: Decor is its line, decoded.
	bool bIsDecor = false;
	McDecor::FDecor Decor;
	bool bBannerWall = false;
	FString BannerBase;
	TArray<McBanner::FLayer> BannerLayers;
	float HitboxWidth = 0.0f;
	float HitboxHeight = 0.0f;
	// The hitbox the magenta box was last made for.
	FVector2D StandInSize = FVector2D::ZeroVector;
	bool bBabyLook = false;
	bool bOfferingLook = false;
	FName HeldLook;
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
	int32 AirTicks = 0;
	int32 HopTicks = -1;

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
