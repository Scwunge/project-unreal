// The colours of a map: the 64 base colours, the four brightnesses, and which base colour each block state is.
// All from the vanilla reference run mapcolors (the harness verb log mapcolors reads the game's own tables).
//
// A map pixel is one byte, base colour * 4 + brightness (brightness 0 low, 1 normal, 2 high, 3 lowest); 0 to 3 is no colour.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

namespace McMapColors
{
	// The base colour (0 none, 1 grass ...) of a block state.
	MCCORE_API uint8 ColorOf(FMcStateId State);

	// The ARGB of a pixel byte (0 for no colour).
	MCCORE_API uint32 Argb(uint8 Pixel);

	inline uint8 Pack(int32 Color, int32 Brightness) { return static_cast<uint8>(Color * 4 + Brightness); }
}
