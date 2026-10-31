// Small picture textures for the materials that draw one picture at a time (the player's skin, the sun and the moon).

#pragma once

#include "CoreMinimal.h"

class UTexture2DArray;
class UObject;

namespace McRenderTextures
{
	// Loads a png into a one-layer texture array with nearest filtering and no mips, which is what the block materials
	// sample (smooth, or repeating, where the picture's .png.mcmeta says blur or clamp: false). Null (and an error in the log) if the file is missing or unreadable.
	// With bWhite every pixel's colour is made white and only its transparency is kept: the picture's silhouette, for a
	// mob flashing white.
	MCRENDER_API UTexture2DArray* LoadPngAsArray(UObject* Outer, const FString& Path, int32& OutWidth, int32& OutHeight, bool bWhite = false);
	// A png's pixels, row by row from the top. False (and an error in the log) if it can't be read. With MaxSide, a picture
	// wider than that (a resource pack's) is shrunk by a whole factor to about MaxSide: for code that makes geometry from
	// the pixels, which can't afford a mesh with a quad per pixel of a 1024 wide picture.
	MCRENDER_API bool LoadPngPixels(const FString& Path, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight, int32 MaxSide = 0);
	// A one-layer texture array of these pixels (Width x Height, rows from the top), as LoadPngAsArray makes.
	// With bBlur it is filtered smoothly; bClamp sets how it is sampled past its edges (clamped, or repeating).
	MCRENDER_API UTexture2DArray* MakeArray(UObject* Outer, const TArray<FColor>& Pixels, int32 Width, int32 Height, bool bBlur = false, bool bClamp = true);
}
