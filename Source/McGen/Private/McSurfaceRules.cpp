#include "McSurfaceRules.h"
#include "Dom/JsonObject.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "McDensityFunction.h"
#include "McGen.h"

namespace
{
	FString TypeOf(const FJsonObject& Json)
	{
		return McCache::Namespaced(Json.GetStringField(TEXT("type")));
	}

	// The terracotta of each band, before the per-seed shuffle; plain terracotta between the bands.
	const TCHAR* const BandColours[] = {
		TEXT("minecraft:orange_terracotta"), TEXT("minecraft:yellow_terracotta"), TEXT("minecraft:brown_terracotta"),
		TEXT("minecraft:red_terracotta"), TEXT("minecraft:white_terracotta"), TEXT("minecraft:light_gray_terracotta"),
	};
	constexpr int32 NumBands = 192;
}

bool FMcSurfaceRules::Load(const FString& Id, FMcDensityGraph& InGraph, uint64 Seed, int32 InMinY, int32 InHeight)
{
	Graph = &InGraph;
	WorldSeed = Seed;
	MinY = InMinY;
	Height = InHeight;
	SurfaceNoise = GetNoise(TEXT("minecraft:surface"));
	SecondaryNoise = GetNoise(TEXT("minecraft:surface_secondary"));
	BandOffsetNoise = GetNoise(TEXT("minecraft:clay_bands_offset"));

	// Badlands bands: our own pattern, runs of colour scattered over plain terracotta.
	const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	const FMcStateId Terracotta = Blocks.FindDefaultState(TEXT("minecraft:terracotta")).Get(0);
	Bands.Init(Terracotta, NumBands);
	FMcRandom Random = FMcRandom(Seed).Fork(TEXT("minecraft:clay_bands"));
	for (const TCHAR* Colour : BandColours)
	{
		const FMcStateId State = Blocks.FindDefaultState(Colour).Get(Terracotta);
		const int32 Runs = Random.NextIntBetween(2, 6);
		for (int32 Run = 0; Run < Runs; ++Run)
		{
			const int32 Start = Random.NextInt(NumBands);
			const int32 Length = Random.NextIntBetween(1, 3);
			for (int32 Offset = 0; Offset < Length; ++Offset)
			{
				Bands[(Start + Offset) % NumBands] = State;
			}
		}
	}

	Root = LoadNamed(TEXT("material_rule"), Id, NamedRules, true);
	if (Root == INDEX_NONE || bErrors)
	{
		UE_LOG(LogMcGen, Error, TEXT("Surface rule %s didn't load"), *Id);
		return false;
	}
	return true;
}

int32 FMcSurfaceRules::GetNoise(const FString& Id)
{
	const FString Key = McCache::Namespaced(Id);
	if (const int32* Found = NoiseIds.Find(Key))
	{
		return *Found;
	}
	FMcNoiseParams Params;
	if (!FMcNoiseParams::Load(Key, Params))
	{
		bErrors = true;
		return INDEX_NONE;
	}
	// Seeded like the density graph's noises, so a noise shared by both is the same noise.
	FMcRandom Random = FMcRandom(WorldSeed).Fork(Key);
	const int32 Index = Noises.Emplace(Random, Params);
	NoiseIds.Add(Key, Index);
	return Index;
}

int32 FMcSurfaceRules::LoadNamed(const FString& Folder, const FString& Id, TMap<FString, int32>& Named, bool bRule)
{
	const FString Key = McCache::Namespaced(Id);
	if (const int32* Found = Named.Find(Key))
	{
		return *Found;
	}
	const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(McCache::Data(TEXT("worldgen/") + Folder + TEXT("/") + McCache::PathOf(Key) + TEXT(".json")));
	if (!Json.IsValid())
	{
		bErrors = true;
		return INDEX_NONE;
	}
	const TSharedPtr<FJsonValue> Value = MakeShared<FJsonValueObject>(Json);
	const int32 Index = bRule ? ParseRule(Value) : ParseCondition(Value);
	Named.Add(Key, Index);
	return Index;
}

