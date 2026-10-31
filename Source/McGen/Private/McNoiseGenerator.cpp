#include "McNoiseGenerator.h"
#include "Dom/JsonObject.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "McChunk.h"
#include "McGen.h"
#include <limits>

namespace
{
	// The game's deep lava level: open space below it is lava, not air.
	constexpr int32 LavaLevel = -54;

	TOptional<FMcStateId> ReadBlock(const FJsonObject& Json, const TCHAR* Field)
	{
		const TSharedPtr<FJsonValue> Value = Json.TryGetField(Field);
		if (!Value.IsValid())
		{
			return {};
		}
		// Either "minecraft:stone" or { "Name": "minecraft:stone", "Properties": {...} }.
		FString Name;
		if (Value->Type == EJson::String)
		{
			Name = Value->AsString();
		}
		else if (Value->Type == EJson::Object)
		{
			Name = Value->AsObject()->GetStringField(TEXT("Name"));
		}
		return FMcBlockRegistry::Get().FindDefaultState(FName(*McCache::Namespaced(Name)));
	}
}

bool FMcNoiseSettings::Load(const FString& Id, FMcNoiseSettings& Out)
{
	const FString Path = McCache::Data(TEXT("worldgen/noise_settings/") + McCache::PathOf(Id) + TEXT(".json"));
	const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(Path);
	const TSharedPtr<FJsonObject>* Noise;
	const TSharedPtr<FJsonObject>* RouterObject;
	if (!Json.IsValid() || !Json->TryGetObjectField(TEXT("noise"), Noise) || !Json->TryGetObjectField(TEXT("noise_router"), RouterObject))
	{
		UE_LOG(LogMcGen, Error, TEXT("Noise settings %s not found or incomplete"), *Id);
		return false;
	}

	FMcNoiseSettings Result;
	Result.Id = McCache::Namespaced(Id);
	Result.MinY = (*Noise)->GetIntegerField(TEXT("min_y"));
	Result.Height = (*Noise)->GetIntegerField(TEXT("height"));
	Result.SeaLevel = Json->GetIntegerField(TEXT("sea_level"));
	const TOptional<FMcStateId> Block = ReadBlock(*Json, TEXT("default_block"));
	const TOptional<FMcStateId> Fluid = ReadBlock(*Json, TEXT("default_fluid"));
	if (!Block.IsSet() || !Fluid.IsSet())
	{
		UE_LOG(LogMcGen, Error, TEXT("Noise settings %s: unknown default block or fluid"), *Id);
		return false;
	}
	Result.DefaultBlock = Block.GetValue();
	Result.DefaultFluid = Fluid.GetValue();
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*RouterObject)->Values)
	{
		Result.Router.Add(Entry.Key, Entry.Value);
	}
	const TSharedPtr<FJsonObject>* Aquifers;
	if (Json->TryGetObjectField(TEXT("aquifers"), Aquifers))
	{
		Result.SurfaceLevel = (*Aquifers)->TryGetField(TEXT("surface_level"));
		Result.Aquifers = MakeShared<FJsonValueObject>(*Aquifers);
	}
	Json->TryGetStringField(TEXT("material_rule"), Result.SurfaceRule);
	Out = MoveTemp(Result);
	return true;
}

