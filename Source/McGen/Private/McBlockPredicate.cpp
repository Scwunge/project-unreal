#include "McBlockPredicate.h"
#include "Dom/JsonObject.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McGen.h"
#include "McGenRegion.h"
#include "McRandom.h"

TOptional<FMcStateId> McParseBlockState(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid())
	{
		return {};
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (Json->Type == EJson::String)
	{
		return Registry.ParseState(Json->AsString());
	}
	const TSharedPtr<FJsonObject> Object = Json->AsObject();
	if (!Object.IsValid())
	{
		return {};
	}
	FString Name;
	if (!Object->TryGetStringField(TEXT("id"), Name) && !Object->TryGetStringField(TEXT("Name"), Name))
	{
		return {};
	}
	TMap<FName, FName> Properties;
	const TSharedPtr<FJsonObject>* PropertyObject;
	if (Object->TryGetObjectField(TEXT("properties"), PropertyObject) || Object->TryGetObjectField(TEXT("Properties"), PropertyObject))
	{
		for (const auto& Entry : (*PropertyObject)->Values)
		{
			Properties.Add(FName(*Entry.Key), FName(*Entry.Value->AsString()));
		}
	}
	return Registry.FindState(FName(*McCache::Namespaced(Name)), Properties);
}

FIntVector McParseOffset(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid() || Json->Type != EJson::Array || Json->AsArray().Num() != 3)
	{
		return FIntVector::ZeroValue;
	}
	const TArray<TSharedPtr<FJsonValue>>& Values = Json->AsArray();
	return FIntVector(static_cast<int32>(Values[0]->AsNumber()), static_cast<int32>(Values[1]->AsNumber()), static_cast<int32>(Values[2]->AsNumber()));
}

namespace
{
	FString TypeOf(const FJsonObject& Json, const TCHAR* Field = TEXT("type"))
	{
		return McCache::Namespaced(Json.GetStringField(Field));
	}

