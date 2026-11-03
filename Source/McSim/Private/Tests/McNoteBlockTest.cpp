#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McNoteBlock.h"
#include "McSimulation.h"
#include "McSound.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcNoteBlockTest, "McClient.McSim.NoteBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcNoteBlockTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };

	// The numbers measured in the reference client.
	TestTrue(TEXT("pitch 0 is 0.5"), FMath::IsNearlyEqual(McNoteBlock::GetPitch(0), 0.5f, 1e-4f));
	TestTrue(TEXT("pitch 12 is 1"), FMath::IsNearlyEqual(McNoteBlock::GetPitch(12), 1.0f, 1e-4f));
	TestTrue(TEXT("pitch 6 is 0.7071"), FMath::IsNearlyEqual(McNoteBlock::GetPitch(6), 0.7071f, 1e-4f));
	TestTrue(TEXT("pitch 24 is 2"), FMath::IsNearlyEqual(McNoteBlock::GetPitch(24), 2.0f, 1e-4f));
	TestEqual(TEXT("planks bass"), McNoteBlock::GetInstrumentBelow(FName("minecraft:oak_planks")), FName("bass"));
	TestEqual(TEXT("oak stairs bass"), McNoteBlock::GetInstrumentBelow(FName("minecraft:oak_stairs")), FName("bass"));
	TestEqual(TEXT("stone bass drum"), McNoteBlock::GetInstrumentBelow(FName("minecraft:stone")), FName("basedrum"));
	TestEqual(TEXT("dirt harp"), McNoteBlock::GetInstrumentBelow(FName("minecraft:dirt")), FName("harp"));
	TestEqual(TEXT("glass hat"), McNoteBlock::GetInstrumentBelow(FName("minecraft:glass")), FName("hat"));
	TestEqual(TEXT("clay flute"), McNoteBlock::GetInstrumentBelow(FName("minecraft:clay")), FName("flute"));
	TestEqual(TEXT("gold bell"), McNoteBlock::GetInstrumentBelow(FName("minecraft:gold_block")), FName("bell"));
	TestEqual(TEXT("copper trumpet"), McNoteBlock::GetInstrumentBelow(FName("minecraft:copper_block")), FName("trumpet"));
	TestEqual(TEXT("zombie head"), McNoteBlock::GetInstrumentAbove(FName("minecraft:zombie_head")), FName("zombie"));
	TestTrue(TEXT("stone is no head"), McNoteBlock::GetInstrumentAbove(FName("minecraft:stone")).IsNone());
	TestTrue(TEXT("note colours: 0 green, 12 blue"), McNoteBlock::GetNoteColour(0) == FColor(0x59, 0xE8, 0x00) && McNoteBlock::GetNoteColour(12) == FColor(0x59, 0x00, 0xE8));
	{
		const FMcSoundEvent Creeper = McNoteBlock::GetSound(FName("creeper"), 20, FVector::ZeroVector);
		TestTrue(TEXT("a head keeps its own pitch"), Creeper.Name == FName("block.note_block.imitate.creeper") && FMath::IsNearlyEqual(Creeper.Pitch, 0.5f));
		TestTrue(TEXT("a player's head is silent"), McNoteBlock::GetSound(FName("custom_head"), 0, FVector::ZeroVector).Name.IsNone());
		TestTrue(TEXT("volume 3"), FMath::IsNearlyEqual(McNoteBlock::GetSound(FName("harp"), 12, FVector::ZeroVector).Volume, 3.0f));
	}

	// In a world.
	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	FMcSimulation Sim(World);
	TArray<FMcSoundEvent> Heard;
	Sim.OnSound = [&Heard](const FMcSoundEvent& Event) { Heard.Add(Event); };
	const FIntVector Pos(4, 5, 4);
	const auto Property = [&](const TCHAR* Name) { return Registry.GetPropertyValue(World.GetBlock(Pos), Name).ToString(); };
	World.SetBlock(Pos + FIntVector(0, -1, 0), State(TEXT("minecraft:oak_planks")));
	World.SetBlock(Pos, State(TEXT("minecraft:note_block")));
	TestEqual(TEXT("it takes its instrument from what is below"), Property(TEXT("instrument")), FString(TEXT("bass")));
	World.SetBlock(Pos + FIntVector(0, -1, 0), State(TEXT("minecraft:glass")));
	TestEqual(TEXT("and follows it"), Property(TEXT("instrument")), FString(TEXT("hat")));

	TestTrue(TEXT("a note sounds"), Sim.PlayNote(Pos));
	TestTrue(TEXT("hat at the block, note 0"), Heard.Num() == 1 && Heard[0].Name == FName("block.note_block.hat") && FMath::IsNearlyEqual(Heard[0].Pitch, 0.5f, 1e-4f));
	for (int32 Use = 0; Use < 12; ++Use)
	{
		Sim.TuneNote(Pos);
	}
	TestEqual(TEXT("twelve uses: note 12"), Property(TEXT("note")), FString(TEXT("12")));
	TestTrue(TEXT("each plays"), Heard.Num() == 13 && FMath::IsNearlyEqual(Heard.Last().Pitch, 1.0f, 1e-4f));
	for (int32 Use = 0; Use < 13; ++Use)
	{
		Sim.TuneNote(Pos);
	}
	TestEqual(TEXT("the 25th use is back at 0"), Property(TEXT("note")), FString(TEXT("0")));

	Heard.Reset();
	World.SetBlock(Pos + FIntVector(0, 1, 0), State(TEXT("minecraft:stone")));
	TestFalse(TEXT("a block above silences it"), Sim.PlayNote(Pos));
	Sim.TuneNote(Pos);
	TestTrue(TEXT("but it still steps"), Property(TEXT("note")) == TEXT("1") && Heard.IsEmpty());
	World.SetBlock(Pos + FIntVector(0, 1, 0), State(TEXT("minecraft:skeleton_skull[rotation=0]")));
	TestEqual(TEXT("a head over it gives its voice"), Property(TEXT("instrument")), FString(TEXT("skeleton")));
	TestTrue(TEXT("and sounds"), Sim.PlayNote(Pos) && Heard.Num() == 1 && Heard[0].Name == FName("block.note_block.imitate.skeleton"));
	return true;
}

#endif