FMcNoiseGenerator::FMcNoiseGenerator(const FMcNoiseSettings& InSettings, uint64 Seed, const FString& BiomePreset)
	: Settings(InSettings)
	, Graph(Seed)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Settings.Router)
	{
		Router.Add(Entry.Key, Graph.Parse(Entry.Value));
	}
	FinalDensity = GetRouterFunction(TEXT("final_density"));
	const TCHAR* const ClimateNames[] = { TEXT("temperature"), TEXT("vegetation"), TEXT("continents"), TEXT("erosion"), TEXT("depth"), TEXT("ridges") };
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Climate[Index] = GetRouterFunction(ClimateNames[Index]);
	}
	SurfaceLevel = Settings.SurfaceLevel.IsValid() ? Graph.Parse(Settings.SurfaceLevel) : INDEX_NONE;
	bAquifers = Aquifer.Load(Settings.Aquifers, SurfaceLevel, Graph, Seed, Settings.SeaLevel, Settings.DefaultFluid,
		FMcBlockRegistry::Get().FindDefaultState(TEXT("minecraft:lava")).Get(0), FMcBlockRegistry::Get().FindDefaultState(TEXT("minecraft:air")).Get(0),
		Settings.DefaultBlock);
	bool bRulesLoaded = true;
	if (!Settings.SurfaceRule.IsEmpty())
	{
		bRulesLoaded = SurfaceRules.Load(Settings.SurfaceRule, Graph, Seed, Settings.MinY, Settings.Height);
		bSurfaceRules = bRulesLoaded;
	}

	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	Water = Registry.FindDefaultState(TEXT("minecraft:water")).Get(0);
	Lava = Registry.FindDefaultState(TEXT("minecraft:lava")).Get(0);
	Air = Registry.FindDefaultState(TEXT("minecraft:air")).Get(0);
	Bedrock = Registry.FindDefaultState(TEXT("minecraft:bedrock")).Get(0);
	GrassBlock = Registry.FindDefaultState(TEXT("minecraft:grass_block")).Get(0);
	Mycelium = Registry.FindDefaultState(TEXT("minecraft:mycelium")).Get(0);
	Dirt = Registry.FindDefaultState(TEXT("minecraft:dirt")).Get(0);
	LoadCarvers();
	const bool bBiomes = (BiomePreset == TEXT("minecraft:the_end") || Settings.Id == TEXT("minecraft:end")) ? Biomes.LoadEnd() : Biomes.Load(BiomePreset);
	bValid = FinalDensity != INDEX_NONE && !Graph.HasErrors() && bBiomes && bRulesLoaded;
	if (!bValid)
	{
		UE_LOG(LogMcGen, Error, TEXT("Noise settings %s: the density functions didn't all load"), *Settings.Id);
	}
}

int32 FMcNoiseGenerator::GetRouterFunction(const FString& Name) const
{
	const int32* Found = Router.Find(Name);
	return Found ? *Found : INDEX_NONE;
}

void FMcNoiseGenerator::Generate(FMcChunk& Chunk) const
{
	if (!bValid)
	{
		return;
	}
	const int32 BlockX = Chunk.GetPos().X * 16;
	const int32 BlockZ = Chunk.GetPos().Y * 16;
	const int32 MinY = FMath::Max(Chunk.GetMinY(), Settings.MinY);
	const int32 MaxY = FMath::Min(Chunk.GetMaxY(), Settings.MinY + Settings.Height - 1);

	FMcDensityContext Context(Graph);
	Context.BeginChunk(BlockX, BlockZ, Settings.MinY, Settings.Height);
	FMcDensityContext AquiferContext(Graph);
	FMcAquifer::FCache AquiferCache;

	const int32 NumSections = (Chunk.GetMaxY() - Chunk.GetMinY() + 1) / 16;
	TArray<TArray<FMcStateId>> Sections;
	Sections.SetNum(NumSections);
	for (TArray<FMcStateId>& Section : Sections)
	{
		Section.Init(Air, 4096);
	}

	TArray<int32> PreliminarySurface;
	PreliminarySurface.SetNum(256);
	for (int32 Z = 0; Z < 16; ++Z)
	{
		for (int32 X = 0; X < 16; ++X)
		{
			double Surface = Settings.SeaLevel;
			if (SurfaceLevel != INDEX_NONE)
			{
				Context.SetPos(BlockX + X, 0, BlockZ + Z);
				Surface = Graph.Evaluate(SurfaceLevel, Context);
			}
			PreliminarySurface[Z * 16 + X] = FMath::FloorToInt32(Surface);
			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				Context.SetPos(BlockX + X, Y, BlockZ + Z);
				FMcStateId State = Air;
				const double Density = Graph.Evaluate(FinalDensity, Context);
				if (Density > 0.0)
				{
					State = Settings.DefaultBlock;
				}
				else if (bAquifers)
				{
					// Well above the ground only the sea can reach.
					State = Y > Surface + 32 ? Aquifer.GlobalAt(Y) : Aquifer.Compute(BlockX + X, Y, BlockZ + Z, Density, AquiferContext, AquiferCache);
				}
				else if (Y < LavaLevel)
				{
					State = Lava;
				}
				else if (Y < Settings.SeaLevel && Y > Surface)
				{
					State = Settings.DefaultFluid;
				}
				if (State != Air)
				{
					const int32 LocalY = Y - Chunk.GetMinY();
					Sections[LocalY >> 4][((LocalY & 15) << 8) | (Z << 4) | X] = State;
				}
			}
		}
	}
	FillBiomes(Chunk, Context);
	if (bSurfaceRules)
	{
		ApplySurface(Chunk, Sections, PreliminarySurface, Context);
	}
	ApplyCarvers(Chunk, Sections, AquiferContext, AquiferCache);
	Context.EndChunk();

	for (int32 Index = 0; Index < NumSections; ++Index)
	{
		Chunk.LoadSection(Index, Sections[Index]);
	}
}