	// A name or a list of names, e.g. "blocks" or "fluids".
	TArray<FString> ReadNames(const TSharedPtr<FJsonValue>& Json)
	{
		TArray<FString> Names;
		if (!Json.IsValid())
		{
			return Names;
		}
		if (Json->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Value : Json->AsArray())
			{
				Names.Add(McCache::Namespaced(Value->AsString()));
			}
		}
		else if (Json->Type == EJson::String)
		{
			Names.Add(McCache::Namespaced(Json->AsString()));
		}
		return Names;
	}

	// Block indices for names; a "#tag" adds the tag's blocks.
	TSet<int32> ReadBlocks(const TSharedPtr<FJsonValue>& Json)
	{
		TSet<int32> Blocks;
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcBlockTags& Tags = FMcBlockTags::Get();
		for (const FString& Name : ReadNames(Json))
		{
			if (Name.StartsWith(TEXT("#")) || Name.Contains(TEXT(":#")))
			{
				const int32 Tag = Tags.Find(Name.Replace(TEXT("minecraft:#"), TEXT("")));
				for (int32 Block = 0; Block < Registry.NumBlocks(); ++Block)
				{
					if (Tags.HasBlock(Tag, Block))
					{
						Blocks.Add(Block);
					}
				}
			}
			else if (const FMcBlock* Block = Registry.FindBlock(FName(*Name)))
			{
				Blocks.Add(Block->Index);
			}
		}
		return Blocks;
	}

	int32 BlockOf(FMcStateId State)
	{
		return FMcBlockRegistry::Get().GetState(State).Block;
	}

	FName BlockName(FMcStateId State)
	{
		return FMcBlockRegistry::Get().GetBlockOf(State).Name;
	}

	bool IsFullCube(FMcStateId State)
	{
		const TConstArrayView<FBox3f> Boxes = FMcBlockShapes::Get().GetCollision(State);
		for (const FBox3f& Box : Boxes)
		{
			if (Box.Min.GetMax() <= 0.0f && Box.Max.GetMin() >= 1.0f)
			{
				return true;
			}
		}
		return false;
	}

	// Whether the face of State pointing along Direction is a full square.
	bool HasSturdyFace(FMcStateId State, const FIntVector& Direction)
	{
		for (const FBox3f& Box : FMcBlockShapes::Get().GetCollision(State))
		{
			const int32 Axis = Direction.X != 0 ? 0 : (Direction.Y != 0 ? 1 : 2);
			const int32 Sign = Direction.X + Direction.Y + Direction.Z;
			const bool bTouches = Sign > 0 ? Box.Max[Axis] >= 1.0f : Box.Min[Axis] <= 0.0f;
			bool bCovers = true;
			for (int32 Other = 0; Other < 3; ++Other)
			{
				if (Other != Axis)
				{
					bCovers &= Box.Min[Other] <= 0.0f && Box.Max[Other] >= 1.0f;
				}
			}
			if (bTouches && bCovers)
			{
				return true;
			}
		}
		return false;
	}

	FIntVector ParseDirection(const FString& Name)
	{
		if (Name == TEXT("up")) return FIntVector(0, 1, 0);
		if (Name == TEXT("down")) return FIntVector(0, -1, 0);
		if (Name == TEXT("north")) return FIntVector(0, 0, -1);
		if (Name == TEXT("south")) return FIntVector(0, 0, 1);
		if (Name == TEXT("west")) return FIntVector(-1, 0, 0);
		return FIntVector(1, 0, 0);
	}

	class FTrue final : public FMcBlockPredicate
	{
	public:
		virtual bool Test(FMcGenRegion&, const FIntVector&) const override { return true; }
	};

	class FMatchingBlocks final : public FMcBlockPredicate
	{
	public:
		TSet<int32> Blocks;
		FIntVector Offset;
		virtual bool Test(FMcGenRegion& Region, const FIntVector& Pos) const override
		{
			return Blocks.Contains(BlockOf(Region.GetBlock(Pos + Offset)));
		}
	};

	class FMatchingTag final : public FMcBlockPredicate
	{
	public:
		int32 Tag = INDEX_NONE;
		FIntVector Offset;
		virtual bool Test(FMcGenRegion& Region, const FIntVector& Pos) const override
		{
			return FMcBlockTags::Get().Has(Tag, Region.GetBlock(Pos + Offset));
		}
	};

	class FMatchingFluids final : public FMcBlockPredicate
	{
	public:
		bool bWater = false;
		bool bFlowingWater = false;
		bool bLava = false;
		bool bFlowingLava = false;
		FIntVector Offset;
		virtual bool Test(FMcGenRegion& Region, const FIntVector& Pos) const override
		{
			const FMcStateId State = Region.GetBlock(Pos + Offset);
			const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
			const bool bSource = Shapes.GetFluidLevel(State) == 0;
			switch (Shapes.GetFluid(State))
			{
			case EMcFluid::Water:
				return bSource ? bWater : bFlowingWater;
			case EMcFluid::Lava:
				return bSource ? bLava : bFlowingLava;
			default:
				return false;
			}
		}
	};

	class FSolid final : public FMcBlockPredicate
	{
	public:
		FIntVector Offset;
		virtual bool Test(FMcGenRegion& Region, const FIntVector& Pos) const override
		{
			const FMcStateId State = Region.GetBlock(Pos + Offset);
			return FMcBlockShapes::Get().HasCollision(State) && !FMcBlockTags::Get().Has(TEXT("minecraft:replaceable"), State);
		}
	};

	class FReplaceable final : public FMcBlockPredicate
	{
	public:
		int32 Tag = INDEX_NONE;
		FIntVector Offset;
		virtual bool Test(FMcGenRegion& Region, const FIntVector& Pos) const override
		{
			const FMcStateId State = Region.GetBlock(Pos + Offset);
			return Region.IsAir(State) || FMcBlockTags::Get().Has(Tag, State);
		}
	};

	class FWouldSurvive final : public FMcBlockPredicate
	{
	public:
		FMcStateId State = 0;
		FIntVector Offset;
		virtual bool Test(FMcGenRegion& Region, const FIntVector& Pos) const override
		{
			return FMcBlockPredicate::CanSurvive(Region, Pos + Offset, State);
		}
	};

	class FSturdyFace final : public FMcBlockPredicate
	{
	public:
		FIntVector Direction;
		FIntVector Offset;
		virtual bool Test(FMcGenRegion& Region, const FIntVector& Pos) const override
		{
			return HasSturdyFace(Region.GetBlock(Pos + Offset), Direction);
		}
	};

	class FInsideWorld final : public FMcBlockPredicate
	{
	public:
		FIntVector Offset;
		virtual bool Test(FMcGenRegion& Region, const FIntVector& Pos) const override
		{
			const int32 Y = Pos.Y + Offset.Y;
			return Y >= Region.GetMinY() && Y <= Region.GetMaxY();
		}
	};

	class FHeightRange final : public FMcBlockPredicate
	{
	public:
		int32 Min = 0;
		int32 Max = 0;
		virtual bool Test(FMcGenRegion&, const FIntVector& Pos) const override
		{
			return Pos.Y >= Min && Pos.Y <= Max;
		}
	};

	class FVolumeMatch final : public FMcBlockPredicate
	{
	public:
		TSharedPtr<const FMcBlockPredicate> Match;
		FIntVector Min;
		FIntVector Max;
		virtual bool Test(FMcGenRegion& Region, const FIntVector& Pos) const override
		{
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
				{
					for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
					{
						if (!Match->Test(Region, Pos + FIntVector(X, Y, Z)))
						{
							return false;
						}
					}
				}
			}
			return true;
		}
	};

	class FCombine final : public FMcBlockPredicate
	{
	public:
		TArray<TSharedPtr<const FMcBlockPredicate>> Parts;
		bool bAll = true;
		virtual bool Test(FMcGenRegion& Region, const FIntVector& Pos) const override
		{
			for (const TSharedPtr<const FMcBlockPredicate>& Part : Parts)
			{
				if (Part->Test(Region, Pos) != bAll)
				{
					return !bAll;
				}
			}
			return bAll;
		}
	};

	class FNot final : public FMcBlockPredicate
	{
	public:
		TSharedPtr<const FMcBlockPredicate> Inner;
		virtual bool Test(FMcGenRegion& Region, const FIntVector& Pos) const override
		{
			return !Inner->Test(Region, Pos);
		}
	};

	int32 ResolveAnchor(const TSharedPtr<FJsonValue>& Json, int32 SeaLevel, int32 Fallback)
	{
		const TSharedPtr<FJsonObject> Object = Json.IsValid() ? Json->AsObject() : nullptr;
		int32 Value = 0;
		if (!Object.IsValid())
		{
			return Fallback;
		}
		if (Object->TryGetNumberField(TEXT("absolute"), Value)) return Value;
		if (Object->TryGetNumberField(TEXT("above_bottom"), Value)) return -64 + Value;
		if (Object->TryGetNumberField(TEXT("below_top"), Value)) return 319 - Value;
		if (Object->TryGetNumberField(TEXT("relative_to_sea_level"), Value)) return SeaLevel + Value;
		return Fallback;
	}
}

