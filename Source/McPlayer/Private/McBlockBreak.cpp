#include "McBlockBreak.h"
#include "Dom/JsonObject.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McItemRegistry.h"
#include "Misc/Paths.h"

namespace McBlockBreak
{
	namespace
	{
		FName NameOf(FMcStateId State)
		{
			const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
			return Registry.IsValidState(State) ? Registry.GetBlockOf(State).Name : NAME_None;
		}

		// "minecraft:oak_log" -> "oak_log"
		FString PathOfName(FName Name)
		{
			return McCache::PathOf(Name.ToString());
		}

		bool InTag(const TCHAR* Tag, FMcStateId State)
		{
			FMcBlockTags& Tags = FMcBlockTags::Get();
			Tags.EnsureLoaded();
			return Tags.Has(FString(Tag), State);
		}

		// Hardness values from minecraft.wiki ("Hardness"), by block name; -1 can't be broken.
		const TMap<FString, float>& ExactHardness()
		{
			static const TMap<FString, float> Table = {
				{ TEXT("air"), -1 }, { TEXT("cave_air"), -1 }, { TEXT("void_air"), -1 }, { TEXT("water"), -1 }, { TEXT("lava"), -1 },
				{ TEXT("bedrock"), -1 }, { TEXT("barrier"), -1 }, { TEXT("light"), -1 }, { TEXT("structure_void"), -1 }, { TEXT("end_portal"), -1 },
				{ TEXT("end_portal_frame"), -1 }, { TEXT("nether_portal"), -1 }, { TEXT("end_gateway"), -1 }, { TEXT("command_block"), -1 },
				{ TEXT("chain_command_block"), -1 }, { TEXT("repeating_command_block"), -1 }, { TEXT("structure_block"), -1 }, { TEXT("jigsaw"), -1 },
				{ TEXT("moving_piston"), -1 }, { TEXT("bubble_column"), -1 },
				{ TEXT("stone"), 1.5f }, { TEXT("granite"), 1.5f }, { TEXT("diorite"), 1.5f }, { TEXT("andesite"), 1.5f }, { TEXT("cobblestone"), 2.0f },
				{ TEXT("mossy_cobblestone"), 2.0f }, { TEXT("deepslate"), 3.0f }, { TEXT("cobbled_deepslate"), 3.5f }, { TEXT("tuff"), 1.5f },
				{ TEXT("calcite"), 0.75f }, { TEXT("dripstone_block"), 1.5f }, { TEXT("smooth_stone"), 2.0f }, { TEXT("stone_bricks"), 1.5f },
				{ TEXT("bricks"), 2.0f }, { TEXT("dirt"), 0.5f }, { TEXT("coarse_dirt"), 0.5f }, { TEXT("rooted_dirt"), 0.5f }, { TEXT("grass_block"), 0.6f },
				{ TEXT("podzol"), 0.5f }, { TEXT("mycelium"), 0.6f }, { TEXT("dirt_path"), 0.65f }, { TEXT("farmland"), 0.6f }, { TEXT("mud"), 0.5f },
				{ TEXT("sand"), 0.5f }, { TEXT("red_sand"), 0.5f }, { TEXT("gravel"), 0.6f }, { TEXT("clay"), 0.6f }, { TEXT("soul_sand"), 0.5f },
				{ TEXT("soul_soil"), 0.5f }, { TEXT("netherrack"), 0.4f }, { TEXT("magma_block"), 0.5f }, { TEXT("end_stone"), 3.0f }, { TEXT("obsidian"), 50.0f },
				{ TEXT("crying_obsidian"), 50.0f }, { TEXT("respawn_anchor"), 50.0f }, { TEXT("ender_chest"), 22.5f }, { TEXT("spawner"), 5.0f },
				{ TEXT("glass"), 0.3f }, { TEXT("tinted_glass"), 0.3f }, { TEXT("glowstone"), 0.3f }, { TEXT("sea_lantern"), 0.3f }, { TEXT("redstone_lamp"), 0.3f },
				{ TEXT("ice"), 0.5f }, { TEXT("packed_ice"), 0.5f }, { TEXT("blue_ice"), 2.8f }, { TEXT("snow"), 0.1f }, { TEXT("snow_block"), 0.2f },
				{ TEXT("powder_snow"), 0.25f }, { TEXT("cactus"), 0.4f }, { TEXT("sponge"), 0.6f }, { TEXT("wet_sponge"), 0.6f }, { TEXT("hay_block"), 0.5f },
				{ TEXT("pumpkin"), 1.0f }, { TEXT("carved_pumpkin"), 1.0f }, { TEXT("jack_o_lantern"), 1.0f }, { TEXT("melon"), 1.0f },
				{ TEXT("iron_block"), 5.0f }, { TEXT("gold_block"), 3.0f }, { TEXT("diamond_block"), 5.0f }, { TEXT("emerald_block"), 5.0f },
				{ TEXT("netherite_block"), 50.0f }, { TEXT("coal_block"), 5.0f }, { TEXT("lapis_block"), 3.0f }, { TEXT("redstone_block"), 5.0f },
				{ TEXT("copper_block"), 3.0f }, { TEXT("raw_iron_block"), 5.0f }, { TEXT("raw_gold_block"), 5.0f }, { TEXT("raw_copper_block"), 5.0f },
				{ TEXT("crafting_table"), 2.5f }, { TEXT("chest"), 2.5f }, { TEXT("trapped_chest"), 2.5f }, { TEXT("barrel"), 2.5f }, { TEXT("bookshelf"), 1.5f },
				{ TEXT("furnace"), 3.5f }, { TEXT("blast_furnace"), 3.5f }, { TEXT("smoker"), 3.5f }, { TEXT("dispenser"), 3.5f }, { TEXT("dropper"), 3.5f },
				{ TEXT("hopper"), 3.0f }, { TEXT("observer"), 3.0f }, { TEXT("piston"), 1.5f }, { TEXT("sticky_piston"), 1.5f }, { TEXT("note_block"), 0.8f },
				{ TEXT("jukebox"), 2.0f }, { TEXT("anvil"), 5.0f }, { TEXT("enchanting_table"), 5.0f }, { TEXT("cauldron"), 2.0f }, { TEXT("composter"), 0.6f },
				{ TEXT("lectern"), 2.5f }, { TEXT("campfire"), 2.0f }, { TEXT("soul_campfire"), 2.0f }, { TEXT("lantern"), 3.5f }, { TEXT("soul_lantern"), 3.5f },
				{ TEXT("chain"), 5.0f }, { TEXT("iron_bars"), 5.0f }, { TEXT("ladder"), 0.4f }, { TEXT("cobweb"), 4.0f }, { TEXT("bone_block"), 2.0f },
				{ TEXT("nether_bricks"), 2.0f }, { TEXT("red_nether_bricks"), 2.0f }, { TEXT("quartz_block"), 0.8f }, { TEXT("basalt"), 1.25f },
				{ TEXT("blackstone"), 1.5f }, { TEXT("prismarine"), 1.5f }, { TEXT("purpur_block"), 1.5f }, { TEXT("amethyst_block"), 1.5f },
				{ TEXT("moss_block"), 0.1f }, { TEXT("sculk"), 0.2f }, { TEXT("slime_block"), 0.0f }, { TEXT("honey_block"), 0.0f }, { TEXT("tnt"), 0.0f },
				{ TEXT("bamboo"), 1.0f }, { TEXT("target"), 0.5f }, { TEXT("rail"), 0.7f }, { TEXT("powered_rail"), 0.7f }, { TEXT("detector_rail"), 0.7f },
				{ TEXT("activator_rail"), 0.7f }, { TEXT("lever"), 0.5f }, { TEXT("fire"), 0.0f }, { TEXT("soul_fire"), 0.0f }, { TEXT("torch"), 0.0f },
				{ TEXT("wall_torch"), 0.0f }, { TEXT("soul_torch"), 0.0f }, { TEXT("soul_wall_torch"), 0.0f }, { TEXT("redstone_torch"), 0.0f },
				{ TEXT("redstone_wall_torch"), 0.0f }, { TEXT("redstone_wire"), 0.0f }, { TEXT("repeater"), 0.0f }, { TEXT("comparator"), 0.0f },
				{ TEXT("tripwire"), 0.0f }, { TEXT("tripwire_hook"), 0.0f }, { TEXT("vine"), 0.2f }, { TEXT("short_grass"), 0.0f }, { TEXT("tall_grass"), 0.0f },
				{ TEXT("fern"), 0.0f }, { TEXT("large_fern"), 0.0f }, { TEXT("dead_bush"), 0.0f }, { TEXT("sugar_cane"), 0.0f }, { TEXT("wheat"), 0.0f },
				{ TEXT("carrots"), 0.0f }, { TEXT("potatoes"), 0.0f }, { TEXT("beetroots"), 0.0f }, { TEXT("melon_stem"), 0.0f }, { TEXT("pumpkin_stem"), 0.0f },
				{ TEXT("brown_mushroom"), 0.0f }, { TEXT("red_mushroom"), 0.0f }, { TEXT("dandelion"), 0.0f }, { TEXT("poppy"), 0.0f }, { TEXT("kelp"), 0.0f },
				{ TEXT("seagrass"), 0.0f }, { TEXT("tall_seagrass"), 0.0f }, { TEXT("kelp_plant"), 0.0f }, { TEXT("lily_pad"), 0.0f },
			};
			return Table;
		}

