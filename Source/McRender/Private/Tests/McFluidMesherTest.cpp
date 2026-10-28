#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McBlockTextures.h"
#include "McFluidMesher.h"
#include "McSectionMesher.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FMcSectionSnapshot EmptyFluidSnapshot()
	{
		FMcSectionSnapshot Snapshot;
		Snapshot.Blocks.SetNumZeroed(FMcSectionSnapshot::Padded * FMcSectionSnapshot::Padded * FMcSectionSnapshot::Padded);
		Snapshot.Light.Init(0xF0, FMcSectionSnapshot::Padded * FMcSectionSnapshot::Padded * FMcSectionSnapshot::Padded);
		return Snapshot;
	}

	void PlaceFluid(FMcSectionSnapshot& Snapshot, int32 X, int32 Y, int32 Z, FMcStateId State)
	{
		Snapshot.Blocks[FMcSectionSnapshot::Index(X, Y, Z)] = State;
	}

	int32 NumFluidQuads(const FMcSectionMesh& Mesh, EMcRenderPass Pass)
	{
		return Mesh.Passes[static_cast<int32>(Pass)].Positions.Num() / 4;
	}

	FMcSectionMesh BuildFluids(const FMcSectionSnapshot& Snapshot, const McFluidMesher::FFluidPalette& Fluids, const FMcRenderPalette& Blocks)
	{
		FMcSectionMesh Mesh;
		McFluidMesher::Build(Snapshot, Fluids, Blocks, Mesh);
		return Mesh;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcFluidMesherTest, "McClient.McRender.FluidMesher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcFluidMesherTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded())
		|| !TestTrue(TEXT("models bake"), FMcBlockModels::Get().EnsureBaked())
		|| !TestTrue(TEXT("textures load"), FMcBlockTextures::Get().EnsureLoaded()))
	{
		return false;
	}
	const FMcBlockTextures& Textures = FMcBlockTextures::Get();
	const TSharedRef<const FMcRenderPalette> Blocks = FMcRenderPalette::Build(Registry, FMcBlockModels::Get(), Textures, false);
	const TSharedRef<const McFluidMesher::FFluidPalette> Fluids = McFluidMesher::BuildPalette(Registry, Textures);

	auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Water = State(TEXT("minecraft:water[level=0]"));
	const FMcStateId Lava = State(TEXT("minecraft:lava[level=0]"));
	const FMcStateId Flowing = State(TEXT("minecraft:water[level=4]"));

	// What counts as a fluid.
	using McFluidMesher::EFluid;
	TestEqual(TEXT("stone is no fluid"), Fluids->States[Stone].Kind, EFluid::None);
	TestEqual(TEXT("water"), Fluids->States[Water].Kind, EFluid::Water);
	TestEqual(TEXT("lava"), Fluids->States[Lava].Kind, EFluid::Lava);
	TestEqual(TEXT("flowing level"), static_cast<int32>(Fluids->States[Flowing].Level), 4);
	TestTrue(TEXT("falling water"), Fluids->States[State(TEXT("minecraft:water[level=9]"))].bFalling);
	TestEqual(TEXT("kelp holds water"), Fluids->States[State(TEXT("minecraft:kelp[age=0]"))].Kind, EFluid::Water);
	TestEqual(TEXT("seagrass holds water"), Fluids->States[State(TEXT("minecraft:seagrass"))].Kind, EFluid::Water);
	TestEqual(TEXT("waterlogged slab"), Fluids->States[State(TEXT("minecraft:stone_slab[type=bottom,waterlogged=true]"))].Kind, EFluid::Water);
	TestEqual(TEXT("dry slab"), Fluids->States[State(TEXT("minecraft:stone_slab[type=bottom,waterlogged=false]"))].Kind, EFluid::None);
	TestTrue(TEXT("a source is 8/9 high"), FMath::IsNearlyEqual(McFluidMesher::OwnHeight(Fluids->States[Water]), 8.0f / 9.0f));
	TestTrue(TEXT("level 4 is 4/9 high"), FMath::IsNearlyEqual(McFluidMesher::OwnHeight(Fluids->States[Flowing]), 4.0f / 9.0f));

	{
		// Six faces and the top again from underneath.
		FMcSectionSnapshot Snapshot = EmptyFluidSnapshot();
		PlaceFluid(Snapshot, 4, 4, 4, Water);
		const FMcSectionMesh Mesh = BuildFluids(Snapshot, *Fluids, *Blocks);
		TestEqual(TEXT("lone water: 7 translucent quads"), NumFluidQuads(Mesh, EMcRenderPass::Translucent), 7);
		TestEqual(TEXT("lone water: nothing opaque"), NumFluidQuads(Mesh, EMcRenderPass::Opaque), 0);
		TestEqual(TEXT("lone water: 14 triangles"), Mesh.Passes[static_cast<int32>(EMcRenderPass::Translucent)].Triangles.Num(), 42);

		// The top is plains water blue, the bottom half of it; the sides are flowing water.
		const FMcMeshBuffers& Buffers = Mesh.Passes[static_cast<int32>(EMcRenderPass::Translucent)];
		const FColor Blue(0x3F, 0x76, 0xE4);
		bool bTop = false;
		bool bBottom = false;
		bool bBack = false;
		bool bSidesFlow = true;
		for (int32 Vertex = 0; Vertex < Buffers.Positions.Num(); ++Vertex)
		{
			const FVector& Normal = Buffers.Normals[Vertex];
			bTop |= Normal.Z > 0.9 && Buffers.Colors[Vertex] == Blue;
			bBack |= Normal.Z < -0.9 && Buffers.Colors[Vertex] == Blue;
			bBottom |= Normal.Z < -0.9 && Buffers.Colors[Vertex] == FColor(0x20, 0x3B, 0x72);
			if (FMath::Abs(Normal.Z) < 0.1)
			{
				bSidesFlow &= static_cast<uint16>(Buffers.UV1[Vertex].X) == Fluids->WaterFlow;
			}
		}
		TestTrue(TEXT("top is plains blue, facing up"), bTop);
		TestTrue(TEXT("the backface top faces down"), bBack);
		TestTrue(TEXT("bottom is half shade"), bBottom);
		TestTrue(TEXT("sides use the flowing texture"), bSidesFlow);
	}
	{
		// A lone source sags toward its open edges; four together are 8/9 high where they meet.
		FMcSectionSnapshot Snapshot = EmptyFluidSnapshot();
		for (int32 X = 4; X <= 5; ++X)
		{
			for (int32 Z = 4; Z <= 5; ++Z)
			{
				PlaceFluid(Snapshot, X, 4, Z, Water);
			}
		}
		const FMcSectionMesh Mesh = BuildFluids(Snapshot, *Fluids, *Blocks);
		TestEqual(TEXT("2x2 water: each source has 2 outer sides, a bottom and a two-sided top"), NumFluidQuads(Mesh, EMcRenderPass::Translucent), 20);
		const FMcMeshBuffers& Buffers = Mesh.Passes[static_cast<int32>(EMcRenderPass::Translucent)];
		bool bFoundCentre = false;
		for (int32 Vertex = 0; Vertex < Buffers.Positions.Num(); ++Vertex)
		{
			const FVector& P = Buffers.Positions[Vertex];
			if (Buffers.Normals[Vertex].Z > 0.9 && FMath::IsNearlyEqual(P.X, 500.0) && FMath::IsNearlyEqual(P.Y, 500.0))
			{
				bFoundCentre = true;
				TestTrue(TEXT("the shared top corner sits at 8/9 of a block"), FMath::IsNearlyEqual(P.Z, (4.0 + 8.0 / 9.0) * 100.0, 0.01));
			}
		}
		TestTrue(TEXT("found the shared top corner"), bFoundCentre);
	}
	{
		FMcSectionSnapshot Snapshot = EmptyFluidSnapshot();
		PlaceFluid(Snapshot, 4, 4, 4, Water);
		PlaceFluid(Snapshot, 5, 4, 4, Water);
		TestEqual(TEXT("two adjacent sources: shared faces go"), NumFluidQuads(BuildFluids(Snapshot, *Fluids, *Blocks), EMcRenderPass::Translucent), 12);
	}
	{
		// Water above hides the top; the corner heights go to a full block.
		FMcSectionSnapshot Snapshot = EmptyFluidSnapshot();
		PlaceFluid(Snapshot, 4, 4, 4, Water);
		PlaceFluid(Snapshot, 4, 5, 4, Water);
		const FMcSectionMesh Mesh = BuildFluids(Snapshot, *Fluids, *Blocks);
		TestEqual(TEXT("stacked water"), NumFluidQuads(Mesh, EMcRenderPass::Translucent), 5 + 6);
		const FMcMeshBuffers& Buffers = Mesh.Passes[static_cast<int32>(EMcRenderPass::Translucent)];
		double MaxLowerHeight = 0.0;
		for (const FVector& P : Buffers.Positions)
		{
			if (P.Z < 500.0 + 0.01)
			{
				MaxLowerHeight = FMath::Max(MaxLowerHeight, P.Z);
			}
		}
		TestTrue(TEXT("the lower block's sides reach the one above"), FMath::IsNearlyEqual(MaxLowerHeight, 500.0, 0.01));
	}
	{
		// Stone under water hides the bottom; stone beside it hides that side.
		FMcSectionSnapshot Snapshot = EmptyFluidSnapshot();
		PlaceFluid(Snapshot, 4, 4, 4, Water);
		PlaceFluid(Snapshot, 4, 3, 4, Stone);
		PlaceFluid(Snapshot, 5, 4, 4, Stone);
		TestEqual(TEXT("water on stone beside stone"), NumFluidQuads(BuildFluids(Snapshot, *Fluids, *Blocks), EMcRenderPass::Translucent), 7 - 2);
	}
	{
		// A flowing block next to a source slopes away from it and shows the flowing texture on top.
		FMcSectionSnapshot Snapshot = EmptyFluidSnapshot();
		PlaceFluid(Snapshot, 4, 4, 4, Water);
		PlaceFluid(Snapshot, 5, 4, 4, Flowing);
		PlaceFluid(Snapshot, 5, 4, 3, Stone);
		PlaceFluid(Snapshot, 5, 4, 5, Stone);
		PlaceFluid(Snapshot, 6, 4, 4, Stone);
		const FMcSectionMesh Mesh = BuildFluids(Snapshot, *Fluids, *Blocks);
		const FMcMeshBuffers& Buffers = Mesh.Passes[static_cast<int32>(EMcRenderPass::Translucent)];
		bool bFlowTop = false;
		for (int32 Vertex = 0; Vertex < Buffers.Positions.Num(); ++Vertex)
		{
			// The east edge of the flowing block, where no other block's top reaches.
			if (Buffers.Normals[Vertex].Z > 0.9 && FMath::IsNearlyEqual(Buffers.Positions[Vertex].X, 600.0))
			{
				bFlowTop = static_cast<uint16>(Buffers.UV1[Vertex].X) == Fluids->WaterFlow;
				TestTrue(TEXT("flowing block's top corners are lower than a source's"), Buffers.Positions[Vertex].Z < (4.0 + 8.0 / 9.0) * 100.0 - 1.0);
			}
		}
		TestTrue(TEXT("a sloped top uses the flowing texture"), bFlowTop);
	}
	{
		// Lava: opaque, untinted, six faces and no backface.
		FMcSectionSnapshot Snapshot = EmptyFluidSnapshot();
		PlaceFluid(Snapshot, 4, 4, 4, Lava);
		const FMcSectionMesh Mesh = BuildFluids(Snapshot, *Fluids, *Blocks);
		TestEqual(TEXT("lone lava: 6 opaque quads"), NumFluidQuads(Mesh, EMcRenderPass::Opaque), 6);
		TestEqual(TEXT("lone lava: nothing translucent"), NumFluidQuads(Mesh, EMcRenderPass::Translucent), 0);
		const FMcMeshBuffers& Buffers = Mesh.Passes[static_cast<int32>(EMcRenderPass::Opaque)];
		bool bWhiteTop = false;
		bool bLavaLayers = true;
		for (int32 Vertex = 0; Vertex < Buffers.Positions.Num(); ++Vertex)
		{
			bWhiteTop |= Buffers.Normals[Vertex].Z > 0.9 && Buffers.Colors[Vertex] == FColor::White;
			const uint16 Layer = static_cast<uint16>(Buffers.UV1[Vertex].X);
			bLavaLayers &= Layer == Fluids->LavaStill || Layer == Fluids->LavaFlow;
		}
		TestTrue(TEXT("lava top is untinted"), bWhiteTop);
		TestTrue(TEXT("lava uses the lava textures"), bLavaLayers);
	}
	{
		// Water and lava side by side don't hide each other's faces.
		FMcSectionSnapshot Snapshot = EmptyFluidSnapshot();
		PlaceFluid(Snapshot, 4, 4, 4, Water);
		PlaceFluid(Snapshot, 5, 4, 4, Lava);
		const FMcSectionMesh Mesh = BuildFluids(Snapshot, *Fluids, *Blocks);
		TestEqual(TEXT("water beside lava: water keeps all 7"), NumFluidQuads(Mesh, EMcRenderPass::Translucent), 7);
		TestEqual(TEXT("water beside lava: lava keeps all 6"), NumFluidQuads(Mesh, EMcRenderPass::Opaque), 6);
	}
	{
		// Water inside a stair: drawn where the stair's shape leaves it open.
		FMcSectionSnapshot Snapshot = EmptyFluidSnapshot();
		PlaceFluid(Snapshot, 4, 4, 4, State(TEXT("minecraft:oak_stairs[facing=east,half=bottom,shape=straight,waterlogged=true]")));
		const FMcSectionMesh Mesh = BuildFluids(Snapshot, *Fluids, *Blocks);
		TestTrue(TEXT("waterlogged stairs make water quads"), NumFluidQuads(Mesh, EMcRenderPass::Translucent) > 0);
		TestEqual(TEXT("waterlogged stairs make no lava"), NumFluidQuads(Mesh, EMcRenderPass::Opaque), 0);
	}
	{
		// Fluid in the border cells belongs to the next section: nothing is drawn for it here.
		FMcSectionSnapshot Snapshot = EmptyFluidSnapshot();
		PlaceFluid(Snapshot, -1, 4, 4, Water);
		TestTrue(TEXT("border water draws nothing"), BuildFluids(Snapshot, *Fluids, *Blocks).IsEmpty());
	}
	return true;
}

#endif
