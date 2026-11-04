// JSON text, as the game's chat, titles, signs and item names carry it: a string, a number or boolean, an object
// ({"text":..., "color":..., "bold":..., "extra":[...]}) or an array (its first element is the parent of the rest). Read
// into spans of plain text with their resolved style (a child takes its parent's style except where it sets its own), and
// written out as legacy text (the section-sign codes the HUD draws), which is what travels in a chat line.
//
// Read: text, translate (+ fallback, with), keybind (its default key name), extra, color (a name or #RRGGBB), bold, italic,
// underlined, strikethrough, obfuscated, font, click_event and hover_event (kept for the screens that act on them).
// Not read: score, selector, nbt (they need the world); such a component reads as nothing.

#pragma once

#include "CoreMinimal.h"

class FJsonValue;

namespace McText
{
	struct FSpan
	{
		FString Text;
		// 0xRRGGBB, or -1 for the colour of whatever draws it.
		int32 Rgb = -1;
		bool bBold = false;
		bool bItalic = false;
		bool bUnderlined = false;
		bool bStrikethrough = false;
		bool bObfuscated = false;
		// "minecraft:uniform", "zz:pix": empty is the default font.
		FString Font;
		// open_url, run_command, suggest_command, copy_to_clipboard, change_page, open_file, with its value (url, command, ...).
		FString ClickAction;
		FString ClickValue;
		// The text of a show_text hover, as legacy text; empty for none.
		FString HoverText;
	};

	// The colour a name stands for ("gold" 0xFFAA00), or -1.
	MCCORE_API int32 ColourFromName(const FString& Name);
	// The legacy code letter of a named colour's value ('6' for 0xFFAA00), or 0 if the value is not one of the sixteen.
	MCCORE_API TCHAR LegacyCodeOf(int32 Rgb);

	// Spans of a component given as JSON text. False (Out empty) when the text isn't JSON.
	MCCORE_API bool Parse(const FString& Json, TArray<FSpan>& Out);
	MCCORE_API void FromValue(const TSharedPtr<FJsonValue>& Value, TArray<FSpan>& Out);

	// The spans as one string of legacy text: each span starts with its colour (a named one as its letter, any other as the
	// section sign, x and six digits) and styles, after a reset where the previous span had one the next lacks.
	MCCORE_API FString ToLegacy(const TArray<FSpan>& Spans);
	MCCORE_API FString ToPlain(const TArray<FSpan>& Spans);
	// Parse and ToLegacy in one: a string that isn't JSON comes back as it is.
	MCCORE_API FString JsonToLegacy(const FString& Json);
}
