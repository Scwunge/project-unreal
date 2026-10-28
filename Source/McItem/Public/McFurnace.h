// A furnace, blast furnace or smoker as plain logic (minecraft.wiki, "Smelting"): an input, a fuel and an output
// slot, a burn time left on the fuel, and the progress of the item being cooked. Tick it once per game tick.
//
// Fuel and cooking speed come from the item data: a blast furnace and a smoker burn fuel and cook twice as fast.
// A recipe's cooking time is for a plain furnace; the progress made per tick is the fuel's speed multiplier.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"
#include "McRecipe.h"

enum class EMcFurnaceKind : uint8
{
	Furnace,
	BlastFurnace,
	Smoker,
};

// A block that works on its own, tick by tick, on the items it holds: a furnace or a brewing stand. Its block entity
// is what Serialize writes.
class MCITEM_API FMcMachine
{
public:
	virtual ~FMcMachine() = default;
	virtual const TSharedPtr<FMcContainer>& GetItems() const = 0;
	// One game tick. True if a slot changed.
	virtual bool Tick() = 0;
	// Reads or writes, by whether the archive is loading.
	virtual bool Serialize(FArchive& Ar) = 0;
};

class MCITEM_API FMcFurnace : public FMcMachine
{
public:
	static constexpr int32 InputSlot = 0;
	static constexpr int32 FuelSlot = 1;
	static constexpr int32 OutputSlot = 2;

	explicit FMcFurnace(EMcFurnaceKind InKind = EMcFurnaceKind::Furnace);

	EMcFurnaceKind GetKind() const { return Kind; }
	EMcRecipeType GetRecipeType() const;
	virtual const TSharedPtr<FMcContainer>& GetItems() const override { return Items; }

	bool IsLit() const { return LitTimeRemaining > 0; }
	int32 GetLitTimeRemaining() const { return LitTimeRemaining; }
	// The burn time of the fuel that is burning now, for a flame that shrinks.
	int32 GetLitTotalTime() const { return LitTotalTime; }
	float GetCookingProgress() const { return CookingProgress; }
	int32 GetCookingTotalTime() const { return CookingTotalTime; }

	// Whether the item burns in this kind of furnace.
	bool IsFuel(const FMcItemStack& Stack) const;
	// Whether the furnace has a recipe for it.
	bool CanSmelt(const FMcItemStack& Stack) const;

	// One game tick. True if a slot changed.
	virtual bool Tick() override;

	// The experience the output has earned since it was last emptied, whole points, with the leftover fraction
	// rounded up when Roll (a number in [0, 1)) is under it. Clears it.
	int32 TakeExperience(float Roll);

	// Reads or writes, by whether the archive is loading.
	virtual bool Serialize(FArchive& Ar) override;

private:
	bool CanBurn(const FMcRecipe* Recipe) const;
	void Burn(const FMcRecipe* Recipe);

	EMcFurnaceKind Kind;
	TSharedPtr<FMcContainer> Items;
	int32 LitTimeRemaining = 0;
	int32 LitTotalTime = 0;
	float LitSpeed = 1.0f;
	float CookingProgress = 0.0f;
	int32 CookingTotalTime = 0;
	TMap<FName, int32> RecipesUsed;
};

// The furnace screen's menu: input, fuel, output, then the player's slots.
class MCITEM_API FMcFurnaceMenu : public FMcMenu
{
public:
	FMcFurnaceMenu(const TSharedPtr<FMcFurnace>& InFurnace, const TSharedPtr<FMcContainer>& PlayerInventory);

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override;
	virtual bool CanTakeForPickupAll(int32 Slot) const override { return Slot != FMcFurnace::OutputSlot; }
	virtual bool CanDragTo(int32 Slot) const override { return Slot != FMcFurnace::OutputSlot; }
	virtual FMcItemStack QuickMoveStack(int32 Slot) override;

private:
	TSharedPtr<FMcFurnace> Furnace;
};
