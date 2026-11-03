// Section meshing: every block state's baked model quads (McAssets), culled against their neighbours, with the
// texture array layer, tint and face shading of each quad.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcBlockModels;
struct FMcRenderPalette;
class FMcBlockRegistry;
class FMcBlockTextures;
class FMcBlockWorld;
struct FMcLightRegion;

// Where a tinted quad's colour comes from: its own fixed colour, or the biome's.
enum class EMcTintSource : uint8
{
	Fixed,
	Grass,
	Foliage,
	Water,
};

// A copy of one section plus a one-block border from its neighbours, so a worker thread can mesh it while
// the world goes on changing.
struct MCRENDER_API FMcSectionSnapshot
{
	static constexpr int32 Padded = 18;

	FIntPoint ChunkPos;
	int32 SectionIndex = 0;
	int32 MinY = 0;  // world height of the section's bottom block
	TArray<FMcStateId> Blocks;  // Padded^3, see Index()
	TArray<uint8> Light;        // Padded^3: sky light in the high four bits, block light in the low four
	// Biome colours per column (Padded^2 each, see ColumnIndex), blended over 5x5 columns like the game's
	// default Biome Blend: grass, then foliage, then water. Empty means plains everywhere.
	TArray<FColor> Tints;

	// X, Y and Z are local to the section and run from -1 to 16.
	static FORCEINLINE int32 Index(int32 X, int32 Y, int32 Z) { return ((Y + 1) * Padded + (Z + 1)) * Padded + (X + 1); }
	FORCEINLINE FMcStateId Get(int32 X, int32 Y, int32 Z) const { return Blocks[Index(X, Y, Z)]; }
	static FORCEINLINE int32 ColumnIndex(int32 X, int32 Z) { return (Z + 1) * Padded + (X + 1); }
	// The biome colour for a column; Fixed gives white.
	FColor GetTint(EMcTintSource Source, int32 X, int32 Z) const;

	// Game thread. Unloaded neighbours and heights outside the world read as air; light is full sky everywhere;
	// tints are plains'.
	static FMcSectionSnapshot Capture(const FMcBlockWorld& World, FIntPoint ChunkPos, int32 SectionIndex);

	// Any thread: a section of the region's middle column, with the region's computed light and the biome
	// tints of its columns.
	static FMcSectionSnapshot FromRegion(const FMcLightRegion& Region, int32 SectionIndex, const FMcRenderPalette& Palette);
};

// How a quad is drawn; one material each (M_McBlockOpaque, M_McBlockCutout, M_McBlockTranslucent; Water uses the translucent one).
enum class EMcRenderPass : uint8
{
	Opaque,
	Cutout,       // texels are either fully opaque or dropped
	Translucent,  // blended
	Water,        // blended too, but drawn before all of the Translucent (ice, glass) so they show over it
	Num,
};

constexpr int32 McNumRenderPasses = static_cast<int32>(EMcRenderPass::Num);

// What kind of surface a vertex is on, in UV1.y, for Unreal Mode's materials (tools/make_materials.py): water waves,
// lava burns, leaves and plants sway, blocks that give light glow. The vanilla look reads only the particle kinds,
// which are drawn in one colour (the vertex colour) instead of from the texture.
namespace McSurface
{
	enum : uint8
	{
		Plain = 0,
		Water = 1,
		Lava = 2,
		Leaves = 3,         // the whole block sways
		Plant = 4,          // sways by how high up its texture the vertex is, so the roots stay put
		Glow = 5,
		Particle = 9,       // one colour
		GlowParticle = 10,  // one colour, glowing
		TexturedParticle = 11,  // a bit of a block's texture (debris, leaves), with no bump
	};

	// The kind for a block's own quads (never the fluids', which the fluid mesher marks).
	MCRENDER_API uint8 ForBlock(FName Block, uint8 Emission);
}

struct MCRENDER_API FMcMeshBuffers
{
	TArray<FVector> Positions;  // Unreal units, relative to the chunk column's origin at height 0
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;      // the position inside the 16x16 texture, 0..1, V down
	TArray<FVector2D> UV1;      // X is the texture array layer
	TArray<FVector2D> UV2;      // sky light and block light, 0..1, smoothed between blocks
	TArray<FColor> Colors;      // tint times face shade times ambient occlusion, sRGB
	TArray<int32> Triangles;

	bool IsEmpty() const { return Triangles.Num() == 0; }
};

namespace McTranslucent
{
	// Puts the quads of a pass (four vertices and six indices each, in step) in order from the farthest from View to
	// the nearest, by the middle of each, so blending them in that order gives glass behind glass its right colour.
	// View is in the same space as the positions (Unreal units, chunk column relative). The vertices stay where they are.
	MCRENDER_API void SortQuadsBackToFront(FMcMeshBuffers& Buffers, const FVector& View);
}

struct MCRENDER_API FMcSectionMesh
{
	int32 SectionIndex = 0;
	FMcMeshBuffers Passes[McNumRenderPasses];

	bool IsEmpty() const;
};

// What the mesher needs to know about every state and quad, built once on the game thread and shared
// read-only with the mesh tasks.
struct MCRENDER_API FMcRenderPalette
{
	struct FQuadInfo
	{
		uint16 Layer = 0;
		EMcRenderPass Pass = EMcRenderPass::Opaque;
	};

	struct FStateInfo
	{
		int32 FirstQuad = 0;          // into Quads, one entry per quad of the state's baked model, in order
		int32 Block = INDEX_NONE;     // registry index of the state's block
		uint8 OpaqueFaces = 0;        // bit per EMcDirection: opaque quads cover that whole side of the block
		bool bOccludes = false;       // an opaque full cube: darkens corners next to it (ambient occlusion)
		bool bHidesOwnBlock = false;  // glass and the like: faces against the same block are not drawn
		FColor Tint = FColor::White;  // for quads with a tint index when TintSource is Fixed, in sRGB
		EMcTintSource TintSource = EMcTintSource::Fixed;
		uint8 Surface = McSurface::Plain;  // McSurface
	};

	// Each biome's colours, by FMcBiomeRegistry id, in sRGB.
	struct FBiomeTint
	{
		FColor Grass = FColor::White;
		FColor Foliage = FColor::White;
		FColor Water = FColor::White;
	};

	const FMcBlockModels* Models = nullptr;
	TArray<FStateInfo> States;  // indexed by state id
	TArray<FQuadInfo> Quads;
	TArray<FBiomeTint> BiomeTints;
	bool bFlipWinding = false;
	bool bSmoothLighting = true;  // Minecraft's Smooth Lighting option: per-corner light and ambient occlusion

	// Needs the registry loaded, the models baked and the textures loaded.
	static TSharedRef<const FMcRenderPalette> Build(const FMcBlockRegistry& Registry, const FMcBlockModels& Models, const FMcBlockTextures& Textures, bool bFlipWinding);
};

namespace McBlockMesher
{
	// Any thread. Draws every quad of every block, leaving out the quads whose cull face is hidden by the
	// neighbouring block.
	MCRENDER_API FMcSectionMesh Build(const FMcSectionSnapshot& Snapshot, const FMcRenderPalette& Palette);

	// The tint Minecraft gives a block's tinted quads (tint index 0) in a plains biome, in sRGB. Exposed for
	// tests.
	MCRENDER_API FColor GetBlockTint(const FMcBlockRegistry& Registry, FMcStateId State);

	// Whether that tint is the biome's grass, foliage or water colour, or the block's own.
	MCRENDER_API EMcTintSource GetTintSource(const FMcBlockRegistry& Registry, FMcStateId State);
}
