// How hoppers move items (minecraft.wiki, "Hopper"): one item at a time, from a container above (or an item lying
// on top) and into the container the spout faces, then a wait of 8 game ticks.
//
// Each block lets a hopper reach only some of its slots, by the face the hopper is at:
// - Chests, barrels and hoppers: every slot, from any face.
// - Furnaces: from above into the input, from the side into the fuel (fuels only); from below, the output (and an
//   empty bucket left in the fuel slot) comes out.
// - Brewing stands: from above into the ingredient (reagents only), from the side into the bottles and the fuel
//   (blaze powder); from below, the bottles come out.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"

class FMcFurnace;
class FMcBrewingStand;

// What one face of a block lets a hopper do with its slots.
struct MCITEM_API FMcSlotAccess
{
	TSharedPtr<FMcContainer> Items;
	// A second container whose slots follow Items' (the other half of a double chest); slot numbers past Items' count into it.
	TSharedPtr<FMcContainer> More;
	// The slots an item may go into, in the order tried, and those it may come out of.
	TArray<int32> Insert;
	TArray<int32> Extract;
	// Whether the stack may go in that slot (anything, if unset), and how many a slot holds at most (64 if unset).
	TFunction<bool(int32, const FMcItemStack&)> CanInsert;
	TFunction<int32(int32)> SlotLimit;

	bool IsValid() const { return Items.IsValid(); }
	const FMcItemStack& GetStack(int32 Slot) const
	{
		return More.IsValid() && Slot >= Items->Num() ? More->Get(Slot - Items->Num()) : Items->Get(Slot);
	}
	void SetStack(int32 Slot, const FMcItemStack& Stack) const
	{
		if (More.IsValid() && Slot >= Items->Num())
		{
			More->Set(Slot - Items->Num(), Stack);
		}
		else
		{
			Items->Set(Slot, Stack);
		}
	}
};

namespace McHopper
{
	constexpr int32 NumSlots = 5;
	constexpr int32 Cooldown = 8;

	// The face of the other block the hopper is at.
	enum class EFace : uint8 { Top, Bottom, Side };

	MCITEM_API FMcSlotAccess ForContainer(const TSharedPtr<FMcContainer>& Items, const TSharedPtr<FMcContainer>& More = nullptr);
	MCITEM_API FMcSlotAccess ForFurnace(const TSharedPtr<FMcFurnace>& Furnace, EFace Face);
	MCITEM_API FMcSlotAccess ForBrewingStand(const TSharedPtr<FMcBrewingStand>& Stand, EFace Face);

	// Moves a single item from the first of From's extract slots that has one To will take; true if one moved.
	MCITEM_API bool MoveOne(const FMcSlotAccess& From, const FMcSlotAccess& To);
	// Puts as much of Stack as fits into To's insert slots, leaving the rest in Stack; true if any went in.
	MCITEM_API bool Insert(const FMcSlotAccess& To, FMcItemStack& Stack);
}
