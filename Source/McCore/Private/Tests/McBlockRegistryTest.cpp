#include "McBlockRegistry.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBlockRegistryTest, "McClient.McCore.BlockRegistry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBlockRegistryTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("blocks.json loads from cache/"), Registry.EnsureLoaded()))
	{
		return false;
	}

	// The counts the data generator reports for 26.3 (see PLAN.md).
	TestEqual(TEXT("block count"), Registry.NumBlocks(), 1286);
	TestEqual(TEXT("state count"), Registry.NumStates(), 35723);

	TestEqual(TEXT("air is state 0"), Registry.FindDefaultState(TEXT("minecraft:air")).Get(MAX_uint16), static_cast<FMcStateId>(0));
	TestTrue(TEXT("air is air"), Registry.IsAir(0));
	TestTrue(TEXT("cave_air is air"), Registry.IsAir(Registry.FindDefaultState(TEXT("minecraft:cave_air")).GetValue()));
	TestFalse(TEXT("stone is not air"), Registry.IsAir(Registry.FindDefaultState(TEXT("minecraft:stone")).GetValue()));

	TestEqual(TEXT("grass_block default is snowy=false"), Registry.FindDefaultState(TEXT("minecraft:grass_block")).Get(0), static_cast<FMcStateId>(9));
	const TOptional<FMcStateId> SnowyGrass = Registry.FindState(TEXT("minecraft:grass_block"), { { TEXT("snowy"), TEXT("true") } });
	TestEqual(TEXT("grass_block[snowy=true]"), SnowyGrass.Get(0), static_cast<FMcStateId>(8));

	const TOptional<FMcStateId> Stairs = Registry.FindState(TEXT("minecraft:oak_stairs"),
		{ { TEXT("facing"), TEXT("north") }, { TEXT("half"), TEXT("top") }, { TEXT("shape"), TEXT("straight") }, { TEXT("waterlogged"), TEXT("true") } });
	TestEqual(TEXT("oak_stairs state id"), Stairs.Get(0), static_cast<FMcStateId>(5463));
	TestEqual(TEXT("oak_stairs description"), Registry.Describe(Stairs.Get(0)),
		FString(TEXT("minecraft:oak_stairs[facing=north,half=top,shape=straight,waterlogged=true]")));
	TestEqual(TEXT("oak_stairs half"), Registry.GetPropertyValue(Stairs.Get(0), TEXT("half")), FName(TEXT("top")));

	TestFalse(TEXT("unknown property value"), Registry.FindState(TEXT("minecraft:grass_block"), { { TEXT("snowy"), TEXT("maybe") } }).IsSet());
	TestFalse(TEXT("unknown block"), Registry.FindDefaultState(TEXT("minecraft:not_a_block")).IsSet());

	for (int32 Id = 0; Id < Registry.NumStates(); ++Id)
	{
		if (!Registry.IsValidState(static_cast<FMcStateId>(Id)))
		{
			AddError(FString::Printf(TEXT("state id %d is missing from blocks.json"), Id));
			break;
		}
	}
	return true;
}

#endif
