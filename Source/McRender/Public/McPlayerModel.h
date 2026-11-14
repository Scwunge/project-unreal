// The player's body, drawn for the players you see: the classic 64x64 skin on a head, torso, two arms and two legs,
// each with an outer layer (hat, jacket, sleeves, pants) a half pixel bigger. Everything here is plain maths
// (skin layout, pivots, swing and head turn) except the component at the end, which draws it.
//
// Model space is Minecraft's with the player facing south: x east, y up, z south, in pixels (16 to a block) above
// the feet, so the player's right side is -x. Angles are degrees unless a name says Rad.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "McPlayerModel.generated.h"

class UTexture2DArray;
class UMaterialInstanceDynamic;

enum class EMcPlayerPart : uint8
{
	Head,
	Body,
	RightArm,
	LeftArm,
	RightLeg,
	LeftLeg,
	Num,
};

constexpr int32 McNumPlayerParts = static_cast<int32>(EMcPlayerPart::Num);

namespace McPlayerModel
{
	constexpr int32 SkinSize = 64;
	// The player is 32 pixels tall in the model but 1.8 blocks (28.8 pixels) in the game.
	constexpr float ModelScale = 0.9375f;
	// The outer layer is this many pixels bigger than the part on every side: the hat's, and the jacket's, sleeves'
	// and trousers' (Minecraft grows those by a quarter pixel; at half a pixel the sleeves floated off the arms).
	constexpr float OuterInflate = 0.5f;
	constexpr float OuterInflateBody = 0.25f;

	// A part's box and where it turns, for the standing pose.
	struct FPartDef
	{
		FIntPoint BaseUV;   // top left corner of the part's net on the skin
		FIntPoint OuterUV;  // the same for the outer layer
		FIntVector Size;    // width (x), height (y), depth (z) in pixels
		FVector Pivot;      // pixels; the point the part turns about
		FVector BoxMin;     // the box's lowest corner, relative to the pivot
	};

	MCRENDER_API const FPartDef& GetPartDef(EMcPlayerPart Part);

	// The six sides of a box, named by the direction they face.
	enum class EBoxFace : uint8 { Up, Down, North, South, East, West, Num };

	// Where a face's picture sits on the skin, in pixels: the part's net is the classic cross with the top and
	// bottom side by side above the four sides (player's right, front, left, back).
	MCRENDER_API FIntRect GetFaceRect(FIntPoint NetOrigin, FIntVector Size, EBoxFace Face);

	// What is needed to pose the player each frame.
	struct FAnim
	{
		float HeadYaw = 0.0f;    // Minecraft yaw of the head: 0 faces south, 90 west
		float BodyYaw = 0.0f;    // the same for the torso
		float HeadPitch = 0.0f;  // positive looks down
		float WalkPos = 0.0f;    // limb swing: grows by the swing amount every tick
		float WalkAmount = 0.0f; // 0 standing, 1 running
		bool bSneaking = false;
		// Gliding on elytra: the body lies along the look (HeadPitch) and the wings spread.
		bool bGliding = false;
		// The head isn't drawn (the player's own, seen from inside it).
		bool bHeadHidden = false;
		// In bed: on its back, the top of the head toward the head of the bed (BodyYaw faces the foot).
		bool bSleeping = false;
	};

	struct FPartPose
	{
		FVector Pivot = FVector::ZeroVector;  // pixels, relative to the feet
		float PitchRad = 0.0f;                // about the pivot's east axis; positive tips the top forward
		float YawRad = 0.0f;                  // about the vertical axis, relative to the body; positive turns right-handed
		float RollRad = 0.0f;                 // about the pivot's south axis, first; positive swings the bottom to the left (+x)
	};

	struct FPose
	{
		float BodyYaw = 0.0f;  // the whole model turns this much (Minecraft degrees) about the feet
		bool bHeadHidden = false;  // the head part isn't drawn
		float TiltRad = 0.0f;  // and leans this far forward about them, before turning (gliding)
		FPartPose Parts[McNumPlayerParts];
		// The left wing of the elytra; the right one is its mirror image.
		FPartPose Wing;
	};

