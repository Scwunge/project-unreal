#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McBlockTextures.h"
#include "McSectionMesher.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FMcSectionSnapshot EmptySnapshot()
	{
		FMcSectionSnapshot Snapshot;
		Snapshot.Blocks.SetNumZeroed(FMcSectionSnapshot::Padded * FMcSectionSnapshot::Padded * FMcSectionSnapshot::Padded);
		Snapshot.Light.Init(0xF0, FMcSectionSnapshot::Padded * FMcSectionSnapshot::Padded * FMcSectionSnapshot::Padded);
		return Snapshot;
	}

	void Place(FMcSectionSnapshot& Snapshot, int32 X, int32 Y, int32 Z, FMcStateId State)
	{
		Snapshot.Blocks[FMcSectionSnapshot::Index(X, Y, Z)] = State;
	}

	int32 NumQuads(const FMcSectionMesh& Mesh, EMcRenderPass Pass)
	{
		return Mesh.Passes[static_cast<int32>(Pass)].Positions.Num() / 4;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBlockMesherTest, "McClient.McRender.BlockMesher",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMcBlockMesherTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded())
		|| !TestTrue(TEXT("models bake"), FMcBlockModels::Get().EnsureBaked())
		|| !TestTrue(TEXT("textures load"), FMcBlockTextures::Get().EnsureLoaded()))
	{
		return false;
	}
	const TSharedRef<const FMcRenderPalette> Palette = FMcRenderPalette::Build(Registry, FMcBlockModels::Get(), FMcBlockTextures::Get(), false);

	auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Glass = State(TEXT("minecraft:glass"));
	const FMcStateId Leaves = State(TEXT("minecraft:oak_leaves[distance=1,persistent=true,waterlogged=false]"));
	const FMcStateId BottomSlab = State(TEXT("minecraft:stone_slab[type=bottom,waterlogged=false]"));
	const FMcStateId GrassBlock = State(TEXT("minecraft:grass_block[snowy=false]"));

	TestEqual(TEXT("stone covers all six sides"), Palette->States[Stone].OpaqueFaces, static_cast<uint8>(0x3F));
	TestEqual(TEXT("glass covers no side"), Palette->States[Glass].OpaqueFaces, static_cast<uint8>(0));
	TestTrue(TEXT("glass hides its own faces"), Palette->States[Glass].bHidesOwnBlock);
	TestFalse(TEXT("leaves show the leaves behind"), Palette->States[Leaves].bHidesOwnBlock);
	TestEqual(TEXT("a bottom slab covers only its bottom"), Palette->States[BottomSlab].OpaqueFaces, static_cast<uint8>(1 << static_cast<int32>(EMcDirection::Down)));

	{
		FMcSectionSnapshot Snapshot = EmptySnapshot();
		Place(Snapshot, 4, 4, 4, Stone);
		const FMcSectionMesh Mesh = McBlockMesher::Build(Snapshot, *Palette);
		TestEqual(TEXT("lone stone: 6 opaque quads"), NumQuads(Mesh, EMcRenderPass::Opaque), 6);
		TestEqual(TEXT("lone stone: 12 triangles"), Mesh.Passes[0].Triangles.Num(), 36);
	}
	{
		FMcSectionSnapshot Snapshot = EmptySnapshot();
		Place(Snapshot, 4, 4, 4, Stone);
		Place(Snapshot, 5, 4, 4, Stone);
		TestEqual(TEXT("two stones: the shared faces go"), NumQuads(McBlockMesher::Build(Snapshot, *Palette), EMcRenderPass::Opaque), 10);
	}
	{
		// The border comes from the neighbouring section: a stone there hides the face against it.
		FMcSectionSnapshot Snapshot = EmptySnapshot();
		Place(Snapshot, 0, 4, 4, Stone);
		Place(Snapshot, -1, 4, 4, Stone);
		TestEqual(TEXT("stone against the border"), NumQuads(McBlockMesher::Build(Snapshot, *Palette), EMcRenderPass::Opaque), 5);
	}
	{
		FMcSectionSnapshot Snapshot = EmptySnapshot();
		Place(Snapshot, 4, 4, 4, Glass);
		Place(Snapshot, 5, 4, 4, Glass);
		const FMcSectionMesh Mesh = McBlockMesher::Build(Snapshot, *Palette);
		TestEqual(TEXT("two glass: translucent, shared faces go"), NumQuads(Mesh, EMcRenderPass::Translucent), 10);
		TestEqual(TEXT("two glass: nothing opaque"), NumQuads(Mesh, EMcRenderPass::Opaque), 0);
	}
	{
		FMcSectionSnapshot Snapshot = EmptySnapshot();
		Place(Snapshot, 4, 4, 4, Leaves);
		Place(Snapshot, 5, 4, 4, Leaves);
		TestEqual(TEXT("two leaves: cutout, all faces drawn"), NumQuads(McBlockMesher::Build(Snapshot, *Palette), EMcRenderPass::Cutout), 12);
	}
	{
		// A slab on stone hides the stone's top; stone on a slab keeps its bottom (the slab's top is open).
		FMcSectionSnapshot Snapshot = EmptySnapshot();
		Place(Snapshot, 4, 4, 4, Stone);
		Place(Snapshot, 4, 5, 4, BottomSlab);
		TestEqual(TEXT("slab on stone"), NumQuads(McBlockMesher::Build(Snapshot, *Palette), EMcRenderPass::Opaque), 5 + 5);

		FMcSectionSnapshot Above = EmptySnapshot();
		Place(Above, 4, 4, 4, BottomSlab);
		Place(Above, 4, 5, 4, Stone);
		TestEqual(TEXT("stone on slab"), NumQuads(McBlockMesher::Build(Above, *Palette), EMcRenderPass::Opaque), 6 + 6);
	}
	{
		// The grass top is tinted plains green and full brightness; the bottom is untinted dirt at half shade.
		FMcSectionSnapshot Snapshot = EmptySnapshot();
		Place(Snapshot, 4, 4, 4, GrassBlock);
		const FMcSectionMesh Mesh = McBlockMesher::Build(Snapshot, *Palette);
		const FMcMeshBuffers& Opaque = Mesh.Passes[static_cast<int32>(EMcRenderPass::Opaque)];
		const FColor PlainsGreen(0x91, 0xBD, 0x59);
		const FColor HalfGrey(128, 128, 128);
		bool bFoundTop = false;
		bool bFoundBottom = false;
		for (int32 Vertex = 0; Vertex < Opaque.Positions.Num(); ++Vertex)
		{
			bFoundTop |= Opaque.Normals[Vertex].Z > 0.9 && Opaque.Colors[Vertex] == PlainsGreen;
			bFoundBottom |= Opaque.Normals[Vertex].Z < -0.9 && Opaque.Colors[Vertex] == HalfGrey;
		}
		TestTrue(TEXT("grass top is plains green, facing up"), bFoundTop);
		TestTrue(TEXT("grass bottom is half shade, facing down"), bFoundBottom);
	}

	TestEqual(TEXT("unpowered redstone"), McBlockMesher::GetBlockTint(Registry, State(TEXT("minecraft:redstone_wire[east=none,north=none,power=0,south=none,west=none]"))), FColor(77, 0, 0));
	TestEqual(TEXT("birch leaves"), McBlockMesher::GetBlockTint(Registry, State(TEXT("minecraft:birch_leaves[distance=1,persistent=true,waterlogged=false]"))), FColor(0x80, 0xA7, 0x55));
	return true;
}

#endif
