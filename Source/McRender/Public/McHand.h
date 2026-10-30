// The player's own hand in first person: the right arm in its skin at the bottom right of the view, with the item
// or block it holds. It swings when the player hits or uses something, bobs as they walk, and drops and comes up
// again when they change what they hold. Everything is placed relative to the camera (Unreal axes there: x forward,
// y right, z up) and shrunk by Scale, so that it stays in front of the camera and never reaches into a wall the
// player stands against; the picture is the same size as the game's.
//
// The maths (where the arm is for a swing, a bob and an equip) is plain functions; the component at the end draws it.

#pragma once

#include "CoreMinimal.h"
#include "McPlayerModel.h"
#include "ProceduralMeshComponent.h"
#include "McHand.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2DArray;

namespace McHand
{
	// How much smaller than the game's distances everything is drawn, and so how much nearer the camera.
	constexpr double Scale = 1.0;
	constexpr float SwingSeconds = 0.3f;
	constexpr float EquipSeconds = 0.2f;

	// What the hand holds, as it is drawn.
	enum class EHeld : uint8
	{
		Nothing,
		Flat,  // an item's picture, standing in the hand
		Cube,  // a full block: its top and sides
	};

	// How far a swing (0 just started, 1 done) has gone: 0 at rest, up to 1 at the middle of the stroke.
	MCRENDER_API float GetSwingStroke(float Progress);

	// The sway the walk gives: x to the right and y up, in blocks. WalkPos grows by the distance walked, WalkAmount is
	// 0 standing and 1 at a walking pace or faster.
	MCRENDER_API FVector2D GetBob(float WalkPos, float WalkAmount);

	// The arm's place in the camera's space: its shoulder end, and the way it points (the fingertips are along the
	// local -z axis). Swing is the stroke (GetSwingStroke), Equip how far it is lowered out of view (0 up, 1 gone).
	MCRENDER_API FTransform GetArmTransform(float Stroke, float Equip, const FVector2D& Bob);

	// Where the held thing sits when the arm is at rest, in the camera's space.
	MCRENDER_API FTransform GetHeldRestTransform(EHeld Kind);

	// The shift in the camera's space of what is eaten: Lift is how far it is raised toward the face (0 to 1), Time and
	// Seconds the progress of the meal. It bobs for the first four fifths of it.
	MCRENDER_API FTransform GetEatTransform(float Lift, float Time, float Seconds);

	// The fingertips in the arm's own space.
	MCRENDER_API FVector GetTipInArm();

	// The textures of an item or a block held: for a flat one Side only; for a cube Side is the four sides, Top the top.
	struct FHeldPictures
	{
		EHeld Kind = EHeld::Nothing;
		FString Side;
		FString Top;
		// The cube's top is grey in the picture and takes a colour (grass, leaves).
		FColor TopTint = FColor::White;
		FColor SideTint = FColor::White;
	};

	// What picture(s) a held item uses: its own picture (item/<name>) flat, or, for a full block, the sides and top of
	// its textures; a block that is not a full cube is drawn flat from its picture.
	MCRENDER_API FHeldPictures FindHeldPictures(FName Item);

	// The meshes, in the arm's or the held thing's own space. Cubes are 1 block, flats 1 block wide; the held thing's
	// transform shrinks them.
	MCRENDER_API void BuildArmMesh(McPlayerModel::FMeshData& Out);
	// How the held bow is turned while it is drawn, in the camera's space.
	MCRENDER_API FQuat GetBowStandRotation();
	MCRENDER_API void BuildFlatMesh(float VMax, McPlayerModel::FMeshData& Out);
	// The picture's opaque pixels stood one pixel thick, as the game's generated item models are: a front and a back face and a narrow face on every edge between a pixel and a see-through one. Pixels are rows from the top, Width across, and the top Width rows are the picture.
	MCRENDER_API void BuildExtrudedMesh(const TArray<FColor>& Pixels, int32 Width, float VMax, McPlayerModel::FMeshData& Out);
	MCRENDER_API void BuildCubeSidesMesh(const FColor& Tint, float VMax, McPlayerModel::FMeshData& Out);
	MCRENDER_API void BuildCubeTopMesh(const FColor& Tint, float VMax, McPlayerModel::FMeshData& Out);
}

// Draws the hand. Put it on the player's camera; SetHeld says what is held. It swings by itself when the local player
// presses the attack or use button (with no menu open) and takes the walk from the actor's movement.
UCLASS()
class MCRENDER_API UMcHandComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UMcHandComponent(const FObjectInitializer& ObjectInitializer);

	// The item in the selected slot (none for an empty hand). Changing it lowers the hand and brings it up again.
	void SetHeld(FName InItem);
	void SetShown(bool bInShown);
	// Starts a swing (the component also starts one for the buttons).
	void Swing();
	// Whether the held item is being eaten and how long the whole meal takes: the item comes up toward the face and
	// bobs while it is chewed.
	void SetEating(bool bInEating, float InSeconds);
	// Whether a bow is being drawn and for how long: it swings toward the middle of the screen, tilted, and shakes at
	// full draw.
	void SetDrawing(bool bInDrawing, float InSeconds);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void EnsureArm();
	void ShowHeld(FName Item);
	UMaterialInstanceDynamic* GetMaterial(const FString& Path, FIntPoint& OutSize);
	void ClearMaterials();

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UMaterialInstanceDynamic>> Materials;
	TMap<FString, FIntPoint> Sizes;
	UPROPERTY(Transient)
	TObjectPtr<UProceduralMeshComponent> HeldMesh;

	uint32 MaterialGeneration = 0;
	bool bShown = true;
	bool bArmBuilt = false;
	// What is shown now, and what is wanted (the hand lowers to change).
	FName Shown;
	FName Wanted;
	McHand::EHeld ShownKind = McHand::EHeld::Nothing;
	float Equip = 1.0f;
	float SwingAge = 100.0f;
	bool bEating = false;
	float EatSeconds = 1.6f;
	// How far the item is raised (0 to 1) and how long it has been eaten.
	float EatLift = 0.0f;
	float EatTime = 0.0f;
	bool bDrawing = false;
	float DrawSeconds = 0.0f;
	float DrawLift = 0.0f;
	bool bDrawApplied = false;
	float WalkPos = 0.0f;
	float WalkAmount = 0.0f;
	FVector LastLocation = FVector::ZeroVector;
	bool bHasLocation = false;
	FTransform ArmRest;
	FTransform HeldRest;
	// A development hold: -McHandSwing=<0..1> stops the swing there for a picture.
	float FrozenSwing = -1.0f;
};
