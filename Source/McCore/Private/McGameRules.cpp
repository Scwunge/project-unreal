#include "McGameRules.h"

bool FMcGameRules::GetBool(const TMap<FString, FString>& Rules, const FString& Name, bool bDefault)
{
	const FString* Value = Rules.Find(Name);
	if (!Value)
	{
		return bDefault;
	}
	if (Value->Equals(TEXT("true"), ESearchCase::IgnoreCase))
	{
		return true;
	}
	if (Value->Equals(TEXT("false"), ESearchCase::IgnoreCase))
	{
		return false;
	}
	return bDefault;
}

int32 FMcGameRules::GetInt(const TMap<FString, FString>& Rules, const FString& Name, int32 Default)
{
	const FString* Value = Rules.Find(Name);
	if (!Value || !Value->IsNumeric())
	{
		return Default;
	}
	return FCString::Atoi(**Value);
}

FMcGameRules FMcGameRules::FromLevel(const TMap<FString, FString>& Rules, int32 InDifficulty)
{
	FMcGameRules Result;
	Result.Difficulty = FMath::Clamp(InDifficulty, 0, 3);
	Result.bDoMobSpawning = GetBool(Rules, TEXT("spawn_mobs"), Result.bDoMobSpawning);
	Result.bDoWardenSpawning = GetBool(Rules, TEXT("spawn_wardens"), Result.bDoWardenSpawning);
	Result.bDoMobLoot = GetBool(Rules, TEXT("mob_drops"), Result.bDoMobLoot);
	Result.bDoTileDrops = GetBool(Rules, TEXT("block_drops"), Result.bDoTileDrops);
	Result.bMobGriefing = GetBool(Rules, TEXT("mob_griefing"), Result.bMobGriefing);
	Result.bKeepInventory = GetBool(Rules, TEXT("keep_inventory"), Result.bKeepInventory);
	Result.bNaturalRegeneration = GetBool(Rules, TEXT("natural_health_regeneration"), Result.bNaturalRegeneration);
	Result.bFallDamage = GetBool(Rules, TEXT("fall_damage"), Result.bFallDamage);
	Result.bFireDamage = GetBool(Rules, TEXT("fire_damage"), Result.bFireDamage);
	Result.bDrowningDamage = GetBool(Rules, TEXT("drowning_damage"), Result.bDrowningDamage);
	Result.bDoWeatherCycle = GetBool(Rules, TEXT("advance_weather"), Result.bDoWeatherCycle);
	Result.bSpawnerBlocksWork = GetBool(Rules, TEXT("spawner_blocks_work"), Result.bSpawnerBlocksWork);
	Result.bLimitedCrafting = GetBool(Rules, TEXT("limited_crafting"), Result.bLimitedCrafting);
	Result.RandomTickSpeed = FMath::Max(0, GetInt(Rules, TEXT("random_tick_speed"), Result.RandomTickSpeed));
	return Result;
}

