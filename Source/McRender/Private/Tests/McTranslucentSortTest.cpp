#include "McSectionMesher.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcTranslucentSortTest, "McClient.McRender.TranslucentSort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
	// A quad at X, with the diagonal that starts at its second corner for odd ones, as the mesher's choice varies.
	void AddQuadAt(FMcMeshBuffers& Buffers, double X, bool bOtherDiagonal)
	{
		const int32 First = Buffers.Positions.Num();
		Buffers.Positions.Add(FVector(X, 0, 0));
		Buffers.Positions.Add(FVector(X, 100, 0));
		Buffers.Positions.Add(FVector(X, 100, 100));
		Buffers.Positions.Add(FVector(X, 0, 100));
		if (bOtherDiagonal)
		{
			Buffers.Triangles.Append({ First + 1, First + 2, First + 3, First + 1, First + 3, First });
		}
		else
		{
			Buffers.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
		}
	}
}

bool FMcTranslucentSortTest::RunTest(const FString& Parameters)
{
	FMcMeshBuffers Buffers;
	AddQuadAt(Buffers, 100, false);
	AddQuadAt(Buffers, 500, true);
	AddQuadAt(Buffers, 300, false);
	McTranslucent::SortQuadsBackToFront(Buffers, FVector(0, 50, 50));
	if (!TestEqual(TEXT("no index is lost"), Buffers.Triangles.Num(), 18))
	{
		return false;
	}
	// Farthest (X 500, vertices 4 to 7), then 300 (8 to 11), then 100 (0 to 3); a quad keeps its own diagonal.
	TestEqual(TEXT("the farthest quad first"), Buffers.Triangles[0], 5);
	TestEqual(TEXT("its own diagonal kept"), Buffers.Triangles[5], 4);
	TestEqual(TEXT("then the middle"), Buffers.Triangles[6], 8);
	TestEqual(TEXT("the nearest last"), Buffers.Triangles[12], 0);

	// From the other side the order turns round.
	McTranslucent::SortQuadsBackToFront(Buffers, FVector(1000, 50, 50));
	TestEqual(TEXT("now the X 100 quad is the farthest"), Buffers.Triangles[0], 0);
	TestEqual(TEXT("and the X 500 one the nearest"), Buffers.Triangles[12], 5);

	// A single quad or none is left alone.
	FMcMeshBuffers One;
	AddQuadAt(One, 0, false);
	McTranslucent::SortQuadsBackToFront(One, FVector::ZeroVector);
	TestEqual(TEXT("one quad stays"), One.Triangles.Num(), 6);
	return true;
}

#endif
