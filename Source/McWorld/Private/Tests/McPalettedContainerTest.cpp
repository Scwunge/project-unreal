#include "McPalettedContainer.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPalettedContainerTest, "McClient.McWorld.PalettedContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcPalettedContainerTest::RunTest(const FString& Parameters)
{
	FMcPalettedContainer Container(7);
	TestTrue(TEXT("starts uniform"), Container.IsUniform());
	TestEqual(TEXT("uniform read"), static_cast<int32>(Container.Get(4095)), 7);

	// Write a growing number of distinct states, checking every entry against a plain array as it goes.
	TArray<FMcStateId> Expected;
	Expected.Init(7, FMcPalettedContainer::NumEntries);
	FRandomStream Random(1234);
	for (int32 Step = 0; Step < 20000; ++Step)
	{
		const int32 Index = Random.RandHelper(FMcPalettedContainer::NumEntries);
		// Few distinct values at first, then up to 1,000, so the container passes through 4..8 bits and direct.
		const int32 Distinct = Step < 5000 ? 10 : (Step < 10000 ? 200 : 1000);
		const FMcStateId Value = static_cast<FMcStateId>(Random.RandHelper(Distinct) * 31);
		Container.Set(Index, Value);
		Expected[Index] = Value;

		if (Step == 4999)
		{
			TestEqual(TEXT("10 states use 4 bits"), Container.GetBitsPerEntry(), 4);
		}
		if (Step == 9999)
		{
			TestEqual(TEXT("~200 states use 8 bits"), Container.GetBitsPerEntry(), 8);
		}
	}
	TestTrue(TEXT("1,000 states go direct"), Container.IsDirect());

	TArray<FMcStateId> Copy;
	Copy.SetNumUninitialized(FMcPalettedContainer::NumEntries);
	Container.CopyTo(Copy);
	for (int32 Index = 0; Index < FMcPalettedContainer::NumEntries; ++Index)
	{
		if (Container.Get(Index) != Expected[Index] || Copy[Index] != Expected[Index])
		{
			AddError(FString::Printf(TEXT("entry %d is %d, expected %d"), Index, Container.Get(Index), Expected[Index]));
			break;
		}
	}

	Container.Fill(3);
	TestTrue(TEXT("fill makes it uniform"), Container.IsUniform());
	TestEqual(TEXT("filled read"), static_cast<int32>(Container.Get(100)), 3);
	return true;
}

#endif
