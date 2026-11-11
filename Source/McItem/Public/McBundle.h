// Bundles (minecraft.wiki: "Bundle", Java Edition): a bundle carries a list of stacks in "minecraft:bundle_contents",
// newest first. Each item takes 1 / its max stack size of the bundle's room (a bundle itself 1/16 plus what it holds, a
// beehive or bee nest with bees a whole one), and the bundle holds one whole in all. Shulker boxes cannot go in.

#pragma once

#include "CoreMinimal.h"
#include "McItemStack.h"

namespace McBundle
{
	// A bundle of any colour (or the plain one).
	MCITEM_API bool IsBundle(FName Item);

	// What the bundle holds, newest first.
	MCITEM_API TArray<FMcItemStack> GetContents(const FMcItemStack& Bundle);
	MCITEM_API void SetContents(FMcItemStack& Bundle, const TArray<FMcItemStack>& Contents);

	// The room one item of the stack takes (a whole stack's worth is Count times this).
	MCITEM_API double GetItemWeight(const FMcItemStack& Stack);
	// How full the bundle is, 0 to 1.
	MCITEM_API double GetFullness(const FMcItemStack& Bundle);
	MCITEM_API bool CanBundle(const FMcItemStack& Stack);

	// Puts as much of Stack into the bundle as fits (it goes in front: the newest); returns how many went in and takes
	// them off Stack.
	MCITEM_API int32 Insert(FMcItemStack& Bundle, FMcItemStack& Stack);
	// Takes the newest stack out of the bundle; empty if it holds nothing.
	MCITEM_API FMcItemStack RemoveNewest(FMcItemStack& Bundle);
	// Takes the stack at an index of GetContents (the one picked with the wheel); an index out of range takes the newest.
	MCITEM_API FMcItemStack RemoveAt(FMcItemStack& Bundle, int32 Index);
}
