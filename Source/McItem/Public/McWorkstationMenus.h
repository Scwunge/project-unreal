// The stonecutter, the grindstone and the smithing table (minecraft.wiki, "Stonecutter", "Grindstone", "Smithing
// Table"). Each makes its result as the inputs change, and taking it uses up the inputs.
//
// - Stonecutter: slot 0 the input, slot 1 the result, then the player's slots. The input's stonecutting recipes
//   are offered as buttons; the one chosen (ClickButton) fills the result, and stays chosen while the same item
//   is in the input.
// - Smithing table: slots 0, 1 and 2 the template, base and addition, slot 3 the result, then the player's.
// - Grindstone: slots 0 and 1 the inputs, slot 2 the result, then the player's. One item loses its enchantments
//   but the curses; two of the same damageable item are joined, their durability added with 5% more, keeping
//   the curses of both. Taking the result gives experience for the enchantments taken off.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"
#include "McRecipe.h"

class MCITEM_API FMcStonecutterMenu : public FMcMenu
{
public:
	static constexpr int32 InputSlot = 0;
	static constexpr int32 ResultSlot = 1;

	explicit FMcStonecutterMenu(const TSharedPtr<FMcContainer>& PlayerInventory);

	// What the input can be cut into, in the order shown.
	const TArray<const FMcRecipe*>& GetRecipes() const { return Recipes; }
	// The recipe chosen, an index into GetRecipes; -1 for none.
	int32 GetSelected() const { return Selected; }

	virtual bool ClickButton(int32 Id) override;
	virtual void SerializeState(FArchive& Ar) override;

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override { return Slot != ResultSlot; }
	virtual bool CanTakeForPickupAll(int32 Slot) const override { return Slot != ResultSlot; }
	virtual bool CanDragTo(int32 Slot) const override { return Slot != ResultSlot; }
	virtual FMcItemStack QuickMoveStack(int32 Slot) override;
	virtual void SlotChanged(int32 Slot) override;
	virtual void OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped) override;
	virtual void OnClosed(TArray<FMcItemStack>& Dropped) override;

private:
	// Looks again at the input: a different item clears the choice.
	void Refresh();
	void SetupResult();

	TSharedPtr<FMcContainer> Table;
	TSharedPtr<FMcContainer> Player;
	TArray<const FMcRecipe*> Recipes;
	int32 Selected = -1;
	FName RecipesFor;
};

class MCITEM_API FMcGrindstoneMenu : public FMcMenu
{
public:
	static constexpr int32 TopSlot = 0;
	static constexpr int32 BottomSlot = 1;
	static constexpr int32 ResultSlot = 2;

	explicit FMcGrindstoneMenu(const TSharedPtr<FMcContainer>& PlayerInventory);

	// What the grindstone makes of the two inputs (either may be empty); empty if nothing.
	static FMcItemStack GetResult(const FMcItemStack& Top, const FMcItemStack& Bottom);
	// The experience the enchantments taken off are worth, before the random half (minecraft.wiki: the sum of the
	// minimum costs of the enchantments' levels).
	static int32 GetExperienceValue(const FMcItemStack& Top, const FMcItemStack& Bottom);
	// The random draw TakeExperience uses; tests set it.
	TFunction<float()> Random;

	virtual int32 TakeExperience() override;
	virtual void SerializeState(FArchive& Ar) override { Refresh(); }

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override;
	virtual int32 GetSlotLimit(int32 Slot) const override { return 1; }
	virtual bool CanTakeForPickupAll(int32 Slot) const override { return Slot != ResultSlot; }
	virtual bool CanDragTo(int32 Slot) const override { return Slot != ResultSlot; }
	virtual FMcItemStack QuickMoveStack(int32 Slot) override;
	virtual void SlotChanged(int32 Slot) override;
	virtual void OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped) override;
	virtual void OnClosed(TArray<FMcItemStack>& Dropped) override;

private:
	void Refresh();

	TSharedPtr<FMcContainer> Table;
	TSharedPtr<FMcContainer> Player;
	int32 Earned = 0;
};

class MCITEM_API FMcSmithingMenu : public FMcMenu
{
public:
	static constexpr int32 TemplateSlot = 0;
	static constexpr int32 BaseSlot = 1;
	static constexpr int32 AdditionSlot = 2;
	static constexpr int32 ResultSlot = 3;

	explicit FMcSmithingMenu(const TSharedPtr<FMcContainer>& PlayerInventory);

	const FMcRecipe* GetRecipe() const { return Match.Recipe; }
	// Which input slot an item belongs in (for shift-clicks): the first whose part of some smithing recipe takes
	// it; -1 for none.
	static int32 FindInputSlot(const FMcItemStack& Stack);