		bool EndsWith(const FString& Text, const TCHAR* Suffix)
		{
			return Text.EndsWith(Suffix);
		}

		float HardnessByName(const FString& Path)
		{
			if (const float* Found = ExactHardness().Find(Path))
			{
				return *Found;
			}
			// Families of blocks that share a value.
			if (EndsWith(Path, TEXT("_leaves"))) return 0.2f;
			if (EndsWith(Path, TEXT("_sapling")) || EndsWith(Path, TEXT("_flower")) || EndsWith(Path, TEXT("_tulip")) || Path.Contains(TEXT("_roots")) || EndsWith(Path, TEXT("_fungus"))) return 0.0f;
			if (EndsWith(Path, TEXT("deepslate_bricks")) || EndsWith(Path, TEXT("deepslate_tiles")) || EndsWith(Path, TEXT("polished_deepslate"))) return 3.5f;
			if (Path.StartsWith(TEXT("deepslate_")) && EndsWith(Path, TEXT("_ore"))) return 4.5f;
			if (EndsWith(Path, TEXT("_ore"))) return 3.0f;
			if (EndsWith(Path, TEXT("_log")) || EndsWith(Path, TEXT("_wood")) || EndsWith(Path, TEXT("_stem")) || EndsWith(Path, TEXT("_hyphae"))) return 2.0f;
			if (EndsWith(Path, TEXT("_planks")) || EndsWith(Path, TEXT("_fence")) || EndsWith(Path, TEXT("_fence_gate"))) return 2.0f;
			if (EndsWith(Path, TEXT("_door")) || EndsWith(Path, TEXT("_trapdoor"))) return Path.StartsWith(TEXT("iron_")) || Path.StartsWith(TEXT("copper_")) ? 5.0f : 3.0f;
			if (EndsWith(Path, TEXT("_wool"))) return 0.8f;
			if (EndsWith(Path, TEXT("_carpet"))) return 0.1f;
			if (EndsWith(Path, TEXT("_concrete"))) return 1.8f;
			if (EndsWith(Path, TEXT("_concrete_powder"))) return 0.5f;
			if (EndsWith(Path, TEXT("_glazed_terracotta"))) return 1.4f;
			if (EndsWith(Path, TEXT("_terracotta")) || Path == TEXT("terracotta")) return 1.25f;
			if (EndsWith(Path, TEXT("_glass")) || EndsWith(Path, TEXT("_glass_pane")) || Path == TEXT("glass_pane")) return 0.3f;
			if (EndsWith(Path, TEXT("_bed")) || EndsWith(Path, TEXT("_bud"))) return 0.2f;
			if (EndsWith(Path, TEXT("_button")) || EndsWith(Path, TEXT("_pressure_plate"))) return 0.5f;
			if (EndsWith(Path, TEXT("_sign")) || EndsWith(Path, TEXT("_banner"))) return 1.0f;
			if (EndsWith(Path, TEXT("_shulker_box")) || Path == TEXT("shulker_box")) return 2.0f;
			if (EndsWith(Path, TEXT("sandstone"))) return 0.8f;
			if (EndsWith(Path, TEXT("_stairs")) || EndsWith(Path, TEXT("_slab")) || EndsWith(Path, TEXT("_wall")))
			{
				// Of the same stuff as the block they are named for.
				if (Path.Contains(TEXT("planks")) || Path.StartsWith(TEXT("oak_")) || Path.StartsWith(TEXT("spruce_")) || Path.StartsWith(TEXT("birch_"))
					|| Path.StartsWith(TEXT("jungle_")) || Path.StartsWith(TEXT("acacia_")) || Path.StartsWith(TEXT("dark_oak_")) || Path.StartsWith(TEXT("mangrove_"))
					|| Path.StartsWith(TEXT("cherry_")) || Path.StartsWith(TEXT("bamboo_")) || Path.StartsWith(TEXT("crimson_")) || Path.StartsWith(TEXT("warped_")))
				{
					return 2.0f;
				}
				return Path.Contains(TEXT("cobble")) || Path.Contains(TEXT("brick")) ? 2.0f : 1.5f;
			}
			return -2.0f;
		}
	}

