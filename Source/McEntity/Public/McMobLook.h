// The looks a kind of mob comes in (minecraft.wiki: "Horse", "Wolf", "Rabbit", "Fox", "Llama"). A mob's look is a number,
// an index into the list its model draws from (McRender's McEntityModel keeps the lists in this order), chosen when it
// is born and saved with it.

#pragma once

#include "CoreMinimal.h"

namespace McMobLook
{
	// How many looks a kind has; 0 for one that has only one.
	MCENTITY_API int32 GetCount(FName Kind);

	// The look of a mob of the kind born in the biome ("minecraft:taiga"; none when unknown): its biome's where the game
	// has one (a wolf's breed, a white rabbit in the snow, a gold one in a desert, a fox in snow), else random.
	MCENTITY_API int32 Pick(FName Kind, FName Biome, FRandomStream& Random);
}
