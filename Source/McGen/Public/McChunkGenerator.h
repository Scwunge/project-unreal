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
};
