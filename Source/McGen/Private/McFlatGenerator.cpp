#include "McFlatGenerator.h"
#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "McChunk.h"
#include "McGen.h"

bool FMcFlatSettings::LoadPreset(const FString& Id, FMcFlatSettings& Out)
{
	const FString Path = McCache::Data(TEXT("worldgen/flat_level_generator_preset/") + McCache::PathOf(Id) + TEXT(".json"));
	const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(Path);
	const TSharedPtr<FJsonObject>* SettingsObject;
	if (!Json.IsValid() || !Json->TryGetObjectField(TEXT("settings"), SettingsObject))
	{
		UE_LOG(LogMcGen, Error, TEXT("%s has no settings"), *Path);
		return false;
	}

	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	FMcFlatSettings Result;
	Result.Biome = FName(*(*SettingsObject)->GetStringField(TEXT("biome")));

	const TArray<TSharedPtr<FJsonValue>>* Layers;
	if ((*SettingsObject)->TryGetArrayField(TEXT("layers"), Layers))
	{
		for (const TSharedPtr<FJsonValue>& LayerValue : *Layers)
		{
			const TSharedPtr<FJsonObject>& LayerObject = LayerValue->AsObject();
			const FString Block = McCache::Namespaced(LayerObject->GetStringField(TEXT("block")));
			const TOptional<FMcStateId> State = Registry.FindDefaultState(FName(*Block));
			if (!State.IsSet())
			{
				UE_LOG(LogMcGen, Error, TEXT("%s: unknown block %s"), *Path, *Block);
				return false;
			}
			Result.Layers.Add({ State.GetValue(), LayerObject->GetIntegerField(TEXT("height")) });
		}
	}

	Out = MoveTemp(Result);
	return true;
}

FMcFlatGenerator::FMcFlatGenerator(const FMcFlatSettings& InSettings, int32 InMinY)
	: Settings(InSettings)
	, MinY(InMinY)
{
}

void FMcFlatGenerator::Generate(FMcChunk& Chunk) const
{
	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	Chunk.FillBiome(Biomes.Find(Settings.Biome).Get(Biomes.GetPlains()));
	int32 Y = Chunk.GetMinY();
	for (const FMcFlatLayer& Layer : Settings.Layers)
	{
		for (int32 Step = 0; Step < Layer.Height && Y <= Chunk.GetMaxY(); ++Step, ++Y)
		{
			Chunk.FillLayer(Y, Layer.State);
		}
	}
}

int32 FMcFlatGenerator::FindSurfaceY(int32 X, int32 Z) const
{
	int32 Total = 0;
	for (const FMcFlatLayer& Layer : Settings.Layers)
	{
		Total += Layer.Height;
	}
	return MinY + Total - 1;
}