int32 FMcSurfaceRules::ParseAnchor(const TSharedPtr<FJsonObject>& Json) const
{
	int32 Value = 0;
	if (Json->TryGetNumberField(TEXT("absolute"), Value))
	{
		return Value;
	}
	if (Json->TryGetNumberField(TEXT("above_bottom"), Value))
	{
		return MinY + Value;
	}
	if (Json->TryGetNumberField(TEXT("below_top"), Value))
	{
		return MinY + Height - 1 - Value;
	}
	return 0;
}

TOptional<FMcStateId> FMcSurfaceRules::ParseState(const TSharedPtr<FJsonValue>& Json) const
{
	if (!Json.IsValid())
	{
		return {};
	}
	// "minecraft:stone", or { "Name": ..., "Properties": { ... } }.
	if (Json->Type == EJson::String)
	{
		return FMcBlockRegistry::Get().ParseState(Json->AsString());
	}
	if (Json->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Json->AsObject();
		TMap<FName, FName> Properties;
		const TSharedPtr<FJsonObject>* PropertyObject;
		if (Object->TryGetObjectField(TEXT("Properties"), PropertyObject))
		{
			for (const auto& Entry : (*PropertyObject)->Values)
			{
				Properties.Add(FName(*Entry.Key), FName(*Entry.Value->AsString()));
			}
		}
		return FMcBlockRegistry::Get().FindState(FName(*McCache::Namespaced(Object->GetStringField(TEXT("Name")))), Properties);
	}
	return {};
}

int32 FMcSurfaceRules::ParseRule(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid())
	{
		bErrors = true;
		return INDEX_NONE;
	}
	if (Json->Type == EJson::String)
	{
		return LoadNamed(TEXT("material_rule"), Json->AsString(), NamedRules, true);
	}
	const TSharedPtr<FJsonObject> Object = Json->AsObject();
	if (!Object.IsValid())
	{
		bErrors = true;
		return INDEX_NONE;
	}
	const FString Type = TypeOf(*Object);
	FRule Rule;
	if (Type == TEXT("minecraft:block"))
	{
		const TOptional<FMcStateId> State = ParseState(Object->TryGetField(TEXT("result_state")));
		if (!State.IsSet())
		{
			UE_LOG(LogMcGen, Error, TEXT("Surface rule: unknown block state"));
			bErrors = true;
			return INDEX_NONE;
		}
		Rule.Type = ERule::Block;
		Rule.State = State.GetValue();
	}
	else if (Type == TEXT("minecraft:sequence"))
	{
		Rule.Type = ERule::Sequence;
		for (const TSharedPtr<FJsonValue>& Child : Object->GetArrayField(TEXT("sequence")))
		{
			Rule.Children.Add(ParseRule(Child));
		}
	}
	else if (Type == TEXT("minecraft:condition"))
	{
		Rule.Type = ERule::Condition;
		Rule.If = ParseCondition(Object->TryGetField(TEXT("if_true")));
		Rule.Then = ParseRule(Object->TryGetField(TEXT("then_run")));
	}
	else if (Type == TEXT("minecraft:bandlands"))
	{
		Rule.Type = ERule::Bandlands;
	}
	else if (Type == TEXT("minecraft:ore_vein"))
	{
		Rule.Type = ERule::OreVein;
		Rule.Density = Graph->Parse(Object->TryGetField(TEXT("density")));
		Rule.Gap = Graph->Parse(Object->TryGetField(TEXT("filler_gap")));
		Rule.Richness = Graph->Parse(Object->TryGetField(TEXT("richness")));
		const TOptional<FMcStateId> Ore = ParseState(Object->TryGetField(TEXT("ore_block")));
		const TOptional<FMcStateId> RawOre = ParseState(Object->TryGetField(TEXT("raw_ore_block")));
		const TOptional<FMcStateId> Filler = ParseState(Object->TryGetField(TEXT("filler_block")));
		if (!Ore.IsSet() || !RawOre.IsSet() || !Filler.IsSet() || Rule.Density == INDEX_NONE || Rule.Gap == INDEX_NONE || Rule.Richness == INDEX_NONE)
		{
			UE_LOG(LogMcGen, Error, TEXT("Surface rule: ore vein with unknown blocks or functions"));
			bErrors = true;
			return INDEX_NONE;
		}
		Rule.Ore = Ore.GetValue();
		Rule.RawOre = RawOre.GetValue();
		Rule.Filler = Filler.GetValue();
		Rule.RawChance = Object->GetNumberField(TEXT("raw_ore_chance"));
		Rule.RandomSeed = FMcRandom(WorldSeed).Fork(Object->GetStringField(TEXT("ore_block"))).NextLong();
	}
	else
	{
		UE_LOG(LogMcGen, Error, TEXT("Surface rule: unknown rule type %s"), *Type);
		bErrors = true;
		return INDEX_NONE;
	}
	return Rules.Add(MoveTemp(Rule));
}

