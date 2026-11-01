#include "McPlayerSave.h"
#include "McPlayerData.h"
#include "McWorldSave.h"

namespace
{
	constexpr int32 ArmorFirstSlot = 100;
	constexpr int32 OffhandSlot = 150;
	// The ender chest's slots, after everything the game numbers itself (it keeps them in a list of their own).
	constexpr int32 EnderFirstSlot = 200;

	// The armour container runs head to feet; the game's numbers run feet to head.
	int32 ArmorIndexToSlot(int32 Index) { return ArmorFirstSlot + (UMcPlayerDataComponent::NumArmorSlots - 1 - Index); }

	float GetStat(const FMcPlayerData& Saved, const TCHAR* Name, float Default)
	{
		const double* Found = Saved.Stats.Find(Name);
		return Found ? static_cast<float>(*Found) : Default;
	}
}

void McPlayerSave::Capture(const UMcPlayerDataComponent& Data, FMcPlayerData& Out)
{
	const FMcPlayerStats& Stats = Data.Stats;
	Out.bHasSurvival = true;
	Out.bCreative = Data.bCreative;
	Out.SelectedSlot = Data.SelectedSlot;
	Out.Stats.Reset();
	Out.Stats.Add(TEXT("health"), Stats.Health);
	Out.Stats.Add(TEXT("food"), Stats.Food);
	Out.Stats.Add(TEXT("saturation"), Stats.Saturation);
	Out.Stats.Add(TEXT("exhaustion"), Stats.Exhaustion);
	Out.Stats.Add(TEXT("air"), Stats.Air);
	Out.Stats.Add(TEXT("absorption"), Stats.Absorption);
	Out.Stats.Add(TEXT("xp_level"), Stats.XpLevel);
	Out.Stats.Add(TEXT("xp_progress"), Stats.XpProgress);
	Out.Stats.Add(TEXT("fire_ticks"), Stats.OnFireTicks);
	Out.Stats.Add(TEXT("food_timer"), Stats.FoodTimer);
	if (Data.SpawnBed.IsSet())
	{
		Out.Stats.Add(TEXT("spawn_x"), Data.SpawnBed->X);
		Out.Stats.Add(TEXT("spawn_y"), Data.SpawnBed->Y);
		Out.Stats.Add(TEXT("spawn_z"), Data.SpawnBed->Z);
	}

	Out.Slots.Reset();
	for (int32 Slot = 0; Slot < UMcPlayerDataComponent::InventorySize; ++Slot)
	{
		if (!Data.Inventory->Get(Slot).IsEmpty())
		{
			Out.Slots.Add({ Slot, Data.Inventory->Get(Slot) });
		}
	}
	for (int32 Index = 0; Index < UMcPlayerDataComponent::NumArmorSlots; ++Index)
	{
		if (!Data.Armor->Get(Index).IsEmpty())
		{
			Out.Slots.Add({ ArmorIndexToSlot(Index), Data.Armor->Get(Index) });
		}
	}
	if (!Data.Offhand->Get(0).IsEmpty())
	{
		Out.Slots.Add({ OffhandSlot, Data.Offhand->Get(0) });
	}
	for (int32 Slot = 0; Slot < UMcPlayerDataComponent::EnderChestSize; ++Slot)
	{
		if (!Data.EnderChest->Get(Slot).IsEmpty())
		{
			Out.Slots.Add({ EnderFirstSlot + Slot, Data.EnderChest->Get(Slot) });
		}
	}
}

bool McPlayerSave::Apply(const FMcPlayerData& Saved, UMcPlayerDataComponent& Data)
{
	if (!Saved.bHasSurvival)
	{
		return false;
	}
	FMcPlayerStats Stats;
	Stats.Health = FMath::Clamp(GetStat(Saved, TEXT("health"), Stats.Health), 0.0f, Stats.MaxHealth);
	Stats.Food = FMath::Clamp(FMath::RoundToInt32(GetStat(Saved, TEXT("food"), static_cast<float>(Stats.Food))), 0, 20);
	Stats.Saturation = FMath::Clamp(GetStat(Saved, TEXT("saturation"), Stats.Saturation), 0.0f, static_cast<float>(Stats.Food));
	Stats.Exhaustion = FMath::Clamp(GetStat(Saved, TEXT("exhaustion"), 0.0f), 0.0f, 40.0f);
	Stats.Air = FMath::Clamp(FMath::RoundToInt32(GetStat(Saved, TEXT("air"), static_cast<float>(Stats.Air))), -20, Stats.MaxAir);
	Stats.Absorption = FMath::Max(0.0f, GetStat(Saved, TEXT("absorption"), 0.0f));
	Stats.XpLevel = FMath::Max(0, FMath::RoundToInt32(GetStat(Saved, TEXT("xp_level"), 0.0f)));
	Stats.XpProgress = FMath::Clamp(GetStat(Saved, TEXT("xp_progress"), 0.0f), 0.0f, 0.999f);
	Stats.OnFireTicks = FMath::Max(0, FMath::RoundToInt32(GetStat(Saved, TEXT("fire_ticks"), 0.0f)));
	Stats.FoodTimer = FMath::Max(0, FMath::RoundToInt32(GetStat(Saved, TEXT("food_timer"), 0.0f)));
	// A player saved dead (the file was written between dying and the respawn) comes back with full health.
	if (Stats.Health <= 0.0f)
	{
		Stats.Health = Stats.MaxHealth;
	}

	Data.Inventory->Clear();
	Data.Armor->Clear();
	Data.Offhand->Clear();
	Data.EnderChest->Clear();
	for (const FMcSavedSlot& Slot : Saved.Slots)
	{
		if (Slot.Slot >= 0 && Slot.Slot < UMcPlayerDataComponent::InventorySize)
		{
			Data.Inventory->Set(Slot.Slot, Slot.Stack);
		}
		else if (Slot.Slot >= ArmorFirstSlot && Slot.Slot < ArmorFirstSlot + UMcPlayerDataComponent::NumArmorSlots)
		{
			Data.Armor->Set(UMcPlayerDataComponent::NumArmorSlots - 1 - (Slot.Slot - ArmorFirstSlot), Slot.Stack);
		}
		else if (Slot.Slot == OffhandSlot)
		{
			Data.Offhand->Set(0, Slot.Stack);
		}
		else if (Slot.Slot >= EnderFirstSlot && Slot.Slot < EnderFirstSlot + UMcPlayerDataComponent::EnderChestSize)
		{
			Data.EnderChest->Set(Slot.Slot - EnderFirstSlot, Slot.Stack);
		}
	}
	Data.Stats = Stats;
	Data.SpawnBed.Reset();
	if (Saved.Stats.Contains(TEXT("spawn_x")) && Saved.Stats.Contains(TEXT("spawn_y")) && Saved.Stats.Contains(TEXT("spawn_z")))
	{
		Data.SpawnBed = FIntVector(FMath::RoundToInt32(*Saved.Stats.Find(TEXT("spawn_x"))), FMath::RoundToInt32(*Saved.Stats.Find(TEXT("spawn_y"))),
			FMath::RoundToInt32(*Saved.Stats.Find(TEXT("spawn_z"))));
	}
	// The mode stays what the server was started with (-McCreative); only what the player carried comes back.
	Data.SelectedSlot = FMath::Clamp(Saved.SelectedSlot, 0, 8);
	Data.UpdateArmor();
	return true;
}