FMcClimate FMcNoiseGenerator::SampleClimate(FMcDensityContext& Context) const
{
	FMcClimate Result;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		Result.Values[Index] = Climate[Index] != INDEX_NONE ? Graph.Evaluate(Climate[Index], Context) : 0.0;
	}
	return Result;
}

FMcClimate FMcNoiseGenerator::SampleClimate(int32 X, int32 Y, int32 Z) const
{
	FMcDensityContext Context(Graph);
	Context.SetPos(X, Y, Z);
	return SampleClimate(Context);
}

FMcBiomeId FMcNoiseGenerator::GetBiome(int32 X, int32 Y, int32 Z) const
{
	// Biomes are per 4x4x4 cell, sampled at the cell's corner.
	int32 Hint = INDEX_NONE;
	if (Biomes.IsEnd())
	{
		FMcDensityContext Context(Graph);
		return EndBiomeAt(Context, X, Z);
	}
	return Biomes.Find(SampleClimate(X & ~3, Y & ~3, Z & ~3), Hint);
}

FMcBiomeId FMcNoiseGenerator::EndBiomeAt(FMcDensityContext& Context, int32 X, int32 Z) const
{
	// One biome per chunk, from the island function at the chunk's centre.
	Context.SetPos((X & ~15) + 8, 0, (Z & ~15) + 8);
	return Biomes.FindEnd(Climate[3] != INDEX_NONE ? Graph.Evaluate(Climate[3], Context) : 0.0, X, Z);
}

void FMcNoiseGenerator::LoadCarvers()
{
	const FMcBiomeRegistry& Registry = FMcBiomeRegistry::Get();
	BiomeCarvers.Init(0, Registry.NumBiomes());
	for (int32 Biome = 0; Biome < Registry.NumBiomes(); ++Biome)
	{
		const FString Name = Registry.GetName(static_cast<FMcBiomeId>(Biome)).ToString();
		const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(McCache::Data(TEXT("worldgen/biome/") + McCache::PathOf(Name) + TEXT(".json")));
		const TSharedPtr<FJsonValue> List = Json.IsValid() ? Json->TryGetField(TEXT("carvers")) : nullptr;
		TArray<FString> Ids;
		if (List.IsValid() && List->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Entry : List->AsArray())
			{
				Ids.Add(Entry->AsString());
			}
		}
		else if (List.IsValid() && List->Type == EJson::String)
		{
			Ids.Add(List->AsString());
		}
		for (const FString& Id : Ids)
		{
			const FString Key = McCache::Namespaced(Id);
			int32 Index = Carvers.IndexOfByPredicate([&Key](const FMcCarver& Carver) { return Carver.GetId() == Key; });
			if (Index == INDEX_NONE && Carvers.Num() < 32)
			{
				FMcCarver Carver;
				if (Carver.Load(Key, Settings.MinY, Settings.Height))
				{
					Index = Carvers.Add(MoveTemp(Carver));
				}
			}
			if (Index != INDEX_NONE)
			{
				BiomeCarvers[Biome] |= 1u << Index;
			}
		}
	}
}

