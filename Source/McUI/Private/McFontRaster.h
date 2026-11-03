#pragma once

#include "CoreMinimal.h"

// One character that a font provider draws itself (a TrueType outline, a bit of a Unifont .hex file, a cell of a legacy
// Unicode page) instead of cutting it from a sheet: its picture, white with the shape in the alpha, and where and how big
// it is drawn, in GUI pixels. The pen is at the left of the line and the baseline 7 pixels below its top.
struct FMcRasterGlyph
{
	int32 Width = 0;
	int32 Height = 0;
	TArray<uint8> Bgra;
	float DrawWidth = 0.0f;
	float DrawHeight = 0.0f;
	float OffsetX = 0.0f;
	float OffsetY = 0.0f;
	float Advance = 0.0f;
	float ShadowOffset = 1.0f;
};

// The providers of a font definition that make their characters on demand: "unihex", "legacy_unicode" and "ttf". They are
// kept in the order of the definition (Priority is the provider's place in it) so a character comes from the first one that
// has it, the same as for the providers that cut sheets.
class FMcRasterFonts
{
public:
	struct FProvider;
	FMcRasterFonts();
	~FMcRasterFonts();

	struct FUnihexOverride
	{
		int32 From = 0, To = 0;
		int32 Left = 0, Right = 0;
	};

	// The text of a Unifont .hex file ("0041:0000...", 32, 64, 96 or 128 hex digits for 8 to 32 pixels by 16).
	void AddUnihex(int32 Priority, TArray<uint8> HexText, TArray<FUnihexOverride> Overrides);
	// The sizes file (one byte per character: first column in the high nibble, last in the low) and the file name of a page,
	// "font/unicode_page_%s.png" with the page's two hex digits for %s.
	void AddLegacyUnicode(int32 Priority, TArray<uint8> Sizes, const FString& PageTemplate);
	// A TrueType font file: size in GUI pixels of its em, the factor it is drawn
	// larger and scaled down by, the shift of its glyphs, and the characters it leaves out.
	void AddTrueType(int32 Priority, TArray<uint8> File, float Size, float Oversample, FVector2D Shift, const FString& Skip);

	// The code point from the first provider before BeforePriority that has it.
	bool Make(int32 Char, int32 BeforePriority, FMcRasterGlyph& Out);

private:
	TArray<TSharedPtr<FProvider>> Providers;
};
