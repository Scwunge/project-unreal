#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"
#include "McEntityHost.h"
#include "McItemRegistry.h"
#include "McLoot.h"
#include "McRandom.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcExplosionDropsTest, "McClient.McServer.ExplosionDrops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcExplosionDropsTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	const auto State = [&](const TCHAR* Text) { return Registry.ParseState(Text).Get(0); };
	const FMcStateId Stone = State(TEXT("minecraft:stone"));

	{
		// The tables, rolled for a block broken by hand: no tool, so no silk touch, and nothing blown up.
		FMcRandom Random(5);
		const auto Roll = [&](const TCHAR* Block, float Radius)
		{
			McLoot::FBlockContext Context;
			Context.State = State(Block);
			Context.ExplosionRadius = Radius;
			TArray<FMcItemStack> Out;
			McLoot::RollBlock(Context, Random, Out);
			return Out;
		};
		TArray<FMcItemStack> Out = Roll(TEXT("minecraft:stone"), 0.0f);
		TestTrue(TEXT("stone gives cobblestone, not stone"), Out.Num() == 1 && Out[0].Item == FName("minecraft:cobblestone") && Out[0].Count == 1);
		Out = Roll(TEXT("minecraft:oak_slab[type=double]"), 0.0f);
		TestTrue(TEXT("a double slab gives two"), Out.Num() == 1 && Out[0].Item == FName("minecraft:oak_slab") && Out[0].Count == 2);
		Out = Roll(TEXT("minecraft:oak_slab[type=bottom]"), 0.0f);
		TestTrue(TEXT("a single one gives one"), Out.Num() == 1 && Out[0].Count == 1);
		Out = Roll(TEXT("minecraft:oak_door[half=upper]"), 0.0f);
		TestTrue(TEXT("the top of a door gives nothing"), Out.IsEmpty());
		Out = Roll(TEXT("minecraft:oak_door[half=lower]"), 0.0f);
		TestTrue(TEXT("and the bottom the door"), Out.Num() == 1 && Out[0].Item == FName("minecraft:oak_door"));
		bool bLeavesDropped = false;
		for (int32 Try = 0; Try < 200; ++Try)
		{
			for (const FMcItemStack& Stack : Roll(TEXT("minecraft:oak_leaves"), 0.0f))
			{
				bLeavesDropped |= Stack.Item == FName("minecraft:oak_leaves");
			}
		}
		TestFalse(TEXT("leaves never drop themselves without shears or silk touch"), bLeavesDropped);

		// Blown up with a power of 3, a third of the items stay.
		int32 Kept = 0;
		constexpr int32 Tries = 1500;
		for (int32 Try = 0; Try < Tries; ++Try)
		{
			Kept += Roll(TEXT("minecraft:stone"), 3.0f).Num();
		}
		TestTrue(*FString::Printf(TEXT("about a third of blown up stone drops (kept %d of %d)"), Kept, Tries), Kept > Tries * 0.28 && Kept < Tries * 0.39);
		// explosion_decay thins a stack: each of 2 slab halves survives by itself.
		int32 Halves = 0;
		for (int32 Try = 0; Try < Tries; ++Try)
		{
			for (const FMcItemStack& Stack : Roll(TEXT("minecraft:oak_slab[type=double]"), 3.0f))
			{
				Halves += Stack.Count;
			}
		}
		TestTrue(*FString::Printf(TEXT("each half of a blown up double slab stays with a third (kept %d of %d)"), Halves, 2 * Tries), Halves > 2 * Tries * 0.28 && Halves < 2 * Tries * 0.39);
		TestTrue(TEXT("air has no table"), !McLoot::RollBlock({ State(TEXT("minecraft:air")), 0.0f }, Random, Out));

		// copy_components: a shulker box that is broken keeps what it held (a barrel's table copies only its name, so its contents spill).
		FMcBlockEntity Box = FMcBlockEntity::MakeContainer(27);
		Box.Slots[3] = FMcItemStack(FName("minecraft:diamond"), 5);
		McLoot::FBlockContext BoxContext;
		BoxContext.State = State(TEXT("minecraft:shulker_box[facing=up]"));
		BoxContext.BlockEntity = &Box;
		Out.Reset();
		TestTrue(TEXT("a shulker box has a table"), McLoot::RollBlock(BoxContext, Random, Out));
		TestTrue(TEXT("a shulker box drops itself"), Out.Num() == 1 && Out[0].Item == FName("minecraft:shulker_box"));
		TestTrue(TEXT("a shulker box drop carries its contents"), (Out.Num() == 1 && Out[0].Get(FName("minecraft:container")).IsValid() && Out[0].Get(FName("minecraft:container"))->AsArray().Num() == 1));
		BoxContext.BlockEntity = nullptr;
		Out.Reset();
		McLoot::RollBlock(BoxContext, Random, Out);
		TestTrue(TEXT("a shulker box with no block entity carries no contents"), (Out.Num() == 1 && Out[0].Get(FName("minecraft:container")).IsValid() && Out[0].Get(FName("minecraft:container"))->AsArray().Num() == 0));
	}

	{
		// An explosion in a stone floor: it drops cobblestone and never stone, destroys the items it hurts and pushes
		// the ones it only shakes, and keeps the items it has just dropped.
		FMcBlockWorld World{FMcDimensionType()};
		for (int32 Z = -1; Z <= 1; ++Z)
		{
			for (int32 X = -1; X <= 1; ++X)
			{
				World.AddChunk(World.MakeChunk(FIntPoint(X, Z)));
			}
		}
		for (int32 X = -16; X <= 31; ++X)
		{
			for (int32 Y = 0; Y <= 3; ++Y)
			{
				for (int32 Z = -16; Z <= 31; ++Z)
				{
					World.SetBlock(FIntVector(X, Y, Z), Stone);
				}
			}
		}
		FMcEntityHost Host(World, 1);
		FMcEntityManager& Manager = Host.GetManager();
		FMcItemEntity& Near = Manager.DropItem(FVector(10.5, 4.0, 8.5), FMcItemStack(FName("minecraft:dirt"), 1));
		FMcItemEntity& Edge = Manager.DropItem(FVector(14.0, 4.0, 8.5), FMcItemStack(FName("minecraft:dirt"), 1));
		FMcItemEntity& Far = Manager.DropItem(FVector(26.5, 4.0, 8.5), FMcItemStack(FName("minecraft:dirt"), 1));
		FMcItemEntity& Resistant = Manager.DropItem(FVector(9.5, 4.0, 8.5), FMcItemStack(FName("minecraft:netherite_ingot"), 1));
		for (FMcItemEntity* Item : { &Near, &Edge, &Far, &Resistant })
		{
			Item->Body.Velocity = FVector::ZeroVector;
		}
		Manager.Explode(FVector(8.5, 4.0625, 8.5), 3.0f);

		TestTrue(TEXT("an item two blocks from the blast is destroyed"), Near.bRemoved);
		TestFalse(TEXT("one that resists explosions isn't"), Resistant.bRemoved);
		TestTrue(TEXT("and is pushed away from it"), Resistant.Body.Velocity.X > 0.1);
		TestFalse(TEXT("an item at the edge of the blast lives"), Edge.bRemoved);
		TestTrue(TEXT("and is shoved along"), Edge.Body.Velocity.X > 0.0);
		TestTrue(TEXT("one far away is left alone"), !Far.bRemoved && Far.Body.Velocity.IsNearlyZero());

		int32 Cobblestone = 0;
		int32 Stones = 0;
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			if (Entity->GetType() == FMcItemEntity::StaticType() && !Entity->bRemoved)
			{
				const FMcItemEntity& Item = static_cast<const FMcItemEntity&>(*Entity);
				Cobblestone += Item.Stack.Item == FName("minecraft:cobblestone") ? Item.Stack.Count : 0;
				Stones += Item.Stack.Item == FName("minecraft:stone") ? Item.Stack.Count : 0;
			}
		}
		TestTrue(TEXT("the crater leaves cobblestone"), Cobblestone > 0);
		TestEqual(TEXT("and no stone"), Stones, 0);
	}
	return true;
}

#endif