bool McHasSturdyFace(FMcStateId State, const FIntVector& Direction)
{
	return HasSturdyFace(State, Direction);
}

bool McIsSolid(FMcStateId State)
{
	return FMcBlockShapes::Get().HasCollision(State) && !FMcBlockTags::Get().Has(TEXT("minecraft:replaceable"), State);
}

TSharedPtr<const FMcBlockPredicate> FMcBlockPredicate::Parse(const TSharedPtr<FJsonValue>& Json, int32 SeaLevel)
{
	const TSharedPtr<FJsonObject> Object = Json.IsValid() ? Json->AsObject() : nullptr;
	if (!Object.IsValid())
	{
		UE_LOG(LogMcGen, Error, TEXT("Block predicate isn't an object"));
		return nullptr;
	}
	const FString Type = TypeOf(*Object);
	const FIntVector Offset = McParseOffset(Object->TryGetField(TEXT("offset")));
	if (Type == TEXT("minecraft:true"))
	{
		return MakeShared<FTrue>();
	}
	if (Type == TEXT("minecraft:matching_blocks"))
	{
		TSharedRef<FMatchingBlocks> Result = MakeShared<FMatchingBlocks>();
		Result->Blocks = ReadBlocks(Object->TryGetField(TEXT("blocks")));
		Result->Offset = Offset;
		return Result;
	}
	if (Type == TEXT("minecraft:matching_block_tag"))
	{
		TSharedRef<FMatchingTag> Result = MakeShared<FMatchingTag>();
		Result->Tag = FMcBlockTags::Get().Find(Object->GetStringField(TEXT("tag")));
		Result->Offset = Offset;
		return Result;
	}
	if (Type == TEXT("minecraft:matching_fluids"))
	{
		TSharedRef<FMatchingFluids> Result = MakeShared<FMatchingFluids>();
		for (const FString& Name : ReadNames(Object->TryGetField(TEXT("fluids"))))
		{
			Result->bWater |= Name == TEXT("minecraft:water");
			Result->bFlowingWater |= Name == TEXT("minecraft:flowing_water");
			Result->bLava |= Name == TEXT("minecraft:lava");
			Result->bFlowingLava |= Name == TEXT("minecraft:flowing_lava");
		}
		Result->Offset = Offset;
		return Result;
	}
	if (Type == TEXT("minecraft:solid"))
	{
		TSharedRef<FSolid> Result = MakeShared<FSolid>();
		Result->Offset = Offset;
		return Result;
	}
	if (Type == TEXT("minecraft:replaceable"))
	{
		TSharedRef<FReplaceable> Result = MakeShared<FReplaceable>();
		Result->Tag = FMcBlockTags::Get().Find(TEXT("minecraft:replaceable"));
		Result->Offset = Offset;
		return Result;
	}
	if (Type == TEXT("minecraft:would_survive"))
	{
		const TOptional<FMcStateId> State = McParseBlockState(Object->TryGetField(TEXT("state")));
		if (!State.IsSet())
		{
			UE_LOG(LogMcGen, Error, TEXT("would_survive: unknown state"));
			return nullptr;
		}
		TSharedRef<FWouldSurvive> Result = MakeShared<FWouldSurvive>();
		Result->State = State.GetValue();
		Result->Offset = Offset;
		return Result;
	}
	if (Type == TEXT("minecraft:has_sturdy_face"))
	{
		TSharedRef<FSturdyFace> Result = MakeShared<FSturdyFace>();
		Result->Direction = ParseDirection(Object->GetStringField(TEXT("direction")));
		Result->Offset = Offset;
		return Result;
	}
	if (Type == TEXT("minecraft:inside_world_bounds"))
	{
		TSharedRef<FInsideWorld> Result = MakeShared<FInsideWorld>();
		Result->Offset = Offset;
		return Result;
	}
	if (Type == TEXT("minecraft:height_range"))
	{
		TSharedRef<FHeightRange> Result = MakeShared<FHeightRange>();
		Result->Min = ResolveAnchor(Object->TryGetField(TEXT("min_inclusive")), SeaLevel, MIN_int32);
		Result->Max = ResolveAnchor(Object->TryGetField(TEXT("max_inclusive")), SeaLevel, MAX_int32);
		return Result;
	}
	if (Type == TEXT("minecraft:volume_match"))
	{
		TSharedRef<FVolumeMatch> Result = MakeShared<FVolumeMatch>();
		Result->Match = Parse(Object->TryGetField(TEXT("match")), SeaLevel);
		Result->Min = McParseOffset(Object->TryGetField(TEXT("min")));
		Result->Max = McParseOffset(Object->TryGetField(TEXT("max")));
		return Result->Match.IsValid() ? Result : TSharedPtr<const FMcBlockPredicate>();
	}
	if (Type == TEXT("minecraft:all_of") || Type == TEXT("minecraft:any_of"))
	{
		TSharedRef<FCombine> Result = MakeShared<FCombine>();
		Result->bAll = Type == TEXT("minecraft:all_of");
		for (const TSharedPtr<FJsonValue>& Part : Object->GetArrayField(TEXT("predicates")))
		{
			TSharedPtr<const FMcBlockPredicate> Parsed = Parse(Part, SeaLevel);
			if (!Parsed.IsValid())
			{
				return nullptr;
			}
			Result->Parts.Add(Parsed);
		}
		return Result;
	}
	if (Type == TEXT("minecraft:not"))
	{
		TSharedRef<FNot> Result = MakeShared<FNot>();
		Result->Inner = Parse(Object->TryGetField(TEXT("predicate")), SeaLevel);
		return Result->Inner.IsValid() ? Result : TSharedPtr<const FMcBlockPredicate>();
	}
	UE_LOG(LogMcGen, Error, TEXT("Unknown block predicate %s"), *Type);
	return nullptr;
}