	float GetHardness(FMcStateId State)
	{
		const FName Name = NameOf(State);
		if (Name.IsNone())
		{
			return -1.0f;
		}
		const float ByName = HardnessByName(PathOfName(Name));
		if (ByName > -1.5f)
		{
			return ByName;
		}
		// Anything not in the table: by the tool that suits it.
		if (InTag(TEXT("minecraft:mineable/pickaxe"), State)) return 1.5f;
		if (InTag(TEXT("minecraft:mineable/axe"), State)) return 2.0f;
		return 0.5f;
	}

	bool IsBreakable(FMcStateId State)
	{
		return GetHardness(State) >= 0.0f;
	}

	bool RequiresTool(FMcStateId State)
	{
		const FString Path = PathOfName(NameOf(State));
		if (Path.IsEmpty())
		{
			return false;
		}
		static const TSet<FString> Free = { TEXT("glowstone"), TEXT("redstone_lamp"), TEXT("sea_lantern"), TEXT("ice"), TEXT("packed_ice"), TEXT("blue_ice"),
			TEXT("glass"), TEXT("tinted_glass"), TEXT("glass_pane"), TEXT("beacon"), TEXT("shulker_box"), TEXT("note_block") };
		if (Free.Contains(Path) || Path.EndsWith(TEXT("_stained_glass")) || Path.EndsWith(TEXT("_stained_glass_pane")) || Path.EndsWith(TEXT("_shulker_box")))
		{
			return false;
		}
		return InTag(TEXT("minecraft:needs_stone_tool"), State) || InTag(TEXT("minecraft:needs_iron_tool"), State) || InTag(TEXT("minecraft:needs_diamond_tool"), State)
			|| InTag(TEXT("minecraft:mineable/pickaxe"), State);
	}

