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
	// The outer layer is this many pixels bigger than the part on every side.
	constexpr float OuterInflate = 0.5f;

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
	};

	struct FPartPose
	{
		FVector Pivot = FVector::ZeroVector;  // pixels, relative to the feet
		float PitchRad = 0.0f;                // about the pivot's east axis; positive tips the top forward
		float YawRad = 0.0f;                  // about the vertical axis, relative to the body; positive turns right-handed
	};

	struct FPose
	{
		float BodyYaw = 0.0f;  // the whole model turns this much (Minecraft degrees) about the feet
		FPartPose Parts[McNumPlayerParts];
	};

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

	// Draws all twelve boxes (six parts, each with its outer layer) in the pose.
	MCRENDER_API void BuildMesh(const FPose& Pose, FMeshData& Out);

	// Slow-moving state that makes the pose out of what the world sees: where the player is, and where they look.
	struct FAnimState
	{
		FAnim Current;
		FAnim Previous;  // one tick ago, for drawing between ticks
		FVector2D LastPosition = FVector2D::ZeroVector;  // blocks, x and z
		bool bHasPosition = false;

		// Once a game tick. Position in blocks (Minecraft axes); yaw/pitch where the player looks.
		MCRENDER_API void Tick(const FVector& FeetPos, float McYaw, float McPitch, bool bSneaking);
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
	void SetLook(float InYaw, float InPitch, bool bInSneaking);
	// Hides or shows the model; the local player doesn't see their own.
	void SetShown(bool bShown);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	bool EnsureMaterial();

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;
	UPROPERTY(Transient)
	TObjectPtr<UTexture2DArray> Skin;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	McPlayerModel::FAnimState State;
	float Yaw = 0.0f;
	float Pitch = 0.0f;
	bool bSneaking = false;
	bool bShown = true;
	bool bFailed = false;
	bool bHasSection = false;
	float TickAccumulator = 0.0f;
};
