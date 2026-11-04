// Block pictures bigger than a layer of the texture array (signs and hanging signs are 32 pixels, straw_bed 64) are cut
// into 16-pixel tiles, each a layer of its own, so they keep every pixel instead of being scaled down. The baker splits
// each quad drawn with such a picture along the tiles' edges, one quad a tile, and names the tile in the quad's texture:
// "minecraft:block/oak_sign#tile:1,0:2,2" is the tile one across and none down of a picture two tiles by two.

#pragma once

#include "CoreMinimal.h"
#include "McAssets.h"

struct FMcBakedQuad;

namespace McTextureTiles
{
	// How many tiles across and down a picture is cut into: (1, 1) for one that fits a layer, is animated, or isn't a
	// whole number of 16-pixel tiles. Reads only the PNG's header (and whether there is a .mcmeta).
	MCASSETS_API FIntPoint GetGrid(FName TextureId);
	// The same from a picture's size and whether it is animated.
	MCASSETS_API FIntPoint GetGrid(int32 Width, int32 Height, bool bAnimated);

	MCASSETS_API FName MakeTileId(FName TextureId, FIntPoint Tile, FIntPoint Grid);
	// Splits a tile's id back into its picture, the tile and the grid. False for any other id.
	MCASSETS_API bool ParseTileId(FName Id, FName& OutTextureId, FIntPoint& OutTile, FIntPoint& OutGrid);

	// The quad cut along the tiles' edges: a quad for each tile its picture region touches, with UVs 0..1 over that tile
	// and the tile's id. The pieces keep the quad's corner order and cover it exactly. A quad whose UVs aren't an
	// axis-aligned rectangle (none in the game's models) is given to the tile under its middle whole.
	MCASSETS_API void SplitQuad(const FMcBakedQuad& Quad, FIntPoint Grid, TArray<FMcBakedQuad>& Out);
}
