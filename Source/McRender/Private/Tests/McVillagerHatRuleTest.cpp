#include "McEntityModel.h"
#include "McPlayerModel.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Which villagers show their biome's hat layer (the desert and snow wraps). Measured against vanilla shots (2026-11-11, villhat3_*): shown on a
// jobless villager and a nitwit, hidden under a farmer, librarian (hat full) and butcher (hat partial, the type's own value full).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcVillagerHatRuleTest, "McClient.McRender.VillagerHatRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcVillagerHatRuleTest::RunTest(const FString& Parameters)
{
	const auto TypeHidden = [](bool bZombie, const TCHAR* Type, const TCHAR* Job)
	{
		const TArray<FString> Textures = McEntityModel::GetVillagerTextures(bZombie, FName(Type), FName(Job), 1);
		return Textures.IsValidIndex(1) && Textures[1].EndsWith(TEXT("?nohat"));
	};
	TestFalse(TEXT("desert shows on a jobless villager"), TypeHidden(false, TEXT("minecraft:desert"), TEXT("")));
	TestFalse(TEXT("desert shows on a nitwit"), TypeHidden(false, TEXT("minecraft:desert"), TEXT("minecraft:nitwit")));
	TestFalse(TEXT("desert shows on an armorer (no hat value)"), TypeHidden(false, TEXT("minecraft:desert"), TEXT("minecraft:armorer")));
	TestTrue(TEXT("desert is hidden under a farmer"), TypeHidden(false, TEXT("minecraft:desert"), TEXT("minecraft:farmer")));
	TestTrue(TEXT("desert is hidden under a librarian"), TypeHidden(false, TEXT("minecraft:desert"), TEXT("minecraft:librarian")));
	TestTrue(TEXT("snow is hidden under a butcher (partial, but snow's own value is full)"), TypeHidden(false, TEXT("minecraft:snow"), TEXT("minecraft:butcher")));
	TestTrue(TEXT("desert is hidden under a butcher"), TypeHidden(false, TEXT("minecraft:desert"), TEXT("minecraft:butcher")));
	TestFalse(TEXT("plains shows under a butcher (partial, the type is not full)"), TypeHidden(false, TEXT("minecraft:plains"), TEXT("minecraft:butcher")));
	TestTrue(TEXT("a zombie villager's desert is hidden under a farmer"), TypeHidden(true, TEXT("minecraft:desert"), TEXT("minecraft:farmer")));
	// The hat's brim (a 16 x 16 box turned flat) makes the villager's mesh as wide as the brim, 16 pixels at 15/16 size, in x and z.
	{
		const McEntityModel::FModel* Villager = McEntityModel::Find(TEXT("minecraft:villager"));
		if (TestNotNull(TEXT("the villager model"), Villager))
		{
			McPlayerModel::FMeshData Data;
			McEntityModel::BuildMesh(*Villager, McEntityModel::FAnim(), 0, Data);
			const ::FBox Bounds(Data.Positions);
			const double Width = (Bounds.Max.X - Bounds.Min.X) / 100.0;
			const double Depth = (Bounds.Max.Y - Bounds.Min.Y) / 100.0;
			TestTrue(*FString::Printf(TEXT("the brim is flat at head height (width %.3f, depth %.3f blocks)"), Width, Depth), Width > 0.9 && Depth > 0.9);
			// The brim's own vertices (those far out in depth) sit at one height, 5.5 pixels above the neck (the logged hat_rim cube): 29.5 pixels at 15/16 size, 1.73 blocks.
			double LowZ = 1.0e9, HighZ = -1.0e9, MinX = 1.0e9, MaxX = -1.0e9;
			for (const FVector& Point : Data.Positions)
			{
				if (FMath::Abs(Point.Y) > 40.0)
				{
					LowZ = FMath::Min(LowZ, Point.Z / 100.0);
					HighZ = FMath::Max(HighZ, Point.Z / 100.0);
					MinX = FMath::Min(MinX, Point.X / 100.0);
					MaxX = FMath::Max(MaxX, Point.X / 100.0);
				}
			}
			TestTrue(*FString::Printf(TEXT("the brim lies flat near 1.73 blocks (height %.3f to %.3f, x %.3f to %.3f)"), LowZ, HighZ, MinX, MaxX), LowZ > 1.65 && HighZ < 1.8);
		}
	}
	return true;
}

#endif
