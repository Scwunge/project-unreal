#include "HAL/FileManager.h"
#include "McCache.h"
#include "McDensityFunction.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcDensityFunctionTest, "McClient.McGen.DensityFunction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

namespace
{
	int32 ParseText(FMcDensityGraph& Graph, const FString& Text)
	{
		TSharedPtr<FJsonValue> Json;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json);
		return Graph.Parse(Json);
	}

	double At(const FMcDensityGraph& Graph, int32 Node, int32 X, int32 Y, int32 Z)
	{
		FMcDensityContext Context(Graph);
		Context.SetPos(X, Y, Z);
		return Graph.Evaluate(Node, Context);
	}
}

bool FMcDensityFunctionTest::RunTest(const FString& Parameters)
{
	// Small graphs with known answers.
	{
		FMcDensityGraph Graph(1);
		const int32 Sum = ParseText(Graph, TEXT("{\"type\":\"minecraft:add\",\"left\":2.5,\"right\":{\"type\":\"minecraft:mul\",\"left\":3,\"right\":-1}}"));
		TestEqual(TEXT("add and mul"), At(Graph, Sum, 0, 0, 0), -0.5);

		const int32 Y = Graph.LoadFunction(TEXT("minecraft:y"));
		TestEqual(TEXT("y is the height"), At(Graph, Y, 5, 37, -9), 37.0);
		TestEqual(TEXT("y below zero"), At(Graph, Y, 0, -64, 0), -64.0);
		TestFalse(TEXT("y doesn't depend on x"), (Graph.GetNode(Y).Depends & 1) != 0);

		const int32 Gradient = ParseText(Graph, TEXT("{\"type\":\"minecraft:gradient\",\"axis\":\"y\",\"from_coordinate\":0,\"to_coordinate\":10,\"from_value\":1,\"to_value\":-1}"));
		TestEqual(TEXT("gradient start"), At(Graph, Gradient, 0, 0, 0), 1.0);
		TestEqual(TEXT("gradient middle"), At(Graph, Gradient, 0, 5, 0), 0.0);
		TestEqual(TEXT("gradient clamps"), At(Graph, Gradient, 0, 50, 0), -1.0);

		const int32 Squeeze = ParseText(Graph, TEXT("{\"type\":\"minecraft:squeeze\",\"input\":5}"));
		TestEqual(TEXT("squeeze clamps then curves"), At(Graph, Squeeze, 0, 0, 0), 0.5 - 1.0 / 24.0);

		const int32 Quarter = ParseText(Graph, TEXT("{\"type\":\"minecraft:quarter_negative\",\"input\":-2}"));
		TestEqual(TEXT("quarter_negative"), At(Graph, Quarter, 0, 0, 0), -0.5);

		const int32 Range = ParseText(Graph, TEXT("{\"type\":\"minecraft:range_choice\",\"input\":\"minecraft:y\",\"min_inclusive\":0,\"max_exclusive\":10,\"when_in_range\":1,\"when_out_of_range\":-1}"));
		TestEqual(TEXT("range includes its minimum"), At(Graph, Range, 0, 0, 0), 1.0);
		TestEqual(TEXT("range excludes its maximum"), At(Graph, Range, 0, 10, 0), -1.0);

		const int32 Select = ParseText(Graph, TEXT("{\"type\":\"minecraft:interval_select\",\"input\":\"minecraft:y\",\"thresholds\":[0,10],\"functions\":[1,2,3]}"));
		TestEqual(TEXT("interval below the first threshold"), At(Graph, Select, 0, -1, 0), 1.0);
		TestEqual(TEXT("interval at a threshold"), At(Graph, Select, 0, 0, 0), 2.0);
		TestEqual(TEXT("interval above the last threshold"), At(Graph, Select, 0, 99, 0), 3.0);

		// A spline through (0, 0) and (10, 10), flat at both ends, carried on straight outside.
		const int32 Spline = ParseText(Graph, TEXT("{\"type\":\"minecraft:spline\",\"spline\":{\"coordinate\":\"minecraft:y\",\"points\":[")
			TEXT("{\"location\":0,\"value\":0,\"derivative\":0},{\"location\":10,\"value\":10,\"derivative\":2}]}}"));
		TestEqual(TEXT("spline at a point"), At(Graph, Spline, 0, 0, 0), 0.0);
		TestEqual(TEXT("spline at the last point"), At(Graph, Spline, 0, 10, 0), 10.0);
		TestEqual(TEXT("spline extrapolates with the end slope"), At(Graph, Spline, 0, 12, 0), 14.0);
		TestEqual(TEXT("spline before the first point is flat"), At(Graph, Spline, 0, -5, 0), 0.0);
		const double Mid = At(Graph, Spline, 0, 5, 0);
		TestTrue(TEXT("spline between points is smooth"), Mid > 2.0 && Mid < 5.0);

		const int32 Lerp = ParseText(Graph, TEXT("{\"type\":\"minecraft:lerp\",\"alpha\":0.25,\"first\":0,\"second\":8}"));
		TestEqual(TEXT("lerp"), At(Graph, Lerp, 0, 0, 0), 2.0);

		const int32 Slice = ParseText(Graph, TEXT("{\"type\":\"minecraft:slice\",\"axis\":\"y\",\"coordinate\":7,\"input\":\"minecraft:y\"}"));
		TestEqual(TEXT("slice fixes a coordinate"), At(Graph, Slice, 0, 100, 0), 7.0);
		TestFalse(TEXT("a y slice doesn't depend on y"), Graph.DependsOnY(Slice));

		// The top block at or above 0 where y < 20 (a step of 4 finds 16).
		const int32 Top = ParseText(Graph, TEXT("{\"type\":\"minecraft:find_top_surface\",\"cell_height\":4,\"lower_bound\":-64,\"upper_bound\":100,")
			TEXT("\"density\":{\"type\":\"minecraft:sub\",\"left\":20,\"right\":\"minecraft:y\"}}"));
		TestEqual(TEXT("find_top_surface finds the top"), At(Graph, Top, 0, 0, 0), 16.0);
		TestEqual(TEXT("find_top_surface ignores y"), At(Graph, Top, 0, 200, 0), 16.0);
		TestFalse(TEXT("graph has no errors"), Graph.HasErrors());
	}

	// Every density function in the data loads.
	{
		TArray<FString> Files;
		const FString Root = McCache::Data(TEXT("worldgen/density_function/"));
		IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.json"), true, false);
		TestTrue(TEXT("the jar has density functions"), Files.Num() > 40);
		FMcDensityGraph Graph(2);
		for (FString File : Files)
		{
			FPaths::MakePathRelativeTo(File, *Root);
			const FString Id = TEXT("minecraft:") + FPaths::GetBaseFilename(File, false).Replace(TEXT("\\"), TEXT("/"));
			TestTrue(FString::Printf(TEXT("%s loads"), *Id), Graph.LoadFunction(Id) != INDEX_NONE);
		}
		TestFalse(TEXT("no load errors"), Graph.HasErrors());
		AddInfo(FString::Printf(TEXT("%d files, %d nodes"), Files.Num(), Graph.NumNodes()));
	}

	// The overworld: solid deep down, air high up, land and sea in sensible amounts.
	{
		FMcDensityGraph Graph(12345);
		const int32 Final = Graph.LoadFunction(TEXT("minecraft:overworld/final_density"));
		const int32 Continents = Graph.LoadFunction(TEXT("minecraft:overworld/continents"));
		const int32 Surface = Graph.LoadFunction(TEXT("minecraft:overworld/preliminary_surface_level"));
		if (!TestFalse(TEXT("overworld loads"), Graph.HasErrors() || Final == INDEX_NONE))
		{
			return false;
		}

		FMcDensityContext Context(Graph);
		int32 Ocean = 0;
		int32 Solid = 0;
		int32 Air = 0;
		double SurfaceSum = 0.0;
		constexpr int32 Columns = 400;
		for (int32 Index = 0; Index < Columns; ++Index)
		{
			const int32 X = (Index % 20) * 811 - 8000;
			const int32 Z = (Index / 20) * 787 - 8000;
			Context.SetPos(X, 0, Z);
			Ocean += Graph.Evaluate(Continents, Context) < -0.19 ? 1 : 0;
			SurfaceSum += Graph.Evaluate(Surface, Context);
			Context.SetPos(X, -58, Z);
			Solid += Graph.Evaluate(Final, Context) > 0.0 ? 1 : 0;
			Context.SetPos(X, 300, Z);
			Air += Graph.Evaluate(Final, Context) <= 0.0 ? 1 : 0;
		}
		const double OceanShare = static_cast<double>(Ocean) / Columns;
		const double MeanSurface = SurfaceSum / Columns;
		AddInfo(FString::Printf(TEXT("ocean %.0f%%, mean surface %.1f, solid at -58 %d, air at 300 %d of %d"), OceanShare * 100, MeanSurface, Solid, Air, Columns));
		TestTrue(TEXT("a sensible share of ocean"), OceanShare > 0.1 && OceanShare < 0.6);
		TestTrue(TEXT("surface around sea level"), MeanSurface > 30.0 && MeanSurface < 120.0);
		TestTrue(TEXT("mostly solid deep down"), Solid > Columns * 8 / 10);
		TestEqual(TEXT("all air at the top"), Air, Columns);

		// Interpolation in a chunk matches interpolation point by point.
		FMcDensityContext ChunkContext(Graph);
		ChunkContext.BeginChunk(32, -48, -64, 384);
		bool bSame = true;
		for (int32 Index = 0; Index < 40; ++Index)
		{
			const int32 X = 32 + (Index * 7) % 16;
			const int32 Y = -64 + (Index * 37) % 384;
			const int32 Z = -48 + (Index * 5) % 16;
			ChunkContext.SetPos(X, Y, Z);
			Context.SetPos(X, Y, Z);
			const double InChunk = Graph.Evaluate(Final, ChunkContext);
			const double Alone = Graph.Evaluate(Final, Context);
			if (!FMath::IsNearlyEqual(InChunk, Alone, 1e-6))
			{
				AddError(FString::Printf(TEXT("at %d %d %d: %f in a chunk, %f alone"), X, Y, Z, InChunk, Alone));
				bSame = false;
				break;
			}
		}
		TestTrue(TEXT("chunk and point evaluation agree"), bSame);
	}
	return true;
}

#endif