void FMcGameRules::MigrateLegacyNames(TMap<FString, FString>& Rules)
{
	// Old name, new name, whether the value is flipped.
	struct FRename { const TCHAR* Old; const TCHAR* New; bool bFlip; };
	static const FRename Renames[] = {
		{ TEXT("allowEnteringNetherUsingPortals"), TEXT("allow_entering_nether_using_portals"), false },
		{ TEXT("announceAdvancements"), TEXT("show_advancement_messages"), false },
		{ TEXT("blockExplosionDropDecay"), TEXT("block_explosion_drop_decay"), false },
		{ TEXT("commandBlockOutput"), TEXT("command_block_output"), false },
		{ TEXT("commandBlocksEnabled"), TEXT("command_blocks_work"), false },
		{ TEXT("enableCommandBlocks"), TEXT("command_blocks_work"), false },
		{ TEXT("commandModificationBlockLimit"), TEXT("max_block_modifications"), false },
		{ TEXT("disableElytraMovementCheck"), TEXT("elytra_movement_check"), true },
		{ TEXT("disablePlayerMovementCheck"), TEXT("player_movement_check"), true },
		{ TEXT("disableRaids"), TEXT("raids"), true },
		{ TEXT("doDaylightCycle"), TEXT("advance_time"), false },
		{ TEXT("doEntityDrops"), TEXT("entity_drops"), false },
		{ TEXT("doImmediateRespawn"), TEXT("immediate_respawn"), false },
		{ TEXT("doInsomnia"), TEXT("spawn_phantoms"), false },
		{ TEXT("doLimitedCrafting"), TEXT("limited_crafting"), false },
		{ TEXT("doMobLoot"), TEXT("mob_drops"), false },
		{ TEXT("doMobSpawning"), TEXT("spawn_mobs"), false },
		{ TEXT("doPatrolSpawning"), TEXT("spawn_patrols"), false },
		{ TEXT("doTileDrops"), TEXT("block_drops"), false },
		{ TEXT("doTraderSpawning"), TEXT("spawn_wandering_traders"), false },
		{ TEXT("doVinesSpread"), TEXT("spread_vines"), false },
		{ TEXT("doWardenSpawning"), TEXT("spawn_wardens"), false },
		{ TEXT("doWeatherCycle"), TEXT("advance_weather"), false },
		{ TEXT("drowningDamage"), TEXT("drowning_damage"), false },
		{ TEXT("enderPearlsVanishOnDeath"), TEXT("ender_pearls_vanish_on_death"), false },
		{ TEXT("fallDamage"), TEXT("fall_damage"), false },
		{ TEXT("fireDamage"), TEXT("fire_damage"), false },
		{ TEXT("forgiveDeadPlayers"), TEXT("forgive_dead_players"), false },
		{ TEXT("freezeDamage"), TEXT("freeze_damage"), false },
		{ TEXT("globalSoundEvents"), TEXT("global_sound_events"), false },
		{ TEXT("keepInventory"), TEXT("keep_inventory"), false },
		{ TEXT("lavaSourceConversion"), TEXT("lava_source_conversion"), false },
		{ TEXT("locatorBar"), TEXT("locator_bar"), false },
		{ TEXT("logAdminCommands"), TEXT("log_admin_commands"), false },
		{ TEXT("maxCommandChainLength"), TEXT("max_command_sequence_length"), false },
		{ TEXT("maxCommandForkCount"), TEXT("max_command_forks"), false },
		{ TEXT("maxEntityCramming"), TEXT("max_entity_cramming"), false },
		{ TEXT("minecartMaxSpeed"), TEXT("max_minecart_speed"), false },
		{ TEXT("mobExplosionDropDecay"), TEXT("mob_explosion_drop_decay"), false },
		{ TEXT("mobGriefing"), TEXT("mob_griefing"), false },
		{ TEXT("naturalRegeneration"), TEXT("natural_health_regeneration"), false },
		{ TEXT("playersNetherPortalCreativeDelay"), TEXT("players_nether_portal_creative_delay"), false },
		{ TEXT("playersNetherPortalDefaultDelay"), TEXT("players_nether_portal_default_delay"), false },
		{ TEXT("playersSleepingPercentage"), TEXT("players_sleeping_percentage"), false },
		{ TEXT("projectilesCanBreakBlocks"), TEXT("projectiles_can_break_blocks"), false },
		{ TEXT("randomTickSpeed"), TEXT("random_tick_speed"), false },
		{ TEXT("reducedDebugInfo"), TEXT("reduced_debug_info"), false },
		{ TEXT("sendCommandFeedback"), TEXT("send_command_feedback"), false },
		{ TEXT("showDeathMessages"), TEXT("show_death_messages"), false },
		{ TEXT("snowAccumulationHeight"), TEXT("max_snow_accumulation_height"), false },
		{ TEXT("spawnerBlocksWork"), TEXT("spawner_blocks_work"), false },
		{ TEXT("spawnMonsters"), TEXT("spawn_monsters"), false },
		{ TEXT("spawnRadius"), TEXT("respawn_radius"), false },
		{ TEXT("spectatorsGenerateChunks"), TEXT("spectators_generate_chunks"), false },
		{ TEXT("tntExplodes"), TEXT("tnt_explodes"), false },
		{ TEXT("tntExplosionDropDecay"), TEXT("tnt_explosion_drop_decay"), false },
		{ TEXT("universalAnger"), TEXT("universal_anger"), false },
		{ TEXT("waterSourceConversion"), TEXT("water_source_conversion"), false },
	};
	// Fire: the old on/off switch became a radius around players; only an "off" is carried (no radius at all).
	if (const FString* FireTick = Rules.Find(TEXT("doFireTick")))
	{
		if (*FireTick == TEXT("false") && !Rules.Contains(TEXT("fire_spread_radius_around_player")))
		{
			Rules.Add(TEXT("fire_spread_radius_around_player"), TEXT("0"));
		}
	}
	for (const FRename& Rename : Renames)
	{
		FString Value;
		if (!Rules.RemoveAndCopyValue(Rename.Old, Value) || Rules.Contains(Rename.New))
		{
			continue;
		}
		if (Rename.bFlip)
		{
			Value = Value == TEXT("true") ? TEXT("false") : Value == TEXT("false") ? TEXT("true") : Value;
		}
		Rules.Add(Rename.New, Value);
	}
	// What vanilla no longer has.
	for (const TCHAR* Gone : { TEXT("doFireTick"), TEXT("allowFireTicksAwayFromPlayer"), TEXT("spawnChunkRadius"), TEXT("entitiesWithPassengersCanUsePortals") })
	{
		Rules.Remove(Gone);
	}
}
