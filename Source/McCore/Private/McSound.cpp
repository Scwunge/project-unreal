#include "McSound.h"

FMcSoundBus& FMcSoundBus::Get()
{
	static FMcSoundBus Bus;
	return Bus;
}

namespace
{
	enum class EMatch : uint8 { Exact, Prefix, Suffix, Contains };

	struct FRule
	{
		EMatch Match;
		const TCHAR* Text;
		const TCHAR* Group;
	};

	// In order: the first rule that fits decides. The game gives every block a sound group in code; these follow it for
	// the blocks that matter (and the families: a slab or a stair has the sound of its material).
	const FRule Rules[] = {
		// The ground.
		{ EMatch::Exact, TEXT("grass_block"), TEXT("grass") }, { EMatch::Exact, TEXT("podzol"), TEXT("grass") },
		{ EMatch::Exact, TEXT("mycelium"), TEXT("grass") }, { EMatch::Exact, TEXT("hay_block"), TEXT("grass") },
		{ EMatch::Exact, TEXT("dirt"), TEXT("gravel") }, { EMatch::Exact, TEXT("coarse_dirt"), TEXT("gravel") },
		{ EMatch::Exact, TEXT("farmland"), TEXT("gravel") }, { EMatch::Exact, TEXT("dirt_path"), TEXT("gravel") },
		{ EMatch::Exact, TEXT("clay"), TEXT("gravel") }, { EMatch::Exact, TEXT("gravel"), TEXT("gravel") },
		{ EMatch::Exact, TEXT("suspicious_gravel"), TEXT("suspicious_gravel") }, { EMatch::Exact, TEXT("sand"), TEXT("sand") },
		{ EMatch::Exact, TEXT("red_sand"), TEXT("sand") }, { EMatch::Exact, TEXT("suspicious_sand"), TEXT("suspicious_sand") },
		{ EMatch::Exact, TEXT("rooted_dirt"), TEXT("rooted_dirt") }, { EMatch::Exact, TEXT("soul_sand"), TEXT("soul_sand") },
		{ EMatch::Exact, TEXT("soul_soil"), TEXT("soul_soil") }, { EMatch::Exact, TEXT("mud"), TEXT("mud") },
		{ EMatch::Exact, TEXT("packed_mud"), TEXT("packed_mud") }, { EMatch::Exact, TEXT("snow"), TEXT("snow") },
		{ EMatch::Exact, TEXT("snow_block"), TEXT("snow") }, { EMatch::Exact, TEXT("powder_snow"), TEXT("powder_snow") },
		{ EMatch::Exact, TEXT("netherrack"), TEXT("netherrack") },
		// Stone variants with their own sounds.
		{ EMatch::Prefix, TEXT("deepslate_bricks"), TEXT("deepslate_bricks") }, { EMatch::Prefix, TEXT("cracked_deepslate_bricks"), TEXT("deepslate_bricks") },
		{ EMatch::Prefix, TEXT("deepslate_tiles"), TEXT("deepslate_tiles") }, { EMatch::Prefix, TEXT("cracked_deepslate_tiles"), TEXT("deepslate_tiles") },
		{ EMatch::Prefix, TEXT("polished_deepslate"), TEXT("polished_deepslate") }, { EMatch::Prefix, TEXT("deepslate"), TEXT("deepslate") },
		{ EMatch::Prefix, TEXT("cobbled_deepslate"), TEXT("deepslate") }, { EMatch::Prefix, TEXT("chiseled_deepslate"), TEXT("deepslate") },
		{ EMatch::Prefix, TEXT("tuff_brick"), TEXT("tuff_bricks") }, { EMatch::Prefix, TEXT("polished_tuff"), TEXT("polished_tuff") },
		{ EMatch::Prefix, TEXT("tuff"), TEXT("tuff") }, { EMatch::Prefix, TEXT("chiseled_tuff"), TEXT("tuff") },
		{ EMatch::Prefix, TEXT("mud_brick"), TEXT("mud_bricks") },
		{ EMatch::Contains, TEXT("nether_brick"), TEXT("nether_bricks") },
		{ EMatch::Contains, TEXT("basalt"), TEXT("basalt") },
		{ EMatch::Exact, TEXT("calcite"), TEXT("calcite") }, { EMatch::Exact, TEXT("amethyst_block"), TEXT("amethyst_block") },
		{ EMatch::Exact, TEXT("budding_amethyst"), TEXT("amethyst_block") }, { EMatch::Exact, TEXT("bone_block"), TEXT("bone_block") },
		{ EMatch::Exact, TEXT("dripstone_block"), TEXT("dripstone_block") }, { EMatch::Exact, TEXT("pointed_dripstone"), TEXT("pointed_dripstone") },
		{ EMatch::Exact, TEXT("nether_gold_ore"), TEXT("nether_gold_ore") }, { EMatch::Exact, TEXT("nether_quartz_ore"), TEXT("nether_ore") },
		{ EMatch::Exact, TEXT("ancient_debris"), TEXT("ancient_debris") }, { EMatch::Exact, TEXT("netherite_block"), TEXT("netherite_block") },
		{ EMatch::Exact, TEXT("lodestone"), TEXT("lodestone") },
		// Metal.
		{ EMatch::Prefix, TEXT("anvil"), TEXT("anvil") }, { EMatch::Prefix, TEXT("chipped_anvil"), TEXT("anvil") },
		{ EMatch::Prefix, TEXT("damaged_anvil"), TEXT("anvil") }, { EMatch::Exact, TEXT("chain"), TEXT("chain") },
		{ EMatch::Exact, TEXT("lantern"), TEXT("lantern") }, { EMatch::Exact, TEXT("soul_lantern"), TEXT("lantern") },
		{ EMatch::Exact, TEXT("iron_bars"), TEXT("iron") },
		{ EMatch::Suffix, TEXT("_ore"), TEXT("stone") },
		{ EMatch::Contains, TEXT("copper"), TEXT("copper") },
		{ EMatch::Exact, TEXT("iron_block"), TEXT("metal") }, { EMatch::Exact, TEXT("gold_block"), TEXT("metal") },
		{ EMatch::Exact, TEXT("diamond_block"), TEXT("metal") }, { EMatch::Exact, TEXT("emerald_block"), TEXT("metal") },
		{ EMatch::Exact, TEXT("lapis_block"), TEXT("metal") }, { EMatch::Exact, TEXT("redstone_block"), TEXT("metal") },
		{ EMatch::Exact, TEXT("raw_iron_block"), TEXT("metal") }, { EMatch::Exact, TEXT("raw_gold_block"), TEXT("metal") },
		{ EMatch::Exact, TEXT("iron_door"), TEXT("metal") }, { EMatch::Exact, TEXT("iron_trapdoor"), TEXT("metal") },
		{ EMatch::Exact, TEXT("hopper"), TEXT("metal") }, { EMatch::Exact, TEXT("cauldron"), TEXT("metal") },
		// Soft things.
		{ EMatch::Exact, TEXT("slime_block"), TEXT("slime_block") }, { EMatch::Exact, TEXT("honey_block"), TEXT("honey_block") },
		{ EMatch::Exact, TEXT("sponge"), TEXT("sponge") }, { EMatch::Exact, TEXT("wet_sponge"), TEXT("wet_sponge") },
		{ EMatch::Exact, TEXT("cobweb"), TEXT("cobweb") }, { EMatch::Exact, TEXT("vine"), TEXT("vine") },
		{ EMatch::Exact, TEXT("ladder"), TEXT("ladder") }, { EMatch::Exact, TEXT("scaffolding"), TEXT("scaffolding") },
		{ EMatch::Exact, TEXT("spawner"), TEXT("spawner") }, { EMatch::Exact, TEXT("trial_spawner"), TEXT("trial_spawner") },
		{ EMatch::Exact, TEXT("vault"), TEXT("vault") }, { EMatch::Exact, TEXT("moss_block"), TEXT("moss") },
		{ EMatch::Exact, TEXT("moss_carpet"), TEXT("moss_carpet") }, { EMatch::Exact, TEXT("cactus"), TEXT("wool") },
		{ EMatch::Exact, TEXT("nether_wart_block"), TEXT("wart_block") }, { EMatch::Exact, TEXT("warped_wart_block"), TEXT("wart_block") },
		{ EMatch::Exact, TEXT("shroomlight"), TEXT("shroomlight") }, { EMatch::Exact, TEXT("sweet_berry_bush"), TEXT("sweet_berry_bush") },
		{ EMatch::Prefix, TEXT("sculk"), TEXT("sculk") },
		{ EMatch::Exact, TEXT("crimson_nylium"), TEXT("nylium") }, { EMatch::Exact, TEXT("warped_nylium"), TEXT("nylium") },
		{ EMatch::Suffix, TEXT("_wool"), TEXT("wool") }, { EMatch::Suffix, TEXT("_carpet"), TEXT("wool") },
		{ EMatch::Exact, TEXT("candle"), TEXT("candle") }, { EMatch::Suffix, TEXT("_candle"), TEXT("candle") },
		// Glass and ice.
		{ EMatch::Contains, TEXT("glass"), TEXT("glass") }, { EMatch::Suffix, TEXT("ice"), TEXT("glass") },
		{ EMatch::Exact, TEXT("glowstone"), TEXT("glass") }, { EMatch::Exact, TEXT("sea_lantern"), TEXT("glass") },
		{ EMatch::Exact, TEXT("beacon"), TEXT("glass") }, { EMatch::Exact, TEXT("redstone_lamp"), TEXT("glass") },
		// Plants.
		{ EMatch::Exact, TEXT("azalea_leaves"), TEXT("azalea_leaves") }, { EMatch::Exact, TEXT("flowering_azalea_leaves"), TEXT("azalea_leaves") },
		{ EMatch::Exact, TEXT("cherry_leaves"), TEXT("cherry_leaves") }, { EMatch::Suffix, TEXT("_leaves"), TEXT("grass") },
		{ EMatch::Exact, TEXT("cherry_sapling"), TEXT("cherry_sapling") }, { EMatch::Exact, TEXT("bamboo_sapling"), TEXT("bamboo_sapling") },
		{ EMatch::Suffix, TEXT("_sapling"), TEXT("grass") }, { EMatch::Exact, TEXT("bamboo"), TEXT("bamboo") },
		{ EMatch::Exact, TEXT("azalea"), TEXT("azalea") }, { EMatch::Exact, TEXT("flowering_azalea"), TEXT("flowering_azalea") },
		{ EMatch::Exact, TEXT("wheat"), TEXT("crop") }, { EMatch::Exact, TEXT("carrots"), TEXT("crop") },
		{ EMatch::Exact, TEXT("potatoes"), TEXT("crop") }, { EMatch::Exact, TEXT("beetroots"), TEXT("crop") },
		{ EMatch::Exact, TEXT("melon_stem"), TEXT("crop") }, { EMatch::Exact, TEXT("pumpkin_stem"), TEXT("crop") },
		{ EMatch::Exact, TEXT("seagrass"), TEXT("wet_grass") }, { EMatch::Exact, TEXT("tall_seagrass"), TEXT("wet_grass") },
		{ EMatch::Exact, TEXT("kelp"), TEXT("wet_grass") }, { EMatch::Exact, TEXT("kelp_plant"), TEXT("wet_grass") },
		{ EMatch::Exact, TEXT("short_grass"), TEXT("grass") }, { EMatch::Exact, TEXT("tall_grass"), TEXT("grass") },
		{ EMatch::Exact, TEXT("fern"), TEXT("grass") }, { EMatch::Exact, TEXT("large_fern"), TEXT("grass") },
		{ EMatch::Exact, TEXT("dead_bush"), TEXT("grass") }, { EMatch::Exact, TEXT("dandelion"), TEXT("grass") },
		{ EMatch::Exact, TEXT("poppy"), TEXT("grass") }, { EMatch::Exact, TEXT("allium"), TEXT("grass") },
		{ EMatch::Exact, TEXT("azure_bluet"), TEXT("grass") }, { EMatch::Exact, TEXT("cornflower"), TEXT("grass") },
		{ EMatch::Exact, TEXT("oxeye_daisy"), TEXT("grass") }, { EMatch::Exact, TEXT("blue_orchid"), TEXT("grass") },
		{ EMatch::Exact, TEXT("lilac"), TEXT("grass") }, { EMatch::Exact, TEXT("rose_bush"), TEXT("grass") },
		{ EMatch::Exact, TEXT("peony"), TEXT("grass") }, { EMatch::Exact, TEXT("sunflower"), TEXT("grass") },
		{ EMatch::Exact, TEXT("lily_of_the_valley"), TEXT("grass") }, { EMatch::Suffix, TEXT("_tulip"), TEXT("grass") },
		{ EMatch::Exact, TEXT("mangrove_roots"), TEXT("mangrove_roots") }, { EMatch::Exact, TEXT("muddy_mangrove_roots"), TEXT("muddy_mangrove_roots") },
		{ EMatch::Suffix, TEXT("_roots"), TEXT("roots") },
		{ EMatch::Suffix, TEXT("_fungus"), TEXT("fungus") },
		// Wood.
		{ EMatch::Suffix, TEXT("_stem"), TEXT("stem") }, { EMatch::Suffix, TEXT("_hyphae"), TEXT("stem") },
		{ EMatch::Prefix, TEXT("crimson_"), TEXT("nether_wood") }, { EMatch::Prefix, TEXT("warped_"), TEXT("nether_wood") },
		{ EMatch::Prefix, TEXT("cherry_"), TEXT("cherry_wood") }, { EMatch::Prefix, TEXT("bamboo_"), TEXT("bamboo_wood") },
		{ EMatch::Prefix, TEXT("stripped_"), TEXT("wood") }, { EMatch::Prefix, TEXT("oak_"), TEXT("wood") },
		{ EMatch::Prefix, TEXT("dark_oak_"), TEXT("wood") }, { EMatch::Prefix, TEXT("pale_oak_"), TEXT("wood") },
		{ EMatch::Prefix, TEXT("spruce_"), TEXT("wood") }, { EMatch::Prefix, TEXT("birch_"), TEXT("wood") },
		{ EMatch::Prefix, TEXT("jungle_"), TEXT("wood") }, { EMatch::Prefix, TEXT("acacia_"), TEXT("wood") },
		{ EMatch::Prefix, TEXT("mangrove_"), TEXT("wood") },
		{ EMatch::Exact, TEXT("crafting_table"), TEXT("wood") }, { EMatch::Exact, TEXT("bookshelf"), TEXT("wood") },
		{ EMatch::Exact, TEXT("chest"), TEXT("wood") }, { EMatch::Exact, TEXT("trapped_chest"), TEXT("wood") },
		{ EMatch::Exact, TEXT("barrel"), TEXT("wood") }, { EMatch::Exact, TEXT("jukebox"), TEXT("wood") },
		{ EMatch::Exact, TEXT("note_block"), TEXT("wood") }, { EMatch::Exact, TEXT("lectern"), TEXT("wood") },
		{ EMatch::Exact, TEXT("loom"), TEXT("wood") }, { EMatch::Exact, TEXT("composter"), TEXT("wood") },
		{ EMatch::Exact, TEXT("cartography_table"), TEXT("wood") }, { EMatch::Exact, TEXT("fletching_table"), TEXT("wood") },
		{ EMatch::Exact, TEXT("smithing_table"), TEXT("wood") }, { EMatch::Exact, TEXT("torch"), TEXT("wood") },
		{ EMatch::Exact, TEXT("wall_torch"), TEXT("wood") }, { EMatch::Exact, TEXT("pumpkin"), TEXT("wood") },
		{ EMatch::Exact, TEXT("carved_pumpkin"), TEXT("wood") }, { EMatch::Exact, TEXT("jack_o_lantern"), TEXT("wood") },
		{ EMatch::Exact, TEXT("melon"), TEXT("wood") }, { EMatch::Suffix, TEXT("_bed"), TEXT("wood") },
		{ EMatch::Suffix, TEXT("_banner"), TEXT("wood") },
	};

