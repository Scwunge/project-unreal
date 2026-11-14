// Copper's weathering and wax (minecraft.wiki, "Oxidation"; rates fitted to the reference run copper1b, see PLAN.md "Copper").
// Pure names: a block is in the family when the exposed, weathered and oxidized versions of it exist in the block registry.

#pragma once

#include "CoreMinimal.h"

namespace McCopper
{
	// A copper block that weathers: Age 0 to 3 (plain, exposed, weathered, oxidized); false for waxed blocks and everything else.
	MCCORE_API bool GetAge(FName Block, int32& OutAge);
	// The same block one step more weathered (NAME_None at 3 or for a block that does not weather), and one step less.
	MCCORE_API FName GetNextAge(FName Block);
	MCCORE_API FName GetPreviousAge(FName Block);
	// The waxed twin of a block of the family (NAME_None if it has none or is waxed already), and the unwaxed one of a waxed block.
	MCCORE_API FName GetWaxed(FName Block);
	MCCORE_API FName GetUnwaxed(FName Block);

	// Chance per random tick that a copper block starts to weather, before the neighbour rule.
	constexpr float WeatherChance = 0.05688889f;
	// The neighbour rule: nothing within 4 steps (taxicab) less weathered; with Higher blocks more and Same as weathered,
	// the chance is ((Higher + 1) / (Higher + Same + 1))^2, times 0.75 for the plain block.
	MCCORE_API float GetWeatherFactor(int32 Age, int32 Higher, int32 Same);
}
