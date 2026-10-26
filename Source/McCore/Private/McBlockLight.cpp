#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "HAL/PlatformTime.h"

namespace
{
	// The properties of one state, by name.
	struct FStateView
	{
		const FMcBlockRegistry& Registry;
		FMcStateId Id;

		bool Is(const TCHAR* Property, const TCHAR* Value) const
		{
			return Registry.GetPropertyValue(Id, FName(Property)) == FName(Value);
		}
		int32 Int(const TCHAR* Property) const
		{
			return FCString::Atoi(*Registry.GetPropertyValue(Id, FName(Property)).ToString());
		}
	};

	bool EndsWith(const FString& Name, const TCHAR* Suffix)
	{
		return Name.EndsWith(Suffix);
	}

	// Light level 15 down to 1, from the wiki's list of emitters. Blocks that glow only in some states are
	// handled below the table.
	const TMap<FString, uint8>& FixedEmitters()
	{
		static const TMap<FString, uint8> Table = {
			{ TEXT("beacon"), 15 }, { TEXT("conduit"), 15 }, { TEXT("end_gateway"), 15 }, { TEXT("end_portal"), 15 }, { TEXT("fire"), 15 },
			{ TEXT("glowstone"), 15 }, { TEXT("jack_o_lantern"), 15 }, { TEXT("lantern"), 15 }, { TEXT("lava"), 15 }, { TEXT("lava_cauldron"), 15 },
			{ TEXT("sea_lantern"), 15 }, { TEXT("shroomlight"), 15 }, { TEXT("ochre_froglight"), 15 }, { TEXT("verdant_froglight"), 15 },
			{ TEXT("pearlescent_froglight"), 15 },
			{ TEXT("copper_torch"), 14 }, { TEXT("copper_wall_torch"), 14 }, { TEXT("end_rod"), 14 }, { TEXT("torch"), 14 }, { TEXT("wall_torch"), 14 },
			{ TEXT("nether_portal"), 11 },
			{ TEXT("crying_obsidian"), 10 }, { TEXT("soul_fire"), 10 }, { TEXT("soul_lantern"), 10 }, { TEXT("soul_torch"), 10 }, { TEXT("soul_wall_torch"), 10 },
			{ TEXT("enchanting_table"), 7 }, { TEXT("ender_chest"), 7 }, { TEXT("glow_lichen"), 7 },
			{ TEXT("sculk_catalyst"), 6 }, { TEXT("amethyst_cluster"), 5 }, { TEXT("large_amethyst_bud"), 4 }, { TEXT("magma_block"), 3 },
			{ TEXT("medium_amethyst_bud"), 2 }, { TEXT("firefly_bush"), 2 },
			{ TEXT("brewing_stand"), 1 }, { TEXT("brown_mushroom"), 1 }, { TEXT("calibrated_sculk_sensor"), 1 }, { TEXT("dragon_egg"), 1 },
			{ TEXT("end_portal_frame"), 1 }, { TEXT("sculk_sensor"), 1 }, { TEXT("small_amethyst_bud"), 1 },
		};
		return Table;
	}

