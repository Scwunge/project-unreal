#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McExplosion.h"
#include "McMob.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	void FillBox(FMcBlockWorld& World, int32 X0, int32 Y0, int32 Z0, int32 X1, int32 Y1, int32 Z1, FMcStateId Block)
	{
		for (int32 X = X0; X <= X1; ++X)
		{
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 Z = Z0; Z <= Z1; ++Z)
				{
					World.SetBlock(FIntVector(X, Y, Z), Block);
				}
			}
		}
	}

	void AddChunks(FMcBlockWorld& World)
	{
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcExplosionTest, "McClient.McEntity.Explosion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcExplosionTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Air = State(TEXT("minecraft:air"));
	const FMcStateId Obsidian = State(TEXT("minecraft:obsidian"));

	// The numbers.
	TestTrue(TEXT("stone resists 6"), FMath::IsNearlyEqual(McExplosion::GetBlastResistance(Stone), 6.0f));
	TestTrue(TEXT("obsidian 1200"), FMath::IsNearlyEqual(McExplosion::GetBlastResistance(Obsidian), 1200.0f));
	TestTrue(TEXT("bedrock can't be broken"), McExplosion::GetBlastResistance(State(TEXT("minecraft:bedrock"))) >= 3600000.0f);
	TestTrue(TEXT("water 100"), FMath::IsNearlyEqual(McExplosion::GetBlastResistance(State(TEXT("minecraft:water[level=0]"))), 100.0f));
	TestTrue(TEXT("dirt 0.5"), FMath::IsNearlyEqual(McExplosion::GetBlastResistance(State(TEXT("minecraft:dirt"))), 0.5f));
	TestTrue(TEXT("planks 3"), FMath::IsNearlyEqual(McExplosion::GetBlastResistance(State(TEXT("minecraft:oak_planks"))), 3.0f));
	TestTrue(TEXT("air nothing"), FMath::IsNearlyEqual(McExplosion::GetBlastResistance(Air), 0.0f));
	TestTrue(TEXT("a creeper's blast point blank does 43"), FMath::IsNearlyEqual(McExplosion::GetDamage(3.0f, 0.0f, 1.0f), 43.0f));
	TestTrue(TEXT("and nothing past twice its power"), FMath::IsNearlyEqual(McExplosion::GetDamage(3.0f, 6.0f, 1.0f), 0.0f));
	TestTrue(TEXT("or behind a wall"), FMath::IsNearlyEqual(McExplosion::GetDamage(3.0f, 1.0f, 0.0f), 0.0f));

	// A floor of stone four deep, open air above it.
	FMcBlockWorld World{FMcDimensionType()};
	AddChunks(World);
	FillBox(World, -16, 0, -16, 31, 3, 31, Stone);
	FRandomStream Random(11);
	// A hair above the floor, as a creeper's is.
	const FVector Centre(8.5, 4.0625, 8.5);
	{
		TArray<FIntVector> Cells;
		McExplosion::Collect(World, Centre, 3.0f, Random, Cells);
		TestTrue(TEXT("the blast digs"), Cells.Num() > 5);
		TestTrue(TEXT("down into the floor under it"), Cells.Contains(FIntVector(8, 3, 8)));
		bool bFar = false;
		bool bAbove = false;
		for (const FIntVector& Cell : Cells)
		{
			bFar |= FVector::Dist(FVector(Cell) + FVector(0.5), Centre) > 6.0;
			bAbove |= Cell.Y >= 4;
		}
		TestFalse(TEXT("but never far (a power of 3 reaches 5 blocks at most)"), bFar);
		TestFalse(TEXT("and takes nothing out of the open air"), bAbove);
		bool bDeep = false;
		for (const FIntVector& Cell : Cells)
		{
			bDeep |= Cell.Y == 0;
		}
		TestFalse(TEXT("stone resists: the crater doesn't reach down four blocks"), bDeep);
	}
	{
		// Through the manager: the blocks go, and what they leave is reported.
		FMcEntityManager Manager(World);
		TArray<FMcDestroyedBlock> Reported;
		Manager.OnBlocksDestroyed = [&Reported](const TArray<FMcDestroyedBlock>& Blocks, float) { Reported = Blocks; };
		FMcMob* Pig = new FMcMob(*FMcMobType::Find(TEXT("minecraft:pig")));
		Pig->Body.Position = FVector(10.5, 4.0, 8.5);
		Manager.Add(TUniquePtr<FMcEntity>(Pig));
		float PlayerDamage = 0.0f;
		Manager.OnPlayerHurt = [&PlayerDamage](const FVector&, float Damage) { PlayerDamage += Damage; };
		Manager.PlayerPositions = { FVector(6.5, 4.0, 8.5) };
		Manager.Explode(Centre, 3.0f);

		TestEqual(TEXT("the block under the blast is gone"), World.GetBlock(FIntVector(8, 3, 8)), Air);
		TestFalse(TEXT("and blocks it reported"), Reported.IsEmpty());
		bool bAllStone = true;
		for (const FMcDestroyedBlock& Block : Reported)
		{
			bAllStone &= Block.State == Stone;
			bAllStone &= World.GetBlock(Block.Pos) == Air;
		}
		TestTrue(TEXT("each was stone and is air now"), bAllStone);
		TestEqual(TEXT("the floor far off is whole"), World.GetBlock(FIntVector(8, 3, 20)), Stone);
		TestTrue(TEXT("a pig two blocks away is killed"), Pig->Health <= 0.0f && Pig->bRemoved);
		TestTrue(TEXT("a player two blocks away is hurt, hard"), PlayerDamage > 20.0f);
	}
	{
		// Obsidian doesn't break; the stone under it is safe, too.
		FMcBlockWorld Hard{FMcDimensionType()};
		AddChunks(Hard);
		FillBox(Hard, -16, 0, -16, 31, 3, 31, Stone);
		FillBox(Hard, 5, 3, 5, 11, 3, 11, Obsidian);
		FMcEntityManager Manager(Hard);
		Manager.Explode(Centre, 3.0f);
		TestEqual(TEXT("obsidian stays"), Hard.GetBlock(FIntVector(8, 3, 8)), Obsidian);
		TestEqual(TEXT("and the stone under it"), Hard.GetBlock(FIntVector(8, 2, 8)), Stone);
		// The same blast in the open takes the stone on a different side of the crater, so the test above isn't an accident.
		FMcBlockWorld Soft{FMcDimensionType()};
		AddChunks(Soft);
		FillBox(Soft, -16, 0, -16, 31, 3, 31, Stone);
		FMcEntityManager SoftManager(Soft);
		SoftManager.Explode(Centre, 3.0f);
		TestEqual(TEXT("without it, that stone goes"), Soft.GetBlock(FIntVector(8, 3, 8)), Air);
	}
	{
		// A wall shields: a box behind a slab of stone sees little of the blast.
		FMcBlockWorld Walled{FMcDimensionType()};
		AddChunks(Walled);
		FillBox(Walled, -16, 0, -16, 31, 0, 31, Stone);
		const FBox Target(FVector(12.0, 1.0, 8.0), FVector(12.6, 2.8, 8.6));
		const FVector Blast(8.5, 1.5, 8.3);
		TestTrue(TEXT("in the open, all of it is exposed"), McExplosion::GetExposure(Walled, Blast, Target) > 0.95f);
		FillBox(Walled, 10, 1, 5, 10, 4, 12, Stone);
		TestTrue(TEXT("behind a wall, none"), McExplosion::GetExposure(Walled, Blast, Target) < 0.05f);
	}
	return true;
}

#endif