bool bMcCarversOff = false;

void FMcNoiseGenerator::ApplyCarvers(FMcChunk& Chunk, TArray<TArray<FMcStateId>>& Sections, FMcDensityContext& AquiferContext, FMcAquifer::FCache& AquiferCache) const
{
	if (Carvers.IsEmpty() || bMcCarversOff)
	{
		return;
	}
	const int32 ChunkX = Chunk.GetPos().X;
	const int32 ChunkZ = Chunk.GetPos().Y;
	const int32 ChunkMinY = Chunk.GetMinY();
	const int32 ChunkMaxY = Chunk.GetMaxY();
	auto At = [&Sections, ChunkMinY](int32 X, int32 Y, int32 Z) -> FMcStateId&
	{
		const int32 LocalY = Y - ChunkMinY;
		return Sections[LocalY >> 4][((LocalY & 15) << 8) | (Z << 4) | X];
	};
	// Start chunks' biomes, looked up once each.
	TMap<FIntPoint, uint32> SourceCarvers;
	for (int32 Index = 0; Index < Carvers.Num(); ++Index)
	{
		const uint32 Bit = 1u << Index;
		const auto Allowed = [&](int32 SourceX, int32 SourceZ)
		{
			const FIntPoint Key(SourceX, SourceZ);
			const uint32* Found = SourceCarvers.Find(Key);
			if (!Found)
			{
				const FMcBiomeId Biome = GetBiome(SourceX * 16 + 8, Settings.SeaLevel, SourceZ * 16 + 8);
				Found = &SourceCarvers.Add(Key, BiomeCarvers.IsValidIndex(Biome) ? BiomeCarvers[Biome] : 0);
			}
			return (*Found & Bit) != 0;
		};
		const auto CarveBlock = [&](int32 X, int32 Y, int32 Z)
		{
			if (Y < ChunkMinY || Y > ChunkMaxY)
			{
				return;
			}
			FMcStateId& State = At(X, Y, Z);
			if (State == Air || State == Water || State == Lava || State == Bedrock)
			{
				return;
			}
			FMcStateId Fill = Y < LavaLevel ? Lava : Air;
			if (bAquifers)
			{
				Fill = Aquifer.Compute(ChunkX * 16 + X, Y, ChunkZ * 16 + Z, 0.0, AquiferContext, AquiferCache);
				if (Fill == Settings.DefaultBlock)
				{
					// An aquifer barrier keeps its stone.
					return;
				}
			}
			const FMcStateId Old = State;
			State = Fill;
			// Carving away the top block brings it down onto the dirt below.
			if ((Old == GrassBlock || Old == Mycelium) && Y - 1 >= ChunkMinY && At(X, Y - 1, Z) == Dirt)
			{
				At(X, Y - 1, Z) = Old;
			}
		};
		Carvers[Index].Carve(ChunkX, ChunkZ, Graph.GetSeed(), Allowed, CarveBlock);
	}
}

double FMcNoiseGenerator::GetPreliminarySurface(int32 X, int32 Z) const
{
	if (SurfaceLevel == INDEX_NONE)
	{
		return Settings.SeaLevel;
	}
	FMcDensityContext Context(Graph);
	Context.SetPos(X, 0, Z);
	return Graph.Evaluate(SurfaceLevel, Context);
}

