#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBlockLightTest, "McClient.McCore.BlockLight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FMcStateId StateOf(const FMcBlockRegistry& Registry, const TCHAR* Block, const TMap<FName, FName>& Properties = {})
	{
		const TOptional<FMcStateId> State = Registry.FindState(FName(Block), Properties);
		check(State.IsSet());
		return State.GetValue();
	}
}

bool FMcBlockLightTest::RunTest(const FString& Parameters)
{
	FMcBlockLight& Light = FMcBlockLight::Get();
	if (!TestTrue(TEXT("light tables build"), Light.EnsureLoaded()))
	{
		return false;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	auto Emission = [&](const TCHAR* Block, const TMap<FName, FName>& Properties = {}) { return static_cast<int32>(Light.GetEmission(StateOf(Registry, Block, Properties))); };
	auto Dampening = [&](const TCHAR* Block, const TMap<FName, FName>& Properties = {}) { return static_cast<int32>(Light.GetDampening(StateOf(Registry, Block, Properties))); };

	// Emitters, from the wiki's table.
	TestEqual(TEXT("glowstone"), Emission(TEXT("minecraft:glowstone")), 15);
	TestEqual(TEXT("torch"), Emission(TEXT("minecraft:torch")), 14);
	TestEqual(TEXT("wall torch"), Emission(TEXT("minecraft:wall_torch")), 14);
	TestEqual(TEXT("soul torch"), Emission(TEXT("minecraft:soul_torch")), 10);
	TestEqual(TEXT("lit redstone torch"), Emission(TEXT("minecraft:redstone_torch"), { { TEXT("lit"), TEXT("true") } }), 7);
	TestEqual(TEXT("unlit redstone torch"), Emission(TEXT("minecraft:redstone_torch"), { { TEXT("lit"), TEXT("false") } }), 0);
	TestEqual(TEXT("sea lantern"), Emission(TEXT("minecraft:sea_lantern")), 15);
	TestEqual(TEXT("lava"), Emission(TEXT("minecraft:lava")), 15);
	TestEqual(TEXT("lit furnace"), Emission(TEXT("minecraft:furnace"), { { TEXT("lit"), TEXT("true") } }), 13);
	TestEqual(TEXT("unlit furnace"), Emission(TEXT("minecraft:furnace"), { { TEXT("lit"), TEXT("false") } }), 0);
	TestEqual(TEXT("lit smoker"), Emission(TEXT("minecraft:smoker"), { { TEXT("lit"), TEXT("true") } }), 13);
	TestEqual(TEXT("magma block"), Emission(TEXT("minecraft:magma_block")), 3);
	TestEqual(TEXT("glow lichen"), Emission(TEXT("minecraft:glow_lichen")), 7);
	TestEqual(TEXT("lit redstone ore"), Emission(TEXT("minecraft:redstone_ore"), { { TEXT("lit"), TEXT("true") } }), 9);
	TestEqual(TEXT("lit redstone lamp"), Emission(TEXT("minecraft:redstone_lamp"), { { TEXT("lit"), TEXT("true") } }), 15);
	TestEqual(TEXT("soul campfire"), Emission(TEXT("minecraft:soul_campfire"), { { TEXT("lit"), TEXT("true") } }), 10);
	TestEqual(TEXT("cave vines with berries"), Emission(TEXT("minecraft:cave_vines"), { { TEXT("berries"), TEXT("true") } }), 14);
	TestEqual(TEXT("cave vines without berries"), Emission(TEXT("minecraft:cave_vines"), { { TEXT("berries"), TEXT("false") } }), 0);
	TestEqual(TEXT("exposed copper lantern"), Emission(TEXT("minecraft:waxed_exposed_copper_lantern")), 15);
	TestEqual(TEXT("stone"), Emission(TEXT("minecraft:stone")), 0);
	TestEqual(TEXT("air"), Emission(TEXT("minecraft:air")), 0);

	// State-dependent emitters.
	TestEqual(TEXT("light block level 9"), Emission(TEXT("minecraft:light"), { { TEXT("level"), TEXT("9") } }), 9);
	TestEqual(TEXT("lit candle x1"), Emission(TEXT("minecraft:candle"), { { TEXT("candles"), TEXT("1") }, { TEXT("lit"), TEXT("true") } }), 3);
	TestEqual(TEXT("lit candle x4"), Emission(TEXT("minecraft:red_candle"), { { TEXT("candles"), TEXT("4") }, { TEXT("lit"), TEXT("true") } }), 12);
	TestEqual(TEXT("unlit candle x4"), Emission(TEXT("minecraft:candle"), { { TEXT("candles"), TEXT("4") }, { TEXT("lit"), TEXT("false") } }), 0);
	TestEqual(TEXT("lit candle cake"), Emission(TEXT("minecraft:candle_cake"), { { TEXT("lit"), TEXT("true") } }), 3);
	TestEqual(TEXT("sea pickle x4 in water"), Emission(TEXT("minecraft:sea_pickle"), { { TEXT("pickles"), TEXT("4") }, { TEXT("waterlogged"), TEXT("true") } }), 15);
	TestEqual(TEXT("sea pickle x1 in water"), Emission(TEXT("minecraft:sea_pickle"), { { TEXT("pickles"), TEXT("1") }, { TEXT("waterlogged"), TEXT("true") } }), 6);
	TestEqual(TEXT("sea pickle x4 dry"), Emission(TEXT("minecraft:sea_pickle"), { { TEXT("pickles"), TEXT("4") }, { TEXT("waterlogged"), TEXT("false") } }), 0);
	for (int32 Charges = 0; Charges <= 4; ++Charges)
	{
		const int32 Expected[] = { 0, 3, 7, 11, 15 };
		TestEqual(*FString::Printf(TEXT("respawn anchor with %d charges"), Charges),
			Emission(TEXT("minecraft:respawn_anchor"), { { TEXT("charges"), FName(*FString::FromInt(Charges)) } }), Expected[Charges]);
	}
	TestEqual(TEXT("lit copper bulb"), Emission(TEXT("minecraft:copper_bulb"), { { TEXT("lit"), TEXT("true") } }), 15);
	TestEqual(TEXT("lit exposed copper bulb"), Emission(TEXT("minecraft:exposed_copper_bulb"), { { TEXT("lit"), TEXT("true") } }), 12);
	TestEqual(TEXT("lit waxed weathered copper bulb"), Emission(TEXT("minecraft:waxed_weathered_copper_bulb"), { { TEXT("lit"), TEXT("true") } }), 8);
	TestEqual(TEXT("lit oxidized copper bulb"), Emission(TEXT("minecraft:oxidized_copper_bulb"), { { TEXT("lit"), TEXT("true") } }), 4);
	TestEqual(TEXT("unlit copper bulb"), Emission(TEXT("minecraft:copper_bulb"), { { TEXT("lit"), TEXT("false") } }), 0);
	TestEqual(TEXT("inactive vault"), Emission(TEXT("minecraft:vault"), { { TEXT("vault_state"), TEXT("inactive") } }), 6);
	TestEqual(TEXT("active vault"), Emission(TEXT("minecraft:vault"), { { TEXT("vault_state"), TEXT("active") } }), 12);

	// Dampening.
	TestEqual(TEXT("stone"), Dampening(TEXT("minecraft:stone")), 15);
	TestEqual(TEXT("glass"), Dampening(TEXT("minecraft:glass")), 0);
	TestEqual(TEXT("water"), Dampening(TEXT("minecraft:water")), 1);
	TestEqual(TEXT("ice"), Dampening(TEXT("minecraft:ice")), 1);
	TestEqual(TEXT("oak leaves"), Dampening(TEXT("minecraft:oak_leaves")), 1);
	TestEqual(TEXT("cobweb"), Dampening(TEXT("minecraft:cobweb")), 1);
	TestEqual(TEXT("air"), Dampening(TEXT("minecraft:air")), 0);
	TestEqual(TEXT("torch"), Dampening(TEXT("minecraft:torch")), 0);
	TestEqual(TEXT("oak planks"), Dampening(TEXT("minecraft:oak_planks")), 15);
	TestEqual(TEXT("shulker box"), Dampening(TEXT("minecraft:red_shulker_box")), 1);
	TestEqual(TEXT("lantern in water"), Dampening(TEXT("minecraft:lantern"), { { TEXT("waterlogged"), TEXT("true") } }), 1);
	TestEqual(TEXT("lantern in air"), Dampening(TEXT("minecraft:lantern"), { { TEXT("waterlogged"), TEXT("false") } }), 0);
	TestEqual(TEXT("double slab"), Dampening(TEXT("minecraft:oak_slab"), { { TEXT("type"), TEXT("double") } }), 15);
	TestEqual(TEXT("bottom slab"), Dampening(TEXT("minecraft:oak_slab"), { { TEXT("type"), TEXT("bottom") } }), 0);
	TestEqual(TEXT("full snow"), Dampening(TEXT("minecraft:snow"), { { TEXT("layers"), TEXT("8") } }), 15);
	TestEqual(TEXT("snow layer"), Dampening(TEXT("minecraft:snow"), { { TEXT("layers"), TEXT("1") } }), 0);
	TestEqual(TEXT("retracted piston"), Dampening(TEXT("minecraft:piston"), { { TEXT("extended"), TEXT("false") } }), 15);

	// Skylight and shapes.
	auto Skylight = [&](const TCHAR* Block, const TMap<FName, FName>& Properties = {}) { return Light.PropagatesSkylightDown(StateOf(Registry, Block, Properties)); };
	TestTrue(TEXT("sky light goes through air"), Skylight(TEXT("minecraft:air")));
	TestTrue(TEXT("sky light goes through glass"), Skylight(TEXT("minecraft:glass")));
	TestTrue(TEXT("sky light goes through a fence"), Skylight(TEXT("minecraft:oak_fence")));
	TestFalse(TEXT("sky light stops at stone"), Skylight(TEXT("minecraft:stone")));
	TestFalse(TEXT("sky light is dimmed by leaves"), Skylight(TEXT("minecraft:oak_leaves")));
	TestFalse(TEXT("sky light is dimmed by water"), Skylight(TEXT("minecraft:water")));
	TestFalse(TEXT("sky light stops at a slab"), Skylight(TEXT("minecraft:oak_slab"), { { TEXT("type"), TEXT("bottom") } }));
	TestTrue(TEXT("slabs use their shape"), Light.UsesShapeForLight(StateOf(Registry, TEXT("minecraft:oak_slab"))));
	TestTrue(TEXT("stairs use their shape"), Light.UsesShapeForLight(StateOf(Registry, TEXT("minecraft:oak_stairs"))));
	TestFalse(TEXT("stone doesn't"), Light.UsesShapeForLight(StateOf(Registry, TEXT("minecraft:stone"))));

	// Every state has something sensible.
	int32 Bad = 0;
	for (int32 Id = 0; Id < Registry.NumStates(); ++Id)
	{
		const uint8 Emitted = Light.GetEmission(static_cast<FMcStateId>(Id));
		const uint8 Dim = Light.GetDampening(static_cast<FMcStateId>(Id));
		if (Emitted > 15 || (Dim != 0 && Dim != 1 && Dim != 15))
		{
			AddError(FString::Printf(TEXT("%s has emission %d, dampening %d"), *Registry.Describe(static_cast<FMcStateId>(Id)), Emitted, Dim));
			if (++Bad > 10)
			{
				break;
			}
		}
	}
	return true;
}

#endif