	FString PathOf(FName Block)
	{
		FString Text = Block.ToString();
		int32 Colon;
		if (Text.FindChar(TEXT(':'), Colon))
		{
			Text.RightChopInline(Colon + 1);
		}
		// The trader's llamas sound as llamas.
		if (Text == TEXT("trader_llama"))
		{
			return TEXT("llama");
		}
		return Text;
	}

	bool Matches(const FRule& Rule, const FString& Path)
	{
		switch (Rule.Match)
		{
		case EMatch::Exact: return Path == Rule.Text;
		case EMatch::Prefix: return Path.StartsWith(Rule.Text);
		case EMatch::Suffix: return Path.EndsWith(Rule.Text);
		default: return Path.Contains(Rule.Text);
		}
	}

	const TCHAR* BlockKindName(McSound::EBlockSound Kind)
	{
		switch (Kind)
		{
		case McSound::EBlockSound::Break: return TEXT("break");
		case McSound::EBlockSound::Place: return TEXT("place");
		case McSound::EBlockSound::Step: return TEXT("step");
		case McSound::EBlockSound::Hit: return TEXT("hit");
		default: return TEXT("fall");
		}
	}

	// Which sounds each mob has of its own.
	struct FMobSounds
	{
		const TCHAR* Path;
		bool bAmbient;
		bool bHurt;
		bool bDeath;
		bool bStep;
	};
	const FMobSounds MobSounds[] = {
		{ TEXT("pig"), true, true, true, true }, { TEXT("cow"), true, true, true, true },
		{ TEXT("sheep"), true, true, true, true }, { TEXT("chicken"), true, true, true, true },
		{ TEXT("zombie"), true, true, true, true }, { TEXT("skeleton"), true, true, true, true },
		{ TEXT("spider"), true, true, true, true },
		// A creeper has no ambient sound, and these two take the block's step.
		{ TEXT("creeper"), false, true, true, false }, { TEXT("enderman"), true, true, true, false },
		{ TEXT("slime"), false, true, true, false },
		// The animals added later. A rabbit, fox and bee have no step of their own, a turtle's ambient sound is
		// named for the land and a bee hums on a loop, so those are left off.
		{ TEXT("wolf"), true, true, true, true }, { TEXT("rabbit"), true, true, true, false },
		{ TEXT("fox"), true, true, true, false }, { TEXT("horse"), true, true, true, true },
		{ TEXT("goat"), true, true, true, true }, { TEXT("llama"), true, true, true, true },
		{ TEXT("turtle"), false, true, true, false }, { TEXT("frog"), true, true, true, true },
		{ TEXT("bee"), false, true, true, false },
		// The villagers and their kin: a golem has no ambient sound, and a villager and a trader no step.
		{ TEXT("iron_golem"), false, true, true, true }, { TEXT("villager"), true, true, true, false },
		{ TEXT("wandering_trader"), true, true, true, false }, { TEXT("zombie_villager"), true, true, true, true },
	};
}

