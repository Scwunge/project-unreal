#include "McAquifer.h"
#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McChunk.h"
#include "McNoiseGenerator.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcAquiferTest, "McClient.McGen.Aquifer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcAquiferTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Blocks.EnsureLoaded() && FMcBiomeRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcNoiseSettings Settings;
	if (!TestTrue(TEXT("overworld settings load"), FMcNoiseSettings::Load(TEXT("minecraft:overworld"), Settings)))
	{
		return false;
	}
	TestTrue(TEXT("the overworld has aquifers"), Settings.Aquifers.IsValid());
	const FMcNoiseGenerator Generator(Settings, 12345, TEXT("minecraft:overworld"));
	if (!TestTrue(TEXT("generator is valid"), Generator.IsValid()))
	{
		return false;
	}

	const FMcStateId Air = Blocks.FindDefaultState(TEXT("minecraft:air")).Get(0);
	const FMcStateId Water = Blocks.FindDefaultState(TEXT("minecraft:water")).Get(0);
	const FMcStateId Lava = Blocks.FindDefaultState(TEXT("minecraft:lava")).Get(0);

	// Caves underground hold air, water and lava, and fluid rarely sits beside air at the same height (a leak).
	int32 CaveAir = 0;
	int32 CaveWater = 0;
	int32 CaveLava = 0;
	int32 DeepLava = 0;
	int32 Leaks = 0;
	int32 FluidSides = 0;
	for (int32 ChunkIndex = 0; ChunkIndex < 16; ++ChunkIndex)
	{
		FMcChunk Chunk(FIntPoint((ChunkIndex % 4) * 9 - 18, (ChunkIndex / 4) * 9 - 18), -64, 24);
		Generator.Generate(Chunk);
		for (int32 Y = -64; Y < 30; ++Y)
		{
			for (int32 Z = 0; Z < 16; ++Z)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					const FMcStateId Block = Chunk.GetBlock(X, Y, Z);
					if (Y < -54)
					{
						DeepLava += Block == Lava ? 1 : 0;
					}
					CaveAir += Block == Air ? 1 : 0;
					CaveWater += Block == Water ? 1 : 0;
					CaveLava += Block == Lava && Y >= -54 ? 1 : 0;
					if ((Block == Water || Block == Lava) && X < 15 && Z < 15)
					{
						for (const FIntPoint Side : { FIntPoint(1, 0), FIntPoint(0, 1) })
						{
							++FluidSides;
							Leaks += Chunk.GetBlock(X + Side.X, Y, Z + Side.Y) == Air ? 1 : 0;
						}
					}
				}
			}
		}
	}
	AddInfo(FString::Printf(TEXT("Below y 30: %d air, %d water, %d lava above -54, %d lava below; %d of %d fluid sides touch air"),
		CaveAir, CaveWater, CaveLava, DeepLava, Leaks, FluidSides));
	TestTrue(TEXT("caves have air"), CaveAir > 0);
	TestTrue(TEXT("some caves are flooded"), CaveWater > 0);
	TestTrue(TEXT("few fluid sides touch air"), Leaks * 20 < FluidSides);
	return true;
}

#endif
