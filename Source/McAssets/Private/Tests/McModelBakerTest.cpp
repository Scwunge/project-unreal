#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcModelBakerTest, "McClient.McAssets.ModelBaker",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
	FBox QuadBounds(const FMcBakedQuad& Quad)
	{
		FBox Box(ForceInit);
		for (const FVector3f& P : Quad.Positions)
		{
			Box += FVector(P);
		}
		return Box;
	}

	// True if some quad points Face and lies flat at Plane (on the face's own axis) covering exactly the other two ranges.
	bool HasFlatQuad(const FMcBakedModel& Model, EMcDirection Face, float Plane, FVector2f RangeA, FVector2f RangeB)
	{
		const int32 Axis = (Face == EMcDirection::Down || Face == EMcDirection::Up) ? 1 : (Face == EMcDirection::North || Face == EMcDirection::South) ? 2 : 0;
		const int32 AxisA = Axis == 0 ? 1 : 0;
		const int32 AxisB = Axis == 2 ? 1 : 2;
		for (const FMcBakedQuad& Quad : Model.Quads)
		{
			const FBox Box = QuadBounds(Quad);
			if (Quad.Face == Face
				&& FMath::IsNearlyEqual(static_cast<float>(Box.Min[Axis]), Plane, 1e-4f) && FMath::IsNearlyEqual(static_cast<float>(Box.Max[Axis]), Plane, 1e-4f)
				&& FMath::IsNearlyEqual(static_cast<float>(Box.Min[AxisA]), RangeA.X, 1e-4f) && FMath::IsNearlyEqual(static_cast<float>(Box.Max[AxisA]), RangeA.Y, 1e-4f)
				&& FMath::IsNearlyEqual(static_cast<float>(Box.Min[AxisB]), RangeB.X, 1e-4f) && FMath::IsNearlyEqual(static_cast<float>(Box.Max[AxisB]), RangeB.Y, 1e-4f))
			{
				return true;
			}
		}
		return false;
	}
}

