#include "McMobLook.h"
#include "McCache.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace McMobLook
{
	namespace
	{
		// The order of the rabbit list in McEntityModel.cpp (the kinds that aren't data driven have their own).
		enum ERabbit { Brown, White, BlackRabbit, Gold, Salt, Splotched, Toast };

		FString WithNamespace(const FString& Id)
		{
			return Id.Contains(TEXT(":")) ? Id : TEXT("minecraft:") + Id;
		}

		TSharedPtr<FJsonObject> LoadJson(const FString& Path)
		{
			FString Text;
			TSharedPtr<FJsonObject> Object;
			if (FFileHelper::LoadFileToString(Text, *Path))
			{
				FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object);
			}
			return Object;
		}

		// The biomes a list ("minecraft:forest", "#minecraft:is_jungle" or a list of those) names, tags opened.
		void AddBiomes(const TSharedPtr<FJsonValue>& Value, TSet<FString>& Out, int32 Depth = 0)
		{
			if (!Value.IsValid() || Depth > 8)
			{
				return;
			}
			FString Text;
			if (Value->TryGetString(Text))
			{
				if (!Text.StartsWith(TEXT("#")))
				{
					Out.Add(WithNamespace(Text));
					return;
				}
				const FString Id = WithNamespace(Text.Mid(1));
				const TSharedPtr<FJsonObject> Tag = LoadJson(McCache::Data(TEXT("tags/worldgen/biome/") + Id.Mid(Id.Find(TEXT(":")) + 1) + TEXT(".json")));
				const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
				if (Tag.IsValid() && Tag->TryGetArrayField(TEXT("values"), Values))
				{
					for (const TSharedPtr<FJsonValue>& Entry : *Values)
					{
						// An entry is a name, or an object with the name as its id.
						const TSharedPtr<FJsonObject>* Object = nullptr;
						FString Named;
						if (Entry->TryGetObject(Object))
						{
							if ((*Object)->TryGetStringField(TEXT("id"), Named))
							{
								AddBiomes(MakeShared<FJsonValueString>(Named), Out, Depth + 1);
							}
						}
						else
						{
							AddBiomes(Entry, Out, Depth + 1);
						}
					}
				}
				return;
			}
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (Value->TryGetArray(List))
			{
				for (const TSharedPtr<FJsonValue>& Entry : *List)
				{
					AddBiomes(Entry, Out, Depth + 1);
				}
			}
		}

		// One way a variant can spawn: in any of the biomes (or anywhere, when it has no condition) at a priority.
		struct FSpawn
		{
			bool bAnywhere = false;
			TSet<FString> Biomes;
			int32 Priority = 0;
		};

		struct FVariants
		{
			TArray<FString> Names;
			TArray<TArray<FSpawn>> Spawns;
		};

		// The kinds whose looks are the data's: the files of data/minecraft/<name>_variant/, in name order.
		const TCHAR* GetFolder(FName Kind)
		{
			if (Kind == TEXT("minecraft:wolf")) { return TEXT("wolf_variant"); }
			if (Kind == TEXT("minecraft:pig")) { return TEXT("pig_variant"); }
			if (Kind == TEXT("minecraft:cow")) { return TEXT("cow_variant"); }
			if (Kind == TEXT("minecraft:chicken")) { return TEXT("chicken_variant"); }
			return nullptr;
		}

		const FVariants* GetVariants(FName Kind)
		{
			const TCHAR* Folder = GetFolder(Kind);
			if (!Folder)
			{
				return nullptr;
			}
			static TMap<FName, FVariants> Cache;
			if (const FVariants* Found = Cache.Find(Kind))
			{
				return Found->Names.IsEmpty() ? nullptr : Found;
			}
			FVariants& Out = Cache.Add(Kind);
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *McCache::Data(FString(Folder) + TEXT("/*.json")), true, false);
			Files.Sort();
			for (const FString& File : Files)
			{
				const TSharedPtr<FJsonObject> Object = LoadJson(McCache::Data(FString(Folder) + TEXT("/") + File));
				const TArray<TSharedPtr<FJsonValue>>* Conditions = nullptr;
				if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("spawn_conditions"), Conditions))
				{
					continue;
				}
				TArray<FSpawn> Spawns;
				for (const TSharedPtr<FJsonValue>& Entry : *Conditions)
				{
					const TSharedPtr<FJsonObject>* Spawn = nullptr;
					if (!Entry->TryGetObject(Spawn))
					{
						continue;
					}
					FSpawn Way;
					Way.Priority = static_cast<int32>((*Spawn)->GetNumberField(TEXT("priority")));
					const TSharedPtr<FJsonObject>* Condition = nullptr;
					if (!(*Spawn)->TryGetObjectField(TEXT("condition"), Condition))
					{
						Way.bAnywhere = true;
					}
					else if ((*Condition)->GetStringField(TEXT("type")) == TEXT("minecraft:biome"))
					{
						AddBiomes((*Condition)->TryGetField(TEXT("biomes")), Way.Biomes);
					}
					else
					{
						// A condition on something other than the biome (a structure, the moon) never holds here.
						continue;
					}
					Spawns.Add(Way);
				}
				Out.Names.Add(FPaths::GetBaseFilename(File));
				Out.Spawns.Add(Spawns);
			}
			return Out.Names.IsEmpty() ? nullptr : &Out;
		}

		bool IsIn(const TCHAR* Tag, FName Biome)
		{
			TSet<FString> Biomes;
			AddBiomes(MakeShared<FJsonValueString>(FString(TEXT("#minecraft:")) + Tag), Biomes);
			return Biomes.Contains(WithNamespace(Biome.ToString()));
		}
	}

	int32 GetCount(FName Kind)
	{
		if (const FVariants* Variants = GetVariants(Kind))
		{
			return Variants->Names.Num();
		}
		static const TMap<FName, int32> Counts = {
			{ TEXT("minecraft:horse"), 35 }, { TEXT("minecraft:wolf"), 9 }, { TEXT("minecraft:llama"), 4 },
			{ TEXT("minecraft:fox"), 2 }, { TEXT("minecraft:rabbit"), 7 }, { TEXT("minecraft:pig"), 3 },
			{ TEXT("minecraft:cow"), 3 }, { TEXT("minecraft:chicken"), 3 },
		};
		const int32* Found = Counts.Find(Kind);
		return Found ? *Found : 0;
	}

	FName GetName(FName Kind, int32 Index)
	{
		const FVariants* Variants = GetVariants(Kind);
		return Variants && Variants->Names.IsValidIndex(Index) ? FName(*Variants->Names[Index]) : NAME_None;
	}

	int32 Pick(FName Kind, FName Biome, FRandomStream& Random)
	{
		const int32 Count = GetCount(Kind);
		if (Count == 0)
		{
			return 0;
		}
		if (const FVariants* Variants = GetVariants(Kind))
		{
			// The variants that can spawn here, at the highest priority; those that tie are as likely as each other.
			const FString Here = Biome.IsNone() ? FString() : WithNamespace(Biome.ToString());
			TArray<int32> Best;
			int32 BestPriority = MIN_int32;
			for (int32 Index = 0; Index < Variants->Names.Num(); ++Index)
			{
				for (const FSpawn& Way : Variants->Spawns[Index])
				{
					if (!Way.bAnywhere && !Way.Biomes.Contains(Here))
					{
						continue;
					}
					if (Way.Priority > BestPriority)
					{
						BestPriority = Way.Priority;
						Best.Reset();
					}
					if (Way.Priority == BestPriority)
					{
						Best.AddUnique(Index);
					}
				}
			}
			if (!Best.IsEmpty())
			{
				return Best[Random.RandRange(0, Best.Num() - 1)];
			}
		}
		else if (Kind == TEXT("minecraft:fox") && !Biome.IsNone())
		{
			return IsIn(TEXT("spawns_snow_foxes"), Biome) ? 1 : 0;
		}
		else if (Kind == TEXT("minecraft:rabbit") && !Biome.IsNone())
		{
			if (IsIn(TEXT("spawns_gold_rabbits"), Biome))
			{
				return Gold;
			}
			if (IsIn(TEXT("spawns_white_rabbits"), Biome))
			{
				return White;
			}
			// Elsewhere half are brown, with salt and pepper, black and black and white among the rest.
			const int32 Roll = Random.RandRange(0, 99);
			return Roll < 50 ? Brown : Roll < 80 ? Salt : Roll < 90 ? BlackRabbit : Splotched;
		}
		return Random.RandRange(0, Count - 1);
	}
}