int32 FMcSurfaceRules::ParseCondition(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid())
	{
		bErrors = true;
		return INDEX_NONE;
	}
	if (Json->Type == EJson::String)
	{
		return LoadNamed(TEXT("material_condition"), Json->AsString(), NamedConditions, false);
	}
	const TSharedPtr<FJsonObject> Object = Json->AsObject();
	if (!Object.IsValid())
	{
		bErrors = true;
		return INDEX_NONE;
	}
	const FString Type = TypeOf(*Object);
	FCondition Condition;
	if (Type == TEXT("minecraft:biome"))
	{
		Condition.Type = ECondition::Biome;
		const TSharedPtr<FJsonValue> Biomes = Object->TryGetField(TEXT("biome_is"));
		TArray<FString> Names;
		if (Biomes.IsValid() && Biomes->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Name : Biomes->AsArray())
			{
				Names.Add(Name->AsString());
			}
		}
		else if (Biomes.IsValid())
		{
			Names.Add(Biomes->AsString());
		}
		for (const FString& Name : Names)
		{
			// A biome this version doesn't have can't match; not an error.
			const TOptional<FMcBiomeId> Biome = FMcBiomeRegistry::Get().Find(FName(*Name));
			if (Biome.IsSet())
			{
				Condition.Biomes.Add(Biome.GetValue());
			}
		}
	}
	else if (Type == TEXT("minecraft:noise_threshold"))
	{
		Condition.Type = ECondition::NoiseThreshold;
		Condition.Noise = GetNoise(Object->GetStringField(TEXT("noise")));
		Condition.Min = Object->GetNumberField(TEXT("min_threshold"));
		Condition.Max = Object->GetNumberField(TEXT("max_threshold"));
		Object->TryGetBoolField(TEXT("is_3d"), Condition.bIs3d);
	}
	else if (Type == TEXT("minecraft:vertical_gradient"))
	{
		Condition.Type = ECondition::VerticalGradient;
		Condition.AnchorA = ParseAnchor(Object->GetObjectField(TEXT("true_at_and_below")));
		Condition.AnchorB = ParseAnchor(Object->GetObjectField(TEXT("false_at_and_above")));
		Condition.RandomSeed = FMcRandom(WorldSeed).Fork(McCache::Namespaced(Object->GetStringField(TEXT("random_name")))).NextLong();
	}
	else if (Type == TEXT("minecraft:y_above"))
	{
		Condition.Type = ECondition::YAbove;
		Condition.AnchorA = ParseAnchor(Object->GetObjectField(TEXT("anchor")));
		Object->TryGetNumberField(TEXT("surface_depth_multiplier"), Condition.SurfaceDepthMultiplier);
		Object->TryGetBoolField(TEXT("add_stone_depth"), Condition.bAddStoneDepth);
	}
	else if (Type == TEXT("minecraft:water"))
	{
		Condition.Type = ECondition::Water;
		Object->TryGetNumberField(TEXT("offset"), Condition.Offset);
		Object->TryGetNumberField(TEXT("surface_depth_multiplier"), Condition.SurfaceDepthMultiplier);
		Object->TryGetBoolField(TEXT("add_stone_depth"), Condition.bAddStoneDepth);
	}
	else if (Type == TEXT("minecraft:stone_depth"))
	{
		Condition.Type = ECondition::StoneDepth;
		Object->TryGetNumberField(TEXT("offset"), Condition.Offset);
		Object->TryGetBoolField(TEXT("add_surface_depth"), Condition.bAddSurfaceDepth);
		Object->TryGetNumberField(TEXT("secondary_depth_range"), Condition.SecondaryRange);
		Condition.bCeiling = Object->GetStringField(TEXT("surface_type")) == TEXT("ceiling");
	}
	else if (Type == TEXT("minecraft:not"))
	{
		Condition.Type = ECondition::Not;
		Condition.Inner = ParseCondition(Object->TryGetField(TEXT("invert")));
	}
	else if (Type == TEXT("minecraft:steep"))
	{
		Condition.Type = ECondition::Steep;
	}
	else if (Type == TEXT("minecraft:hole"))
	{
		Condition.Type = ECondition::Hole;
	}
	else if (Type == TEXT("minecraft:above_preliminary_surface"))
	{
		Condition.Type = ECondition::AbovePreliminarySurface;
	}
	else
	{
		UE_LOG(LogMcGen, Error, TEXT("Surface rule: unknown condition type %s"), *Type);
		bErrors = true;
		return INDEX_NONE;
	}
	return Conditions.Add(MoveTemp(Condition));
}

