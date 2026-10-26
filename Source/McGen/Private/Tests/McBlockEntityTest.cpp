#include "McBlockEntity.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBiomeRegistry.h"
#include "McCache.h"
#include "McChunk.h"
#include "McGenRegion.h"
#include "McLoot.h"
#include "McRandom.h"
#include "McStructureTemplate.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBlockEntityTest, "McClient.McGen.BlockEntities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBlockEntityTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("registries load"), FMcBlockRegistry::Get().EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId Stone = Registry.ParseState(TEXT("minecraft:stone")).Get(0);
	const FMcStateId Air = Registry.FindDefaultState(TEXT("minecraft:air")).Get(0);
	const FMcStateId ChestNorth = Registry.ParseState(TEXT("minecraft:chest[facing=north]")).Get(0);
	const FMcStateId ChestSouth = Registry.ParseState(TEXT("minecraft:chest[facing=south]")).Get(0);

	// ---- The chunk keeps block entities with their blocks ----
	FMcChunk Chunk(FIntPoint(3, -2), -64, 24);
	Chunk.SetBlock(1, 10, 2, ChestNorth);
	FMcBlockEntity Chest = FMcBlockEntity::MakeContainer(27);
	Chest.Slots[0] = { FName("minecraft:diamond"), 3 };
	Chest.Slots[26] = { FName("minecraft:bread"), 64 };
	Chunk.SetBlockEntity(1, 10, 2, Chest);
	const FMcBlockEntity* Found = Chunk.FindBlockEntity(1, 10, 2);
	if (!TestNotNull(TEXT("the chest's block entity is found"), Found))
	{
		return false;
	}
	TestEqual(TEXT("it holds 27 slots"), Found->Slots.Num(), 27);
	TestEqual(TEXT("slot 0 holds the diamonds"), Found->Slots[0].Count, 3);
	TestNull(TEXT("none next to it"), Chunk.FindBlockEntity(2, 10, 2));
	Chunk.SetBlock(1, 10, 2, ChestSouth);
	TestNotNull(TEXT("turning the chest keeps its contents"), Chunk.FindBlockEntity(1, 10, 2));
	Chunk.SetBlock(1, 10, 2, Stone);
	TestNull(TEXT("replacing the chest drops its contents"), Chunk.FindBlockEntity(1, 10, 2));
	Chunk.SetBlock(1, 10, 2, Air);
	Chunk.SetBlockEntity(1, 999, 2, Chest);
	TestEqual(TEXT("an entity above the world is ignored"), Chunk.GetBlockEntities().Num(), 0);

	// ---- Saving ----
	Chunk.SetBlock(1, 10, 2, ChestNorth);
	Chunk.SetBlockEntity(1, 10, 2, Chest);
	Chunk.SetBlockEntity(15, -64, 0, FMcBlockEntity::MakeSpawner(FName("minecraft:zombie")));
	FMcBlockEntity Looted = FMcBlockEntity::MakeContainer(9);
	TestTrue(TEXT("a table fills a container"), McLoot::Fill(TEXT("minecraft:chests/simple_dungeon"), 1234, Looted));
	Chunk.SetBlockEntity(0, 200, 7, Looted);
	const TArray<uint8> Bytes = Chunk.EncodeBlockEntities();
	FMcChunk Loaded(FIntPoint(3, -2), -64, 24);
	TestTrue(TEXT("the bytes decode"), Loaded.DecodeBlockEntities(Bytes));
	TestEqual(TEXT("all three come back"), Loaded.GetBlockEntities().Num(), 3);
	for (const TPair<FIntVector, FMcBlockEntity>& Entry : Chunk.GetBlockEntities())
	{
		const FMcBlockEntity* Back = Loaded.GetBlockEntities().Find(Entry.Key);
		TestTrue(TEXT("each comes back as it was"), Back && *Back == Entry.Value);
	}
	TestEqual(TEXT("the spawner's mob"), Loaded.FindBlockEntity(15, -64, 0) ? Loaded.FindBlockEntity(15, -64, 0)->EntityId : FName(), FName("minecraft:zombie"));
	TestEqual(TEXT("the loot table is kept"), Loaded.FindBlockEntity(0, 200, 7) ? Loaded.FindBlockEntity(0, 200, 7)->LootTable : FName(), FName("minecraft:chests/simple_dungeon"));
	TestEqual(TEXT("saving again gives the same bytes"), Loaded.EncodeBlockEntities(), Bytes);
	TestEqual(TEXT("no entities is four bytes"), FMcChunk(FIntPoint(0, 0), -64, 24).EncodeBlockEntities().Num(), 4);
	FMcChunk Damaged(FIntPoint(3, -2), -64, 24);
	TestFalse(TEXT("cut short bytes are refused"), Damaged.DecodeBlockEntities(TConstArrayView<uint8>(Bytes).Left(Bytes.Num() / 2)));
	TestEqual(TEXT("and leave nothing behind"), Damaged.GetBlockEntities().Num(), 0);
	TArray<uint8> Wild = Bytes;
	Wild[4] = 77;
	TestFalse(TEXT("an unknown kind is refused"), Damaged.DecodeBlockEntities(Wild));

	// ---- Loot tables ----
	{
		FMcBlockEntity A = FMcBlockEntity::MakeContainer(27);
		FMcBlockEntity B = FMcBlockEntity::MakeContainer(27);
		FMcBlockEntity C = FMcBlockEntity::MakeContainer(27);
		McLoot::Fill(TEXT("minecraft:chests/simple_dungeon"), 42, A);
		McLoot::Fill(TEXT("minecraft:chests/simple_dungeon"), 42, B);
		McLoot::Fill(TEXT("minecraft:chests/simple_dungeon"), 43, C);
		TestTrue(TEXT("the same seed rolls the same chest"), A == B);
		TestFalse(TEXT("another seed rolls another"), A == C);
		TestEqual(TEXT("the table is recorded"), A.LootTable, FName("minecraft:chests/simple_dungeon"));
		TestEqual(TEXT("and the seed"), A.LootSeed, static_cast<int64>(42));

		FMcBlockEntity Untouched = FMcBlockEntity::MakeContainer(27);
		TestFalse(TEXT("an unknown table fills nothing"), McLoot::Fill(TEXT("minecraft:chests/no_such_table"), 1, Untouched));
		TestTrue(TEXT("and leaves the container alone"), Untouched.LootTable.IsNone());

		// Every chest table in the jar rolls, and its stacks make sense.
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *McCache::Data(TEXT("loot_table/chests")), TEXT("*.json"), true, false);
		TestTrue(TEXT("the jar has chest tables"), Files.Num() > 40);
		int32 Rolled = 0;
		int32 Empty = 0;
		for (const FString& File : Files)
		{
			// Nested folders (village/, trial_chambers/) keep their folder in the id.
			FString Relative = File;
			FPaths::NormalizeFilename(Relative);
			Relative.RightChopInline(Relative.Find(TEXT("/loot_table/chests/")) + FString(TEXT("/loot_table/chests/")).Len());
			const FString Table = TEXT("minecraft:chests/") + FPaths::GetBaseFilename(Relative, false);
			bool bAnyRoll = false;
			for (int32 Seed = 1; Seed <= 20; ++Seed)
			{
				FMcRandom Random(Seed);
				TArray<FMcItemStack> Stacks;
				if (!McLoot::Roll(Table, Random, Stacks))
				{
					AddError(FString::Printf(TEXT("%s can't be rolled"), *Table));
					break;
				}
				for (const FMcItemStack& Stack : Stacks)
				{
					if (Stack.Item.IsNone() || Stack.Count < 0 || Stack.Count > 1000)
					{
						AddError(FString::Printf(TEXT("%s gave a bad stack %s x%d"), *Table, *Stack.Item.ToString(), Stack.Count));
					}
				}
				bAnyRoll |= !Stacks.IsEmpty();
			}
			++Rolled;
			Empty += bAnyRoll ? 0 : 1;
		}
		AddInfo(FString::Printf(TEXT("%d chest tables rolled, %d never gave anything"), Rolled, Empty));
		TestTrue(TEXT("most chest tables give items"), Empty < Rolled / 4);

		// The dungeon table: 1 to 3 rolls of the first pool and 4 of the second, many items.
		int32 Stacks = 0;
		for (int32 Seed = 1; Seed <= 100; ++Seed)
		{
			FMcRandom Random(Seed);
			TArray<FMcItemStack> Out;
			McLoot::Roll(TEXT("minecraft:chests/simple_dungeon"), Random, Out);
			Stacks += Out.Num();
		}
		TestTrue(TEXT("a dungeon chest averages a handful of stacks"), Stacks / 100 >= 3 && Stacks / 100 <= 20);
	}

	// ---- A template's chests are filled when it is placed ----
	{
		TArray<FString> Ids;
		FMcStructureTemplate::ListIds(Ids);
		TSharedPtr<const FMcStructureTemplate> WithLoot;
		int32 LootBlock = INDEX_NONE;
		for (const FString& Id : Ids)
		{
			if (!Id.StartsWith(TEXT("abandoned_camp")) && !Id.StartsWith(TEXT("village/plains/houses")))
			{
				continue;
			}
			const TSharedPtr<const FMcStructureTemplate> Template = FMcStructureTemplate::Find(Id);
			for (int32 Index = 0; Template.IsValid() && Index < Template->Blocks.Num(); ++Index)
			{
				const FMcStructureTemplate::FBlock& Block = Template->Blocks[Index];
				if (Block.Nbt.IsValid() && !Block.Nbt->GetString(TEXT("LootTable")).IsEmpty() && Template->Palettes.Num() > 0 && Template->Palettes[0].IsValidIndex(Block.Palette)
					&& McBlockEntities::ContainerSlots(Registry.GetBlockOf(Template->Palettes[0][Block.Palette]).Name) > 0)
				{
					WithLoot = Template;
					LootBlock = Index;
					break;
				}
			}
			if (WithLoot.IsValid())
			{
				break;
			}
		}
		if (TestTrue(TEXT("a template with a loot chest turns up"), WithLoot.IsValid()))
		{
			TArray<TUniquePtr<FMcChunk>> Chunks;
			FMcChunk* Around[9];
			for (int32 Index = 0; Index < 9; ++Index)
			{
				Chunks.Add(MakeUnique<FMcChunk>(FIntPoint(Index % 3, Index / 3), -64, 24));
				Chunks.Last()->FillBiome(FMcBiomeRegistry::Get().GetPlains());
				Around[Index] = Chunks.Last().Get();
			}
			FMcGenRegion Region(FIntPoint(1, 1), Around);
			FMcTemplatePlaceSettings Settings;
			Settings.Palette = 0;
			FMcRandom Random(9);
			const FIntVector Origin(20, 64, 20);
			WithLoot->Place(Region, Origin, Settings, Random);
			const FMcStructureTemplate::FBlock& Block = WithLoot->Blocks[LootBlock];
			const FIntVector At = WithLoot->ToWorld(Block.Pos, Origin, Settings);
			const FMcBlockEntity* Entity = Region.GetBlockEntity(At);
			FMcBlockEntity First;
			if (TestNotNull(TEXT("the placed chest has a block entity"), Entity))
			{
				First = *Entity;
				TestEqual(TEXT("it names the template's loot table"), Entity->LootTable.ToString(), Block.Nbt->GetString(TEXT("LootTable")));
				int32 Items = 0;
				for (const FMcItemStack& Slot : Entity->Slots)
				{
					Items += Slot.IsEmpty() ? 0 : 1;
				}
				TestTrue(TEXT("and was filled"), Items > 0);
			}
			// Placing it again with the same stream gives the same contents.
			FMcRandom Again(9);
			WithLoot->Place(Region, Origin, Settings, Again);
			const FMcBlockEntity* Second = Region.GetBlockEntity(At);
			TestTrue(TEXT("placing again fills it the same way"), Entity && Second && First == *Second);
		}
	}
	return true;
}

#endif