bool FMcBlockPredicate::CanSurvive(FMcGenRegion& Region, const FIntVector& Pos, FMcStateId State)
{
	// From minecraft.wiki's placement rules, with the jar's support tags.
	const FMcBlockTags& Tags = FMcBlockTags::Get();
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	const FString Name = BlockName(State).ToString();
	const FIntVector Down(0, -1, 0);
	const FMcStateId Below = Region.GetBlock(Pos + Down);
	const auto Supports = [&Tags, Below](const TCHAR* Tag) { return Tags.Has(Tag, Below); };
	const auto WaterBeside = [&Region, &Shapes](const FIntVector& At)
	{
		for (const FIntVector Side : { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) })
		{
			const FMcStateId Next = Region.GetBlock(At + Side);
			if (Shapes.GetFluid(Next) == EMcFluid::Water || BlockName(Next) == TEXT("minecraft:frosted_ice"))
			{
				return true;
			}
		}
		return false;
	};

	if (Name == TEXT("minecraft:sugar_cane"))
	{
		return BlockName(Below) == TEXT("minecraft:sugar_cane")
			|| (Supports(TEXT("minecraft:supports_sugar_cane")) && (!Supports(TEXT("minecraft:supports_sugar_cane_adjacently")) || WaterBeside(Pos + Down)));
	}
	if (Name == TEXT("minecraft:cactus"))
	{
		for (const FIntVector Side : { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) })
		{
			const FMcStateId Next = Region.GetBlock(Pos + Side);
			if (Shapes.HasCollision(Next) || Shapes.GetFluid(Next) == EMcFluid::Lava)
			{
				return false;
			}
		}
		return Supports(TEXT("minecraft:supports_cactus"));
	}
	if (Name == TEXT("minecraft:bamboo") || Name == TEXT("minecraft:bamboo_sapling"))
	{
		return Supports(TEXT("minecraft:supports_bamboo"));
	}
	if (Name == TEXT("minecraft:lily_pad"))
	{
		// Resting on water (a still source) or on ice; the tag lists only the ice.
		return ((Shapes.GetFluid(Below) == EMcFluid::Water && Shapes.GetFluidLevel(Below) == 0) || Supports(TEXT("minecraft:supports_lily_pad"))) && Shapes.GetFluid(Region.GetBlock(Pos)) == EMcFluid::None;
	}
	if (Name == TEXT("minecraft:kelp") || Name == TEXT("minecraft:kelp_plant") || Name == TEXT("minecraft:seagrass") || Name == TEXT("minecraft:tall_seagrass"))
	{
		const bool bKelp = Name.StartsWith(TEXT("minecraft:kelp"));
		return Shapes.GetFluid(Region.GetBlock(Pos)) == EMcFluid::Water
			&& ((bKelp && Name == TEXT("minecraft:kelp") && BlockName(Below).ToString().StartsWith(TEXT("minecraft:kelp")))
				|| (HasSturdyFace(Below, FIntVector(0, 1, 0)) && !Supports(bKelp ? TEXT("minecraft:cannot_support_kelp") : TEXT("minecraft:cannot_support_seagrass"))));
	}
	if (Name == TEXT("minecraft:snow"))
	{
		if (Supports(TEXT("minecraft:cannot_support_snow_layer")))
		{
			return false;
		}
		return Supports(TEXT("minecraft:support_override_snow_layer")) || HasSturdyFace(Below, FIntVector(0, 1, 0));
	}
	if (Name.Contains(TEXT("mushroom")) && !Name.Contains(TEXT("block")) && !Name.Contains(TEXT("stem")))
	{
		return Supports(TEXT("minecraft:overrides_mushroom_light_requirement")) || IsFullCube(Below);
	}
	if (Name == TEXT("minecraft:dead_bush") || Name.Contains(TEXT("dry_")) || Name == TEXT("minecraft:cactus_flower"))
	{
		return Supports(TEXT("minecraft:supports_dry_vegetation")) || Supports(TEXT("minecraft:supports_vegetation"));
	}
	if (Name == TEXT("minecraft:azalea") || Name == TEXT("minecraft:flowering_azalea"))
	{
		return Supports(TEXT("minecraft:supports_azalea"));
	}
	if (Name == TEXT("minecraft:spore_blossom"))
	{
		// Hangs from a ceiling with a sturdy underside (it is in the flowers tag, which would ask for ground).
		return HasSturdyFace(Region.GetBlock(Pos + FIntVector(0, 1, 0)), FIntVector(0, -1, 0));
	}
	if (Name == TEXT("minecraft:mangrove_propagule"))
	{
		return Supports(TEXT("minecraft:supports_mangrove_propagule"));
	}
	if (Name == TEXT("minecraft:small_dripleaf"))
	{
		return Supports(TEXT("minecraft:supports_small_dripleaf"));
	}
	if (Name == TEXT("minecraft:big_dripleaf"))
	{
		return Supports(TEXT("minecraft:supports_big_dripleaf"));
	}
	if (Name == TEXT("minecraft:wither_rose"))
	{
		return Supports(TEXT("minecraft:supports_wither_rose"));
	}
	if (Name == TEXT("minecraft:crimson_fungus") || Name == TEXT("minecraft:warped_fungus"))
	{
		return Supports(Name == TEXT("minecraft:crimson_fungus") ? TEXT("minecraft:supports_crimson_fungus") : TEXT("minecraft:supports_warped_fungus"));
	}
	if (Name == TEXT("minecraft:crimson_roots") || Name == TEXT("minecraft:warped_roots") || Name == TEXT("minecraft:nether_sprouts") || Name == TEXT("minecraft:nether_wart"))
	{
		// The jar's support tags (supports_crimson_roots and so on): roots need nylium or soil, not bare netherrack.
		return Supports(Name == TEXT("minecraft:crimson_roots") ? TEXT("minecraft:supports_crimson_roots") : Name == TEXT("minecraft:warped_roots") ? TEXT("minecraft:supports_warped_roots") : Name == TEXT("minecraft:nether_sprouts") ? TEXT("minecraft:supports_nether_sprouts") : TEXT("minecraft:supports_nether_wart"));
	}
	// Saplings, flowers, grass, ferns and bushes stand on dirt-like blocks; tall plants need room above too.
	const bool bPlant = Tags.Has(TEXT("minecraft:saplings"), State) || Tags.Has(TEXT("minecraft:flowers"), State) || Name.Contains(TEXT("grass"))
		|| Name.Contains(TEXT("fern")) || Name.Contains(TEXT("bush")) || Name == TEXT("minecraft:sweet_berry_bush") || Name.Contains(TEXT("leaf_litter"))
		|| Name.Contains(TEXT("wildflowers")) || Name.Contains(TEXT("petals"));
	if (bPlant && Name != TEXT("minecraft:grass_block"))
	{
		const bool bLower = Region.GetBlock(Pos) == State || FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("half")) != TEXT("upper");
		return bLower ? Supports(TEXT("minecraft:supports_vegetation")) : BlockName(Below) == BlockName(State);
	}
	return true;
}