void FMcNoiseGenerator::FillBiomes(FMcChunk& Chunk, FMcDensityContext& Context) const
{
	const int32 BlockX = Chunk.GetPos().X * 16;
	const int32 BlockZ = Chunk.GetPos().Y * 16;
	const int32 Layers = Chunk.NumSections() * 4;
	int32 Hint = INDEX_NONE;
	if (Biomes.IsEnd())
	{
		const FMcBiomeId Biome = EndBiomeAt(Context, BlockX, BlockZ);
		for (int32 CellZ = 0; CellZ < 4; ++CellZ)
		{
			for (int32 CellX = 0; CellX < 4; ++CellX)
			{
				for (int32 Layer = 0; Layer < Layers; ++Layer)
				{
					Chunk.SetBiomeCell(CellX, Layer, CellZ, Biome);
				}
			}
		}
		return;
	}
	for (int32 CellZ = 0; CellZ < 4; ++CellZ)
	{
		for (int32 CellX = 0; CellX < 4; ++CellX)
		{
			for (int32 Layer = 0; Layer < Layers; ++Layer)
			{
				Context.SetPos(BlockX + CellX * 4, Chunk.GetMinY() + Layer * 4, BlockZ + CellZ * 4);
				Chunk.SetBiomeCell(CellX, Layer, CellZ, Biomes.Find(SampleClimate(Context), Hint));
			}
		}
	}
}

void FMcNoiseGenerator::ApplySurface(FMcChunk& Chunk, TArray<TArray<FMcStateId>>& Sections, const TArray<int32>& PreliminarySurface, FMcDensityContext& Context) const
{
	const int32 BlockX = Chunk.GetPos().X * 16;
	const int32 BlockZ = Chunk.GetPos().Y * 16;
	const int32 ChunkMinY = Chunk.GetMinY();
	const int32 MinY = FMath::Max(ChunkMinY, Settings.MinY);
	const int32 MaxY = FMath::Min(Chunk.GetMaxY(), Settings.MinY + Settings.Height - 1);
	auto At = [&Sections, ChunkMinY](int32 X, int32 Y, int32 Z) -> FMcStateId&
	{
		const int32 LocalY = Y - ChunkMinY;
		return Sections[LocalY >> 4][((LocalY & 15) << 8) | (Z << 4) | X];
	};
	auto IsOpen = [this](FMcStateId State) { return State == Air || State == Water || State == Lava; };

	// The height of each column (one above its top non-air block), for steep slopes.
	int32 Heights[16][16];
	for (int32 Z = 0; Z < 16; ++Z)
	{
		for (int32 X = 0; X < 16; ++X)
		{
			Heights[Z][X] = MinY;
			for (int32 Y = MaxY; Y >= MinY; --Y)
			{
				if (At(X, Y, Z) != Air)
				{
					Heights[Z][X] = Y + 1;
					break;
				}
			}
		}
	}
	// "Above the preliminary surface" is judged against the lowest surface in the chunk.
	int32 MinSurfaceLevel = MAX_int32;
	for (const int32 Level : PreliminarySurface)
	{
		MinSurfaceLevel = FMath::Min(MinSurfaceLevel, Level - 8);
	}

	TArray<double> NoiseCache;
	TArray<int32> DepthBelow;
	DepthBelow.SetNum(MaxY - MinY + 1);
	FMcSurfacePoint Point;
	Point.Density = &Context;
	Point.NoiseCache = &NoiseCache;
	Point.MinSurfaceLevel = MinSurfaceLevel;
	for (int32 Z = 0; Z < 16; ++Z)
	{
		for (int32 X = 0; X < 16; ++X)
		{
			Point.X = BlockX + X;
			Point.Z = BlockZ + Z;
			Point.SurfaceDepth = SurfaceRules.GetSurfaceDepth(Point.X, Point.Z);
			Point.SecondaryDepth = SurfaceRules.GetSecondaryDepth(Point.X, Point.Z);
			const auto Height = [&Heights](int32 HX, int32 HZ) { return Heights[FMath::Clamp(HZ, 0, 15)][FMath::Clamp(HX, 0, 15)]; };
			Point.bSteep = Height(X, Z + 1) >= Height(X, Z - 1) + 4 || Height(X - 1, Z) >= Height(X + 1, Z) + 4;
			NoiseCache.Init(std::numeric_limits<double>::quiet_NaN(), SurfaceRules.NumNoises());

			// Solid blocks counted up from the nearest opening below, for ceilings.
			int32 Run = 0;
			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				Run = IsOpen(At(X, Y, Z)) ? 0 : Run + 1;
				DepthBelow[Y - MinY] = Run;
			}
			// Then down from the top: solid blocks counted from the nearest opening above, and the water.
			Run = 0;
			int32 WaterHeight = MIN_int32;
			FMcStateId Above = Air;
			for (int32 Y = MaxY; Y >= MinY; --Y)
			{
				FMcStateId& State = At(X, Y, Z);
				if (State == Air)
				{
					Run = 0;
					WaterHeight = MIN_int32;
				}
				else if (State == Water || State == Lava)
				{
					Run = 0;
					if (Above == Air || Above != State)
					{
						WaterHeight = Y + 1;
					}
				}
				else
				{
					++Run;
				}
				Above = State;
				if (State != Settings.DefaultBlock)
				{
					continue;
				}
				Point.Y = Y;
				Point.StoneDepthAbove = Run;
				Point.StoneDepthBelow = DepthBelow[Y - MinY];
				Point.WaterHeight = WaterHeight;
				Point.Biome = Chunk.GetBiome(X, Y, Z);
				const int32 Result = SurfaceRules.Apply(Point);
				if (Result != INDEX_NONE)
				{
					State = static_cast<FMcStateId>(Result);
				}
			}
		}
	}
}