int32 FMcSurfaceRules::GetSurfaceDepth(int32 X, int32 Z) const
{
	const double Noise = SurfaceNoise != INDEX_NONE ? Noises[SurfaceNoise].Sample(X, 0.0, Z) : 0.0;
	const double Jitter = FMcRandom(WorldSeed).Fork(X, 0, Z).NextDouble() * 0.25;
	return FMath::FloorToInt32(Noise * 2.75 + 3.0 + Jitter);
}

double FMcSurfaceRules::GetSecondaryDepth(int32 X, int32 Z) const
{
	return SecondaryNoise != INDEX_NONE ? FMath::Clamp(Noises[SecondaryNoise].Sample(X, 0.0, Z), -1.0, 1.0) : 0.0;
}

double FMcSurfaceRules::NoiseAt(int32 Noise, FMcSurfacePoint& Point) const
{
	if (Point.NoiseCache)
	{
		double& Cached = (*Point.NoiseCache)[Noise];
		if (FMath::IsNaN(Cached))
		{
			Cached = Noises[Noise].Sample(Point.X, 0.0, Point.Z);
		}
		return Cached;
	}
	return Noises[Noise].Sample(Point.X, 0.0, Point.Z);
}

bool FMcSurfaceRules::Test(int32 Index, FMcSurfacePoint& Point) const
{
	const FCondition& Condition = Conditions[Index];
	switch (Condition.Type)
	{
	case ECondition::Biome:
		return Condition.Biomes.Contains(Point.Biome);
	case ECondition::NoiseThreshold:
	{
		const double Value = Condition.bIs3d ? Noises[Condition.Noise].Sample(Point.X, Point.Y, Point.Z) : NoiseAt(Condition.Noise, Point);
		return Value >= Condition.Min && Value <= Condition.Max;
	}
	case ECondition::VerticalGradient:
	{
		if (Point.Y <= Condition.AnchorA)
		{
			return true;
		}
		if (Point.Y >= Condition.AnchorB)
		{
			return false;
		}
		const double Chance = static_cast<double>(Condition.AnchorB - Point.Y) / (Condition.AnchorB - Condition.AnchorA);
		return FMcRandom(Condition.RandomSeed).Fork(Point.X, Point.Y, Point.Z).NextDouble() < Chance;
	}
	case ECondition::YAbove:
		return Point.Y + (Condition.bAddStoneDepth ? Point.StoneDepthAbove : 0) >= Condition.AnchorA + Point.SurfaceDepth * Condition.SurfaceDepthMultiplier;
	case ECondition::Water:
		return Point.WaterHeight == MIN_int32
			// The offset and surface depth move the water line, not the block: not_underwater (offset -1) holds for a
			// floor with one block of water over it, and not_under_deep_water (offset -6, depth x -1) for floors up
			// to six blocks plus the surface depth below the water. Measured: shallow ocean floors are dirt, sand
			// and gravel (disks), deep ones gravel.
			|| Point.Y + (Condition.bAddStoneDepth ? Point.StoneDepthAbove : 0) >= Point.WaterHeight + Condition.Offset + Point.SurfaceDepth * Condition.SurfaceDepthMultiplier;
	case ECondition::StoneDepth:
	{
		const int32 Depth = Condition.bCeiling ? Point.StoneDepthBelow : Point.StoneDepthAbove;
		int32 Limit = 1 + Condition.Offset + (Condition.bAddSurfaceDepth ? Point.SurfaceDepth : 0);
		if (Condition.SecondaryRange > 0)
		{
			Limit += FMath::FloorToInt32((Point.SecondaryDepth + 1.0) * 0.5 * Condition.SecondaryRange);
		}
		return Depth <= Limit;
	}
	case ECondition::Not:
		return Condition.Inner != INDEX_NONE && !Test(Condition.Inner, Point);
	case ECondition::Steep:
		return Point.bSteep;
	case ECondition::Hole:
		return Point.SurfaceDepth <= 0;
	case ECondition::AbovePreliminarySurface:
		return Point.Y >= Point.MinSurfaceLevel;
	}
	return false;
}

