// What makes new chunks for a dimension. Generators are immutable once built, so the server can run several
// chunks at once on worker threads.

#pragma once

#include "CoreMinimal.h"

class FMcChunk;

class MCGEN_API FMcChunkGenerator
{
public:
	virtual ~FMcChunkGenerator() = default;

	// Fills a freshly made, empty chunk. Safe to call from any thread.
	virtual void Generate(FMcChunk& Chunk) const = 0;

	// The y of the highest solid or liquid block in a column, for placing players.
	virtual int32 FindSurfaceY(int32 X, int32 Z) const = 0;

	// For a dimension with a roof (the Nether): the y of the highest solid block below BelowY with two blocks of air,
	// not fluid, above it, so somewhere to stand under the ceiling. Below the column's bottom (MinY - 1) if there is none.
	virtual int32 FindFloorY(int32 X, int32 Z, int32 BelowY) const { return FindSurfaceY(X, Z); }

	// The y of the top solid block, below the sea as well, so a sea column can be told from land (FindSurfaceY holds at sea level).
	virtual int32 FindSolidTopY(int32 X, int32 Z) const { return FindSurfaceY(X, Z); }
};
