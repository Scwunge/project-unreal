#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McDimensionType.h"
#include "McPortal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FMcDimensionType MakeDimension(const TCHAR* Name, double Scale)
	{
		FMcDimensionType Type;
		Type.Name = Name;
		Type.CoordinateScale = Scale;
		return Type;
	}

	// Chunks -2..2 on both axes, all air.
	void AddChunks(FMcBlockWorld& World)
	{
		for (int32 Z = -2; Z <= 2; ++Z)
		{
			for (int32 X = -2; X <= 2; ++X)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcPortalTest, "McClient.McSim.Portal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcPortalTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Obsidian = State(TEXT("minecraft:obsidian"));
	const FMcStateId Air = State(TEXT("minecraft:air"));
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Fire = State(TEXT("minecraft:fire"));
	const auto Name = [&](FMcBlockWorld& World, const FIntVector& Pos) { return Registry.GetBlockOf(World.GetBlock(Pos)).Name.ToString(); };

	// An obsidian frame round an Width by Height opening whose lowest corner is Pos; the corners are left out when asked.
	const auto MakeFrame = [&](FMcBlockWorld& World, const FIntVector& Pos, int32 Width, int32 Height, bool bAlongX, bool bCorners)
	{
		const FIntVector Dir = bAlongX ? FIntVector(1, 0, 0) : FIntVector(0, 0, 1);
		for (int32 I = -1; I <= Width; ++I)
		{
			for (int32 K = -1; K <= Height; ++K)
			{
				const bool bEdge = I == -1 || I == Width || K == -1 || K == Height;
				const bool bCorner = (I == -1 || I == Width) && (K == -1 || K == Height);
				if (bEdge && (bCorners || !bCorner))
				{
					World.SetBlock(Pos + Dir * I + FIntVector(0, K, 0), Obsidian);
				}
			}
		}
	};
	const auto Set = [](FMcBlockWorld& World) { return McPortal::FSetBlock([&World](const FIntVector& At, FMcStateId S) { World.SetBlock(At, S); }); };

	{
		// Sizes: 2 to 21 wide, 3 to 21 tall.
		TestTrue(TEXT("2 by 3 is the smallest"), McPortal::IsFrameSize(2, 3));
		TestTrue(TEXT("21 by 21 is the largest"), McPortal::IsFrameSize(21, 21));
		TestFalse(TEXT("1 wide"), McPortal::IsFrameSize(1, 3));
		TestFalse(TEXT("2 tall"), McPortal::IsFrameSize(2, 2));
		TestFalse(TEXT("22 wide"), McPortal::IsFrameSize(22, 5));
		TestFalse(TEXT("22 tall"), McPortal::IsFrameSize(3, 22));
	}

	{
		// Frames: valid sizes are found from any open block inside, either way round; invalid ones are not.
		FMcBlockWorld World(MakeDimension(TEXT("minecraft:overworld"), 1.0));
		AddChunks(World);
		const TArray<TPair<int32, int32>> Good = { {2, 3}, {3, 5}, {2, 21}, {21, 3}, {21, 21} };
		for (const bool bAlongX : { true, false })
		{
			for (const TPair<int32, int32>& Size : Good)
			{
				FMcBlockWorld Local(MakeDimension(TEXT("minecraft:overworld"), 1.0));
				AddChunks(Local);
				const FIntVector Pos(-10, 5, -10);
				MakeFrame(Local, Pos, Size.Key, Size.Value, bAlongX, false);
				McPortal::FFrame Frame;
				const FIntVector Inside = Pos + (bAlongX ? FIntVector(Size.Key - 1, 0, 0) : FIntVector(0, 0, Size.Key - 1)) + FIntVector(0, Size.Value - 1, 0);
				const FString Label = FString::Printf(TEXT("%d by %d along %s"), Size.Key, Size.Value, bAlongX ? TEXT("x") : TEXT("z"));
				if (TestTrue(*(Label + TEXT(" is found")), McPortal::FindFrame(Local, Inside, bAlongX, false, Frame)))
				{
					TestTrue(*(Label + TEXT(" corner")), Frame.Pos == Pos);
					TestEqual(*(Label + TEXT(" width")), Frame.Width, Size.Key);
					TestEqual(*(Label + TEXT(" height")), Frame.Height, Size.Value);
				}
				// The other way round it is no frame.
				TestFalse(*(Label + TEXT(" not the other way")), McPortal::FindFrame(Local, Inside, !bAlongX, false, Frame));
			}
		}

		const FIntVector Pos(-10, 5, -10);
		McPortal::FFrame Frame;
		// Too small, too big.
		MakeFrame(World, Pos, 1, 3, true, true);
		TestFalse(TEXT("1 wide is no frame"), McPortal::FindFrame(World, Pos, true, false, Frame));
		MakeFrame(World, Pos + FIntVector(8, 0, 0), 2, 2, true, true);
		TestFalse(TEXT("2 tall is no frame"), McPortal::FindFrame(World, Pos + FIntVector(8, 0, 0), true, false, Frame));
		FMcBlockWorld Big(MakeDimension(TEXT("minecraft:overworld"), 1.0));
		AddChunks(Big);
		MakeFrame(Big, FIntVector(-12, 5, 0), 22, 5, true, true);
		TestFalse(TEXT("22 wide is no frame"), McPortal::FindFrame(Big, FIntVector(-12, 5, 0), true, false, Frame));

		// A gap in the frame, and a block in the opening.
		FMcBlockWorld Broken(MakeDimension(TEXT("minecraft:overworld"), 1.0));
		AddChunks(Broken);
		MakeFrame(Broken, Pos, 2, 3, true, false);
		TestTrue(TEXT("the intact frame is found"), McPortal::FindFrame(Broken, Pos, true, false, Frame));
		Broken.SetBlock(Pos + FIntVector(1, -1, 0), Stone);
		TestFalse(TEXT("a missing obsidian in the floor"), McPortal::FindFrame(Broken, Pos, true, false, Frame));
		Broken.SetBlock(Pos + FIntVector(1, -1, 0), Obsidian);
		Broken.SetBlock(Pos + FIntVector(-1, 1, 0), Air);
		TestFalse(TEXT("a missing obsidian in the side"), McPortal::FindFrame(Broken, Pos, true, false, Frame));
		Broken.SetBlock(Pos + FIntVector(-1, 1, 0), Obsidian);
		Broken.SetBlock(Pos + FIntVector(0, 2, 0), Stone);
		TestFalse(TEXT("a block in the opening"), McPortal::FindFrame(Broken, Pos, true, false, Frame));
		Broken.SetBlock(Pos + FIntVector(0, 2, 0), Fire);
		TestTrue(TEXT("fire in the opening is fine"), McPortal::FindFrame(Broken, Pos, true, false, Frame));
	}

	{
		// Lighting: fire in a complete frame fills it with portal blocks; in the End nothing happens.
		FMcBlockWorld World(MakeDimension(TEXT("minecraft:overworld"), 1.0));
		AddChunks(World);
		const FIntVector Pos(0, 5, 0);
		MakeFrame(World, Pos, 2, 3, true, false);
		World.SetBlock(Pos, Fire);
		TestEqual(TEXT("lighting a 2 by 3 frame makes 6 portal blocks"), McPortal::TryLight(World, Pos, Set(World)), 6);
		TestTrue(TEXT("the opening is portal"), McPortal::IsPortalBlock(World.GetBlock(Pos + FIntVector(1, 2, 0))));
		TestTrue(TEXT("across x"), McPortal::IsAlongX(World.GetBlock(Pos)));

		// Breaking the frame takes the portal with it.
		World.SetBlock(Pos + FIntVector(-1, 1, 0), Air);
		const int32 Removed = McPortal::RemoveBroken(World, Pos + FIntVector(-1, 1, 0), Set(World));
		TestEqual(TEXT("all six portal blocks go"), Removed, 6);
		TestFalse(TEXT("the portal is gone"), McPortal::IsPortalBlock(World.GetBlock(Pos)));

		// A frame facing z lights too.
		FMcBlockWorld Z(MakeDimension(TEXT("minecraft:overworld"), 1.0));
		AddChunks(Z);
		MakeFrame(Z, Pos, 3, 4, false, true);
		Z.SetBlock(Pos + FIntVector(0, 1, 1), Fire);
		TestEqual(TEXT("lighting a 3 by 4 frame along z"), McPortal::TryLight(Z, Pos + FIntVector(0, 1, 1), Set(Z)), 12);
		TestFalse(TEXT("its axis is z"), McPortal::IsAlongX(Z.GetBlock(Pos)));

		// Fire where there is no frame lights nothing.
		TestEqual(TEXT("no frame, no portal"), McPortal::TryLight(Z, FIntVector(10, 5, 10), Set(Z)), 0);

		FMcBlockWorld End(MakeDimension(TEXT("minecraft:the_end"), 1.0));
		AddChunks(End);
		MakeFrame(End, Pos, 2, 3, true, false);
		End.SetBlock(Pos, Fire);
		TestEqual(TEXT("nothing lights in the End"), McPortal::TryLight(End, Pos, Set(End)), 0);
	}

	{
		// Scaling: the Overworld to the Nether divides x and z by 8, rounding down; y stays.
		TestTrue(TEXT("overworld to nether"), McPortal::ScalePosition(FIntVector(80, 64, -80), 1.0, 8.0) == FIntVector(10, 64, -10));
		TestTrue(TEXT("rounds down"), McPortal::ScalePosition(FIntVector(79, 70, 7), 1.0, 8.0) == FIntVector(9, 70, 0));
		TestTrue(TEXT("rounds down below zero"), McPortal::ScalePosition(FIntVector(-1, 70, -9), 1.0, 8.0) == FIntVector(-1, 70, -2));
		TestTrue(TEXT("nether to overworld"), McPortal::ScalePosition(FIntVector(10, 64, -10), 8.0, 1.0) == FIntVector(80, 64, -80));
		TestTrue(TEXT("the same scale changes nothing"), McPortal::ScalePosition(FIntVector(5, 9, 7), 1.0, 1.0) == FIntVector(5, 9, 7));
		TestEqual(TEXT("search 128 for the overworld"), McPortal::GetSearchRadius(1.0), 128);
		TestEqual(TEXT("search 16 for the nether"), McPortal::GetSearchRadius(8.0), 16);
	}

	{
		// Search against creation.
		FMcBlockWorld Nether(MakeDimension(TEXT("minecraft:the_nether"), 8.0));
		AddChunks(Nether);
		McPortal::FFound Found;
		TestFalse(TEXT("an empty nether has no portal"), McPortal::FindPortal(Nether, FIntVector(0, 64, 0), 16, Found));

		bool bBuilt = false;
		const McPortal::FFound Made = McPortal::FindOrBuildPortal(Nether, FIntVector(0, 80, 0), true, Set(Nether), bBuilt);
		TestTrue(TEXT("none there: one is built"), bBuilt);
		TestTrue(TEXT("with portal blocks in it"), McPortal::IsPortalBlock(Nether.GetBlock(Made.Pos)));
		TestTrue(TEXT("and a frame round them"), Name(Nether, Made.Pos + FIntVector(-1, 0, 0)) == TEXT("minecraft:obsidian"));
		TestEqual(TEXT("it is 2 wide inside"), Made.Width, 2);
		TestTrue(TEXT("with nowhere to stand it was forced to the centre"), Made.Pos.X == 0 && Made.Pos.Z == 0);

		bBuilt = true;
		const McPortal::FFound Again = McPortal::FindOrBuildPortal(Nether, FIntVector(10, 80, 6), true, Set(Nether), bBuilt);
		TestFalse(TEXT("within 16 blocks it is found, not built"), bBuilt);
		TestTrue(TEXT("the same portal"), Again.Pos == Made.Pos);

		// Out of range of 16 in the nether, a second is built; in the overworld (128) the far one is still found.
		bBuilt = false;
		const McPortal::FFound Far = McPortal::FindOrBuildPortal(Nether, FIntVector(40, 80, 0), true, Set(Nether), bBuilt);
		TestTrue(TEXT("out of range in the nether: built"), bBuilt);
		TestFalse(TEXT("a different portal"), Far.Pos == Made.Pos);

		FMcBlockWorld Over(MakeDimension(TEXT("minecraft:overworld"), 1.0));
		AddChunks(Over);
		bBuilt = false;
		const McPortal::FFound First = McPortal::FindOrBuildPortal(Over, FIntVector(0, 80, 0), false, Set(Over), bBuilt);
		TestTrue(TEXT("overworld builds the first"), bBuilt);
		bBuilt = true;
		const McPortal::FFound Second = McPortal::FindOrBuildPortal(Over, FIntVector(40, 80, 0), false, Set(Over), bBuilt);
		TestFalse(TEXT("40 blocks away is in range of 128"), bBuilt);
		TestTrue(TEXT("the same one"), Second.Pos == First.Pos);
		TestFalse(TEXT("its axis is z"), First.bAlongX);

		// The nearest of two wins.
		FMcBlockWorld Two(MakeDimension(TEXT("minecraft:overworld"), 1.0));
		AddChunks(Two);
		const FIntVector A(-30, 80, 0), B(20, 80, 0);
		MakeFrame(Two, A, 2, 3, true, false);
		MakeFrame(Two, B, 2, 3, true, false);
		McPortal::FFrame FrameA, FrameB;
		TestTrue(TEXT("frame A"), McPortal::FindFrame(Two, A, true, false, FrameA));
		TestTrue(TEXT("frame B"), McPortal::FindFrame(Two, B, true, false, FrameB));
		McPortal::Fill(FrameA, Set(Two));
		McPortal::Fill(FrameB, Set(Two));
		if (TestTrue(TEXT("a portal is found"), McPortal::FindPortal(Two, FIntVector(10, 80, 0), 128, Found)))
		{
			TestTrue(TEXT("the nearer one"), Found.Pos == B);
		}
		TestFalse(TEXT("none within 5"), McPortal::FindPortal(Two, FIntVector(0, 80, 0), 5, Found));

		// Solid ground: the portal is built on it rather than forced.
		FMcBlockWorld Ground(MakeDimension(TEXT("minecraft:overworld"), 1.0));
		AddChunks(Ground);
		for (int32 X = -30; X <= 30; ++X)
		{
			for (int32 Z = -30; Z <= 30; ++Z)
			{
				Ground.SetBlock(FIntVector(X, 63, Z), Stone);
			}
		}
		const McPortal::FFound OnGround = McPortal::BuildPortal(Ground, FIntVector(5, 64, 5), true, Set(Ground));
		TestEqual(TEXT("built standing on the ground"), OnGround.Pos.Y, 64);
		TestTrue(TEXT("beside the centre"), FMath::Abs(OnGround.Pos.X - 5) <= 16 && FMath::Abs(OnGround.Pos.Z - 5) <= 16);
		TestTrue(TEXT("the standing point is inside the portal"), McPortal::GetStandingPoint(OnGround).Y == 64.0);
	}

	{
		// The end portal: only the twelfth eye opens it.
		FMcBlockWorld World(MakeDimension(TEXT("minecraft:overworld"), 1.0));
		AddChunks(World);
		const FIntVector Centre(0, 40, 0);
		struct FSpot { int32 DX, DZ; const TCHAR* Facing; };
		TArray<FSpot> Ring;
		for (int32 Offset = -1; Offset <= 1; ++Offset)
		{
			Ring.Add({ Offset, -2, TEXT("south") });
			Ring.Add({ Offset, 2, TEXT("north") });
			Ring.Add({ -2, Offset, TEXT("east") });
			Ring.Add({ 2, Offset, TEXT("west") });
		}
		TArray<FIntVector> Frames;
		for (const FSpot& Spot : Ring)
		{
			const FMcStateId Frame = State(*FString::Printf(TEXT("minecraft:end_portal_frame[eye=false,facing=%s]"), Spot.Facing));
			World.SetBlock(Centre + FIntVector(Spot.DX, 0, Spot.DZ), Frame);
			Frames.Add(Centre + FIntVector(Spot.DX, 0, Spot.DZ));
		}
		TestEqual(TEXT("twelve frames"), Frames.Num(), 12);
		for (int32 Index = 0; Index < Frames.Num(); ++Index)
		{
			const FMcStateId Before = World.GetBlock(Frames[Index]);
			TestTrue(TEXT("a frame"), McPortal::IsEndPortalFrame(Before));
			TestFalse(TEXT("without an eye"), McPortal::HasEye(Before));
			World.SetBlock(Frames[Index], McPortal::WithEye(Before));
			TestTrue(TEXT("with an eye"), McPortal::HasEye(World.GetBlock(Frames[Index])));
			const bool bOpened = McPortal::TryOpenEndPortal(World, Frames[Index], Set(World));
			TestEqual(*FString::Printf(TEXT("only the twelfth eye opens it (%d)"), Index + 1), bOpened, Index == 11);
		}
		for (int32 DZ = -1; DZ <= 1; ++DZ)
		{
			for (int32 DX = -1; DX <= 1; ++DX)
			{
				TestTrue(TEXT("the hole is end portal"), Name(World, Centre + FIntVector(DX, 0, DZ)) == TEXT("minecraft:end_portal"));
			}
		}
	}

	{
		// The platform in the End: obsidian 5 by 5 with air over it.
		FMcBlockWorld End(MakeDimension(TEXT("minecraft:the_end"), 1.0));
		for (int32 Z = -1; Z <= 7; ++Z)
		{
			End.AddChunk(End.MakeChunk(FIntPoint(6, Z - 3)));
		}
		End.SetBlock(FIntVector(100, 50, 0), Stone);
		McPortal::BuildEndPlatform(End, Set(End));
		const FIntVector Middle(McPortal::EndPlatformX, McPortal::EndPlatformY, McPortal::EndPlatformZ);
		TestTrue(TEXT("obsidian in the middle"), Name(End, Middle) == TEXT("minecraft:obsidian"));
		TestTrue(TEXT("obsidian at the corner"), Name(End, Middle + FIntVector(2, 0, 2)) == TEXT("minecraft:obsidian"));
		TestTrue(TEXT("the platform is 5 wide"), Name(End, Middle + FIntVector(3, 0, 0)) != TEXT("minecraft:obsidian"));
		TestTrue(TEXT("air above"), Registry.IsAir(End.GetBlock(Middle + FIntVector(0, 3, 0))));
		TestTrue(TEXT("what was in the way is cleared"), Registry.IsAir(End.GetBlock(FIntVector(100, 50, 0))));
	}

	return true;
}

#endif