	uint8 ComputeEmission(const FString& Name, const FStateView& State)
	{
		if (const uint8* Fixed = FixedEmitters().Find(Name))
		{
			return *Fixed;
		}
		// Copper lanterns glow at 15 whatever their age or wax.
		if (EndsWith(Name, TEXT("_copper_lantern")) || Name == TEXT("copper_lantern"))
		{
			return 15;
		}
		if (Name == TEXT("light"))
		{
			return static_cast<uint8>(FMath::Clamp(State.Int(TEXT("level")), 0, 15));
		}
		if (Name == TEXT("furnace") || Name == TEXT("blast_furnace") || Name == TEXT("smoker"))
		{
			return State.Is(TEXT("lit"), TEXT("true")) ? 13 : 0;
		}
		if (Name == TEXT("campfire"))
		{
			return State.Is(TEXT("lit"), TEXT("true")) ? 15 : 0;
		}
		if (Name == TEXT("soul_campfire"))
		{
			return State.Is(TEXT("lit"), TEXT("true")) ? 10 : 0;
		}
		if (Name == TEXT("redstone_lamp"))
		{
			return State.Is(TEXT("lit"), TEXT("true")) ? 15 : 0;
		}
		if (Name == TEXT("redstone_ore") || Name == TEXT("deepslate_redstone_ore"))
		{
			return State.Is(TEXT("lit"), TEXT("true")) ? 9 : 0;
		}
		if (Name == TEXT("redstone_torch") || Name == TEXT("redstone_wall_torch"))
		{
			return State.Is(TEXT("lit"), TEXT("true")) ? 7 : 0;
		}
		if (EndsWith(Name, TEXT("candle_cake")))
		{
			return State.Is(TEXT("lit"), TEXT("true")) ? 3 : 0;
		}
		if (Name == TEXT("candle") || EndsWith(Name, TEXT("_candle")))
		{
			return State.Is(TEXT("lit"), TEXT("true")) ? static_cast<uint8>(3 * State.Int(TEXT("candles"))) : 0;
		}
		if (EndsWith(Name, TEXT("copper_bulb")))
		{
			if (!State.Is(TEXT("lit"), TEXT("true")))
			{
				return 0;
			}
			// The bulb dims as it weathers; waxing doesn't change it.
			return Name.Contains(TEXT("oxidized")) ? 4 : Name.Contains(TEXT("weathered")) ? 8 : Name.Contains(TEXT("exposed")) ? 12 : 15;
		}
		if (Name == TEXT("sea_pickle"))
		{
			// Only glows in water: 6, 9, 12, 15 for one to four pickles.
			return State.Is(TEXT("waterlogged"), TEXT("true")) ? static_cast<uint8>(3 * State.Int(TEXT("pickles")) + 3) : 0;
		}
		if (Name == TEXT("respawn_anchor"))
		{
			const int32 Charges = State.Int(TEXT("charges"));
			return Charges == 0 ? 0 : static_cast<uint8>(Charges * 4 - 1);  // 3, 7, 11, 15
		}
		if (Name == TEXT("cave_vines") || Name == TEXT("cave_vines_plant"))
		{
			return State.Is(TEXT("berries"), TEXT("true")) ? 14 : 0;
		}
		if (Name == TEXT("vault"))
		{
			return State.Is(TEXT("vault_state"), TEXT("inactive")) ? 6 : 12;
		}
		if (Name == TEXT("trial_spawner"))
		{
			return State.Is(TEXT("trial_spawner_state"), TEXT("active")) ? 8 : State.Is(TEXT("trial_spawner_state"), TEXT("waiting_for_players")) ? 4 : 0;
		}
		return 0;
	}

	// Blocks that dim sky light by 1 (the wiki's "light-filtering" list), apart from leaves and shulker boxes.
	bool DimsByOne(const FString& Name)
	{
		static const TSet<FString> Names = {
			TEXT("water"), TEXT("bubble_column"), TEXT("ice"), TEXT("frosted_ice"), TEXT("cobweb"), TEXT("slime_block"), TEXT("honey_block"),
			TEXT("spawner"), TEXT("lava"), TEXT("beacon"), TEXT("end_gateway"), TEXT("chorus_plant"), TEXT("chorus_flower"),
			// Blocks that sit in water all the time.
			TEXT("kelp"), TEXT("kelp_plant"), TEXT("seagrass"), TEXT("tall_seagrass"),
		};
		return Names.Contains(Name) || EndsWith(Name, TEXT("_leaves")) || EndsWith(Name, TEXT("shulker_box"));
	}

