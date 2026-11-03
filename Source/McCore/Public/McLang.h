// The game's English text, from the jar's lang/en_us.json: chat lines, command replies, game mode names. Read once on
// first use. A key the file doesn't have reads as itself, as the game shows it.

#pragma once

#include "CoreMinimal.h"

namespace McLang
{
	// One entry of the language picker: "English (US)" is Name "English", Region "US".
	struct FLanguage
	{
		FString Code;
		FString Name;
		FString Region;
		bool bBidirectional = false;
		FString GetLabel() const { return Name + TEXT(" (") + Region + TEXT(")"); }
	};
	// The languages the game offers, by code: the launcher's pack.mcmeta list, then those a pack adds (a pack's entry for a
	// code the game has replaces its name).
	MCCORE_API TArray<FLanguage> GetLanguages();
	// The chosen language's code, "en_us" until one is chosen. -McLang=<code> picks one for a run without saving it.
	MCCORE_API FString GetLanguage();
	// Chooses a language (kept in Saved/McLanguage.json) and makes the next Get use it. The text is the English file with the
	// chosen language's file over it key by key, then the packs' files of both over those.
	MCCORE_API void SetLanguage(const FString& Code);
	// Bumps when the language or the packs change, for screens that keep their text.
	MCCORE_API uint32 GetGeneration();

	MCCORE_API FString Get(const FString& Key);
	// The text with its %s (and %1$s style) placeholders filled in order.
	MCCORE_API FString Format(const FString& Key, const TArray<FString>& Args);
	// Fills a pattern's placeholders; exposed for tests.
	MCCORE_API FString FormatPattern(const FString& Pattern, const TArray<FString>& Args);
}
