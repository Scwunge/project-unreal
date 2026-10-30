#include "McBlockRegistry.h"
#include "McChestComponent.h"
#include "McEntityModel.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcChestModelTest, "McClient.McRender.ChestModels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcChestModelTest::RunTest(const FString& Parameters)
{
	using namespace McEntityModel;
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()))
	{
		return false;
	}
	const auto Pick = [&](const TCHAR* Text, float& Yaw, FIntVector& Partner, bool bChristmas = false)
	{
		Yaw = -1.0f;
		Partner = FIntVector(9);
		return UMcChestComponent::GetChestModel(Registry.ParseState(Text).Get(0), Yaw, Partner, bChristmas);
	};
	float Yaw;
	FIntVector Partner;
	TestEqual(TEXT("a single chest"), Pick(TEXT("minecraft:chest[facing=north,type=single,waterlogged=false]"), Yaw, Partner), FName(TEXT("mc:chest/normal")));
	TestEqual(TEXT("facing north is unturned"), Yaw, 0.0f);
	TestEqual(TEXT("a single chest has no other half"), Partner, FIntVector::ZeroValue);
	Pick(TEXT("minecraft:chest[facing=east,type=single,waterlogged=false]"), Yaw, Partner);
	TestEqual(TEXT("facing east turns 90"), Yaw, 90.0f);
	// The half whose other half is on its +x side (turned with it) wears the _left picture, whichever the type says.
	TestEqual(TEXT("north, right: the other half is east"), Pick(TEXT("minecraft:chest[facing=north,type=right,waterlogged=false]"), Yaw, Partner), FName(TEXT("mc:chest/normal_left")));
	TestEqual(TEXT("east of it"), Partner, FIntVector(1, 0, 0));
	TestEqual(TEXT("north, left"), Pick(TEXT("minecraft:chest[facing=north,type=left,waterlogged=false]"), Yaw, Partner), FName(TEXT("mc:chest/normal_right")));
	TestEqual(TEXT("south, right: turned, the same picture"), Pick(TEXT("minecraft:chest[facing=south,type=right,waterlogged=false]"), Yaw, Partner), FName(TEXT("mc:chest/normal_left")));
	TestEqual(TEXT("a trapped chest"), Pick(TEXT("minecraft:trapped_chest[facing=north,type=single,waterlogged=false]"), Yaw, Partner), FName(TEXT("mc:chest/trapped")));
	TestEqual(TEXT("an ender chest"), Pick(TEXT("minecraft:ender_chest[facing=north,waterlogged=false]"), Yaw, Partner), FName(TEXT("mc:chest/ender")));
	TestEqual(TEXT("a waxed weathered copper chest"), Pick(TEXT("minecraft:waxed_weathered_copper_chest[facing=north,type=single,waterlogged=false]"), Yaw, Partner),
		FName(TEXT("mc:chest/copper_weathered")));
	TestEqual(TEXT("at Christmas"), Pick(TEXT("minecraft:chest[facing=north,type=single,waterlogged=false]"), Yaw, Partner, true), FName(TEXT("mc:chest/christmas")));
	TestEqual(TEXT("an ender chest isn't Christmassy"), Pick(TEXT("minecraft:ender_chest[facing=north,waterlogged=false]"), Yaw, Partner, true), FName(TEXT("mc:chest/ender")));
	TestTrue(TEXT("stone is no chest"), Pick(TEXT("minecraft:stone"), Yaw, Partner).IsNone());

	for (const TCHAR* Kind : { TEXT("mc:chest/normal"), TEXT("mc:chest/normal_left"), TEXT("mc:chest/copper_oxidized_right"), TEXT("mc:chest/ender") })
	{
		TestNotNull(FString::Printf(TEXT("%s has a model"), Kind), Find(FName(Kind)));
	}
	const FModel* Chest = Find(TEXT("mc:chest/normal"));
	if (!Chest)
	{
		return false;
	}
	FAnim Shut;
	McPlayerModel::FMeshData Data;
	BuildMesh(*Chest, Shut, 0, Data);
	const ::FBox Box(Data.Positions);
	// 14 across, a pixel in from each side, and 14 high; the lock stands a pixel out at the front (north, Unreal -Y).
	TestTrue(TEXT("14 across"), FMath::IsNearlyEqual(Box.Min.X, -43.75, 0.1) && FMath::IsNearlyEqual(Box.Max.X, 43.75, 0.1));
	TestTrue(TEXT("14 high"), FMath::IsNearlyEqual(Box.Min.Z, 0.0, 0.1) && FMath::IsNearlyEqual(Box.Max.Z, 87.5, 0.1));
	TestTrue(TEXT("the lock at the north"), FMath::IsNearlyEqual(Box.Min.Y, -50.0, 0.1) && FMath::IsNearlyEqual(Box.Max.Y, 43.75, 0.1));
	// The top of the lid is the light square of the picture's top row (28 to 42 across), not the dark inside of the lid.
	bool bLightTop = false;
	bool bDarkTop = false;
	for (int32 Index = 0; Index + 3 < Data.Positions.Num(); Index += 4)
	{
		if (Data.Normals[Index].Z > 0.9 && FMath::IsNearlyEqual(Data.Positions[Index].Z, 87.5, 0.1))
		{
			const FVector2D Middle = (Data.UV0[Index] + Data.UV0[Index + 2]) * 0.5 * 64.0;
			bLightTop |= Middle.X > 28.0 && Middle.X < 42.0 && Middle.Y < 14.0;
			bDarkTop |= Middle.X > 14.0 && Middle.X < 28.0 && Middle.Y < 14.0;
		}
	}
	TestTrue(TEXT("the lid's top is the outside of the lid"), bLightTop && !bDarkTop);
	FAnim Open;
	Open.Lid = 1.0f;
	BuildMesh(*Chest, Open, 0, Data);
	const ::FBox Opened(Data.Positions);
	// Swung up about its back edge: it stands 14 (and the lock 1) above the hinge at 9, over the back.
	TestTrue(TEXT("the open lid stands up"), Opened.Max.Z > 140.0);
	TestTrue(TEXT("and leans back, not forward"), Opened.Min.Y > -45.0);
	return true;
}

#endif
