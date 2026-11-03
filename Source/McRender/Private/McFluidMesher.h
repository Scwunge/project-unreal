// Water and lava surfaces: sloped tops, flowing sides and the water inside waterlogged blocks. Drawn separately
// from the block models because a fluid's shape depends on its neighbours rather than on the state alone.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"
#include "McSectionMesher.h"

class FMcBlockRegistry;
class FMcBlockTextures;

namespace McFluidMesher
{
	enum class EFluid : uint8
	{
		None,
		Water,
		Lava,
	};

	struct FFluidInfo
	{
		EFluid Kind = EFluid::None;
		uint8 Level = 0;       // 0 is a source, 1..7 flowing further from it
		bool bFalling = false; // level 8 and up in the report: fluid that is falling
	};

	// Built once on the game thread and shared read-only with the mesh tasks.
	struct FFluidPalette
	{
		TArray<FFluidInfo> States;  // indexed by state id
		uint16 WaterStill = 0;      // texture array layers
		uint16 WaterFlow = 0;
		uint16 LavaStill = 0;
		uint16 LavaFlow = 0;
		// The quarter of the flowing pictures a side shows, unscaled ("<id>#side", FMcBlockTextures).
		uint16 WaterFlowSide = 0;
		uint16 LavaFlowSide = 0;
		FColor WaterTint = FColor::White;  // plains'; meshes take the biome's from the snapshot
	};

	// Needs the registry and textures loaded.
	TSharedRef<const FFluidPalette> BuildPalette(const FMcBlockRegistry& Registry, const FMcBlockTextures& Textures);

	// Any thread. Appends water to the translucent pass and lava to the opaque one.
	void Build(const FMcSectionSnapshot& Snapshot, const FFluidPalette& Fluids, const FMcRenderPalette& Blocks, FMcSectionMesh& InOut);

	// The height of the fluid's own block, 0..1 (a source is 8/9). Exposed for tests.
	float OwnHeight(const FFluidInfo& Info);
}
