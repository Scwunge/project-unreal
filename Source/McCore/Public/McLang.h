// The game's English text, from the jar's lang/en_us.json: chat lines, command replies, game mode names. Read once on
// first use. A key the file doesn't have reads as itself, as the game shows it.

#pragma once

#include "CoreMinimal.h"

namespace McLang
{
	MCCORE_API FString Get(const FString& Key);
	// The text with its %s (and %1$s style) placeholders filled in order.
	MCCORE_API FString Format(const FString& Key, const TArray<FString>& Args);
	// Fills a pattern's placeholders; exposed for tests.
	MCCORE_API FString FormatPattern(const FString& Pattern, const TArray<FString>& Args);
}
