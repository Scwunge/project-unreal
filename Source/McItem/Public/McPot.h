// Decorated pots' sides (minecraft.wiki: "Decorated Pot", "Pottery Sherd"): back, left, right and front, each a pottery
// sherd or a brick. An item carries them as "minecraft:pot_decorations", {"back": {"id": "minecraft:brick"}, ...} (the
// cache's components report); a placed pot keeps them in its block entity's first four slots, in the same order.

#pragma once

#include "CoreMinimal.h"
#include "McItemStack.h"

namespace McPot
{
	constexpr int32 NumSides = 4;
	// The component's keys, in slot order.
	MCITEM_API const TCHAR* GetSideName(int32 Side);

	// The four sides of a pot item (bricks for those it doesn't say).
	MCITEM_API TArray<FName> GetSides(const FMcItemStack& Pot);
	// Writes the four sides onto a pot item; all bricks leaves the component off, as a plain pot has none.
	MCITEM_API void SetSides(FMcItemStack& Pot, const TArray<FName>& Sides);

	// The picture a side is drawn with, under textures/: "entity/decorated_pot/angler_pottery_pattern" for the angler
	// sherd, the plain "entity/decorated_pot/decorated_pot_side" for a brick (or anything else).
	MCITEM_API FString GetPattern(FName Side);
}
