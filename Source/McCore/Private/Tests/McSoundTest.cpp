#include "McSound.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcSoundTest, "McClient.McCore.Sound",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcSoundTest::RunTest(const FString& Parameters)
{
	using namespace McSound;
	const auto Group = [](const TCHAR* Block) { return GetBlockGroup(FName(Block)); };

	// Block sound groups, by the block's material.
	TestEqual(TEXT("stone"), Group(TEXT("minecraft:stone")), FName("stone"));
	TestEqual(TEXT("cobblestone"), Group(TEXT("minecraft:cobblestone")), FName("stone"));
	TestEqual(TEXT("an ore is stone"), Group(TEXT("minecraft:iron_ore")), FName("stone"));
	TestEqual(TEXT("a stone slab"), Group(TEXT("minecraft:stone_slab")), FName("stone"));
	TestEqual(TEXT("deepslate"), Group(TEXT("minecraft:deepslate")), FName("deepslate"));
	TestEqual(TEXT("a deepslate ore"), Group(TEXT("minecraft:deepslate_iron_ore")), FName("deepslate"));
	TestEqual(TEXT("planks"), Group(TEXT("minecraft:oak_planks")), FName("wood"));
	TestEqual(TEXT("a wooden stair"), Group(TEXT("minecraft:spruce_stairs")), FName("wood"));
	TestEqual(TEXT("a log"), Group(TEXT("minecraft:birch_log")), FName("wood"));
	TestEqual(TEXT("cherry wood has its own"), Group(TEXT("minecraft:cherry_planks")), FName("cherry_wood"));
	TestEqual(TEXT("a crimson stem"), Group(TEXT("minecraft:crimson_stem")), FName("stem"));
	TestEqual(TEXT("crimson planks"), Group(TEXT("minecraft:crimson_planks")), FName("nether_wood"));
	TestEqual(TEXT("grass"), Group(TEXT("minecraft:grass_block")), FName("grass"));
	TestEqual(TEXT("leaves are grass"), Group(TEXT("minecraft:oak_leaves")), FName("grass"));
	TestEqual(TEXT("a sapling is grass"), Group(TEXT("minecraft:oak_sapling")), FName("grass"));
	TestEqual(TEXT("dirt sounds like gravel"), Group(TEXT("minecraft:dirt")), FName("gravel"));
	TestEqual(TEXT("gravel"), Group(TEXT("minecraft:gravel")), FName("gravel"));
	TestEqual(TEXT("sand"), Group(TEXT("minecraft:sand")), FName("sand"));
	TestEqual(TEXT("sandstone is stone"), Group(TEXT("minecraft:sandstone")), FName("stone"));
	TestEqual(TEXT("glass"), Group(TEXT("minecraft:glass")), FName("glass"));
	TestEqual(TEXT("a glass pane"), Group(TEXT("minecraft:blue_stained_glass_pane")), FName("glass"));
	TestEqual(TEXT("ice"), Group(TEXT("minecraft:packed_ice")), FName("glass"));
	TestEqual(TEXT("wool"), Group(TEXT("minecraft:red_wool")), FName("wool"));
	TestEqual(TEXT("carpet"), Group(TEXT("minecraft:white_carpet")), FName("wool"));
	TestEqual(TEXT("snow"), Group(TEXT("minecraft:snow")), FName("snow"));
	TestEqual(TEXT("an iron block"), Group(TEXT("minecraft:iron_block")), FName("metal"));
	TestEqual(TEXT("an anvil"), Group(TEXT("minecraft:anvil")), FName("anvil"));
	TestEqual(TEXT("copper"), Group(TEXT("minecraft:copper_block")), FName("copper"));
	TestEqual(TEXT("copper ore is stone"), Group(TEXT("minecraft:copper_ore")), FName("stone"));
	TestEqual(TEXT("netherrack"), Group(TEXT("minecraft:netherrack")), FName("netherrack"));
	TestEqual(TEXT("nether bricks"), Group(TEXT("minecraft:nether_brick_stairs")), FName("nether_bricks"));
	TestEqual(TEXT("mangrove roots keep their own"), Group(TEXT("minecraft:mangrove_roots")), FName("mangrove_roots"));
	TestEqual(TEXT("bare name works too"), Group(TEXT("stone")), FName("stone"));
	TestTrue(TEXT("air makes none"), Group(TEXT("minecraft:air")).IsNone());

	// The sounds of a block and how loud: break and place at (volume + 1) / 2 and 0.8 of the pitch, steps at 0.15.
	const FVector Where(10.0, 64.0, -3.0);
	FMcSoundEvent Event = GetBlockSound(FName("minecraft:stone"), EBlockSound::Break, Where);
	TestEqual(TEXT("break event"), Event.Name, FName("block.stone.break"));
	TestTrue(TEXT("full volume"), FMath::IsNearlyEqual(Event.Volume, 1.0f));
	TestTrue(TEXT("pitch 0.8"), FMath::IsNearlyEqual(Event.Pitch, 0.8f));
	TestEqual(TEXT("at the block"), Event.Position, Where);
	Event = GetBlockSound(FName("minecraft:oak_planks"), EBlockSound::Place, Where);
	TestEqual(TEXT("place event"), Event.Name, FName("block.wood.place"));
	TestTrue(TEXT("placed at pitch 0.8"), FMath::IsNearlyEqual(Event.Pitch, 0.8f));
	Event = GetBlockSound(FName("minecraft:gravel"), EBlockSound::Step, Where);
	TestEqual(TEXT("step event"), Event.Name, FName("block.gravel.step"));
	TestTrue(TEXT("steps are quiet"), FMath::IsNearlyEqual(Event.Volume, 0.15f) && FMath::IsNearlyEqual(Event.Pitch, 1.0f));
	Event = GetBlockSound(FName("minecraft:stone"), EBlockSound::Hit, Where);
	TestEqual(TEXT("hit event"), Event.Name, FName("block.stone.hit"));
	TestTrue(TEXT("hits are quieter still, and low"), FMath::IsNearlyEqual(Event.Volume, 0.25f) && FMath::IsNearlyEqual(Event.Pitch, 0.5f));
	Event = GetBlockSound(FName("minecraft:iron_block"), EBlockSound::Break, Where);
	TestTrue(TEXT("metal plays high"), FMath::IsNearlyEqual(Event.Pitch, 1.2f));
	Event = GetBlockSound(FName("minecraft:anvil"), EBlockSound::Step, Where);
	TestTrue(TEXT("an anvil is quiet"), FMath::IsNearlyEqual(Event.Volume, 0.3f * 0.15f));
	TestTrue(TEXT("no event for air"), GetBlockSound(FName("minecraft:air"), EBlockSound::Break, Where).Name.IsNone());

	// Mobs' own sounds.
	TestEqual(TEXT("zombie hurt"), GetMobSoundName(FName("minecraft:zombie"), EMobSound::Hurt), FName("entity.zombie.hurt"));
	TestEqual(TEXT("pig ambient"), GetMobSoundName(FName("minecraft:pig"), EMobSound::Ambient), FName("entity.pig.ambient"));
	TestEqual(TEXT("spider death"), GetMobSoundName(FName("minecraft:spider"), EMobSound::Death), FName("entity.spider.death"));
	TestEqual(TEXT("skeleton step"), GetMobSoundName(FName("minecraft:skeleton"), EMobSound::Step), FName("entity.skeleton.step"));
	TestTrue(TEXT("a creeper doesn't talk"), GetMobSoundName(FName("minecraft:creeper"), EMobSound::Ambient).IsNone());
	TestEqual(TEXT("but it is hurt"), GetMobSoundName(FName("minecraft:creeper"), EMobSound::Hurt), FName("entity.creeper.hurt"));
	TestEqual(TEXT("a small slime"), GetMobSoundName(FName("minecraft:slime"), EMobSound::Hurt, 1), FName("entity.slime.hurt_small"));
	TestEqual(TEXT("and a big one"), GetMobSoundName(FName("minecraft:slime"), EMobSound::Death, 4), FName("entity.slime.death"));
	TestTrue(TEXT("an unknown mob is silent"), GetMobSoundName(FName("minecraft:bat"), EMobSound::Hurt).IsNone());
	Event = GetMobStep(FName("minecraft:zombie"), FName("minecraft:stone"), Where);
	TestTrue(TEXT("a zombie's step is its own, at 0.15"), Event.Name == FName("entity.zombie.step") && FMath::IsNearlyEqual(Event.Volume, 0.15f));
	Event = GetMobStep(FName("minecraft:creeper"), FName("minecraft:grass_block"), Where);
	TestTrue(TEXT("a creeper's is the block's"), Event.Name == FName("block.grass.step") && FMath::IsNearlyEqual(Event.Volume, 0.15f));

	// How far, how loud: straight down to nothing at 16 blocks, and over a volume of 1 only further.
	TestTrue(TEXT("audible 16 blocks"), FMath::IsNearlyEqual(GetAudibleRange(1.0f), 16.0f));
	TestTrue(TEXT("a quiet sound has the same reach"), FMath::IsNearlyEqual(GetAudibleRange(0.25f), 16.0f));
	TestTrue(TEXT("an explosion's 4 reaches 64"), FMath::IsNearlyEqual(GetAudibleRange(4.0f), 64.0f));
	TestTrue(TEXT("full beside you"), FMath::IsNearlyEqual(GetGain(1.0f, 0.0f), 1.0f));
	TestTrue(TEXT("half at 8"), FMath::IsNearlyEqual(GetGain(1.0f, 8.0f), 0.5f));
	TestTrue(TEXT("a quarter at 12"), FMath::IsNearlyEqual(GetGain(1.0f, 12.0f), 0.25f));
	TestTrue(TEXT("nothing at 16"), FMath::IsNearlyEqual(GetGain(1.0f, 16.0f), 0.0f));
	TestTrue(TEXT("and beyond"), FMath::IsNearlyEqual(GetGain(1.0f, 40.0f), 0.0f));
	TestTrue(TEXT("a step at 0.15 is 0.075 half way"), FMath::IsNearlyEqual(GetGain(0.15f, 8.0f), 0.075f));
	TestTrue(TEXT("a loud sound is no louder than 1"), FMath::IsNearlyEqual(GetGain(4.0f, 0.0f), 1.0f));
	TestTrue(TEXT("but still half way at 32"), FMath::IsNearlyEqual(GetGain(4.0f, 32.0f), 0.5f));
	TestTrue(TEXT("a sound with a shorter reach (12)"), FMath::IsNearlyEqual(GetGain(1.0f, 6.0f, 12.0f), 0.5f));

	// Voices vary a fifth either way.
	TestTrue(TEXT("pitch 1 on equal draws"), FMath::IsNearlyEqual(GetVoicePitch(0.3f, 0.3f), 1.0f));
	TestTrue(TEXT("up to 1.2"), FMath::IsNearlyEqual(GetVoicePitch(1.0f, 0.0f), 1.2f));
	TestTrue(TEXT("down to 0.8"), FMath::IsNearlyEqual(GetVoicePitch(0.0f, 1.0f), 0.8f));

	// Ambient sounds: the counter climbs, a roll under it speaks, and then it starts again at minus 80.
	int32 Counter = 0;
	TestFalse(TEXT("nothing from a counter of 0"), TickAmbient(Counter, 0));
	TestEqual(TEXT("which counts up"), Counter, 1);
	TestFalse(TEXT("a roll at the counter is too high"), TickAmbient(Counter, 5));
	Counter = 500;
	TestTrue(TEXT("a roll under the counter speaks"), TickAmbient(Counter, 100));
	TestEqual(TEXT("and the counter starts again below zero"), Counter, -80);
	int32 Spoke = 0;
	Counter = -80;
	FRandomStream Random(3);
	for (int32 Tick = 0; Tick < 200000; ++Tick)
	{
		Spoke += TickAmbient(Counter, Random.RandHelper(1000)) ? 1 : 0;
	}
	// After the 80 ticks of quiet it speaks within about 40 more (the chance is c / 1000 on tick c): some 120 ticks, 6
	// seconds, between sounds; 200000 ticks is about 1650 of them.
	TestTrue(*FString::Printf(TEXT("a mob speaks every 6 seconds or so (%d times in 10000 s)"), Spoke), Spoke > 1400 && Spoke < 1900);

	// Footsteps: the first bit of walking steps, then one each 1/0.6 blocks; standing still makes none.
	FStepCounter Steps;
	TestTrue(TEXT("the first move steps"), Steps.Walk(0.2));
	TestFalse(TEXT("standing still doesn't"), Steps.Walk(0.0));
	int32 NumSteps = 1;
	for (int32 Tick = 0; Tick < 49; ++Tick)
	{
		NumSteps += Steps.Walk(0.2) ? 1 : 0;
	}
	TestEqual(TEXT("10 blocks walked is 6 steps"), NumSteps, 6);

	// Landing: more than 4 damage is a big fall.
	TestEqual(TEXT("4 damage is a small fall"), GetPlayerFallSound(4), FName(TEXT("entity.player.small_fall")));
	TestEqual(TEXT("5 is a big one"), GetPlayerFallSound(5), FName(TEXT("entity.player.big_fall")));
	return true;
}

#endif
