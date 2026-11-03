// Small picture textures for the materials that draw one picture at a time (the player's skin, the sun and the moon).

#pragma once

#include "CoreMinimal.h"

class UTexture2DArray;
class UObject;

namespace McRenderTextures
{
	// Loads a png into a one-layer texture array with nearest filtering and no mips, which is what the block materials
	// sample. Null (and an error in the log) if the file is missing or unreadable.
	// With bWhite every pixel's colour is made white and only its transparency is kept: the picture's silhouette, for a
	// mob flashing white.
	MCRENDER_API UTexture2DArray* LoadPngAsArray(UObject* Outer, const FString& Path, int32& OutWidth, int32& OutHeight, bool bWhite = false);
	// A png's pixels, row by row from the top. False (and an error in the log) if it can't be read.
	MCRENDER_API bool LoadPngPixels(const FString& Path, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);
	// Lays Over on Under, straight alpha: where Over is see-through Under shows, where it is solid Over does, between the two
	// they mix. Both are the same size. The result is Under with a picture painted on it.
	MCRENDER_API void CompositeOver(TArray<FColor>& Under, const TArray<FColor>& Over);
	// The pictures in order, each laid over the last, as one texture array (the size of the first). An empty path is skipped.
	// Null (and an error in the log) if the first can't be read.
	MCRENDER_API UTexture2DArray* LoadPngLayersAsArray(UObject* Outer, const TArray<FString>& Paths, int32& OutWidth, int32& OutHeight);
	// A one-layer texture array of these pixels (Width x Height, rows from the top), as LoadPngAsArray makes.
	MCRENDER_API UTexture2DArray* MakeArray(UObject* Outer, const TArray<FColor>& Pixels, int32 Width, int32 Height);
}
