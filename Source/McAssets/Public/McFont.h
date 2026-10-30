// The game's bitmap font (textures/font/ascii.png: 16 by 16 cells of 8 pixels): how wide each glyph is, for the HUD's
// text and for anything that must fit text to a width (a sign's lines). A glyph is as wide as its rightmost pixel, a
// space is 3, and each glyph is followed by a gap of 1.

#pragma once

#include "CoreMinimal.h"

namespace McFont
{
	// How wide each of the 256 glyphs of a font sheet in the cache ("textures/font/ascii.png") is; 5 each if the sheet
	// can't be read.
	MCASSETS_API void ReadGlyphWidths(const TCHAR* Asset, TArray<uint8>& OutWidths);
	// The ascii sheet's, read once.
	MCASSETS_API const TArray<uint8>& GetGlyphWidths();
	// The width of a line in font pixels, each glyph with its gap after it (characters past the sheet count as 5).
	MCASSETS_API int32 GetWidth(const FString& Text);
	// Text cut, from the end, until it is no wider than MaxWidth.
	MCASSETS_API FString FitToWidth(const FString& Text, int32 MaxWidth);
}
