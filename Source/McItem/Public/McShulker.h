// Shulker boxes (minecraft.wiki: "Shulker Box"): a box keeps its 27 slots when it is broken, as the item's
// "minecraft:container" component, and puts them back when placed. The component is a list of
// {"slot": 3, "item": {"id": "minecraft:stone", "count": 5, "components": {...}}}.

#pragma once

#include "CoreMinimal.h"
#include "McItemStack.h"

namespace McShulker
{
	constexpr int32 NumSlots = 27;

	// A shulker box of any colour (or the plain one).
	MCITEM_API bool IsBox(FName Item);

	// The component value for these slots, or null if they are all empty.
	MCITEM_API TSharedPtr<FJsonValue> Pack(const TArray<FMcItemStack>& Slots);
	// The 27 slots a box item carries (empty ones for what it doesn't).
	MCITEM_API TArray<FMcItemStack> Unpack(const FMcItemStack& Box);
}