	// Elytra (minecraft.wiki "Elytra"): two wings, each a 10 by 20 by 2 pixel box drawn from the 64 by 32 wings picture
	// and grown a pixel all round, hung behind the shoulders. Standing they fold down the back, tipped 15 degrees out
	// and back; gliding spreads them into a wide V.
	constexpr int32 WingsWidth = 64;
	constexpr int32 WingsHeight = 32;

	// Worn armor: the game's humanoid armor model over the body, one 64 by 32 picture a piece
	// (textures/entity/equipment/humanoid/<material>.png, the leggings' humanoid_leggings/). The boxes are the body's,
	// grown as the armor stand's were measured: the helmet 1 pixel (its hat 1.5), the chestplate 1, the leggings 0.5 on the
	// body and 0.4 on the legs, the boots 0.9. The left arm and leg are the right ones' mirror image, as the game draws them.
	constexpr int32 ArmorWidth = 64;
	constexpr int32 ArmorHeight = 32;

	// Angles in radians, positive swinging the limb's bottom backwards. Legs swing cos(pos * 0.6662) * 1.4 * amount,
	// the left leg half a cycle behind the right. Each arm swings opposite to the leg on its side, as
	// cos(pos * 0.6662 + pi) * 2 * amount * 0.5: a little less than the legs.
	MCRENDER_API float GetLegSwing(float WalkPos, float WalkAmount, bool bRight);
	MCRENDER_API float GetArmSwing(float WalkPos, float WalkAmount, bool bRight);

	MCRENDER_API FPose ComputePose(const FAnim& Anim);

	// Where the body's feet-relative corner lands in Unreal units, relative to the actor: pixels to blocks to the
	// scale, then Minecraft axes to Unreal's.
	MCRENDER_API FVector ToUnreal(const FVector& ModelPx);

	struct FMeshData
	{
		TArray<FVector> Positions;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0;  // 0..1 over the skin, V down; the texture array's layer is always 0
		TArray<FColor> Colors;  // Minecraft's face shading
		TArray<int32> Triangles;
	};

	// Something worn on the head ("minecraft:zombie_head", "minecraft:carved_pumpkin"): the entity model it is drawn
	// with ("mc:head/zombie", "mc:worn/carved_pumpkin") and how much bigger than as placed; None for anything else.
	// Measured in vanilla from the front (ref_wh_*): a mob head 1.175 times its placed size, sitting on the neck.
	MCRENDER_API FName GetWornHeadModel(FName Item, float& OutScale);
	// Where a worn head's model goes in the pose, relative to the actor: its origin on the neck, turned with the head.
	MCRENDER_API FTransform GetWornHeadTransform(const FPose& Pose, float Scale);

	// Draws all twelve boxes (six parts, each with its outer layer) in the pose.
	MCRENDER_API void BuildMesh(const FPose& Pose, FMeshData& Out);
	// The two wings in the pose, with UVs over the wings picture.
	MCRENDER_API void BuildWings(const FPose& Pose, FMeshData& Out);
	// One armor piece in the pose (slot 0 head, 1 chest, 2 legs, 3 feet), with UVs over its picture; Grow more all round
	// (leather's overlay, drawn a hair outside its dyed base).
	MCRENDER_API void BuildArmor(const FPose& Pose, int32 Slot, FMeshData& Out, float Grow = 0.0f);
	// The picture an item worn in an armor slot is drawn with ("minecraft:iron_chestplate" in slot 1:
	// .../humanoid/iron.png); the overlay is leather's undyed one. Empty for anything not drawn as armor there.
	MCRENDER_API FString GetArmorTexture(FName Item, int32 Slot, bool bOverlay);

	// One part with its outer layer, standing, added to Out: its pivot at the origin, in the same units.
	MCRENDER_API void BuildPartMesh(EMcPlayerPart Part, FMeshData& Out);

	// Slow-moving state that makes the pose out of what the world sees: where the player is, and where they look.
	struct FAnimState
	{
		FAnim Current;
		FAnim Previous;  // one tick ago, for drawing between ticks
		FVector2D LastPosition = FVector2D::ZeroVector;  // blocks, x and z
		bool bHasPosition = false;