namespace
{
	class FAlwaysTrue final : public FMcRuleTest
	{
	public:
		virtual bool Test(FMcStateId, const FIntVector&, FMcRandom&) const override { return true; }
	};

	class FTagMatch final : public FMcRuleTest
	{
	public:
		int32 Tag = INDEX_NONE;
		virtual bool Test(FMcStateId State, const FIntVector&, FMcRandom&) const override { return FMcBlockTags::Get().Has(Tag, State); }
	};

	class FBlockMatch final : public FMcRuleTest
	{
	public:
		int32 Block = INDEX_NONE;
		double Probability = 1.0;
		virtual bool Test(FMcStateId State, const FIntVector&, FMcRandom& Random) const override
		{
			return BlockOf(State) == Block && (Probability >= 1.0 || Random.NextDouble() < Probability);
		}
	};

	class FStateMatch final : public FMcRuleTest
	{
	public:
		FMcStateId Match = 0;
		double Probability = 1.0;
		virtual bool Test(FMcStateId State, const FIntVector&, FMcRandom& Random) const override
		{
			return State == Match && (Probability >= 1.0 || Random.NextDouble() < Probability);
		}
	};

	class FHeightMatch final : public FMcRuleTest
	{
	public:
		int32 Min = MIN_int32;
		int32 Max = MAX_int32;
		virtual bool Test(FMcStateId, const FIntVector& Pos, FMcRandom&) const override { return Pos.Y >= Min && Pos.Y <= Max; }
	};

