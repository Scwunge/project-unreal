#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McResourcePacks.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
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

	// Blockstate "x": an extended piston facing up keeps its 12-pixel body at the bottom, facing down at the top.
	{
		auto HeightRange = [&](const TCHAR* Text)
		{
			FBox Box(ForceInit);
			for (const FMcBakedQuad& Quad : Models.GetModel(Registry.ParseState(Text).Get(0)).Quads)
			{
				Box += QuadBounds(Quad);
			}
			return FVector2D(Box.Min.Y, Box.Max.Y);
		};
		const FVector2D Up = HeightRange(TEXT("minecraft:piston[extended=true,facing=up]"));
		const FVector2D Down = HeightRange(TEXT("minecraft:piston[extended=true,facing=down]"));
		TestTrue(TEXT("piston body facing up is the bottom 12 pixels"), FMath::IsNearlyEqual(Up.X, 0.0) && FMath::IsNearlyEqual(Up.Y, 0.75));
		TestTrue(TEXT("piston body facing down is the top 12 pixels"), FMath::IsNearlyEqual(Down.X, 0.25) && FMath::IsNearlyEqual(Down.Y, 1.0));
	}

	return true;
}

// A pack's blockstates and models replace the game's: a blockstate that names a model of the pack's own (a half-height
// box, written with elements) and one that names a child of block/cube_all with another texture. Both are read through
// the pack stack, so the baker needs no pack code of its own.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPackModelsTest, "McClient.McAssets.PackModels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcPackModelsTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McPackModelsTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const FString Assets = Root / TEXT("assets/minecraft");
	FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97,\"description\":\"models test\"}}"), *(Root / TEXT("pack.mcmeta")));
	FFileHelper::SaveStringToFile(TEXT("{\"variants\":{\"\":{\"model\":\"minecraft:block/zz_half\"}}}"), *(Assets / TEXT("blockstates/stone.json")));
	FFileHelper::SaveStringToFile(TEXT("{\"variants\":{\"\":{\"model\":\"minecraft:block/zz_dirt_cube\"}}}"), *(Assets / TEXT("blockstates/sand.json")));
	FFileHelper::SaveStringToFile(TEXT("{\"textures\":{\"all\":\"minecraft:block/cobblestone\",\"particle\":\"minecraft:block/cobblestone\"},"
		"\"elements\":[{\"from\":[0,0,0],\"to\":[16,8,16],\"faces\":{\"up\":{\"texture\":\"#all\",\"cullface\":\"up\"},\"down\":{\"texture\":\"#all\"},"
		"\"north\":{\"texture\":\"#all\",\"uv\":[0,8,16,16]},\"south\":{\"texture\":\"#all\",\"uv\":[0,8,16,16]},\"west\":{\"texture\":\"#all\",\"uv\":[0,8,16,16]},\"east\":{\"texture\":\"#all\",\"uv\":[0,8,16,16]}}}]}"),
		*(Assets / TEXT("models/block/zz_half.json")));
	FFileHelper::SaveStringToFile(TEXT("{\"parent\":\"minecraft:block/cube_all\",\"textures\":{\"all\":\"minecraft:block/dirt\"}}"), *(Assets / TEXT("models/block/zz_dirt_cube.json")));

	FString Error;
	const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(Root, &Error);
	if (!TestTrue(TEXT("the pack opens: ") + Error, Pack.IsValid()))
	{
		return false;
	}
	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Pack })));
	FMcBlockModels& Models = FMcBlockModels::Get();
	Models.Unload();
	if (TestTrue(TEXT("models bake with the pack"), Models.EnsureBaked()))
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const TOptional<FMcStateId> Stone = Registry.FindDefaultState(FName("minecraft:stone"));
		const TOptional<FMcStateId> Sand = Registry.FindDefaultState(FName("minecraft:sand"));
		if (TestTrue(TEXT("stone and sand exist"), Stone.IsSet() && Sand.IsSet()))
		{
			const FMcBakedModel& Half = Models.GetModel(Stone.GetValue());
			TestEqual(TEXT("the pack's own model has six faces"), Half.Quads.Num(), 6);
			float MaxY = 0.0f;
			for (const FMcBakedQuad& Quad : Half.Quads)
			{
				for (const FVector3f& P : Quad.Positions)
				{
					MaxY = FMath::Max(MaxY, P.Y);
				}
				TestEqual(TEXT("with the model's texture"), Quad.Texture, FName("minecraft:block/cobblestone"));
			}
			TestEqual(TEXT("and half the height"), MaxY, 0.5f);

			const FMcBakedModel& Cube = Models.GetModel(Sand.GetValue());
			TestEqual(TEXT("a child of cube_all has six faces"), Cube.Quads.Num(), 6);
			for (const FMcBakedQuad& Quad : Cube.Quads)
			{
				TestEqual(TEXT("with the child's texture"), Quad.Texture, FName("minecraft:block/dirt"));
			}
		}
	}

	McPacks::SetStack(MakeShared<FMcPackStack>());
	Models.Unload();
	Models.EnsureBaked();
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