FName McSound::GetBlockGroup(FName Block)
{
	const FString Path = PathOf(Block);
	if (Path.IsEmpty() || Path == TEXT("air") || Path == TEXT("cave_air") || Path == TEXT("void_air"))
	{
		return NAME_None;
	}
	for (const FRule& Rule : Rules)
	{
		if (Matches(Rule, Path))
		{
			return FName(Rule.Group);
		}
	}
	// Stone, bricks, ores, concrete, terracotta, obsidian and the rest.
	return FName("stone");
}

McSound::FGroupTuning McSound::GetGroupTuning(FName Group)
{
	FGroupTuning Tuning;
	if (Group == FName("metal"))
	{
		Tuning.Pitch = 1.5f;
	}
	else if (Group == FName("anvil"))
	{
		Tuning.Volume = 0.3f;
	}
	return Tuning;
}

FMcSoundEvent McSound::GetBlockSound(FName Block, EBlockSound Kind, const FVector& Position)
{
	FMcSoundEvent Event;
	Event.Position = Position;
	const FName Group = GetBlockGroup(Block);
	if (Group.IsNone())
	{
		return Event;
	}
	const FGroupTuning Tuning = GetGroupTuning(Group);
	switch (Kind)
	{
	case EBlockSound::Break:
	case EBlockSound::Place:
		Event.Volume = (Tuning.Volume + 1.0f) * 0.5f;
		Event.Pitch = Tuning.Pitch * 0.8f;
		break;
	case EBlockSound::Step:
		Event.Volume = Tuning.Volume * 0.15f;
		Event.Pitch = Tuning.Pitch;
		break;
	case EBlockSound::Hit:
		Event.Volume = (Tuning.Volume + 1.0f) * 0.125f;
		Event.Pitch = Tuning.Pitch * 0.5f;
		break;
	default:
		Event.Volume = Tuning.Volume * 0.5f;
		Event.Pitch = Tuning.Pitch;
		break;
	}
	Event.Name = FName(*FString::Printf(TEXT("block.%s.%s"), *Group.ToString(), BlockKindName(Kind)));
	return Event;
}