	bool CanHarvest(const FMcItemStack& Tool, FMcStateId State)
	{
		return !RequiresTool(State) || (!Tool.IsEmpty() && FMcItemRegistry::Get().IsCorrectToolForDrops(Tool.Item, State));
	}

	float GetDigSpeed(const FMcItemStack& Tool, FMcStateId State, bool bOnGround, bool bHeadInWater, float EffectFactor)
	{
		float Speed = Tool.IsEmpty() ? 1.0f : FMcItemRegistry::Get().GetDestroySpeed(Tool.Item, State);
		if (Speed > 1.0f)
		{
			const int32 Efficiency = Tool.GetEnchantmentLevel(TEXT("minecraft:efficiency"));
			if (Efficiency > 0)
			{
				Speed += Efficiency * Efficiency + 1;
			}
		}
		Speed *= EffectFactor;
		if (bHeadInWater)
		{
			Speed /= 5.0f;
		}
		if (!bOnGround)
		{
			Speed /= 5.0f;
		}
		return Speed;
	}

	float GetProgressPerTick(const FMcItemStack& Tool, FMcStateId State, bool bOnGround, bool bHeadInWater, float EffectFactor)
	{
		const float Hardness = GetHardness(State);
		if (Hardness < 0.0f)
		{
			return 0.0f;
		}
		if (Hardness == 0.0f)
		{
			return 1.0f;
		}
		return GetDigSpeed(Tool, State, bOnGround, bHeadInWater, EffectFactor) / Hardness / (CanHarvest(Tool, State) ? 30.0f : 100.0f);
	}

	int32 GetBreakTicks(const FMcItemStack& Tool, FMcStateId State, bool bOnGround, bool bHeadInWater)
	{
		const float Progress = GetProgressPerTick(Tool, State, bOnGround, bHeadInWater);
		return Progress <= 0.0f ? 0 : FMath::Max(1, FMath::CeilToInt32(1.0f / Progress - 1e-4f));
	}

	int32 GetToolWear(const FMcItemStack& Tool, FMcStateId State)
	{
		FMcToolData Data;
		if (Tool.IsEmpty() || !Tool.IsDamageable() || !FMcItemRegistry::Get().GetTool(Tool.Item, Data) || GetHardness(State) <= 0.0f)
		{
			return 0;
		}
		return Data.DamagePerBlock;
	}

