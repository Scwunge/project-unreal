#include "McGameEvent.h"
#include "McSim.h"

namespace
{
	struct FEntry { const TCHAR* Name; int32 Frequency; };
	// minecraft.wiki, "Vibration": game event, frequency.
	const FEntry GEntries[] = {
		{TEXT("step"), 1}, {TEXT("swim"), 1}, {TEXT("flap"), 1},
		{TEXT("projectile_land"), 2}, {TEXT("hit_ground"), 2}, {TEXT("bounce"), 2}, {TEXT("splash"), 2},
		{TEXT("item_interact_finish"), 3}, {TEXT("projectile_shoot"), 3}, {TEXT("instrument_play"), 3},
		{TEXT("entity_action"), 4}, {TEXT("elytra_glide"), 4}, {TEXT("unequip"), 4},
		{TEXT("entity_dismount"), 5}, {TEXT("equip"), 5},
		{TEXT("entity_mount"), 6}, {TEXT("entity_interact"), 6}, {TEXT("shear"), 6},
		{TEXT("entity_damage"), 7},
		{TEXT("drink"), 8}, {TEXT("eat"), 8},
		{TEXT("container_close"), 9}, {TEXT("block_close"), 9}, {TEXT("block_deactivate"), 9}, {TEXT("block_detach"), 9},
		{TEXT("container_open"), 10}, {TEXT("block_open"), 10}, {TEXT("block_activate"), 10}, {TEXT("block_attach"), 10},
		{TEXT("prime_fuse"), 10}, {TEXT("note_block_play"), 10},
		{TEXT("block_change"), 11},
		{TEXT("block_destroy"), 12}, {TEXT("fluid_pickup"), 12},
		{TEXT("block_place"), 13}, {TEXT("fluid_place"), 13},
		{TEXT("entity_place"), 14}, {TEXT("lightning_strike"), 14}, {TEXT("teleport"), 14},
		{TEXT("entity_die"), 15}, {TEXT("explode"), 15},
	};

	FName Plain(FName Name)
	{
		FString Text = Name.ToString();
		Text.RemoveFromStart(TEXT("minecraft:"));
		return FName(*Text);
	}
}

namespace McGameEvents
{
	int32 FrequencyOf(FName Name)
	{
		static TMap<FName, int32> Map;
		if (Map.IsEmpty())
		{
			for (const FEntry& Entry : GEntries)
			{
				Map.Add(FName(Entry.Name), Entry.Frequency);
			}
			// resonate_1 to resonate_15 re-emit a frequency from a block of amethyst.
			for (int32 Frequency = 1; Frequency <= 15; ++Frequency)
			{
				Map.Add(FName(*FString::Printf(TEXT("resonate_%d"), Frequency)), Frequency);
			}
		}
		const int32* Found = Map.Find(Plain(Name));
		return Found ? *Found : 0;
	}

	const TArray<FName>& AllNames()
	{
		static TArray<FName> Names;
		if (Names.IsEmpty())
		{
			for (const FEntry& Entry : GEntries)
			{
				Names.Add(FName(Entry.Name));
			}
			for (int32 Frequency = 1; Frequency <= 15; ++Frequency)
			{
				Names.Add(FName(*FString::Printf(TEXT("resonate_%d"), Frequency)));
			}
		}
		return Names;
	}

	bool WardenCanHear(const FMcGameEvent& Event, const FIntVector& WardenPos)
	{
		const FMcGameEventSource& Source = Event.Source;
		if (Source.bWarden || Source.bArmorStand || Source.bDying || Source.bCreativeOrSpectator)
		{
			return false;
		}
		if (Source.bPlayer && Source.bSneaking && SneakingHides(Event))
		{
			return false;
		}
		const FIntVector Delta = Event.Pos - WardenPos;
		return static_cast<int64>(Delta.X) * Delta.X + static_cast<int64>(Delta.Y) * Delta.Y + static_cast<int64>(Delta.Z) * Delta.Z <= 15 * 15;
	}

	bool SneakingHides(const FMcGameEvent& Event)
	{
		return Event.Frequency >= 1 && Event.Frequency <= 3;
	}
}