bool FMcModelBakerTest::RunTest(const FString& Parameters)
{
	FMcBlockModels& Models = FMcBlockModels::Get();
	if (!TestTrue(TEXT("models bake from cache/"), Models.EnsureBaked()))
	{
		return false;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();

	// Every state gets a model; the ones that can't be baked are listed.
	TestEqual(TEXT("one model per state"), Models.NumStates(), 35723);
	const FMcBakeStats& Stats = Models.GetStats();
	TestEqual(TEXT("every state is accounted for"), Stats.Baked + Stats.Empty + Stats.Builtin + Stats.Failed, 35723);
	AddInfo(FString::Printf(TEXT("%d with quads (%d quads), %d empty, %d builtin, %d failed; %d unresolved texture references, %d unhandled elements; %d textures"),
		Stats.Baked, Stats.Quads, Stats.Empty, Stats.Builtin, Stats.Failed, Stats.UnresolvedTextures, Stats.UnhandledElements, Models.GetTextures().Num()));
	TestEqual(TEXT("no state failed to bake"), Stats.Failed, 0);
	for (int32 Id = 0; Id < Models.NumStates() && Stats.Failed > 0; ++Id)
	{
		if (Models.GetModel(static_cast<FMcStateId>(Id)).Status == EMcModelStatus::Failed)
		{
			AddError(FString::Printf(TEXT("%s failed to bake"), *Registry.Describe(static_cast<FMcStateId>(Id))));
			break;
		}
	}
	for (const FString& Problem : Stats.Problems)
	{
		AddWarning(Problem);
	}

	TestEqual(TEXT("air is empty"), Models.GetModel(0).Status, EMcModelStatus::Empty);

	// Stone: six full faces, each culled by its own side.
	const FMcBakedModel& Stone = Models.GetModel(Registry.FindDefaultState(TEXT("minecraft:stone")).GetValue());
	if (TestEqual(TEXT("stone has 6 quads"), Stone.Quads.Num(), 6))
	{
		uint32 Seen = 0;
		for (const FMcBakedQuad& Quad : Stone.Quads)
		{
			TestEqual(TEXT("stone face culls to its own side"), Quad.CullFace, Quad.Face);
			TestEqual(TEXT("stone texture"), Quad.Texture, FName(TEXT("minecraft:block/stone")));
			TestEqual(TEXT("stone untinted"), static_cast<int32>(Quad.TintIndex), -1);
			TestTrue(TEXT("stone has ambient occlusion"), Quad.bAmbientOcclusion);
			Seen |= 1u << static_cast<int32>(Quad.Face);
		}
		TestEqual(TEXT("stone has all six faces"), Seen, 0x3Fu);
		TestTrue(TEXT("stone up face is the top of the block"), HasFlatQuad(Stone, EMcDirection::Up, 1.f, FVector2f(0, 1), FVector2f(0, 1)));
		TestTrue(TEXT("stone east face is on the east side"), HasFlatQuad(Stone, EMcDirection::East, 1.f, FVector2f(0, 1), FVector2f(0, 1)));
		TestTrue(TEXT("stone north face is on the north side"), HasFlatQuad(Stone, EMcDirection::North, 0.f, FVector2f(0, 1), FVector2f(0, 1)));

		// The top-left corner of an up face is its north-west corner, and uv (0,0) goes there.
		for (const FMcBakedQuad& Quad : Stone.Quads)
		{
			if (Quad.Face == EMcDirection::Up)
			{
				TestTrue(TEXT("up face starts at the north-west corner"), Quad.Positions[0].Equals(FVector3f(0, 1, 0), 1e-4f));
				TestTrue(TEXT("up face uv starts at the top-left of the texture"), Quad.UVs[0].Equals(FVector2f(0, 0), 1e-4f));
				TestTrue(TEXT("up face uv ends at the bottom-right of the texture"), Quad.UVs[2].Equals(FVector2f(1, 1), 1e-4f));
			}
		}
	}

	// Glass: its textures are {"sprite", "force_translucent"} objects in 26.3.
	{
		const FMcBakedModel& Glass = Models.GetModel(Registry.FindDefaultState(TEXT("minecraft:glass")).GetValue());
		if (TestTrue(TEXT("glass has quads"), Glass.Quads.Num() > 0))
		{
			for (const FMcBakedQuad& Quad : Glass.Quads)
			{
				TestEqual(TEXT("glass texture"), Quad.Texture, FName(TEXT("minecraft:block/glass")));
				TestTrue(TEXT("glass is forced translucent"), Quad.bForceTranslucent);
			}
		}
		for (const FMcBakedQuad& Quad : Stone.Quads)
		{
			TestFalse(TEXT("stone isn't forced translucent"), Quad.bForceTranslucent);
		}
	}

	// Grass block: the top and the sides are tinted differently, and the overlay is tinted.
	{
		const FMcBakedModel& Grass = Models.GetModel(Registry.FindDefaultState(TEXT("minecraft:grass_block")).GetValue());
		int32 Tinted = 0;
		for (const FMcBakedQuad& Quad : Grass.Quads)
		{
			Tinted += Quad.TintIndex >= 0 ? 1 : 0;
		}
		TestTrue(TEXT("grass block has tinted faces"), Tinted > 0);
	}

	// Oak stairs facing east are the unrotated model: a slab with a step on the east half.
	const TOptional<FMcStateId> East = Registry.FindState(TEXT("minecraft:oak_stairs"),
		{ { TEXT("facing"), TEXT("east") }, { TEXT("half"), TEXT("bottom") }, { TEXT("shape"), TEXT("straight") } });
	if (TestTrue(TEXT("found oak_stairs facing east"), East.IsSet()))
	{
		const FMcBakedModel& Stairs = Models.GetModel(East.GetValue());
		TestEqual(TEXT("oak stairs have 11 quads (a slab and a step)"), Stairs.Quads.Num(), 11);
		TestTrue(TEXT("slab bottom"), HasFlatQuad(Stairs, EMcDirection::Down, 0.f, FVector2f(0, 1), FVector2f(0, 1)));
		TestTrue(TEXT("slab top"), HasFlatQuad(Stairs, EMcDirection::Up, 0.5f, FVector2f(0, 1), FVector2f(0, 1)));
		TestTrue(TEXT("step top on the east half"), HasFlatQuad(Stairs, EMcDirection::Up, 1.f, FVector2f(0.5f, 1), FVector2f(0, 1)));
		TestTrue(TEXT("step back face on the east side"), HasFlatQuad(Stairs, EMcDirection::East, 1.f, FVector2f(0.5f, 1), FVector2f(0, 1)));
		TestTrue(TEXT("step riser faces west"), HasFlatQuad(Stairs, EMcDirection::West, 0.5f, FVector2f(0.5f, 1), FVector2f(0, 1)));
	}

	// Facing north, upside down: x = 180 and y = 270 in the blockstate. The slab moves to the top and
	// the step hangs under it on the north half.
	const TOptional<FMcStateId> NorthTop = Registry.FindState(TEXT("minecraft:oak_stairs"),
		{ { TEXT("facing"), TEXT("north") }, { TEXT("half"), TEXT("top") }, { TEXT("shape"), TEXT("straight") } });
	if (TestTrue(TEXT("found oak_stairs facing north, top half"), NorthTop.IsSet()))
	{
		const FMcBakedModel& Stairs = Models.GetModel(NorthTop.GetValue());
		TestEqual(TEXT("rotated stairs keep 11 quads"), Stairs.Quads.Num(), 11);
		TestTrue(TEXT("slab at the top"), HasFlatQuad(Stairs, EMcDirection::Up, 1.f, FVector2f(0, 1), FVector2f(0, 1)));
		TestTrue(TEXT("slab underside"), HasFlatQuad(Stairs, EMcDirection::Down, 0.5f, FVector2f(0, 1), FVector2f(0, 1)));
		TestTrue(TEXT("step underside on the north half"), HasFlatQuad(Stairs, EMcDirection::Down, 0.f, FVector2f(0, 1), FVector2f(0, 0.5f)));
		TestTrue(TEXT("step front on the north side"), HasFlatQuad(Stairs, EMcDirection::North, 0.f, FVector2f(0, 1), FVector2f(0, 0.5f)));
		for (const FMcBakedQuad& Quad : Stairs.Quads)
		{
			if (Quad.CullFace != EMcDirection::None)
			{
				TestEqual(TEXT("the cullface follows the rotation"), Quad.CullFace, Quad.Face);
			}
		}
	}

	// A fence post with no sides is one box; multipart adds the connecting bars.
	{
		const FMcBakedModel& Post = Models.GetModel(Registry.FindDefaultState(TEXT("minecraft:oak_fence")).GetValue());
		const TOptional<FMcStateId> North = Registry.FindState(TEXT("minecraft:oak_fence"), { { TEXT("north"), TEXT("true") } });
		TestTrue(TEXT("fence connected north has more quads than the bare post"), North.IsSet() && Models.GetModel(North.GetValue()).Quads.Num() > Post.Quads.Num());
	}

	return true;
}

#endif