		// Once a game tick. Position in blocks (Minecraft axes); yaw/pitch where the player looks.
		MCRENDER_API void Tick(const FVector& FeetPos, float McYaw, float McPitch, bool bSneaking, bool bGliding = false);
		MCRENDER_API FAnim Blend(float Alpha) const;
	};

	// The body follows the head, but lags until the head is more than this far round.
	constexpr float MaxHeadTurn = 50.0f;
	MCRENDER_API float TurnBody(float BodyYaw, float HeadYaw, bool bMoving);
}

// Draws one player. Put it on the pawn (feet at its origin); SetLook says where the player looks, and movement is
// taken from the actor itself. Meant for players other than the one at the keyboard.
UCLASS()
class MCRENDER_API UMcPlayerModelComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UMcPlayerModelComponent(const FObjectInitializer& ObjectInitializer);

	// Where the player looks (Minecraft yaw and pitch, degrees) and whether they sneak.
	void SetLook(float InYaw, float InPitch, bool bInSneaking, bool bInGliding = false);
	// Whether elytra are worn (in the chest slot): they hang on the back.
	void SetWings(bool bInWings) { bWings = bInWings; }
	// The item in the head slot: a mob head or carved pumpkin is drawn on the head.
	void SetWornHead(FName Item) { WornHead = Item; }
	// The item in an armor slot (0 head, 1 chest, 2 legs, 3 feet): armor is drawn over the body.
	void SetArmor(int32 Slot, FName Item) { if (Slot >= 0 && Slot < 4) { ArmorItems[Slot] = Item; } }
	// Hides or shows the model; the local player doesn't see their own.
	void SetShown(bool bShown);
	// Lit as in full daylight whatever the time (the inventory's picture of the player).
	void SetFullyLit(bool bInFullyLit) { bFullyLit = bInFullyLit; }
	// Held in a pose rather than moving: the body turned to BodyYaw and the head as SetLook says, at once and standing
	// still (the inventory's picture of the player, which turns toward the mouse).
	void SetPosed(float InBodyYaw) { bPosed = true; PosedBodyYaw = InBodyYaw; }
	// In a bed whose head is that way (Minecraft yaw, the bed's facing): lying on the mattress, head on the pillow. The
	// model is placed from the owner's position, which is the middle of the bed's head block.
	void SetSleeping(bool bInSleeping, float InBedYaw) { bSleeping = bInSleeping; BedYaw = InBedYaw; }
	// Leaves out the head (and what is worn on it): the player in bed seeing their own body in first person.
	void SetHeadHidden(bool bInHidden) { bHeadHidden = bInHidden; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool EnsureMaterial();
	// The wings' material, made the first time they are worn; null if the picture is missing.
	UMaterialInstanceDynamic* EnsureWingsMaterial();
	// The material of an armor layer (0..3 the slots, 4..7 leather's overlay of them) for what is worn there; null for
	// nothing (or a missing picture).
	UMaterialInstanceDynamic* EnsureArmorMaterial(int32 Layer);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WingsMaterial;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2DArray> WingsPicture;
	bool bWingsFailed = false;
	bool bHasWingsSection = false;
	bool bWings = false;
	bool bGliding = false;
	FName WornHead;
	UPROPERTY(Transient)
	TObjectPtr<class UMcEntityModelComponent> Worn;
	// The look (McRenderMode) the material was made for.
	uint32 MaterialGeneration = 0;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2DArray> Skin;
	// The file the skin came from (HD Textures reads the same one).
	FString SkinPath;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	McPlayerModel::FAnimState State;
	float Yaw = 0.0f;
	float Pitch = 0.0f;
	bool bSneaking = false;
	bool bShown = true;
	bool bFullyLit = false;
	bool bPosed = false;
	float PosedBodyYaw = 0.0f;
	bool bSleeping = false;
	float BedYaw = 0.0f;
	bool bHeadHidden = false;
	bool bFailed = false;
	bool bHasSection = false;
	float TickAccumulator = 0.0f;

	// Armor: the items worn, each layer's material and the picture it was made for, mesh sections 2..9.
	FName ArmorItems[4];
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ArmorMaterials[8];
	FString ArmorPaths[8];
	bool bHasArmorSection[8] = {};
	// Pictures loaded, by file; null for one that could not be used.
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2DArray>> ArmorPictures;
};