// Two packs: the higher pack's blockstate wins; a model can have its parent in the lower pack; a variant's x rotation turns
// the model over; a multipart blockstate of a pack applies its unconditional part. Swapped, the other pack wins.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPackModelStackTest, "McClient.McAssets.PackModelStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FMcPackModelStackTest::RunTest(const FString& Parameters)
{
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McPackModelStackTest"));
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	const auto Save = [&Root](const TCHAR* Pack, const FString& Relative, const TCHAR* Text)
	{
		FFileHelper::SaveStringToFile(Text, *(Root / Pack / TEXT("assets/minecraft") / Relative));
	};
	for (const TCHAR* Pack : { TEXT("low"), TEXT("high") })
	{
		FFileHelper::SaveStringToFile(TEXT("{\"pack\":{\"pack_format\":97,\"description\":\"model stack test\"}}"), *(Root / Pack / TEXT("pack.mcmeta")));
	}
	// Low: stone is a cube of dirt; a half box and a cube of dirt as models; the fence has a multipart state of one half box.
	Save(TEXT("low"), TEXT("blockstates/stone.json"), TEXT("{\"variants\":{\"\":{\"model\":\"minecraft:block/zz_dirt_cube\"}}}"));
	Save(TEXT("low"), TEXT("models/block/zz_dirt_cube.json"), TEXT("{\"parent\":\"minecraft:block/cube_all\",\"textures\":{\"all\":\"minecraft:block/dirt\"}}"));
	Save(TEXT("low"), TEXT("models/block/zz_half.json"),
		TEXT("{\"textures\":{\"all\":\"minecraft:block/cobblestone\",\"particle\":\"minecraft:block/cobblestone\"},"
			"\"elements\":[{\"from\":[0,0,0],\"to\":[16,8,16],\"faces\":{\"up\":{\"texture\":\"#all\"},\"down\":{\"texture\":\"#all\"},"
			"\"north\":{\"texture\":\"#all\"},\"south\":{\"texture\":\"#all\"},\"west\":{\"texture\":\"#all\"},\"east\":{\"texture\":\"#all\"}}}]}"));
	Save(TEXT("low"), TEXT("blockstates/oak_fence.json"),
		TEXT("{\"multipart\":[{\"apply\":{\"model\":\"minecraft:block/zz_half\"}},{\"when\":{\"north\":\"true\"},\"apply\":{\"model\":\"minecraft:block/zz_dirt_cube\"}}]}"));
	// High: stone is the half box turned over, sand a child of the low pack's dirt cube with another texture.
	Save(TEXT("high"), TEXT("blockstates/stone.json"), TEXT("{\"variants\":{\"\":{\"model\":\"minecraft:block/zz_half\",\"x\":180}}}"));
	Save(TEXT("high"), TEXT("blockstates/sand.json"), TEXT("{\"variants\":{\"\":{\"model\":\"minecraft:block/zz_cobble_child\"}}}"));
	Save(TEXT("high"), TEXT("models/block/zz_cobble_child.json"), TEXT("{\"parent\":\"minecraft:block/zz_dirt_cube\",\"textures\":{\"all\":\"minecraft:block/cobblestone\"}}"));

	FString Error;
	const TSharedPtr<FMcResourcePack> Low = FMcResourcePack::Open(Root / TEXT("low"), &Error);
	const TSharedPtr<FMcResourcePack> High = FMcResourcePack::Open(Root / TEXT("high"), &Error);
	if (!TestTrue(TEXT("the packs open: ") + Error, Low.IsValid() && High.IsValid()))
	{
		return false;
	}
	FMcBlockModels& Models = FMcBlockModels::Get();
	const auto Bounds = [](const FMcBakedModel& Model, float& MinY, float& MaxY)
	{
		MinY = 10.0f;
		MaxY = -10.0f;
		for (const FMcBakedQuad& Quad : Model.Quads)
		{
			for (const FVector3f& P : Quad.Positions)
			{
				MinY = FMath::Min(MinY, P.Y);
				MaxY = FMath::Max(MaxY, P.Y);
			}
		}
	};
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const TOptional<FMcStateId> Stone = Registry.FindDefaultState(FName("minecraft:stone"));
	const TOptional<FMcStateId> Sand = Registry.FindDefaultState(FName("minecraft:sand"));
	const TOptional<FMcStateId> Fence = Registry.FindDefaultState(FName("minecraft:oak_fence"));
	if (!TestTrue(TEXT("stone, sand and the fence exist"), Stone.IsSet() && Sand.IsSet() && Fence.IsSet()))
	{
		return false;
	}

	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ Low, High })));
	Models.Unload();
	if (TestTrue(TEXT("models bake with both packs"), Models.EnsureBaked()))
	{
		float MinY, MaxY;
		const FMcBakedModel& StoneModel = Models.GetModel(Stone.GetValue());
		Bounds(StoneModel, MinY, MaxY);
		TestEqual(TEXT("the higher pack's stone is a box"), StoneModel.Quads.Num(), 6);
		TestEqual(TEXT("turned over by x 180: its bottom is at half height"), MinY, 0.5f);
		TestEqual(TEXT("and its top at the block's"), MaxY, 1.0f);
		const FMcBakedModel& SandModel = Models.GetModel(Sand.GetValue());
		TestEqual(TEXT("a model with its parent in the lower pack has faces"), SandModel.Quads.Num(), 6);
		for (const FMcBakedQuad& Quad : SandModel.Quads)
		{
			TestEqual(TEXT("with the child's own texture"), Quad.Texture, FName("minecraft:block/cobblestone"));
		}
		const FMcBakedModel& FenceModel = Models.GetModel(Fence.GetValue());
		Bounds(FenceModel, MinY, MaxY);
		TestEqual(TEXT("a multipart state: only the unconditional part for the default fence"), FenceModel.Quads.Num(), 6);
		TestEqual(TEXT("and it is the half box"), MaxY, 0.5f);
	}

	McPacks::SetStack(MakeShared<FMcPackStack>(TArray<TSharedPtr<FMcResourcePack>>({ High, Low })));
	Models.Unload();
	if (TestTrue(TEXT("models bake with the packs the other way round"), Models.EnsureBaked()))
	{
		const FMcBakedModel& StoneModel = Models.GetModel(Stone.GetValue());
		TestEqual(TEXT("the low pack's stone now wins: a cube"), StoneModel.Quads.Num(), 6);
		for (const FMcBakedQuad& Quad : StoneModel.Quads)
		{
			TestEqual(TEXT("of dirt"), Quad.Texture, FName("minecraft:block/dirt"));
		}
	}

	McPacks::SetStack(MakeShared<FMcPackStack>());
	Models.Unload();
	Models.EnsureBaked();
	IFileManager::Get().DeleteDirectory(*Root, false, true);
	return true;
}

#endif
