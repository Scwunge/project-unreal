#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Bone meal on 40 young plants of each kind, as the vanilla reference did through dispensers (tools/refscripts/gen_bonemeal.py, run bonemeal):
// the plant states after one meal, logged as "mcbone kind=K ..." lines for tools/refscripts/bonecmp.py.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBonemealFieldTest, "McClient.McSim.BonemealField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBonemealFieldTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const FString& Text) { return Registry.ParseState(*Text).Get(0); };
	struct FKind { const TCHAR* Name; const TCHAR* Plant; const TCHAR* Ground; };
	const FKind Kinds[] = {
		{ TEXT("wheat"), TEXT("minecraft:wheat[age=0]"), TEXT("minecraft:farmland[moisture=7]") },
		{ TEXT("wheat4"), TEXT("minecraft:wheat[age=4]"), TEXT("minecraft:farmland[moisture=7]") },
		{ TEXT("carrots"), TEXT("minecraft:carrots[age=0]"), TEXT("minecraft:farmland[moisture=7]") },
		{ TEXT("potatoes"), TEXT("minecraft:potatoes[age=0]"), TEXT("minecraft:farmland[moisture=7]") },
		{ TEXT("beetroots"), TEXT("minecraft:beetroots[age=0]"), TEXT("minecraft:farmland[moisture=7]") },
		{ TEXT("melon_stem"), TEXT("minecraft:melon_stem[age=0]"), TEXT("minecraft:farmland[moisture=7]") },
		{ TEXT("pumpkin_stem"), TEXT("minecraft:pumpkin_stem[age=3]"), TEXT("minecraft:farmland[moisture=7]") },
		{ TEXT("sweet_berry_bush"), TEXT("minecraft:sweet_berry_bush[age=0]"), TEXT("minecraft:dirt") },
	};
	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	FMcSimulation Sim(World);
	for (const FKind& Kind : Kinds)
	{
		TMap<FString, int32> Counts;
		for (int32 Copy = 0; Copy < 40; ++Copy)
		{
			const FIntVector Pos(Copy % 16, 1, 0);
			World.SetBlock(Pos + FIntVector(0, -1, 0), State(Kind.Ground));
			World.SetBlock(Pos, State(Kind.Plant));
			Sim.Bonemeal(Pos);
			const FMcStateId After = World.GetBlock(Pos);
			FString Key = Registry.GetBlockOf(After).Name.ToString();
			const FName Age = Registry.GetPropertyValue(After, TEXT("age"));
			if (!Age.IsNone()) Key += FString::Printf(TEXT("[age=%s]"), *Age.ToString());
			Counts.FindOrAdd(Key)++;
		}
		FString Out;
		for (const TPair<FString, int32>& Pair : Counts) Out += FString::Printf(TEXT("%s=%d "), *Pair.Key, Pair.Value);
		UE_LOG(LogTemp, Display, TEXT("mcbone kind=%s %s"), Kind.Name, *Out);
	}
	return true;
}

#endif
