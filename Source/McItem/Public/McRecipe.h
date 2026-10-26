// Recipes (minecraft.wiki, "Recipe"), loaded from the jar's data/minecraft/recipe: crafting (shaped, shapeless,
// transmute, decorated pot and the special ones), the four cooking kinds, stonecutting, smithing and brewing.
//
// An ingredient is a set of items (an item, a "#tag" or a list of them, expanded when loaded). Lookups take the
// stacks of a crafting grid, a furnace slot, the stonecutter or the smithing table and give the recipe that fits
// and what it makes. Loaded once, then read only.

#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "McItemStack.h"

enum class EMcRecipeType : uint8
{
	CraftingShaped,
	CraftingShapeless,
	CraftingTransmute,
	CraftingDecoratedPot,
	// One of the special crafting recipes, named by Special.
	CraftingSpecial,
	Smelting,
	Blasting,
	Smoking,
	CampfireCooking,
	Stonecutting,
	SmithingTransform,
	SmithingTrim,
	Brewing,
};

struct MCITEM_API FMcIngredient
{
	// The items that satisfy it; empty for no ingredient (a gap in a shaped recipe).
	TArray<FName> Items;

	bool IsEmpty() const { return Items.IsEmpty(); }
	bool Test(const FMcItemStack& Stack) const { return !Stack.IsEmpty() && Items.Contains(Stack.Item); }
};

struct MCITEM_API FMcRecipe
{
	FName Id;
	EMcRecipeType Type = EMcRecipeType::CraftingShapeless;
	// The name of a special crafting recipe: "crafting_special_bannerduplicate", "crafting_dye" and so on.
	FName Special;
	FString Group;
	FString Category;

	// Crafting: the shaped recipe's grid, row by row (Width * Height entries), or the shapeless recipe's list.
	int32 Width = 0;
	int32 Height = 0;
	TArray<FMcIngredient> Ingredients;

	// Cooking and stonecutting: the Ingredients' only entry is the input. Transmute: Input and Material. Smithing:
	// Template, Base and Addition. Brewing: Input is the bottle and Material the reagent.
	FMcIngredient Template;
	FMcIngredient Base;
	FMcIngredient Addition;
	FMcIngredient Input;
	FMcIngredient Material;
	// Special recipes keep their own ingredient slots by name (the jar's keys: "banner", "dye", "target").
	TMap<FName, FMcIngredient> Named;
	// Brewing: the potion the input bottle must hold (None for any), and the one the output gets.
	FName InputPotion;
	FName OutputPotion;
	// Smithing trim: the pattern, like "minecraft:coast".
	FName TrimPattern;

	// What it makes; for cooking, the stack made per item cooked.
	FMcItemStack Result;
	float Experience = 0.0f;
	// Ticks for one item in a furnace (a blast furnace and a smoker take half).
	int32 CookingTime = 200;

	bool IsCrafting() const { return Type <= EMcRecipeType::CraftingSpecial; }
	bool IsCooking() const { return Type >= EMcRecipeType::Smelting && Type <= EMcRecipeType::CampfireCooking; }
};

// What a lookup in a crafting grid found.
struct MCITEM_API FMcCraftingMatch
{
	const FMcRecipe* Recipe = nullptr;
	FMcItemStack Result;
};

class MCITEM_API FMcRecipeBook
{
public:
	static FMcRecipeBook& Get();

	// Reads every recipe. Needs the item registry. Does nothing if already loaded; safe from any thread.
	bool EnsureLoaded();
	bool IsLoaded() const { return bLoaded; }

	const TArray<FMcRecipe>& GetAll() const { return Recipes; }
	const FMcRecipe* Find(FName Id) const;
	int32 Num() const { return Recipes.Num(); }
	// How many loaded recipes were of a type the book doesn't understand (and so were dropped).
	int32 NumSkipped() const { return Skipped; }

	// ---- Crafting ----
	// The recipe a crafting grid of Width x Height stacks (row by row; 2x2 for the inventory, 3x3 for a table)
	// makes, and what it makes. The first match in id order wins. False if none fits.
	bool FindCraftingMatch(const TArray<FMcItemStack>& Grid, int32 Width, int32 Height, FMcCraftingMatch& Out) const;
	// The items left in the grid after crafting (Recipe may be null): a bucket for the milk used, a bowl for the
	// stew, the banner that was copied. One per grid slot, empty where nothing is left. (The ingredients themselves are used up by the caller, one from each.)
	static void GetRemainders(const FMcRecipe* Recipe, const TArray<FMcItemStack>& Grid, TArray<FMcItemStack>& Out);

	// ---- Cooking ----
	// The recipe that cooks the stack in a furnace of the kind (Smelting, Blasting, Smoking or CampfireCooking).
	const FMcRecipe* FindCooking(EMcRecipeType Kind, const FMcItemStack& Input) const;

	// ---- Stonecutting ----
	void FindStonecutting(const FMcItemStack& Input, TArray<const FMcRecipe*>& Out) const;

	// ---- Smithing ----
	bool FindSmithing(const FMcItemStack& Template, const FMcItemStack& Base, const FMcItemStack& Addition, FMcCraftingMatch& Out) const;

	// ---- Brewing ----
	// What a brewing stand makes from a bottle and a reagent.
	bool FindBrewing(const FMcItemStack& Bottle, const FMcItemStack& Reagent, FMcItemStack& Out) const;
	// Whether the item is a brewing reagent at all (so the stand takes it in its top slot).
	bool IsBrewingReagent(const FMcItemStack& Stack) const;

private:
	bool LoadOne(const FString& Id, const class FJsonObject& Json);
	bool MatchShaped(const FMcRecipe& Recipe, const TArray<FMcItemStack>& Grid, int32 Width, int32 Height) const;
	bool MatchShapeless(const FMcRecipe& Recipe, const TArray<const FMcItemStack*>& Items) const;
	bool MatchSpecial(const FMcRecipe& Recipe, const TArray<FMcItemStack>& Grid, int32 Width, int32 Height, FMcItemStack& Out) const;

	TArray<FMcRecipe> Recipes;
	TMap<FName, int32> Ids;
	int32 Skipped = 0;
	FCriticalSection LoadLock;
	FThreadSafeBool bLoaded;
};
