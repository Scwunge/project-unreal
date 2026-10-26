// The item registry, from the data generator's report of every item's default components
// (reports/minecraft/components/item/<name>.json), the jar's item tags and its enchantments.
//
// An item's components (minecraft.wiki, "Data component format") say what it is: its stack size, durability,
// tool rules, armour slot, food, fuel and so on. They are kept as JSON values by name, and the common ones have
// typed readers here. A stack holds only what differs from its item's defaults (see FMcItemStack).
//
// Loaded once (from whichever thread asks first), then read only, so worker threads may read it freely.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "McCore.h"

using FMcComponents = TMap<FName, TSharedPtr<FJsonValue>>;

struct MCITEM_API FMcFoodData
{
	int32 Nutrition = 0;
	float Saturation = 0.0f;
	bool bCanAlwaysEat = false;
};

// One rule of a tool: the blocks it is about (a block, "#tag" or a list of them) and what the tool does to them.
struct MCITEM_API FMcToolRule
{
	TArray<FString> Blocks;
	// Negative when the rule doesn't say.
	float Speed = -1.0f;
	int8 CorrectForDrops = -1;
};

struct MCITEM_API FMcToolData
{
	TArray<FMcToolRule> Rules;
	float DefaultMiningSpeed = 1.0f;
	int32 DamagePerBlock = 1;
	bool bCanDestroyBlocksInCreative = true;
};

// What an item gives when worn: the slot ("head", "chest", "legs", "feet", "body", "offhand", "mainhand") and
// the armour attributes it carries for that slot.
struct MCITEM_API FMcArmorData
{
	FName Slot;
	double Armor = 0.0;
	double Toughness = 0.0;
	double KnockbackResistance = 0.0;
	FName AssetId;
};

struct MCITEM_API FMcEnchantmentDef
{
	FName Name;
	int32 MaxLevel = 1;
	int32 Weight = 1;
	int32 AnvilCost = 1;
	// The cost to enchant at a level is Base + (Level - 1) * PerLevel.
	int32 MinCostBase = 1;
	int32 MinCostPerLevel = 0;
	int32 MaxCostBase = 1;
	int32 MaxCostPerLevel = 0;
	// The items it can be put on (a "#tag", an item, or a list), and those the enchanting table offers it for.
	TArray<FString> Supported;
	TArray<FString> Primary;
	// The tag of enchantments it can't be combined with; none if empty.
	FString ExclusiveSet;
	TArray<FName> Slots;

	int32 MinCost(int32 Level) const { return MinCostBase + (Level - 1) * MinCostPerLevel; }
	int32 MaxCost(int32 Level) const { return MaxCostBase + (Level - 1) * MaxCostPerLevel; }
};

struct MCITEM_API FMcItemDef
{
	FName Name;
	// The id the protocol uses.
	int32 Id = INDEX_NONE;
	FMcComponents Components;
};

class MCITEM_API FMcItemRegistry
{
public:
	static FMcItemRegistry& Get();

	// Reads every item, tag and enchantment from cache/. Does nothing if already loaded; safe from any thread.
	bool EnsureLoaded();
	bool IsLoaded() const { return bLoaded; }

	int32 NumItems() const { return Items.Num(); }
	const FMcItemDef* Find(FName Item) const { const int32* Index = Ids.Find(Item); return Index ? &Items[*Index] : nullptr; }
	// By protocol id.
	const FMcItemDef* FindById(int32 Id) const { return Items.IsValidIndex(Id) ? &Items[Id] : nullptr; }
	bool Exists(FName Item) const { return Ids.Contains(Item); }
	const TArray<FMcItemDef>& GetAll() const { return Items; }

	// ---- Tags ----
	// The items in a tag ("minecraft:logs", with or without '#'), nested tags included; null if there is no such tag.
	const TSet<FName>* GetTag(FName Tag) const;
	bool HasTag(FName Item, FName Tag) const;
	// Whether the item matches a reference as recipes and rules write it: "minecraft:stick", "#minecraft:logs".
	bool Matches(FName Item, const FString& Reference) const;
	// Expands a reference ("#tag" or an item) to the items it names.
	void Expand(const FString& Reference, TArray<FName>& Out) const;

	// ---- Components ----
	// The item's default value of a component, or null.
	TSharedPtr<FJsonValue> GetDefault(FName Item, FName Component) const;
	int32 GetMaxStackSize(FName Item) const;
	// 0 for an item that doesn't wear out.
	int32 GetMaxDamage(FName Item) const;
	bool GetFood(FName Item, FMcFoodData& Out) const;
	bool GetTool(FName Item, FMcToolData& Out) const;
	bool GetArmor(FName Item, FMcArmorData& Out) const;
	// The enchantability (0 if the item can't be enchanted at a table).
	int32 GetEnchantability(FName Item) const;
	// Ticks it burns in a furnace (or twice as fast in a blast furnace or smoker: Fast), 0 if it isn't fuel.
	int32 GetBurnTime(FName Item, bool bFast = false) const;
	// How fast a fuel cooks relative to the furnace's own speed.
	float GetFuelSpeed(FName Item, bool bFast = false) const;
	// The item left behind when it is used up (a bucket from a bucket of milk); None if there is none.
	FName GetUseRemainder(FName Item) const;

	// The speed a tool mines a block at (the tool's default when no rule names it), and whether it is the right
	// tool for the block to drop what it should. Block tags need to be loaded.
	float GetDestroySpeed(FName Item, FMcStateId State) const;
	bool IsCorrectToolForDrops(FName Item, FMcStateId State) const;

	// ---- Enchantments ----
	const TArray<FMcEnchantmentDef>& GetEnchantments() const { return Enchantments; }
	const FMcEnchantmentDef* FindEnchantment(FName Name) const;
	// Whether the enchantment goes on the item (anvil rules; books take any).
	bool CanEnchant(FName Item, FName Enchantment) const;
	// The enchantments the item can take.
	void GetEnchantmentsFor(FName Item, TArray<FName>& Out) const;
	// Whether two enchantments can be on one item.
	bool AreCompatible(FName A, FName B) const;
	// The members of an enchantment tag (e.g. "minecraft:exclusive_set/damage" or "minecraft:in_enchanting_table").
	const TSet<FName>* GetEnchantmentTag(FName Tag) const;

	// ---- Providers ----
	// A "context int provider" from data/context_int_provider (like cooking/time_coal), by name.
	int32 EvaluateIntProvider(const TSharedPtr<FJsonValue>& Value, bool bFast) const;
	float EvaluateFloatProvider(const TSharedPtr<FJsonValue>& Value, bool bFast) const;

private:
	void LoadTags(const FString& Folder, TMap<FName, TSet<FName>>& Out);
	TSharedPtr<FJsonValue> ReadProvider(const FString& Folder, const FString& Name) const;
	double EvaluateProvider(const TSharedPtr<FJsonValue>& Value, bool bFast, int32 Depth) const;

	TArray<FMcItemDef> Items;
	TMap<FName, int32> Ids;
	TMap<FName, TSet<FName>> ItemTags;
	TMap<FName, TSet<FName>> EnchantmentTags;
	TArray<FMcEnchantmentDef> Enchantments;
	TMap<FName, int32> EnchantmentIds;
	FCriticalSection LoadLock;
	FThreadSafeBool bLoaded;
};

// Reads "minecraft:foo" or "#minecraft:foo" into a name without the '#'; whether it had one.
MCITEM_API bool McSplitReference(const FString& Reference, FName& OutName);
