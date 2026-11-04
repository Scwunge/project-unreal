// Every block state baked into a list of quads, built from the blockstate and model JSON in cache/.

#pragma once

#include "CoreMinimal.h"
#include "McAssets.h"
#include "McCore.h"

// The six face directions in the model format, plus None for "no cullface".
enum class EMcDirection : uint8
{
	Down,
	Up,
	North,  // -Z
	South,  // +Z
	West,   // -X
	East,   // +X
	None,
};

MCASSETS_API const TCHAR* McDirectionName(EMcDirection Direction);

// One face of a model element, after the element's and the blockstate's rotations.
// Coordinates follow the model format, not Unreal's: X east, Y up, Z south, in block units (0..1).
// Use McCoords::BlockToUnreal to place them in the world.
struct FMcBakedQuad
{
	// Counter-clockwise as seen from outside the face: top-left, bottom-left, bottom-right, top-right of the
	// texture region (so Positions[1] - Positions[0] runs "down" the texture).
	FVector3f Positions[4];
	// 0..1 over the whole texture, V down (V = 0 is the top row), matching the corners above.
	FVector2f UVs[4];
	FName Texture;                              // e.g. "minecraft:block/stone"; "minecraft:missingno" if unresolved
	EMcDirection Face = EMcDirection::None;     // the direction the face points after rotation
	EMcDirection CullFace = EMcDirection::None; // skip the quad when the neighbour on this side is opaque
	EMcDirection ShadeFace = EMcDirection::None; // the direction its directional shading is taken from
	int8 TintIndex = -1;                        // -1 for untinted
	bool bShade = true;                         // false: no directional shading at all
	bool bAmbientOcclusion = true;
	bool bForceTranslucent = false;             // the model marks the texture force_translucent: draw in the translucent pass
};

enum class EMcModelStatus : uint8
{
	Baked,    // has at least one quad
	Empty,    // resolved fine but draws nothing (air, barrier, light blocks ...)
	Builtin,  // uses a builtin parent (builtin/generated, builtin/entity): no geometry from JSON
	Failed,   // missing blockstate or model file, or no variant matches the state
};

struct FMcBakedModel
{
	TArray<FMcBakedQuad> Quads;
	EMcModelStatus Status = EMcModelStatus::Failed;
};

struct FMcBakeStats
{
	int32 Baked = 0;
	int32 Empty = 0;
	int32 Builtin = 0;
	int32 Failed = 0;
	int32 Quads = 0;
	int32 UnresolvedTextures = 0;  // faces whose "#variable" never got a value
	int32 UnhandledElements = 0;   // element features the baker ignores (see McBlockModels.cpp)
	TArray<FString> Problems;      // the first few distinct failure messages
};

// A model's "display" transform for one context (gui, firstperson_righthand, ground, fixed ...): rotation in degrees,
// translation in sixteenths of a block, scale.
struct FMcModelDisplay
{
	FVector Rotation = FVector::ZeroVector;
	FVector Translation = FVector::ZeroVector;
	FVector Scale = FVector::OneVector;
};

// A model by id ("minecraft:item/foo", "minecraft:block/bar"), read through the packs with its parents folded in, for what
// is not a block state: an item's icon, the item in the hand. Quads are in the model's own frame (0..1, x east, y up, z
// south) without any rotation of a state. A "builtin/generated" model (flat items) has no quads but its texture layers.
struct FMcItemModel
{
	TArray<FMcBakedQuad> Quads;
	TArray<FName> Layers;  // layer0, layer1 ... of a generated model, as texture ids
	TMap<FString, FMcModelDisplay> Display;
	bool bGenerated = false;
	bool bFailed = false;
	bool bGuiFront = false;  // "gui_light": "front": flat lit in the inventory, not from above
};
MCASSETS_API FMcItemModel McBakeItemModel(const FString& ModelId);

// Loaded once on the game thread, then read only, so mesher threads may read it freely.
class MCASSETS_API FMcBlockModels
{
public:
	static FMcBlockModels& Get();

	// Loads the block registry if needed, then bakes every state. Does nothing if already baked.
	bool EnsureBaked();
	bool IsBaked() const { return Models.Num() > 0; }

	int32 NumStates() const { return Models.Num(); }
	const FMcBakedModel& GetModel(FMcStateId Id) const { return Models[Id]; }

	// Every texture id any quad refers to, sorted, for building the texture array.
	const TArray<FName>& GetTextures() const { return Textures; }

	const FMcBakeStats& GetStats() const { return Stats; }

	// Forgets the baked models, so the next EnsureBaked reads the files again (a resource pack changed). Only while no
	// world is using them: nothing may be meshing.
	void Unload() { Models.Empty(); Textures.Empty(); Stats = FMcBakeStats(); }

private:
	TArray<FMcBakedModel> Models;  // indexed by state id
	TArray<FName> Textures;
	FMcBakeStats Stats;
};
