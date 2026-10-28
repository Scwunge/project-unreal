// Every loot table, density function, feature, structure and template pool the jar ships goes through the
// loader that reads it. What fails to load is an error; what loads but is skipped on purpose (a loot function or
// feature type not written yet) is counted and shown as a warning.

#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McDensityFunction.h"
#include "McFeature.h"
#include "McItemRegistry.h"
#include "McJigsaw.h"
#include "McLoot.h"
#include "McNoiseGenerator.h"
#include "McRandom.h"
#include "McStructures.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// The ids ("overworld/continents", "chests/simple_dungeon") of the json files under data/minecraft/<Folder>.
	TArray<FString> ListDataIds(const FString& Folder)
	{
		FString Root = McCache::Data(Folder);
		FPaths::NormalizeFilename(Root);
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.json"), true, false);
		TArray<FString> Ids;
		for (FString File : Files)
		{
			FPaths::NormalizeFilename(File);
			Ids.Add(FPaths::ChangeExtension(File.RightChop(Root.Len() + 1), TEXT("")));
		}
		Ids.Sort();
		return Ids;
	}

	void ReportErrors(FAutomationTestBase& Test, const TCHAR* What, const TArray<FString>& Bad)
	{
		if (Bad.IsEmpty())
		{
			return;
		}
		for (const FString& Id : Bad)
		{
			UE_LOG(LogTemp, Display, TEXT("DataCoverage: %s: %s"), What, *Id);
		}
		FString Head;
		for (int32 Index = 0; Index < FMath::Min(Bad.Num(), 40); ++Index)
		{
			Head += TEXT(" ") + Bad[Index];
		}
		Test.AddError(FString::Printf(TEXT("%d %s:%s%s"), Bad.Num(), What, *Head, Bad.Num() > 40 ? TEXT(" ...") : TEXT("")));
	}

	void ReportCounts(FAutomationTestBase& Test, const TCHAR* What, const TMap<FString, int32>& Counts)
	{
		TArray<FString> Keys;
		Counts.GetKeys(Keys);
		Keys.Sort();
		for (const FString& Key : Keys)
		{
			UE_LOG(LogTemp, Display, TEXT("DataCoverage: %s %s x%d"), What, *Key, Counts[Key]);
			Test.AddWarning(FString::Printf(TEXT("%s %s x%d"), What, *Key, Counts[Key]));
		}
	}

	bool LoadRegistries()
	{
		return FMcBlockRegistry::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded();
	}

	// The type names the loot code acts on (McLoot.cpp). Anything else in a table is skipped or passes unchecked.
	const TSet<FString>& KnownLootFunctions()
	{
		static const TSet<FString> Names = { TEXT("set_count"), TEXT("explosion_decay"), TEXT("apply_bonus"), TEXT("limit_count"),
			TEXT("set_damage"), TEXT("enchant_randomly"), TEXT("enchant_with_levels"), TEXT("set_enchantments"), TEXT("set_potion"),
			TEXT("set_name"), TEXT("set_components"), TEXT("set_stew_effect"), TEXT("set_ominous_bottle_amplifier"),
			TEXT("set_instrument"), TEXT("filtered"), TEXT("discard") };
		return Names;
	}
	const TSet<FString>& KnownLootConditions()
	{
		static const TSet<FString> Names = { TEXT("random_chance"), TEXT("survives_explosion"), TEXT("match_tool"), TEXT("table_bonus"),
			TEXT("match_block"), TEXT("inverted"), TEXT("all_of"), TEXT("any_of") };
		return Names;
	}
	const TSet<FString>& KnownLootEntries()
	{
		static const TSet<FString> Names = { TEXT("item"), TEXT("alternatives"), TEXT("group"), TEXT("sequence"), TEXT("loot_table"), TEXT("empty") };
		return Names;
	}

	struct FLootScan
	{
		TMap<FString, int32> Functions;
		TMap<FString, int32> Conditions;
		TMap<FString, int32> Entries;
		TArray<FString> Items;  // item entries' names
	};

	void ScanConditions(const TArray<TSharedPtr<FJsonValue>>& List, FLootScan& Scan);
	void ScanFunctions(const TArray<TSharedPtr<FJsonValue>>& List, FLootScan& Scan);

	void ScanEntries(const TArray<TSharedPtr<FJsonValue>>& List, FLootScan& Scan)
	{
		for (const TSharedPtr<FJsonValue>& Value : List)
		{
			const TSharedPtr<FJsonObject> Entry = Value->AsObject();
			if (!Entry.IsValid())
			{
				continue;
			}
			const FString Type = McCache::PathOf(Entry->GetStringField(TEXT("type")));
			if (!KnownLootEntries().Contains(Type))
			{
				++Scan.Entries.FindOrAdd(Type);
			}
			FString Name;
			if (Type == TEXT("item") && Entry->TryGetStringField(TEXT("name"), Name))
			{
				Scan.Items.Add(McCache::Namespaced(Name));
			}
			const TArray<TSharedPtr<FJsonValue>>* Children;
			if (Entry->TryGetArrayField(TEXT("children"), Children))
			{
				ScanEntries(*Children, Scan);
			}
			if (Entry->TryGetArrayField(TEXT("conditions"), Children))
			{
				ScanConditions(*Children, Scan);
			}
			if (Entry->TryGetArrayField(TEXT("functions"), Children))
			{
				ScanFunctions(*Children, Scan);
			}
		}
	}

	void ScanConditions(const TArray<TSharedPtr<FJsonValue>>& List, FLootScan& Scan)
	{
		for (const TSharedPtr<FJsonValue>& Value : List)
		{
			const TSharedPtr<FJsonObject> Condition = Value->Type == EJson::Object ? Value->AsObject() : nullptr;
			if (!Condition.IsValid())
			{
				continue;  // a named predicate, which the loader reads by name
			}
			FString Type = Condition->GetStringField(TEXT("condition"));
			if (Type.IsEmpty())
			{
				Type = Condition->GetStringField(TEXT("type"));
			}
			Type = McCache::PathOf(Type);
			if (!KnownLootConditions().Contains(Type))
			{
				++Scan.Conditions.FindOrAdd(Type);
			}
			const TArray<TSharedPtr<FJsonValue>>* Terms;
			if (Condition->TryGetArrayField(TEXT("terms"), Terms))
			{
				ScanConditions(*Terms, Scan);
			}
		}
	}

	void ScanFunctions(const TArray<TSharedPtr<FJsonValue>>& List, FLootScan& Scan)
	{
		for (const TSharedPtr<FJsonValue>& Value : List)
		{
			const TSharedPtr<FJsonObject> Function = Value->AsObject();
			if (!Function.IsValid())
			{
				continue;
			}
			const FString Type = McCache::PathOf(Function->GetStringField(TEXT("function")));
			if (!KnownLootFunctions().Contains(Type))
			{
				++Scan.Functions.FindOrAdd(Type);
			}
			const TArray<TSharedPtr<FJsonValue>>* Conditions;
			if (Function->TryGetArrayField(TEXT("conditions"), Conditions))
			{
				ScanConditions(*Conditions, Scan);
			}
		}
	}

	void ScanTable(const FJsonObject& Table, FLootScan& Scan)
	{
		const TArray<TSharedPtr<FJsonValue>>* List;
		if (Table.TryGetArrayField(TEXT("functions"), List))
		{
			ScanFunctions(*List, Scan);
		}
		if (Table.TryGetArrayField(TEXT("pools"), List))
		{
			for (const TSharedPtr<FJsonValue>& Pool : *List)
			{
				const TSharedPtr<FJsonObject> Object = Pool->AsObject();
				const TArray<TSharedPtr<FJsonValue>>* Inner;
				if (!Object.IsValid())
				{
					continue;
				}
				if (Object->TryGetArrayField(TEXT("entries"), Inner))
				{
					ScanEntries(*Inner, Scan);
				}
				if (Object->TryGetArrayField(TEXT("conditions"), Inner))
				{
					ScanConditions(*Inner, Scan);
				}
				if (Object->TryGetArrayField(TEXT("functions"), Inner))
				{
					ScanFunctions(*Inner, Scan);
				}
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDataCoverageLootTest, "McClient.McGen.DataCoverageLoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDataCoverageLootTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadRegistries() && FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const TArray<FString> Ids = ListDataIds(TEXT("loot_table"));
	TArray<FString> NotRolled;
	TArray<FString> UnknownItems;
	FLootScan Scan;
	int32 Pools = 0;
	for (const FString& Id : Ids)
	{
		FMcRandom Random(1234);
		TArray<FMcItemStack> Out;
		if (!McLoot::Roll(McCache::Namespaced(Id), Random, Out))
		{
			NotRolled.Add(Id);
			continue;
		}
		if (const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(McCache::Data(TEXT("loot_table/") + Id + TEXT(".json"))))
		{
			const TArray<TSharedPtr<FJsonValue>>* List;
			Pools += Json->TryGetArrayField(TEXT("pools"), List) ? List->Num() : 0;
			FLootScan One;
			ScanTable(*Json, One);
			for (const FString& Item : One.Items)
			{
				if (!FMcItemRegistry::Get().Exists(FName(*Item)))
				{
					UnknownItems.Add(Id + TEXT(" -> ") + Item);
				}
			}
			for (const TPair<FString, int32>& Pair : One.Functions) { Scan.Functions.FindOrAdd(Pair.Key) += Pair.Value; }
			for (const TPair<FString, int32>& Pair : One.Conditions) { Scan.Conditions.FindOrAdd(Pair.Key) += Pair.Value; }
			for (const TPair<FString, int32>& Pair : One.Entries) { Scan.Entries.FindOrAdd(Pair.Key) += Pair.Value; }
		}
	}
	UE_LOG(LogTemp, Display, TEXT("DataCoverage: loot tables: %d files, %d pools, %d did not roll"), Ids.Num(), Pools, NotRolled.Num());
	AddInfo(FString::Printf(TEXT("%d loot tables (%d pools)"), Ids.Num(), Pools));
	TestTrue(TEXT("there are loot tables"), Ids.Num() > 1000);
	ReportErrors(*this, TEXT("loot tables that did not load"), NotRolled);
	ReportErrors(*this, TEXT("loot entries naming an item that does not exist"), UnknownItems);
	ReportCounts(*this, TEXT("loot function skipped:"), Scan.Functions);
	ReportCounts(*this, TEXT("loot condition not understood:"), Scan.Conditions);
	ReportCounts(*this, TEXT("loot entry type not understood:"), Scan.Entries);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDataCoverageDensityTest, "McClient.McGen.DataCoverageDensityFunctions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDataCoverageDensityTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadRegistries()))
	{
		return false;
	}
	const TArray<FString> Ids = ListDataIds(TEXT("worldgen/density_function"));
	TArray<FString> Bad;
	for (const FString& Id : Ids)
	{
		FMcDensityGraph Graph(1);
		if (Graph.LoadFunction(TEXT("minecraft:") + Id) == INDEX_NONE || Graph.HasErrors())
		{
			Bad.Add(Id);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("DataCoverage: density functions: %d files, %d failed"), Ids.Num(), Bad.Num());
	AddInfo(FString::Printf(TEXT("%d density functions"), Ids.Num()));
	TestTrue(TEXT("there are density functions"), Ids.Num() > 30);
	ReportErrors(*this, TEXT("density functions that did not load"), Bad);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDataCoverageFeaturesTest, "McClient.McGen.DataCoverageFeatures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDataCoverageFeaturesTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadRegistries()))
	{
		return false;
	}
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("overworld settings load"), FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	// The cache keeps configured features in worldgen/feature.
	const TArray<FString> Configured = ListDataIds(TEXT("worldgen/feature"));
	const TArray<FString> Placed = ListDataIds(TEXT("worldgen/placed_feature"));
	FMcFeatureLibrary Library(Settings.SeaLevel, Settings.MinY, Settings.Height);
	TArray<FString> BadConfigured;
	TArray<FString> BadPlaced;
	for (const FString& Id : Configured)
	{
		if (!Library.ParseFeature(MakeShared<FJsonValueString>(TEXT("minecraft:") + Id)))
		{
			BadConfigured.Add(Id);
		}
	}
	for (const FString& Id : Placed)
	{
		if (!Library.GetPlaced(TEXT("minecraft:") + Id))
		{
			BadPlaced.Add(Id);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("DataCoverage: features: %d configured, %d placed, errors %d"), Configured.Num(), Placed.Num(), Library.HasErrors());
	AddInfo(FString::Printf(TEXT("%d configured features, %d placed features"), Configured.Num(), Placed.Num()));
	TestTrue(TEXT("there are features"), Configured.Num() > 100 && Placed.Num() > 100);
	ReportErrors(*this, TEXT("configured features that did not parse"), BadConfigured);
	ReportErrors(*this, TEXT("placed features that did not parse"), BadPlaced);
	ReportCounts(*this, TEXT("feature type not written:"), Library.GetUnsupported());
	if (Library.HasErrors())
	{
		AddWarning(TEXT("the feature library logged errors while parsing (see the log)"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDataCoverageStructuresTest, "McClient.McGen.DataCoverageStructures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcDataCoverageStructuresTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), LoadRegistries()))
	{
		return false;
	}
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("overworld settings load"), FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	FMcFeatureLibrary Features(Settings.SeaLevel, Settings.MinY, Settings.Height);
	TestTrue(TEXT("biome features load"), Features.LoadBiomes());

	// ---- Structure sets and structures ----
	const TArray<FString> StructureIds = ListDataIds(TEXT("worldgen/structure"));
	const TArray<FString> SetIds = ListDataIds(TEXT("worldgen/structure_set"));
	FMcStructureLibrary Structures(Settings.SeaLevel, Settings.MinY);
	TestTrue(TEXT("structure sets load"), Structures.Load(&Features));
	UE_LOG(LogTemp, Display, TEXT("DataCoverage: structures: %d files, %d loaded; sets: %d files, %d loaded"),
		StructureIds.Num(), Structures.NumStructures(), SetIds.Num(), Structures.NumSets());
	AddInfo(FString::Printf(TEXT("%d structure files (%d loaded), %d structure set files (%d loaded)"),
		StructureIds.Num(), Structures.NumStructures(), SetIds.Num(), Structures.NumSets()));
	TestEqual(TEXT("every structure file is loaded"), Structures.NumStructures(), StructureIds.Num());
	TestEqual(TEXT("every structure set file is loaded"), Structures.NumSets(), SetIds.Num());
	ReportCounts(*this, TEXT("structure type not built:"), Structures.GetUnsupported());

	// ---- Template pools (and, through them, the templates) ----
	const TArray<FString> PoolIds = ListDataIds(TEXT("worldgen/template_pool"));
	FMcJigsawLibrary Jigsaws;
	TArray<FString> BadPools;
	for (const FString& Id : PoolIds)
	{
		const FName Name(*(TEXT("minecraft:") + Id));
		if (!Jigsaws.LoadPool(Name, &Features) || !Jigsaws.GetPool(Name))
		{
			BadPools.Add(Id);
		}
	}
	TArray<FString> Missing = Jigsaws.GetMissing().Array();
	Missing.Sort();
	UE_LOG(LogTemp, Display, TEXT("DataCoverage: template pools: %d files, %d loaded, %d templates missing"), PoolIds.Num(), Jigsaws.NumPools(), Missing.Num());
	AddInfo(FString::Printf(TEXT("%d template pool files (%d loaded)"), PoolIds.Num(), Jigsaws.NumPools()));
	TestTrue(TEXT("there are template pools"), PoolIds.Num() > 100);
	ReportErrors(*this, TEXT("template pools that did not load"), BadPools);
	// The jar's own pools can name a template it does not ship (ancient_city/walls/no_corners names a fifth
	// stairs wall), so this is a warning, not an error.
	for (const FString& Template : Missing)
	{
		AddWarning(FString::Printf(TEXT("a pool names the template %s, which is not in the jar"), *Template));
	}
	return true;
}

#endif
