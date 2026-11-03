#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityManager.h"
#include "McFishing.h"
#include "McItemRegistry.h"
#include "McLoot.h"
#include "McRandom.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcFishingTest, "McClient.McPlayer.Fishing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcFishingTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };

	// The wait for a bite: 100 to 600 ticks, 100 less at each end a level of Lure, never nothing.
	{
		FRandomStream Random(7);
		int32 Low = MAX_int32, High = 0, LureLow = MAX_int32, LureHigh = 0;
		for (int32 Roll = 0; Roll < 4000; ++Roll)
		{
			const int32 Wait = McFishing::RollWait(Random, 0);
			Low = FMath::Min(Low, Wait);
			High = FMath::Max(High, Wait);
			const int32 Lured = McFishing::RollWait(Random, 3);
			LureLow = FMath::Min(LureLow, Lured);
			LureHigh = FMath::Max(LureHigh, Lured);
		}
		TestTrue(TEXT("unlured waits are 100 to 600 ticks"), Low >= 100 && Low < 110 && High <= 600 && High > 590);
		TestTrue(TEXT("Lure III waits at most 300 and at least a tick"), LureLow == 1 && LureHigh <= 300 && LureHigh > 290);

		int32 Open = 0, Covered = 0, Rain = 0;
		for (int32 Tick = 0; Tick < 10000; ++Tick)
		{
			Open += McFishing::CountdownStep(Random, true, false);
			Covered += McFishing::CountdownStep(Random, false, false);
			Rain += McFishing::CountdownStep(Random, true, true);
		}
		TestEqual(TEXT("under the sky every tick counts"), Open, 10000);
		TestTrue(TEXT("under a roof about half of them"), Covered > 4700 && Covered < 5300);
		TestTrue(TEXT("in rain a quarter count twice"), Rain > 12200 && Rain < 12800);
	}

	// A pool three deep in a stone floor.
	FMcBlockWorld World{FMcDimensionType()};
	for (int32 Z = -1; Z <= 1; ++Z)
	{
		for (int32 X = -1; X <= 1; ++X)
		{
			World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
		}
	}
	const FMcStateId Stone = State(TEXT("minecraft:stone"));
	const FMcStateId Water = State(TEXT("minecraft:water[level=0]"));
	for (int32 X = -8; X <= 8; ++X)
	{
		for (int32 Z = -8; Z <= 8; ++Z)
		{
			World.SetBlock(FIntVector(X, 0, Z), Stone);
			for (int32 Y = 1; Y <= 3; ++Y)
			{
				World.SetBlock(FIntVector(X, Y, Z), FMath::Abs(X) <= 6 && FMath::Abs(Z) <= 6 ? Water : Stone);
			}
		}
	}
	TestTrue(TEXT("the middle of the pool is open water"), McFishing::IsOpenWater(World, FIntVector(0, 3, 0)));
	TestFalse(TEXT("not by the edge"), McFishing::IsOpenWater(World, FIntVector(5, 3, 5)));
	World.SetBlock(FIntVector(1, 4, 1), Stone);
	TestFalse(TEXT("nor with a block over the water"), McFishing::IsOpenWater(World, FIntVector(0, 3, 0)));
	World.SetBlock(FIntVector(1, 4, 1), State(TEXT("minecraft:air")));

	// Cast from the edge, it lands in the water, floats at the surface, a fish bites, and reeling in then catches it.
	FMcEntityManager Manager(World);
	{
		const FVector Eye(0.5, 5.62, -7.5);
		FRandomStream Random(3);
		TUniquePtr<FMcFishingBobber> Owned = MakeUnique<FMcFishingBobber>();
		FMcFishingBobber& Bobber = *Owned;
		Bobber.Cast(Eye, 0.0f, 10.0f, Random);
		Bobber.AnglerEye = Eye;
		Manager.Add(MoveTemp(Owned));
		int32 Ticks = 0;
		while (Bobber.State != FMcFishingBobber::EState::Bobbing && Ticks++ < 200)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("the bobber lands in the water"), Bobber.State, FMcFishingBobber::EState::Bobbing);
		TestTrue(TEXT("ahead of the angler"), Bobber.Body.Position.Z > -5.0);
		for (int32 Settle = 0; Settle < 60; ++Settle)
		{
			Manager.Tick();
		}
		const double Middle = Bobber.Body.Position.Y + Bobber.Body.Height * 0.5;
		TestTrue(TEXT("and floats with its middle at the surface"), FMath::Abs(Middle - (3.0 + 8.0 / 9.0)) < 0.15 || Bobber.BiteTicks > 0);
		TestTrue(TEXT("in open water"), Bobber.bOpenWater);

		bool bApproached = false;
		Ticks = 0;
		while (Bobber.BiteTicks == 0 && Ticks++ < 1200)
		{
			bApproached |= Bobber.ApproachTicks > 0;
			Manager.Tick();
		}
		TestTrue(TEXT("a fish swims in"), bApproached);
		TestTrue(TEXT("and bites within the longest wait"), Bobber.BiteTicks > 0 && Bobber.Bites == 1);
		const FMcFishingBobber::FReel Reel = Bobber.Reel(Manager);
		TestTrue(TEXT("reeling in on a bite catches it"), Reel.bCatch);
		TestEqual(TEXT("and wears the rod by 1"), Reel.Damage, McFishing::CatchDamage);
		TestTrue(TEXT("the bobber is gone"), Bobber.bRemoved);
	}
	Manager.Tick();

	// Wound in with nothing on: no catch, and nothing worn.
	{
		TUniquePtr<FMcFishingBobber> Owned = MakeUnique<FMcFishingBobber>();
		FMcFishingBobber& Bobber = *Owned;
		Bobber.Body.Position = FVector(0.5, 3.8, 0.5);
		Manager.Add(MoveTemp(Owned));
		Manager.Tick();
		const FMcFishingBobber::FReel Reel = Bobber.Reel(Manager);
		TestFalse(TEXT("no fish, no catch"), Reel.bCatch);
		TestEqual(TEXT("no wear"), Reel.Damage, 0);
	}
	Manager.Tick();

	// Thrown at an item on the floor, it hooks it; reeling in pulls it toward the angler and wears the rod by 3.
	{
		World.SetBlock(FIntVector(7, 4, 7), Stone);
		FMcItemEntity& Item = Manager.DropItem(FVector(7.5, 5.0, 7.5), FMcItemStack(FName("minecraft:dirt"), 1));
		Item.Body.Velocity = FVector::ZeroVector;
		for (int32 Settle = 0; Settle < 20; ++Settle)
		{
			Manager.Tick();
		}
		TUniquePtr<FMcFishingBobber> Owned = MakeUnique<FMcFishingBobber>();
		FMcFishingBobber& Bobber = *Owned;
		Bobber.Body.Position = Item.Body.Position + FVector(-0.8, 0.1, 0.0);
		Bobber.Body.Velocity = FVector(0.5, 0.0, 0.0);
		Bobber.AnglerEye = FVector(0.5, 5.6, 7.5);
		Manager.Add(MoveTemp(Owned));
		for (int32 Tick = 0; Tick < 10 && Bobber.State != FMcFishingBobber::EState::Hooked; ++Tick)
		{
			Manager.Tick();
		}
		TestEqual(TEXT("it hooks the item"), Bobber.State, FMcFishingBobber::EState::Hooked);
		const FMcFishingBobber::FReel Reel = Bobber.Reel(Manager);
		TestEqual(TEXT("an item wears the rod by 3"), Reel.Damage, McFishing::ItemDamage);
		TestTrue(TEXT("and is pulled to the angler"), Item.Body.Velocity.X < -0.3);
	}

	// The catch, from the data's tables: mostly fish, treasure only in open water, bamboo only in a jungle.
	{
		const TSet<FName> Treasure = { FName("minecraft:name_tag"), FName("minecraft:saddle"), FName("minecraft:nautilus_shell"), FName("minecraft:bow"), FName("minecraft:book"),
			FName("minecraft:enchanted_book") };
		const TSet<FName> Fish = { FName("minecraft:cod"), FName("minecraft:salmon"), FName("minecraft:tropical_fish"), FName("minecraft:pufferfish") };
		const auto Count = [&](const McLoot::FFishingContext& Context, const TSet<FName>& Of, int32 Rolls)
		{
			FMcRandom Random(99);
			int32 Found = 0;
			for (int32 Roll = 0; Roll < Rolls; ++Roll)
			{
				TArray<FMcItemStack> Catch;
				McLoot::RollFishing(Context, Random, Catch);
				for (const FMcItemStack& Stack : Catch)
				{
					Found += Of.Contains(Stack.Item) ? 1 : 0;
				}
			}
			return Found;
		};
		McLoot::FFishingContext Open;
		Open.Biome = FName("minecraft:plains");
		McLoot::FFishingContext Shut = Open;
		Shut.bOpenWater = false;
		McLoot::FFishingContext Lucky = Open;
		Lucky.Luck = 3;
		McLoot::FFishingContext Jungle = Open;
		Jungle.Biome = FName("minecraft:jungle");
		const int32 FishCaught = Count(Open, Fish, 2000);
		TestTrue(TEXT("about 85% fish"), FishCaught > 1600 && FishCaught < 1800);
		const int32 OpenTreasure = Count(Open, Treasure, 2000);
		TestTrue(TEXT("about 5% treasure in open water"), OpenTreasure > 50 && OpenTreasure < 160);
		TestEqual(TEXT("no treasure out of open water"), Count(Shut, { FName("minecraft:name_tag"), FName("minecraft:saddle"), FName("minecraft:nautilus_shell") }, 2000), 0);
		TestTrue(TEXT("Luck of the Sea III brings more treasure"), Count(Lucky, Treasure, 2000) > OpenTreasure * 3 / 2);
		TestEqual(TEXT("no bamboo on the plains"), Count(Open, { FName("minecraft:bamboo") }, 2000), 0);
		TestTrue(TEXT("but some in a jungle"), Count(Jungle, { FName("minecraft:bamboo") }, 2000) > 0);
	}
	return true;
}

#endif