FName McSound::GetMobSoundName(FName Mob, EMobSound Kind, int32 SlimeSize)
{
	const FString Path = PathOf(Mob);
	for (const FMobSounds& Sounds : MobSounds)
	{
		if (Path != Sounds.Path)
		{
			continue;
		}
		const TCHAR* Suffix = nullptr;
		switch (Kind)
		{
		case EMobSound::Ambient: Suffix = Sounds.bAmbient ? TEXT("ambient") : nullptr; break;
		case EMobSound::Hurt: Suffix = Sounds.bHurt ? TEXT("hurt") : nullptr; break;
		case EMobSound::Death: Suffix = Sounds.bDeath ? TEXT("death") : nullptr; break;
		default: Suffix = Sounds.bStep ? TEXT("step") : nullptr; break;
		}
		if (!Suffix)
		{
			return NAME_None;
		}
		const bool bSmallSlime = Path == TEXT("slime") && SlimeSize <= 1;
		return FName(*FString::Printf(TEXT("entity.%s.%s%s"), Sounds.Path, Suffix, bSmallSlime ? TEXT("_small") : TEXT("")));
	}
	return NAME_None;
}

FMcSoundEvent McSound::GetMobStep(FName Mob, FName BlockBelow, const FVector& Position)
{
	const FName Own = GetMobSoundName(Mob, EMobSound::Step);
	if (Own.IsNone())
	{
		return GetBlockSound(BlockBelow, EBlockSound::Step, Position);
	}
	FMcSoundEvent Event;
	Event.Name = Own;
	Event.Position = Position;
	Event.Volume = 0.15f;
	return Event;
}

float McSound::GetAudibleRange(float Volume, float AttenuationDistance)
{
	return AttenuationDistance * FMath::Max(1.0f, Volume);
}

float McSound::GetGain(float Volume, float Distance, float AttenuationDistance)
{
	// A volume over 1 only carries the sound further; the loudness itself stops at 1.
	const float Range = GetAudibleRange(Volume, AttenuationDistance);
	return FMath::Clamp(Volume, 0.0f, 1.0f) * FMath::Clamp(1.0f - Distance / Range, 0.0f, 1.0f);
}

bool McSound::TickAmbient(int32& Counter, int32 Roll1000, int32 IntervalTicks)
{
	const bool bPlay = Roll1000 < Counter;
	++Counter;
	if (bPlay)
	{
		Counter = -IntervalTicks;
	}
	return bPlay;
}
