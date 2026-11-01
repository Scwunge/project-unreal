// The sign editor's window (minecraft.wiki, "Sign"): no slots, only the four lines of the side being written. It opens
// with the lines the side has; the player's screen sends them back, joined by '\n', when they press Done or Esc, and
// the server writes them to the sign as the window closes.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"

class MCITEM_API FMcSignMenu final : public FMcMenu
{
public:
	static constexpr int32 NumLines = 4;
	// No line is longer than this many characters (the screen stops at the sign's width long before).
	static constexpr int32 MaxLineLength = 90;

	FMcSignMenu() { Lines.SetNum(NumLines); }

	// Whether the back is being written, and the sign's kind for the screen (McSign::EKind, as a byte).
	bool bBack = false;
	uint8 SignKind = 0;
	// The block, for the screen's picture of the board ("minecraft:oak_sign").
	FName Block;
	// The dye the side is written in, for the screen.
	uint32 TextColour = 0;
	TArray<FString> Lines;

	// Takes up to four lines joined by '\n'. Characters the game won't put on a sign (the section sign that starts a
	// formatting code, control characters) are dropped, and a line is cut at MaxLineLength.
	virtual bool SetText(const FString& Text) override;
	virtual void SerializeState(FArchive& Ar) override;

	static FString Join(const TArray<FString>& Lines);
	static TArray<FString> Split(const FString& Text);
};