	// Blocks that aren't opaque cubes, so light passes: plants, redstone parts, glass, fences, stairs ... Anything
	// not matched here is an opaque block (dampening 15). The model baker's idea of "full opaque cube" agrees
	// with this list for every state except the few exceptions the McAssets test names.
	bool IsTransparent(const FString& Name)
	{
		static const TSet<FString> Names = {
			TEXT("air"), TEXT("cave_air"), TEXT("void_air"), TEXT("barrier"), TEXT("light"), TEXT("structure_void"),
			// Plants.
			TEXT("azalea"), TEXT("flowering_azalea"), TEXT("allium"), TEXT("azure_bluet"), TEXT("blue_orchid"), TEXT("cornflower"),
			TEXT("dandelion"), TEXT("golden_dandelion"), TEXT("lily_of_the_valley"), TEXT("oxeye_daisy"), TEXT("poppy"), TEXT("torchflower"),
			TEXT("wither_rose"), TEXT("closed_eyeblossom"), TEXT("open_eyeblossom"), TEXT("lilac"), TEXT("peony"), TEXT("rose_bush"),
			TEXT("sunflower"), TEXT("pitcher_plant"), TEXT("pink_petals"), TEXT("wildflowers"), TEXT("leaf_litter"), TEXT("bush"),
			TEXT("firefly_bush"), TEXT("fern"), TEXT("large_fern"), TEXT("short_grass"), TEXT("tall_grass"), TEXT("short_dry_grass"),
			TEXT("tall_dry_grass"), TEXT("dead_bush"), TEXT("seagrass"), TEXT("tall_seagrass"), TEXT("kelp"), TEXT("kelp_plant"),
			TEXT("bamboo"), TEXT("sugar_cane"), TEXT("cactus"), TEXT("cactus_flower"), TEXT("vine"), TEXT("glow_lichen"), TEXT("sculk_vein"),
			TEXT("pale_hanging_moss"), TEXT("spore_blossom"), TEXT("small_dripleaf"), TEXT("big_dripleaf"), TEXT("big_dripleaf_stem"),
			TEXT("nether_sprouts"), TEXT("nether_wart"), TEXT("weeping_vines"), TEXT("weeping_vines_plant"), TEXT("twisting_vines"),
			TEXT("twisting_vines_plant"), TEXT("cave_vines"), TEXT("cave_vines_plant"), TEXT("sweet_berry_bush"), TEXT("wheat"),
			TEXT("carrots"), TEXT("potatoes"), TEXT("beetroots"), TEXT("cocoa"), TEXT("mangrove_propagule"), TEXT("red_shrub"),
			TEXT("resin_clump"), TEXT("frogspawn"), TEXT("sniffer_egg"), TEXT("turtle_egg"), TEXT("dragon_egg"), TEXT("chorus_plant"),
			TEXT("chorus_flower"), TEXT("lily_pad"), TEXT("pointed_dripstone"), TEXT("sulfur_spike"),
			// Redstone and rails.
			TEXT("rail"), TEXT("powered_rail"), TEXT("detector_rail"), TEXT("activator_rail"), TEXT("lever"), TEXT("tripwire"),
			TEXT("tripwire_hook"), TEXT("redstone_wire"), TEXT("repeater"), TEXT("comparator"), TEXT("daylight_detector"),
			TEXT("sculk_sensor"), TEXT("calibrated_sculk_sensor"), TEXT("sculk_shrieker"), TEXT("lightning_rod"),
			// Fluids, ice and the like; most of these also dim sky light, see DimsByOne.
			TEXT("water"), TEXT("lava"), TEXT("bubble_column"), TEXT("ice"), TEXT("frosted_ice"), TEXT("slime_block"), TEXT("honey_block"),
			TEXT("cobweb"), TEXT("fire"), TEXT("soul_fire"), TEXT("nether_portal"), TEXT("end_portal"), TEXT("end_gateway"),
			TEXT("powder_snow"),
			// Glass and metalwork.
			TEXT("glass"), TEXT("glass_pane"), TEXT("iron_bars"), TEXT("anvil"), TEXT("chipped_anvil"), TEXT("damaged_anvil"), TEXT("bell"),
			TEXT("scaffolding"), TEXT("ladder"),
			// Containers, workstations and other partial blocks.
			TEXT("chest"), TEXT("trapped_chest"), TEXT("brewing_stand"), TEXT("cauldron"), TEXT("lava_cauldron"), TEXT("water_cauldron"),
			TEXT("powder_snow_cauldron"), TEXT("composter"), TEXT("hopper"), TEXT("grindstone"), TEXT("stonecutter"), TEXT("lectern"),
			TEXT("enchanting_table"), TEXT("end_portal_frame"), TEXT("end_rod"), TEXT("conduit"), TEXT("beacon"), TEXT("campfire"),
			TEXT("soul_campfire"), TEXT("cake"), TEXT("decorated_pot"), TEXT("dried_ghast"), TEXT("flower_pot"), TEXT("heavy_core"),
			TEXT("moving_piston"), TEXT("piston_head"), TEXT("spawner"), TEXT("trial_spawner"), TEXT("vault"), TEXT("farmland"),
			TEXT("dirt_path"), TEXT("amethyst_cluster"), TEXT("shulker_box"),
			// Names the suffix rules below would get wrong or miss.
			TEXT("torch"), TEXT("lantern"), TEXT("soul_lantern"), TEXT("copper_lantern"), TEXT("sea_pickle"), TEXT("candle"), TEXT("candle_cake"),
			TEXT("melon_stem"), TEXT("pumpkin_stem"), TEXT("attached_melon_stem"), TEXT("attached_pumpkin_stem"),
			TEXT("mangrove_roots"), TEXT("crimson_roots"), TEXT("warped_roots"), TEXT("hanging_roots"),
		};
		if (Names.Contains(Name))
		{
			return true;
		}

		static const TCHAR* const Suffixes[] = {
			TEXT("_button"), TEXT("_door"), TEXT("_trapdoor"), TEXT("_fence"), TEXT("_fence_gate"), TEXT("_pressure_plate"), TEXT("_sign"),
			TEXT("_shelf"), TEXT("_stairs"), TEXT("_wall"), TEXT("_slab"), TEXT("_carpet"), TEXT("_candle"), TEXT("_candle_cake"), TEXT("_bed"),
			TEXT("_banner"), TEXT("_head"), TEXT("_skull"), TEXT("_torch"), TEXT("_copper_lantern"), TEXT("_chain"), TEXT("_bars"), TEXT("_chest"),
			TEXT("_golem_statue"), TEXT("_lightning_rod"), TEXT("_grate"), TEXT("_bud"), TEXT("_leaves"), TEXT("_sapling"),
			TEXT("_crop"), TEXT("_fungus"), TEXT("_mushroom"), TEXT("_tulip"), TEXT("_stained_glass"),
			TEXT("_stained_glass_pane"), TEXT("shulker_box"),
		};
		for (const TCHAR* Suffix : Suffixes)
		{
			if (EndsWith(Name, Suffix))
			{
				return true;
			}
		}
		// Every coral form except the full blocks, and every potted plant.
		return (Name.Contains(TEXT("coral")) && !EndsWith(Name, TEXT("_coral_block"))) || Name.StartsWith(TEXT("potted_"));
	}