	// ---- Drops ----

	namespace
	{
		struct FContext
		{
			FMcStateId State;
			const FMcItemStack& Tool;
			FRandomStream& Random;
			int32 Fortune;
			bool bSilk;
			bool bShear;
		};

		struct FCandidate
		{
			FName Item;
			int32 Weight = 1;
			const TArray<TSharedPtr<FJsonValue>>* Modifiers = nullptr;
			TSharedPtr<FJsonValue> Single;
		};

		double ReadNumber(const TSharedPtr<FJsonValue>& Value, FRandomStream& Random, double Default)
		{
			if (!Value.IsValid())
			{
				return Default;
			}
			double Number = 0.0;
			if (Value->TryGetNumber(Number))
			{
				return Number;
			}
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Value->TryGetObject(Object))
			{
				return Default;
			}
			const FString Type = (*Object)->GetStringField(TEXT("type"));
			if (Type.EndsWith(TEXT("uniform")))
			{
				const double Min = ReadNumber((*Object)->TryGetField(TEXT("min")), Random, 0.0);
				const double Max = ReadNumber((*Object)->TryGetField(TEXT("max")), Random, 0.0);
				if (FMath::FloorToDouble(Min) == Min && FMath::FloorToDouble(Max) == Max)
				{
					return Random.RandRange(static_cast<int32>(Min), static_cast<int32>(Max));
				}
				return Min + Random.FRand() * (Max - Min);
			}
			if (Type.EndsWith(TEXT("constant")))
			{
				return ReadNumber((*Object)->TryGetField(TEXT("value")), Random, Default);
			}
			if (Type.EndsWith(TEXT("binomial")))
			{
				const int32 N = static_cast<int32>(ReadNumber((*Object)->TryGetField(TEXT("n")), Random, 0.0));
				const double P = ReadNumber((*Object)->TryGetField(TEXT("p")), Random, 0.0);
				int32 Hits = 0;
				for (int32 Trial = 0; Trial < N; ++Trial)
				{
					Hits += Random.FRand() < P ? 1 : 0;
				}
				return Hits;
			}
			return Default;
		}

		bool EvaluateCondition(const TSharedPtr<FJsonValue>& Condition, FContext& Context);

