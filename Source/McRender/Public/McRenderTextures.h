// Small picture textures for the materials that draw one picture at a time (the player's skin, the sun and the moon).

#pragma once

#include "CoreMinimal.h"

class UTexture2DArray;
class UObject;

namespace McRenderTextures
{
	// Loads a png into a one-layer texture array with nearest filtering and no mips, which is what the block materials
	// sample. Null (and an error in the log) if the file is missing or unreadable.
	MCRENDER_API UTexture2DArray* LoadPngAsArray(UObject* Outer, const FString& Path, int32& OutWidth, int32& OutHeight);
}