	class FRuleCombine final : public FMcRuleTest
	{
	public:
		TArray<TSharedPtr<const FMcRuleTest>> Parts;
		bool bAll = true;
		virtual bool Test(FMcStateId State, const FIntVector& Pos, FMcRandom& Random) const override
		{
			for (const TSharedPtr<const FMcRuleTest>& Part : Parts)
			{
				if (Part->Test(State, Pos, Random) != bAll)
				{
					return !bAll;
				}
			}
			return bAll;
		}
	};

	class FRuleNot final : public FMcRuleTest
	{
	public:
		TSharedPtr<const FMcRuleTest> Inner;
		virtual bool Test(FMcStateId State, const FIntVector& Pos, FMcRandom& Random) const override { return !Inner->Test(State, Pos, Random); }
	};
}

TSharedPtr<const FMcRuleTest> FMcRuleTest::Parse(const TSharedPtr<FJsonValue>& Json)
{
	const TSharedPtr<FJsonObject> Object = Json.IsValid() ? Json->AsObject() : nullptr;
	if (!Object.IsValid())
	{
		return nullptr;
	}
	const FString Type = TypeOf(*Object, TEXT("predicate_type"));
	if (Type == TEXT("minecraft:always_true"))
	{
		return MakeShared<FAlwaysTrue>();
	}
	if (Type == TEXT("minecraft:tag_match"))
	{
		TSharedRef<FTagMatch> Result = MakeShared<FTagMatch>();
		Result->Tag = FMcBlockTags::Get().Find(Object->GetStringField(TEXT("tag")));
		return Result;
	}
	if (Type == TEXT("minecraft:block_match") || Type == TEXT("minecraft:random_block_match"))
	{
		const FMcBlock* Block = FMcBlockRegistry::Get().FindBlock(FName(*McCache::Namespaced(Object->GetStringField(TEXT("block")))));
		if (!Block)
		{
			return nullptr;
		}
		TSharedRef<FBlockMatch> Result = MakeShared<FBlockMatch>();
		Result->Block = Block->Index;
		Object->TryGetNumberField(TEXT("probability"), Result->Probability);
		return Result;
	}
	if (Type == TEXT("minecraft:blockstate_match") || Type == TEXT("minecraft:random_blockstate_match"))
	{
		const TOptional<FMcStateId> State = McParseBlockState(Object->TryGetField(TEXT("block_state")));
		if (!State.IsSet())
		{
			return nullptr;
		}
		TSharedRef<FStateMatch> Result = MakeShared<FStateMatch>();
		Result->Match = State.GetValue();
		Object->TryGetNumberField(TEXT("probability"), Result->Probability);
		return Result;
	}
	if (Type == TEXT("minecraft:height_match"))
	{
		TSharedRef<FHeightMatch> Result = MakeShared<FHeightMatch>();
		Object->TryGetNumberField(TEXT("min_inclusive"), Result->Min);
		Object->TryGetNumberField(TEXT("max_inclusive"), Result->Max);
		return Result;
	}
	if (Type == TEXT("minecraft:all_of") || Type == TEXT("minecraft:any_of"))
	{
		TSharedRef<FRuleCombine> Result = MakeShared<FRuleCombine>();
		Result->bAll = Type == TEXT("minecraft:all_of");
		for (const TSharedPtr<FJsonValue>& Part : Object->GetArrayField(TEXT("rules")))
		{
			TSharedPtr<const FMcRuleTest> Parsed = Parse(Part);
			if (!Parsed.IsValid())
			{
				return nullptr;
			}
			Result->Parts.Add(Parsed);
		}
		return Result;
	}
	if (Type == TEXT("minecraft:not"))
	{
		TSharedRef<FRuleNot> Result = MakeShared<FRuleNot>();
		Result->Inner = Parse(Object->TryGetField(TEXT("rule")));
		return Result->Inner.IsValid() ? Result : TSharedPtr<const FMcRuleTest>();
	}
	UE_LOG(LogMcGen, Error, TEXT("Unknown rule test %s"), *Type);
	return nullptr;
}
