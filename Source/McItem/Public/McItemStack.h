// An item stack (minecraft.wiki, "Item format"): an item, how many, and the components that differ from the
// item's defaults ("minecraft:damage", "minecraft:enchantments", "minecraft:custom_name" and so on).
//
// Components are JSON values by name, as the data packs write them. A JSON null value means the component is
// removed from the stack, though the item has it by default.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "McItemRegistry.h"

class FArchive;

struct MCITEM_API FMcItemStack
{
	// An empty slot has no item or a count of 0.
	FName Item;
	int32 Count = 0;
	// What differs from the item's defaults.
	FMcComponents Components;

	// Out of line so that other modules link to them rather than instantiating their own.
	FMcItemStack();
	~FMcItemStack();
	FMcItemStack(const FMcItemStack& Other);
	FMcItemStack(FMcItemStack&& Other);
	FMcItemStack& operator=(const FMcItemStack& Other);
	FMcItemStack& operator=(FMcItemStack&& Other);
	FMcItemStack(FName InItem, int32 InCount = 1) : Item(InItem), Count(InCount) {}

	bool IsEmpty() const { return Item.IsNone() || Count <= 0; }
	void Clear() { *this = FMcItemStack(); }

	// ---- Components ----
	// The stack's value of a component: its own if it has one, else the item's default; null if there is none.
	TSharedPtr<FJsonValue> Get(FName Component) const;
	bool Has(FName Component) const { return Get(Component).IsValid(); }
	void Set(FName Component, TSharedPtr<FJsonValue> Value) { Components.Add(Component, MoveTemp(Value)); }
	// Removes the component, even if it's a default of the item.
	void Remove(FName Component);
	// Puts the stack's own value back to the item's default (drops it from the patch).
	void Reset(FName Component) { Components.Remove(Component); }
	// Drops patch entries that say nothing: ones equal to the item's default, and removals of what it doesn't have.
	void Normalize();

	// ---- Typed readers ----
	int32 GetMaxStackSize() const;
	int32 GetMaxDamage() const;
	bool IsDamageable() const { return GetMaxDamage() > 0 && !Has(TEXT("minecraft:unbreakable")); }
	int32 GetDamage() const;
	void SetDamage(int32 Damage);
	bool IsDamaged() const { return IsDamageable() && GetDamage() > 0; }
	// Damages the stack by Amount; true if it broke (the caller removes it).
	bool HurtAndBreak(int32 Amount);
	// The enchantments, name to level.
	TMap<FName, int32> GetEnchantments() const;
	void SetEnchantments(const TMap<FName, int32>& Enchantments);
	int32 GetEnchantmentLevel(FName Enchantment) const;
	bool IsEnchanted() const { return GetEnchantments().Num() > 0; }
	// The plain text of the custom name, empty if none.
	FString GetCustomName() const;
	void SetCustomName(const FString& Name);

	// ---- Stacking ----
	// Whether the two can share a slot: the same item and components (counts aside).
	bool IsSameItemSameComponents(const FMcItemStack& Other) const;
	// How many of Other this stack takes in: all of it if there is room, none if it can't stack with it.
	int32 GetSpaceFor(const FMcItemStack& Other, int32 SlotLimit = MAX_int32) const;
	// Takes up to Amount off this stack and returns it as a stack of its own.
	FMcItemStack Split(int32 Amount);
	FMcItemStack Copy() const { return *this; }

	// Reads or writes, by whether the archive is loading. Names are saved as text, so a save doesn't depend on
	// the name table. False if a loading archive is malformed.
	bool Serialize(FArchive& Ar);

	bool operator==(const FMcItemStack& Other) const { return Count == Other.Count && IsSameItemSameComponents(Other); }
	bool operator!=(const FMcItemStack& Other) const { return !(*this == Other); }
};

namespace McComponents
{
	// Deep equality of two component values; null equals null only.
	MCITEM_API bool ValuesEqual(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B);
	MCITEM_API bool MapsEqual(const FMcComponents& A, const FMcComponents& B);
	MCITEM_API FString ToText(const TSharedPtr<FJsonValue>& Value);
	MCITEM_API TSharedPtr<FJsonValue> FromText(const FString& Text);
}