	virtual void SerializeState(FArchive& Ar) override { Refresh(); }

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override { return Slot != ResultSlot; }
	virtual bool CanTakeForPickupAll(int32 Slot) const override { return Slot != ResultSlot; }
	virtual bool CanDragTo(int32 Slot) const override { return Slot != ResultSlot; }
	virtual FMcItemStack QuickMoveStack(int32 Slot) override;
	virtual void SlotChanged(int32 Slot) override;
	virtual void OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped) override;
	virtual void OnClosed(TArray<FMcItemStack>& Dropped) override;

private:
	void Refresh();

	TSharedPtr<FMcContainer> Table;
	TSharedPtr<FMcContainer> Player;
	FMcCraftingMatch Match;
};

// The loom (minecraft.wiki, "Loom"): slot 0 the banner, 1 the dye, 2 the banner pattern item, 3 the result, then the
// player's slots. With a banner and a dye in, the patterns that can go on are offered as buttons: those that need no
// item, or, with a pattern item in, the one it stands for. A banner takes at most six patterns. The one chosen
// (ClickButton) fills the result: the banner with the pattern added in the dye's colour. Taking it uses one banner
// and one dye; the pattern item stays.
class MCITEM_API FMcLoomMenu : public FMcMenu
{
public:
	static constexpr int32 BannerSlot = 0;
	static constexpr int32 DyeSlot = 1;
	static constexpr int32 PatternSlot = 2;
	static constexpr int32 ResultSlot = 3;
	static constexpr int32 MaxPatterns = 6;

	explicit FMcLoomMenu(const TSharedPtr<FMcContainer>& PlayerInventory);

	// The patterns offered, in the order shown ("minecraft:border"), and the one chosen (an index; -1 for none).
	const TArray<FName>& GetPatterns() const { return Patterns; }
	int32 GetSelected() const { return Selected; }

	// The patterns an item stands for: none for anything but a banner pattern item.
	static TArray<FName> GetPatternsOfItem(FName Item);
	// The patterns that need no item.
	static const TArray<FName>& GetFreePatterns();

	virtual bool ClickButton(int32 Id) override;
	virtual void SerializeState(FArchive& Ar) override;

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override;
	virtual bool CanTakeForPickupAll(int32 Slot) const override { return Slot != ResultSlot; }
	virtual bool CanDragTo(int32 Slot) const override { return Slot != ResultSlot; }
	virtual FMcItemStack QuickMoveStack(int32 Slot) override;
	virtual void SlotChanged(int32 Slot) override;
	virtual void OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped) override;
	virtual void OnClosed(TArray<FMcItemStack>& Dropped) override;

private:
	// Offers the patterns the inputs allow, keeping the choice if it is still there, and sets the result.
	void Refresh();

	TSharedPtr<FMcContainer> Table;
	TSharedPtr<FMcContainer> Player;
	TArray<FName> Patterns;
	int32 Selected = -1;
};

// The cartography table (minecraft.wiki, "Cartography Table"): slot 0 the filled map, 1 the material, 2 the result,
// then the player's slots. Paper zooms an unlocked map out one scale (at most 4); an empty map makes a copy (two maps
// come out, sharing the map's id); a glass pane makes a locked copy, a new map with the picture as it is (a locked
// map cannot be locked again). Taking the result uses one of each input.
class MCITEM_API FMcCartographyMenu : public FMcMenu
{
public:
	static constexpr int32 MapSlot = 0;
	static constexpr int32 MaterialSlot = 1;
	static constexpr int32 ResultSlot = 2;

	enum class EOperation : uint8 { None, Zoom, Copy, Lock };

	explicit FMcCartographyMenu(const TSharedPtr<FMcContainer>& PlayerInventory);

	// What the two inputs would do.
	static EOperation GetOperation(const FMcItemStack& Map, const FMcItemStack& Material);
	// The result shown: a locked copy still points at the source map until it is taken.
	static FMcItemStack GetPreview(const FMcItemStack& Map, const FMcItemStack& Material);
	EOperation GetCurrentOperation() const { return GetOperation(Table->Get(MapSlot), Table->Get(MaterialSlot)); }

	virtual void SerializeState(FArchive& Ar) override { Refresh(); }

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override;
	virtual bool CanTakeForPickupAll(int32 Slot) const override { return Slot != ResultSlot; }
	virtual bool CanDragTo(int32 Slot) const override { return Slot != ResultSlot; }
	virtual FMcItemStack QuickMoveStack(int32 Slot) override;
	virtual void SlotChanged(int32 Slot) override;
	virtual void OnTake(int32 Slot, FMcItemStack& Taken, TArray<FMcItemStack>& Dropped) override;
	virtual void OnClosed(TArray<FMcItemStack>& Dropped) override;

private:
	void Refresh();
	// The result as it is taken (a locked copy gets its own map now), and the inputs used up.
	FMcItemStack MakeResult() const;
	void Consume();

	TSharedPtr<FMcContainer> Table;
	TSharedPtr<FMcContainer> Player;
};
