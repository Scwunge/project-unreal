#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McItemRegistry.h"
#include "McSimulation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcRedstoneOreTest, "McClient.McSim.RedstoneOre",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

// Reference run redore (tools/refscripts/redore.txt): a click on a redstone or deepslate redstone ore lights it, so does standing on it
// (LightRedstoneOre), and a lit one goes out on a random tick (about one in 1400 ticks at random tick speed 3 in the log).
// UNVERIFIED: a hit that starts breaking it lights it too (wiki only).
bool FMcRedstoneOreTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcBlockTags::Get().EnsureLoaded()
		&& FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	FMcBlockWorld World{FMcDimensionType()};
	World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	FMcSimulation Sim(World);
	const auto Lit = [&](const FIntVector& Pos) { return Registry.GetPropertyValue(World.GetBlock(Pos), TEXT("lit")) == TEXT("true"); };
	const FIntVector A(2, 1, 2), B(5, 1, 2), C(8, 1, 2), Stone(11, 1, 2);
	World.SetBlock(A, State(TEXT("minecraft:redstone_ore")));
	World.SetBlock(B, State(TEXT("minecraft:deepslate_redstone_ore")));
	World.SetBlock(C, State(TEXT("minecraft:redstone_ore")));
	World.SetBlock(Stone, State(TEXT("minecraft:stone")));
	TestFalse(TEXT("an ore starts dark"), Lit(A));
	Sim.UseBlock(A, 0.0f);
	TestTrue(TEXT("a click lights it"), Lit(A));
	Sim.UseBlock(B, 0.0f);
	TestTrue(TEXT("a deepslate ore too"), Lit(B));
	Sim.LightRedstoneOre(C);
	TestTrue(TEXT("standing on it lights it"), Lit(C));
	Sim.LightRedstoneOre(Stone);
	TestEqual(TEXT("stone is left alone"), World.GetBlock(Stone), State(TEXT("minecraft:stone")));
	Sim.RandomTickBlock(A);
	TestFalse(TEXT("a random tick puts a lit ore out"), Lit(A));
	Sim.RandomTickBlock(A);
	TestFalse(TEXT("and a dark one stays dark"), Lit(A));
	return true;
}

#endif
