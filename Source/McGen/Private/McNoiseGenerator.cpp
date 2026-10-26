#include "McNoiseGenerator.h"
#include "Dom/JsonObject.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "McChunk.h"
#include "McGen.h"

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
	}
	Out = MoveTemp(Result);
	return true;
}

FMcNoiseGenerator::FMcNoiseGenerator(const FMcNoiseSettings& InSettings, uint64 Seed)
	: Settings(InSettings)
	, Graph(Seed)
{
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Settings.Router)
	{
		Router.Add(Entry.Key, Graph.Parse(Entry.Value));
	}
	FinalDensity = GetRouterFunction(TEXT("final_density"));
	SurfaceLevel = Settings.SurfaceLevel.IsValid() ? Graph.Parse(Settings.SurfaceLevel) : INDEX_NONE;

	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	Water = Registry.FindDefaultState(TEXT("minecraft:water")).Get(0);
	Lava = Registry.FindDefaultState(TEXT("minecraft:lava")).Get(0);
	Air = Registry.FindDefaultState(TEXT("minecraft:air")).Get(0);
	bValid = FinalDensity != INDEX_NONE && !Graph.HasErrors();
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

	const int32 NumSections = (Chunk.GetMaxY() - Chunk.GetMinY() + 1) / 16;
	TArray<TArray<FMcStateId>> Sections;
	Sections.SetNum(NumSections);
	for (TArray<FMcStateId>& Section : Sections)
	{
		Section.Init(Air, 4096);
	}

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
			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				Context.SetPos(BlockX + X, Y, BlockZ + Z);
				FMcStateId State = Air;
				if (Graph.Evaluate(FinalDensity, Context) > 0.0)
				{
					State = Settings.DefaultBlock;
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
	Context.EndChunk();

	for (int32 Index = 0; Index < NumSections; ++Index)
	{
		Chunk.LoadSection(Index, Sections[Index]);
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
