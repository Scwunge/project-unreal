#include "McBlockRegistry.h"
#include "McChunk.h"
#include "McChunkCodec.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcChunkCodecTest, "McClient.McNet.ChunkCodec",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcChunkCodecTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()))
	{
		return false;
	}
	auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };

	// A flat world's chunk with a few odd blocks, including a state with a high id.
	FMcChunk Chunk(FIntPoint(3, -2), -64, 24);
	Chunk.FillLayer(-64, State(TEXT("minecraft:bedrock")));
	Chunk.FillLayer(-63, State(TEXT("minecraft:dirt")));
	Chunk.FillLayer(-62, State(TEXT("minecraft:dirt")));
	Chunk.FillLayer(-61, State(TEXT("minecraft:grass_block")));
	Chunk.SetBlock(5, 100, 7, State(TEXT("minecraft:oak_stairs[facing=east,half=top]")));
	Chunk.SetBlock(0, 319, 15, Registry.NumStates() - 1);
	for (int32 X = 0; X < 16; ++X)
	{
		Chunk.SetBlock(X, 0, X, State(TEXT("minecraft:stone")));
	}

	const TArray<uint8> Bytes = McChunkCodec::Encode(Chunk);
	TestTrue(TEXT("a mostly flat chunk is small"), Bytes.Num() > 0 && Bytes.Num() < 2048);
	AddInfo(FString::Printf(TEXT("encoded size: %d bytes"), Bytes.Num()));

	FMcChunk Copy(FIntPoint(3, -2), -64, 24);
	if (!TestTrue(TEXT("decodes"), McChunkCodec::Decode(Bytes, Copy)))
	{
		return false;
	}
	bool bSame = true;
	for (int32 Y = -64; Y < 320 && bSame; ++Y)
	{
		for (int32 Z = 0; Z < 16 && bSame; ++Z)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				if (Chunk.GetBlock(X, Y, Z) != Copy.GetBlock(X, Y, Z))
				{
					AddError(FString::Printf(TEXT("differs at %d %d %d"), X, Y, Z));
					bSame = false;
					break;
				}
			}
		}
	}
	TestTrue(TEXT("every block comes back"), bSame);
	for (int32 Index = 0; Index < 24; ++Index)
	{
		TestEqual(TEXT("non-air counts match"), Copy.GetSection(Index).NonAirCount, Chunk.GetSection(Index).NonAirCount);
	}

	FMcChunk Short(FIntPoint(3, -2), 0, 16);
	TestFalse(TEXT("refuses a chunk of another height"), McChunkCodec::Decode(Bytes, Short));
	AddExpectedError(TEXT("Failed to uncompress memory"), EAutomationExpectedErrorFlags::Contains, 1);
	TArray<uint8> Damaged = Bytes;
	Damaged.SetNum(Damaged.Num() / 2);
	TestFalse(TEXT("refuses cut-off bytes"), McChunkCodec::Decode(Damaged, Copy));
	return true;
}

#endif