int32 FMcNoiseGenerator::FindSurfaceY(int32 X, int32 Z) const
{
	if (!bValid)
	{
		return Settings.SeaLevel;
	}
	FMcDensityContext Context(Graph);
	for (int32 Y = Settings.MinY + Settings.Height - 1; Y >= Settings.MinY; --Y)
	{
		Context.SetPos(X, Y, Z);
		if (Graph.Evaluate(FinalDensity, Context) > 0.0)
		{
			// Under the sea the surface is the water's.
			return FMath::Max(Y, Settings.SeaLevel - 1);
		}
	}
	return Settings.MinY;
}

int32 FMcNoiseGenerator::FindSolidTopY(int32 X, int32 Z) const
{
	if (!bValid)
	{
		return Settings.SeaLevel;
	}
	FMcDensityContext Context(Graph);
	for (int32 Y = Settings.MinY + Settings.Height - 1; Y >= Settings.MinY; --Y)
	{
		Context.SetPos(X, Y, Z);
		if (Graph.Evaluate(FinalDensity, Context) > 0.0)
		{
			return Y;
		}
	}
	return Settings.MinY;
}

int32 FMcNoiseGenerator::FindFloorY(int32 X, int32 Z, int32 BelowY) const
{
	if (!bValid)
	{
		return Settings.SeaLevel;
	}
	FMcDensityContext Context(Graph);
	// Below the sea level the open cells are the sea's fluid (the Nether's lava), not room to stand.
	int32 Open = 0;
	for (int32 Y = FMath::Min(BelowY, Settings.MinY + Settings.Height - 1); Y >= FMath::Max(Settings.MinY, Settings.SeaLevel - 1); --Y)
	{
		Context.SetPos(X, Y, Z);
		if (Graph.Evaluate(FinalDensity, Context) > 0.0)
		{
			if (Open >= 2)
			{
				return Y;
			}
			Open = 0;
		}
		else
		{
			++Open;
		}
	}
	return Settings.MinY - 1;
}
