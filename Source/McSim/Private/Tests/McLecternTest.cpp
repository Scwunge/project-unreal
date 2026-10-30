#include "McBlockEntity.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McBook.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McItemRegistry.h"
#include "McLecternMenu.h"
#include "McSimulation.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcLecternTest, "McClient.McSim.Lectern",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcLecternTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()
		&& FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const auto Json = [](const TCHAR* Text)
	{
		TSharedPtr<FJsonValue> Value;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Value);
		return Value;
	};
	// A written book of five pages.
	FMcItemStack Book(TEXT("minecraft:written_book"));
	Book.Set(TEXT("minecraft:written_book_content"), Json(TEXT("{\"title\": {\"raw\": \"T\"}, \"author\": \"A\", \"pages\": [")
		TEXT("{\"raw\": \"one\"}, {\"raw\": {\"text\": \"tw\", \"extra\": [\"o\"]}}, \"three\", {\"raw\": [\"fo\", {\"text\": \"ur\"}]}, {\"raw\": \"five\"}]}")));

	// The pages, plain, whichever way they are written; a book and quill's too; a book with none has one empty page.
	{
		const TArray<FString> Pages = McBook::GetPages(Book);
		if (TestEqual(TEXT("five pages"), Pages.Num(), 5))
		{
			TestEqual(TEXT("a raw string"), Pages[0], FString(TEXT("one")));
			TestEqual(TEXT("a component with extras"), Pages[1], FString(TEXT("two")));
			TestEqual(TEXT("a bare page"), Pages[2], FString(TEXT("three")));
			TestEqual(TEXT("a list of components"), Pages[3], FString(TEXT("four")));
		}
		FMcItemStack Quill(TEXT("minecraft:writable_book"));
		Quill.Set(TEXT("minecraft:writable_book_content"), Json(TEXT("{\"pages\": [{\"raw\": \"draft\"}]}")));
		TestEqual(TEXT("a book and quill's page"), McBook::GetPages(Quill), TArray<FString>{ TEXT("draft") });
		TestEqual(TEXT("an empty book has one blank page"), McBook::GetPages(FMcItemStack(TEXT("minecraft:writable_book"))).Num(), 1);
		TestTrue(TEXT("both go on a lectern"), McBook::IsLecternBook(TEXT("minecraft:writable_book")) && McBook::IsLecternBook(TEXT("minecraft:written_book")));
		TestFalse(TEXT("a plain book doesn't"), McBook::IsLecternBook(TEXT("minecraft:book")) || McBook::IsLecternBook(TEXT("minecraft:enchanted_book")));
	}

	// The comparator's reading: floor(1 + 14 (P - 1) / (M - 1)), 15 for a book of one page.
	TestEqual(TEXT("one page reads 15"), McBook::GetComparatorSignal(0, 1), 15);
	TestEqual(TEXT("the first page reads 1"), McBook::GetComparatorSignal(0, 5), 1);
	TestEqual(TEXT("the middle of five reads 8"), McBook::GetComparatorSignal(2, 5), 8);
	TestEqual(TEXT("the last reads 15"), McBook::GetComparatorSignal(4, 5), 15);
	TestEqual(TEXT("page 2 of 3 reads 8"), McBook::GetComparatorSignal(1, 3), 8);
	TestEqual(TEXT("page 2 of 4 reads 5"), McBook::GetComparatorSignal(1, 4), 5);

	// The menu turns pages within the book, opens at a page asked for, and takes the book; its state crosses the wire.
	{
		FMcLecternMenu Menu;
		Menu.Pages = McBook::GetPages(Book);
		TestFalse(TEXT("no page before the first"), Menu.ClickButton(FMcLecternMenu::PreviousPage));
		TestTrue(TEXT("on a page"), Menu.ClickButton(FMcLecternMenu::NextPage) && Menu.Page == 1);
		TestTrue(TEXT("straight to the last"), Menu.ClickButton(FMcLecternMenu::GoToPage + 4) && Menu.Page == 4);
		TestFalse(TEXT("no page after the last"), Menu.ClickButton(FMcLecternMenu::NextPage));
		TestFalse(TEXT("nor one past it"), Menu.ClickButton(FMcLecternMenu::GoToPage + 5));
		TestFalse(TEXT("an unknown button"), Menu.ClickButton(50));
		TestTrue(TEXT("back a page"), Menu.ClickButton(FMcLecternMenu::PreviousPage) && Menu.Page == 3);
		TestTrue(TEXT("Take Book is asked"), Menu.ClickButton(FMcLecternMenu::TakeBook) && Menu.bTakeAsked);
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Menu.SerializeState(Writer);
		FMcLecternMenu Copy;
		FMemoryReader Reader(Bytes);
		Copy.SerializeState(Reader);
		TestTrue(TEXT("the pages and page cross"), !Reader.IsError() && Copy.Pages == Menu.Pages && Copy.Page == 3);
	}

	// The block entity keeps the book and its page through a save.
	{
		FMcBlockEntity Lectern = FMcBlockEntity::MakeLectern(Book);
		Lectern.Page = 3;
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		Lectern.Serialize(Writer);
		FMcBlockEntity Loaded;
		FMemoryReader Reader(Bytes);
		TestTrue(TEXT("a lectern loads"), Loaded.Serialize(Reader));
		TestTrue(TEXT("as it was saved"), Loaded == Lectern);
	}

	// A comparator reads the page the book lies open at; a page turn's pulse lights a lamp beside the lectern and stops
	// after 2 ticks.
	{
		FMcBlockWorld World{FMcDimensionType()};
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
			}
		}
		for (int32 X = -8; X <= 8; ++X)
		{
			for (int32 Z = -8; Z <= 8; ++Z)
			{
				World.SetBlock(FIntVector(X, 0, Z), State(TEXT("minecraft:stone")));
			}
		}
		FMcSimulation Sim(World);
		const auto Run = [&](int32 Ticks)
		{
			for (int32 Index = 0; Index < Ticks; ++Index)
			{
				Sim.Tick();
			}
		};
		const FIntVector At(1, 1, 0);
		FMcBlockEntity Lectern = FMcBlockEntity::MakeLectern(Book);
		Lectern.Page = 2;
		// The block first: setting a block takes away the entity of the one it replaces.
		World.SetBlock(At, State(TEXT("minecraft:lectern[facing=north,has_book=true,powered=false]")));
		World.FindChunk(FIntPoint(0, 0))->SetBlockEntity(At.X, At.Y, At.Z, Lectern);
		World.SetBlock(FIntVector(-1, 1, 0), State(TEXT("minecraft:redstone_wire")));
		World.SetBlock(FIntVector(0, 1, 0), State(TEXT("minecraft:comparator[facing=east,mode=compare]")));
		Run(4);
		TestEqual(TEXT("open at the middle of five pages: 8"), Sim.GetDustPower(FIntVector(-1, 1, 0)), 8);

		FMcBlockEntity* Placed = World.FindChunk(FIntPoint(0, 0))->FindBlockEntity(At.X, At.Y, At.Z);
		if (!TestNotNull(TEXT("the lectern keeps its book"), Placed))
		{
			return false;
		}
		Placed->Page = 4;
		World.SetBlock(FIntVector(2, 1, 0), State(TEXT("minecraft:redstone_lamp[lit=false]")));
		World.SetBlock(At, State(TEXT("minecraft:lectern[facing=north,has_book=true,powered=true]")));
		Sim.ScheduleTick(At, 2);
		Run(1);
		TestEqual(TEXT("the pulse lights the lamp"), Registry.GetPropertyValue(World.GetBlock(FIntVector(2, 1, 0)), TEXT("lit")).ToString(), FString(TEXT("true")));
		Run(3);
		TestEqual(TEXT("and ends after 2 ticks"), Registry.GetPropertyValue(World.GetBlock(At), TEXT("powered")).ToString(), FString(TEXT("false")));
		TestEqual(TEXT("the comparator reads the last page: 15"), Sim.GetDustPower(FIntVector(-1, 1, 0)), 15);
		Run(6);
		TestEqual(TEXT("the lamp goes out"), Registry.GetPropertyValue(World.GetBlock(FIntVector(2, 1, 0)), TEXT("lit")).ToString(), FString(TEXT("false")));

		World.FindChunk(FIntPoint(0, 0))->RemoveBlockEntity(At.X, At.Y, At.Z);
		World.SetBlock(At, State(TEXT("minecraft:lectern[facing=north,has_book=false,powered=false]")));
		Run(4);
		TestEqual(TEXT("an empty lectern reads 0"), Sim.GetDustPower(FIntVector(-1, 1, 0)), 0);
	}
	return true;
}

#endif
