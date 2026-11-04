#include "McMob.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcWolfTest, "McClient.McServer.WolfState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcWolfTest::RunTest(const FString& Parameters)
{
	const FMcMobType* Type = FMcMobType::Find(TEXT("minecraft:wolf"));
	if (!TestNotNull(TEXT("wolf type"), Type))
	{
		return false;
	}
	FMcMob Wolf(*Type);
	Wolf.UpdateWolfPose();
	TestEqual(TEXT("wild wolf stands in the default collar"), Wolf.ArmPose, 14 << 8);
	Wolf.bTamed = true;
	Wolf.bSitting = true;
	Wolf.CollarColor = 3;
	Wolf.OwnerName = TEXT("Steve");
	Wolf.UpdateWolfPose();
	TestEqual(TEXT("tame and sitting"), Wolf.ArmPose, 1 | 4 | (3 << 8));
	Wolf.bTamed = false;
	Wolf.bSitting = false;
	Wolf.AngerTicks = 10;
	Wolf.UpdateWolfPose();
	TestEqual(TEXT("angry"), Wolf.ArmPose & 7, 2);

	Wolf.bTamed = true;
	Wolf.bSitting = true;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	TestTrue(TEXT("saves"), Wolf.Serialize(Writer));
	FMcMob Loaded(*Type);
	FMemoryReader Reader(Bytes);
	TestTrue(TEXT("loads"), Loaded.Serialize(Reader));
	TestTrue(TEXT("tame kept"), Loaded.bTamed);
	TestTrue(TEXT("sitting kept"), Loaded.bSitting);
	TestEqual(TEXT("collar kept"), Loaded.CollarColor, 3);
	TestEqual(TEXT("owner kept"), Loaded.OwnerName, FString(TEXT("Steve")));
	return true;
}

#endif
