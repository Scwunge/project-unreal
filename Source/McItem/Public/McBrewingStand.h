// The brewing stand (minecraft.wiki, "Brewing Stand"): three bottle slots (0 to 2), the ingredient (3) and the
// blaze powder that fuels it (4).
//
// - A blaze powder gives 20 brews; one is taken from the fuel slot when the stand runs out.
// - A brew takes 400 ticks and starts when the ingredient makes something of at least one bottle (the recipe
//   book's brewing recipes). It stops if the ingredient is taken or changed.
// - When it finishes, every bottle it fits is brewed and one ingredient is used.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"
#include "McFurnace.h"

class MCITEM_API FMcBrewingStand : public FMcMachine
{
public:
	static constexpr int32 FirstBottle = 0;
	static constexpr int32 IngredientSlot = 3;
	static constexpr int32 FuelSlot = 4;
	static constexpr int32 BrewTime = 400;
	static constexpr int32 FuelPerPowder = 20;

	FMcBrewingStand();

	virtual const TSharedPtr<FMcContainer>& GetItems() const override { return Items; }
	virtual bool Tick() override;
	virtual bool Serialize(FArchive& Ar) override;

	// Ticks left of the brew under way (0 if none), and brews left of the fuel.
	int32 GetBrewTimeLeft() const { return BrewTimeLeft; }
	int32 GetFuel() const { return Fuel; }
	bool IsBrewing() const { return BrewTimeLeft > 0; }

	static bool IsBottle(const FMcItemStack& Stack);
	static bool IsFuel(const FMcItemStack& Stack);
	// Whether the ingredient would brew any of the bottles.
	bool CanBrew() const;

private:
	void Brew();

	TSharedPtr<FMcContainer> Items;
	int32 BrewTimeLeft = 0;
	int32 Fuel = 0;
	// The ingredient the brew under way was started with.
	FName Brewing;
};

class MCITEM_API FMcBrewingMenu : public FMcMenu
{
public:
	FMcBrewingMenu(const TSharedPtr<FMcBrewingStand>& InStand, const TSharedPtr<FMcContainer>& PlayerInventory);

protected:
	virtual bool MayPlace(int32 Slot, const FMcItemStack& Stack) const override;
	virtual int32 GetSlotLimit(int32 Slot) const override { return Slot < FMcBrewingStand::IngredientSlot ? 1 : 64; }
	virtual FMcItemStack QuickMoveStack(int32 Slot) override;

private:
	TSharedPtr<FMcBrewingStand> Stand;
};