FMcStateId FMcSurfaceRules::GetBand(int32 X, int32 Y, int32 Z) const
{
	const double Offset = BandOffsetNoise != INDEX_NONE ? Noises[BandOffsetNoise].Sample(X, 0.0, Z) : 0.0;
	const int32 Index = Y + FMath::RoundToInt32(Offset * 4.0);
	return Bands[((Index % NumBands) + NumBands) % NumBands];
}

int32 FMcSurfaceRules::Run(int32 Index, FMcSurfacePoint& Point) const
{
	if (Index == INDEX_NONE)
	{
		return INDEX_NONE;
	}
	const FRule& Rule = Rules[Index];
	switch (Rule.Type)
	{
	case ERule::Block:
		return Rule.State;
	case ERule::Sequence:
		for (const int32 Child : Rule.Children)
		{
			const int32 Result = Run(Child, Point);
			if (Result != INDEX_NONE)
			{
				return Result;
			}
		}
		return INDEX_NONE;
	case ERule::Condition:
		return Rule.If != INDEX_NONE && Test(Rule.If, Point) ? Run(Rule.Then, Point) : INDEX_NONE;
	case ERule::Bandlands:
		return GetBand(Point.X, Point.Y, Point.Z);
	case ERule::OreVein:
	{
		if (!Point.Density)
		{
			return INDEX_NONE;
		}
		Point.Density->SetPos(Point.X, Point.Y, Point.Z);
		const double Density = Graph->Evaluate(Rule.Density, *Point.Density);
		if (Density <= 0.0)
		{
			return INDEX_NONE;
		}
		FMcRandom Random = FMcRandom(Rule.RandomSeed).Fork(Point.X, Point.Y, Point.Z);
		if (Random.NextDouble() >= Density)
		{
			return INDEX_NONE;
		}
		if (Random.NextDouble() < Graph->Evaluate(Rule.Richness, *Point.Density) && Graph->Evaluate(Rule.Gap, *Point.Density) < 0.0)
		{
			return Random.NextDouble() < Rule.RawChance ? Rule.RawOre : Rule.Ore;
		}
		return Rule.Filler;
	}
	}
	return INDEX_NONE;
}

int32 FMcSurfaceRules::Apply(FMcSurfacePoint& Point) const
{
	return Run(Root, Point);
}
