#include "McBlockHighlight.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBlockHighlightTest, "McClient.McRender.BlockHighlight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcBlockHighlightTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("no cracks before the dig starts"), McHighlight::GetCrackStage(0.0f), -1);
	TestEqual(TEXT("first crack"), McHighlight::GetCrackStage(0.05f), 0);
	TestEqual(TEXT("halfway"), McHighlight::GetCrackStage(0.5f), 5);
	TestEqual(TEXT("last crack just before the block goes"), McHighlight::GetCrackStage(0.999f), 9);
	TestEqual(TEXT("never past the last picture"), McHighlight::GetCrackStage(1.5f), 9);

	// The outline: twelve bars of six faces, each face two triangles seen from both sides.
	TArray<FVector> Positions;
	TArray<int32> Triangles;
	McHighlight::AddOutline(FBox(FVector(0.0), FVector(1.0)), 0.012, 0.003, Positions, Triangles);
	TestEqual(TEXT("twelve bars of 24 vertices"), Positions.Num(), 12 * 24);
	TestEqual(TEXT("two windings of 12 triangles a bar"), Triangles.Num(), 12 * 6 * 6 * 2);
	const FBox Bounds(Positions);
	TestTrue(TEXT("the outline hugs the block, a hair outside"), Bounds.Min.X < 0.0 && Bounds.Min.X > -0.02 && Bounds.Max.Z > 1.0 && Bounds.Max.Z < 1.02);

	TArray<FVector2D> UVs;
	Positions.Reset();
	Triangles.Reset();
	McHighlight::AddFaces(FBox(FVector(0.0), FVector(1.0)), 0.002, Positions, Triangles, UVs);
	TestEqual(TEXT("six faces of four vertices"), Positions.Num(), 24);
	TestEqual(TEXT("each with a picture"), UVs.Num(), 24);
	return true;
}

#endif