		bool MatchesBlock(const TSharedPtr<FJsonObject>& Object, const FContext& Context)
		{
			const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
			const FName Name = NameOf(Context.State);
			FString Blocks;
			TArray<FString> Names;
			if (Object->TryGetStringField(TEXT("blocks"), Blocks))
			{
				Names.Add(Blocks);
			}
			else
			{
				Object->TryGetStringArrayField(TEXT("blocks"), Names);
			}
			if (!Names.IsEmpty() && !Names.Contains(Name.ToString()))
			{
				return false;
			}
			const TSharedPtr<FJsonObject>* Properties = nullptr;
			if (Object->TryGetObjectField(TEXT("state"), Properties))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>> Pair : (*Properties)->Values)
				{
					FString Want;
					if (Pair.Value->TryGetString(Want) && Registry.GetPropertyValue(Context.State, FName(*Pair.Key)) != FName(*Want))
					{
						return false;
					}
				}
			}
			return true;
		}

		bool EvaluateCondition(const TSharedPtr<FJsonValue>& Condition, FContext& Context)
		{
			if (!Condition.IsValid())
			{
				return true;
			}
			FString Id;
			TSharedPtr<FJsonObject> Object;
			const TSharedPtr<FJsonObject>* ObjectPtr = nullptr;
			if (Condition->TryGetString(Id))
			{
			}
			else if (Condition->TryGetObject(ObjectPtr))
			{
				Object = *ObjectPtr;
				Id = Object->GetStringField(TEXT("type"));
			}
			if (Id == TEXT("minecraft:tool/can_silk_touch")) return Context.bSilk;
			if (Id == TEXT("minecraft:tool/can_shear")) return Context.bShear;
			if (!Object.IsValid())
			{
				return true;
			}
			if (Id == TEXT("minecraft:any_of") || Id == TEXT("minecraft:all_of"))
			{
				const bool bAny = Id == TEXT("minecraft:any_of");
				const TArray<TSharedPtr<FJsonValue>>* Terms = nullptr;
				if (Object->TryGetArrayField(TEXT("terms"), Terms))
				{
					for (const TSharedPtr<FJsonValue>& Term : *Terms)
					{
						if (EvaluateCondition(Term, Context) == bAny)
						{
							return bAny;
						}
					}
				}
				return !bAny;
			}
			if (Id == TEXT("minecraft:inverted")) return !EvaluateCondition(Object->TryGetField(TEXT("term")), Context);
			if (Id == TEXT("minecraft:match_block")) return MatchesBlock(Object, Context);
			if (Id == TEXT("minecraft:random_chance"))
			{
				return Context.Random.FRand() < ReadNumber(Object->TryGetField(TEXT("chance")), Context.Random, 1.0);
			}
			if (Id == TEXT("minecraft:table_bonus"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Chances = nullptr;
				if (Object->TryGetArrayField(TEXT("chances"), Chances) && !Chances->IsEmpty())
				{
					const int32 Index = FMath::Min(Context.Fortune, Chances->Num() - 1);
					return Context.Random.FRand() < (*Chances)[Index]->AsNumber();
				}
			}
			if (Id == TEXT("minecraft:match_tool"))
			{
				const TSharedPtr<FJsonObject>* Predicate = nullptr;
				FString Items;
				if (Object->TryGetObjectField(TEXT("predicate"), Predicate) && (*Predicate)->TryGetStringField(TEXT("items"), Items))
				{
					return !Context.Tool.IsEmpty() && FMcItemRegistry::Get().Matches(Context.Tool.Item, Items);
				}
			}
			// Survives explosion, location and the like: nothing here asks them.
			return true;
		}

		void ApplyModifier(const TSharedPtr<FJsonObject>& Modifier, FMcItemStack& Stack, FContext& Context)
		{
			if (!EvaluateCondition(Modifier->TryGetField(TEXT("condition")), Context))
			{
				return;
			}
			const FString Type = Modifier->GetStringField(TEXT("type"));
			if (Type == TEXT("minecraft:set_count"))
			{
				const int32 Count = FMath::FloorToInt32(ReadNumber(Modifier->TryGetField(TEXT("count")), Context.Random, 1.0));
				bool bAdd = false;
				Modifier->TryGetBoolField(TEXT("add"), bAdd);
				Stack.Count = bAdd ? Stack.Count + Count : Count;
			}
			else if (Type == TEXT("minecraft:apply_bonus"))
			{
				const FString Formula = Modifier->GetStringField(TEXT("formula"));
				if (Formula == TEXT("minecraft:ore_drops") && Context.Fortune > 0)
				{
					const int32 Bonus = Context.Random.RandRange(0, Context.Fortune + 1) - 1;
					Stack.Count *= 1 + FMath::Max(0, Bonus);
				}
				else if (Formula == TEXT("minecraft:binomial_with_bonus_count"))
				{
					const TSharedPtr<FJsonObject>* Parameters = nullptr;
					if (Modifier->TryGetObjectField(TEXT("parameters"), Parameters))
					{
						const int32 Extra = static_cast<int32>(ReadNumber((*Parameters)->TryGetField(TEXT("extra")), Context.Random, 0.0));
						const double Probability = ReadNumber((*Parameters)->TryGetField(TEXT("probability")), Context.Random, 0.5);
						for (int32 Trial = 0; Trial < Context.Fortune + Extra; ++Trial)
						{
							Stack.Count += Context.Random.FRand() < Probability ? 1 : 0;
						}
					}
				}
				else if (Formula == TEXT("minecraft:uniform_bonus_count") && Context.Fortune > 0)
				{
					const TSharedPtr<FJsonObject>* Parameters = nullptr;
					const int32 Multiplier = Modifier->TryGetObjectField(TEXT("parameters"), Parameters)
						? static_cast<int32>(ReadNumber((*Parameters)->TryGetField(TEXT("bonusMultiplier")), Context.Random, 1.0)) : 1;
					Stack.Count += Context.Random.RandRange(0, Context.Fortune * Multiplier);
				}
			}
		}

		// Collects the leaf entries an entry gives; false if its condition fails (or, for sequences and alternatives,
		// nothing expanded).
		bool Expand(const TSharedPtr<FJsonObject>& Entry, FContext& Context, TArray<FCandidate>& Out)
		{
			if (!EvaluateCondition(Entry->TryGetField(TEXT("condition")), Context))
			{
				return false;
			}
			const FString Type = Entry->GetStringField(TEXT("type"));
			const TArray<TSharedPtr<FJsonValue>>* Children = nullptr;
			if (Type == TEXT("minecraft:alternatives") && Entry->TryGetArrayField(TEXT("children"), Children))
			{
				for (const TSharedPtr<FJsonValue>& Child : *Children)
				{
					if (Expand(Child->AsObject(), Context, Out))
					{
						return true;
					}
				}
				return false;
			}
			if ((Type == TEXT("minecraft:group") || Type == TEXT("minecraft:sequence")) && Entry->TryGetArrayField(TEXT("children"), Children))
			{
				for (const TSharedPtr<FJsonValue>& Child : *Children)
				{
					if (!Expand(Child->AsObject(), Context, Out) && Type == TEXT("minecraft:sequence"))
					{
						return false;
					}
				}
				return true;
			}
			if (Type == TEXT("minecraft:item"))
			{
				FCandidate Candidate;
				Candidate.Item = FName(*Entry->GetStringField(TEXT("name")));
				int32 Weight = 1;
				Entry->TryGetNumberField(TEXT("weight"), Weight);
				Candidate.Weight = FMath::Max(1, Weight);
				if (Entry->TryGetArrayField(TEXT("modifier"), Candidate.Modifiers) == false)
				{
					Candidate.Single = Entry->TryGetField(TEXT("modifier"));
				}
				Out.Add(Candidate);
				return true;
			}
			// Empty, nested tables and the rest give nothing here.
			return true;
		}

		TSharedPtr<FJsonObject> LoadTable(FName Block)
		{
			static TMap<FName, TSharedPtr<FJsonObject>> Cache;
			if (const TSharedPtr<FJsonObject>* Found = Cache.Find(Block))
			{
				return *Found;
			}
			const FString Path = McCache::Data(TEXT("loot_table/blocks/") + PathOfName(Block) + TEXT(".json"));
			const TSharedPtr<FJsonObject> Table = FPaths::FileExists(Path) ? McCache::LoadJsonObject(Path) : nullptr;
			Cache.Add(Block, Table);
			return Table;
		}
	}

	void GetDrops(FMcStateId State, const FMcItemStack& Tool, FRandomStream& Random, TArray<FMcItemStack>& Out)
	{
		if (!CanHarvest(Tool, State))
		{
			return;
		}
		const TSharedPtr<FJsonObject> Table = LoadTable(NameOf(State));
		if (!Table.IsValid())
		{
			return;
		}
		FContext Context{ State, Tool, Random, Tool.GetEnchantmentLevel(TEXT("minecraft:fortune")), Tool.GetEnchantmentLevel(TEXT("minecraft:silk_touch")) > 0,
			!Tool.IsEmpty() && Tool.Item == TEXT("minecraft:shears") };
		const TArray<TSharedPtr<FJsonValue>>* Pools = nullptr;
		if (!Table->TryGetArrayField(TEXT("pools"), Pools))
		{
			return;
		}
		// Modifiers on the whole table apply to everything it gives.
		TArray<TSharedPtr<FJsonValue>> TableModifiers;
		const TArray<TSharedPtr<FJsonValue>>* TableList = nullptr;
		if (Table->TryGetArrayField(TEXT("modifier"), TableList))
		{
			TableModifiers = *TableList;
		}
		else if (Table->HasField(TEXT("modifier")))
		{
			TableModifiers.Add(Table->TryGetField(TEXT("modifier")));
		}

		for (const TSharedPtr<FJsonValue>& PoolValue : *Pools)
		{
			const TSharedPtr<FJsonObject> Pool = PoolValue->AsObject();
			if (!EvaluateCondition(Pool->TryGetField(TEXT("condition")), Context))
			{
				continue;
			}
			const int32 Rolls = FMath::FloorToInt32(ReadNumber(Pool->TryGetField(TEXT("rolls")), Random, 1.0));
			for (int32 Roll = 0; Roll < Rolls; ++Roll)
			{
				TArray<FCandidate> Candidates;
				const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
				if (Pool->TryGetArrayField(TEXT("entries"), Entries))
				{
					for (const TSharedPtr<FJsonValue>& Entry : *Entries)
					{
						Expand(Entry->AsObject(), Context, Candidates);
					}
				}
				if (Candidates.IsEmpty())
				{
					continue;
				}
				int32 Total = 0;
				for (const FCandidate& Candidate : Candidates)
				{
					Total += Candidate.Weight;
				}
				int32 Pick = Random.RandRange(0, Total - 1);
				const FCandidate* Chosen = &Candidates[0];
				for (const FCandidate& Candidate : Candidates)
				{
					if (Pick < Candidate.Weight)
					{
						Chosen = &Candidate;
						break;
					}
					Pick -= Candidate.Weight;
				}
				FMcItemStack Stack(Chosen->Item, 1);
				TArray<TSharedPtr<FJsonValue>> Modifiers;
				if (Chosen->Modifiers)
				{
					Modifiers = *Chosen->Modifiers;
				}
				else if (Chosen->Single.IsValid())
				{
					Modifiers.Add(Chosen->Single);
				}
				Modifiers.Append(TableModifiers);
				for (const TSharedPtr<FJsonValue>& Modifier : Modifiers)
				{
					if (const TSharedPtr<FJsonObject> Object = Modifier->AsObject())
					{
						ApplyModifier(Object, Stack, Context);
					}
				}
				if (!Stack.IsEmpty() && FMcItemRegistry::Get().Exists(Stack.Item))
				{
					Out.Add(Stack);
				}
			}
		}
	}

	// ---- Items and blocks ----

	TOptional<FMcStateId> GetBlockForItem(FName Item)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		static const TMap<FName, FName> Special = {
			{ TEXT("minecraft:redstone"), TEXT("minecraft:redstone_wire") }, { TEXT("minecraft:wheat_seeds"), TEXT("minecraft:wheat") },
			{ TEXT("minecraft:potato"), TEXT("minecraft:potatoes") }, { TEXT("minecraft:carrot"), TEXT("minecraft:carrots") },
			{ TEXT("minecraft:beetroot_seeds"), TEXT("minecraft:beetroots") }, { TEXT("minecraft:melon_seeds"), TEXT("minecraft:melon_stem") },
			{ TEXT("minecraft:pumpkin_seeds"), TEXT("minecraft:pumpkin_stem") }, { TEXT("minecraft:string"), TEXT("minecraft:tripwire") },
		};
		if (const FName* Mapped = Special.Find(Item))
		{
			return Registry.FindDefaultState(*Mapped);
		}
		return Registry.FindDefaultState(Item);
	}

	bool IsBlockOfItem(FMcStateId State, FName Item)
	{
		const FName Block = NameOf(State);
		if (Block.IsNone())
		{
			return false;
		}
		if (Block == Item)
		{
			return true;
		}
		// A torch or a sign placed on a wall is another block.
		FString Path = PathOfName(Block);
		Path.ReplaceInline(TEXT("wall_"), TEXT(""));
		Path.ReplaceInline(TEXT("_wall_"), TEXT("_"));
		if (FName(*(TEXT("minecraft:") + Path)) == Item)
		{
			return true;
		}
		const TOptional<FMcStateId> Placed = GetBlockForItem(Item);
		return Placed && NameOf(Placed.GetValue()) == Block;
	}

	FName GetItemForBlock(FMcStateId State)
	{
		const FName Block = NameOf(State);
		if (Block.IsNone())
		{
			return NAME_None;
		}
		FString Path = PathOfName(Block);
		Path.ReplaceInline(TEXT("wall_"), TEXT(""));
		const FName Item(*(TEXT("minecraft:") + Path));
		return FMcItemRegistry::Get().Exists(Item) ? Item : (FMcItemRegistry::Get().Exists(Block) ? Block : NAME_None);
	}

	int32 GetXpDropped(FMcStateId State, FRandomStream& Random)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		if (!Registry.IsValidState(State))
		{
			return 0;
		}
		const FString Name = Registry.GetBlockOf(State).Name.ToString();
		const auto Range = [&](int32 Min, int32 Max) { return Random.RandRange(Min, Max); };
		if (Name.EndsWith(TEXT("coal_ore")))
		{
			return Range(0, 2);
		}
		if (Name.EndsWith(TEXT("diamond_ore")) || Name.EndsWith(TEXT("emerald_ore")))
		{
			return Range(3, 7);
		}
		if (Name.EndsWith(TEXT("lapis_ore")) || Name == TEXT("minecraft:nether_quartz_ore"))
		{
			return Range(2, 5);
		}
		if (Name.EndsWith(TEXT("redstone_ore")))
		{
			return Range(1, 5);
		}
		if (Name == TEXT("minecraft:nether_gold_ore"))
		{
			return Range(0, 1);
		}
		if (Name == TEXT("minecraft:spawner"))
		{
			return Range(15, 43);
		}
		return 0;
	}
}