	bool IsShapeBased(const FString& Name)
	{
		static const TSet<FString> Names = {
			TEXT("piston"), TEXT("sticky_piston"), TEXT("piston_head"), TEXT("moving_piston"), TEXT("daylight_detector"),
			TEXT("enchanting_table"), TEXT("farmland"), TEXT("lectern"), TEXT("stonecutter"), TEXT("dirt_path"), TEXT("snow"),
			TEXT("end_portal_frame"),
		};
		return Names.Contains(Name) || EndsWith(Name, TEXT("_slab")) || EndsWith(Name, TEXT("_stairs")) || EndsWith(Name, TEXT("_shelf"));
	}

	uint8 ComputeDampening(const FString& Name, const FStateView& State)
	{
		// The few blocks whose answer depends on the state.
		if (EndsWith(Name, TEXT("_slab")))
		{
			return State.Is(TEXT("type"), TEXT("double")) ? 15 : (State.Is(TEXT("waterlogged"), TEXT("true")) ? 1 : 0);
		}
		if (Name == TEXT("snow"))
		{
			return State.Int(TEXT("layers")) >= 8 ? 15 : 0;
		}
		if (Name == TEXT("piston") || Name == TEXT("sticky_piston"))
		{
			return State.Is(TEXT("extended"), TEXT("true")) ? 0 : 15;
		}

		if (DimsByOne(Name))
		{
			return 1;
		}
		if (IsTransparent(Name))
		{
			// Water in the block's cell filters light like water.
			return State.Is(TEXT("waterlogged"), TEXT("true")) ? 1 : 0;
		}
		return 15;
	}
}

FMcBlockLight& FMcBlockLight::Get()
{
	static FMcBlockLight Instance;
	return Instance;
}

bool FMcBlockLight::EnsureLoaded()
{
	check(IsInGameThread());
	if (IsLoaded())
	{
		return true;
	}

	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!Registry.EnsureLoaded())
	{
		return false;
	}

	const double StartTime = FPlatformTime::Seconds();
	const int32 NumStates = Registry.NumStates();
	TArray<uint8> NewEmission;
	TArray<uint8> NewDampening;
	TBitArray<> NewSkylightDown(false, NumStates);
	TBitArray<> NewShapeBased(false, NumStates);
	NewEmission.SetNumZeroed(NumStates);
	NewDampening.SetNumZeroed(NumStates);

	for (int32 BlockIndex = 0; BlockIndex < Registry.NumBlocks(); ++BlockIndex)
	{
		const FMcBlock& Block = Registry.GetBlock(BlockIndex);
		FString Name = Block.Name.ToString();
		Name.RemoveFromStart(TEXT("minecraft:"));
		const bool bShape = IsShapeBased(Name);

		for (const FMcStateId Id : Block.States)
		{
			const FStateView State{ Registry, Id };
			NewEmission[Id] = ComputeEmission(Name, State);
			NewDampening[Id] = ComputeDampening(Name, State);
			NewShapeBased[Id] = bShape;
			NewSkylightDown[Id] = NewDampening[Id] == 0 && !bShape;
		}
	}

	Emission = MoveTemp(NewEmission);
	Dampening = MoveTemp(NewDampening);
	SkylightDown = MoveTemp(NewSkylightDown);
	ShapeBased = MoveTemp(NewShapeBased);
	UE_LOG(LogMcCore, Log, TEXT("Built the light tables for %d states in %.0f ms"), NumStates, (FPlatformTime::Seconds() - StartTime) * 1000.0);
	return true;
}
