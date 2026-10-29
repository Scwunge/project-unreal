#include "McChestLink.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockUse.h"
#include "McBuckets.h"
#include "McBlockWorld.h"
#include "McRails.h"
#include "McChunk.h"
#include "McCombat.h"
#include "McEntityHost.h"
#include "McEntityManager.h"
#include "McFurnace.h"
#include "McItemRegistry.h"
#include "McTravel.h"
#include "McWorldActor.h"
#include "McXpOrb.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcCombatTest, "McClient.McPlayer.CombatAndXp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcCombatTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	// The bare hand, and a sword.
	const McCombat::FAttack Hand = McCombat::GetAttack(FMcItemStack());
	TestEqual(TEXT("a hand does 1"), Hand.Damage, 1.0f);
	TestEqual(TEXT("a hand swings 4 a second"), Hand.Speed, 4.0f);
	const FMcItemStack Sword(TEXT("minecraft:iron_sword"), 1);
	const McCombat::FAttack WithSword = McCombat::GetAttack(Sword);
	TestEqual(TEXT("an iron sword does 6"), WithSword.Damage, 6.0f);
	TestEqual(TEXT("an iron sword swings 1.6 a second"), WithSword.Speed, 1.6f, 0.001f);

	// The recharge, and what it does to the damage.
	TestEqual(TEXT("a swing at once is a tenth charged"), McCombat::GetStrength(0, 4.0f), 0.1f, 0.001f);
	TestEqual(TEXT("a recharged swing is whole"), McCombat::GetStrength(20, 4.0f), 1.0f);
	TestEqual(TEXT("a full charge does the full damage"), McCombat::GetDamage(7.0f, 1.0f, false), 7.0f, 0.001f);
	TestEqual(TEXT("a critical does half as much again"), McCombat::GetDamage(7.0f, 1.0f, true), 10.5f, 0.001f);
	TestEqual(TEXT("a tenth charge does 0.2 + 0.8 * 0.01"), McCombat::GetDamage(7.0f, 0.1f, false), 7.0f * 0.208f, 0.001f);

	// Tools wear.
	TestEqual(TEXT("a sword wears 1"), McCombat::GetWear(Sword), 1);
	TestEqual(TEXT("a pickaxe wears 2"), McCombat::GetWear(FMcItemStack(TEXT("minecraft:iron_pickaxe"), 1)), 2);
	TestEqual(TEXT("a block wears 0"), McCombat::GetWear(FMcItemStack(TEXT("minecraft:stone"), 1)), 0);

	// A hit pushes the victim away from the attacker, and up.
	const FVector Push = McCombat::GetKnockback(FVector(1.0, 0.0, 0.0), FVector::ZeroVector, FVector::ZeroVector);
	TestTrue(TEXT("away from the attacker"), Push.X > 0.3 && FMath::Abs(Push.Z) < 0.001);
	TestEqual(TEXT("and up by 0.4"), Push.Y, 0.4, 0.001);
	const FVector Moving = McCombat::GetKnockback(FVector(1.0, 0.0, 0.0), FVector::ZeroVector, FVector(0.2, 0.0, 0.0));
	TestTrue(TEXT("what it had is half kept"), Moving.X > Push.X);

	// Orbs: the reward splits into the sizes the game uses.
	TArray<int32> Sizes;
	FMcXpOrb::Split(1000, Sizes);
	int32 Sum = 0;
	for (const int32 Size : Sizes)
	{
		Sum += Size;
	}
	TestEqual(TEXT("the pieces add up"), Sum, 1000);
	TestTrue(TEXT("largest first"), Sizes.Num() > 0 && Sizes[0] == 617);
	Sizes.Reset();
	FMcXpOrb::Split(2477, Sizes);
	TestTrue(TEXT("the largest size is one orb"), Sizes.Num() == 1 && Sizes[0] == 2477);
	Sizes.Reset();
	FMcXpOrb::Split(0, Sizes);
	TestEqual(TEXT("nothing is no orbs"), Sizes.Num(), 0);

	// The portal sends the Overworld to the Nether and the Nether to the Overworld.
	TestEqual(TEXT("overworld to nether"), McTravel::GetPortalDestination(TEXT("minecraft:overworld")), FString(TEXT("minecraft:the_nether")));
	TestEqual(TEXT("nether to overworld"), McTravel::GetPortalDestination(TEXT("minecraft:the_nether")), FString(TEXT("minecraft:overworld")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcBlockEntityDataTest, "McClient.McWorld.FurnaceBlockEntity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcBlockEntityDataTest::RunTest(const FString& Parameters)
{
	if (!TestTrue(TEXT("items load"), FMcItemRegistry::Get().EnsureLoaded()))
	{
		return false;
	}
	// A furnace in a chunk's block entities survives the chunk's encoding.
	FMcFurnace Furnace(EMcFurnaceKind::Furnace);
	Furnace.GetItems()->Set(FMcFurnace::InputSlot, FMcItemStack(TEXT("minecraft:raw_iron"), 5));
	Furnace.GetItems()->Set(FMcFurnace::FuelSlot, FMcItemStack(TEXT("minecraft:coal"), 3));
	FMcBlockEntity Entity;
	Entity.Kind = EMcBlockEntityKind::Furnace;
	{
		FMemoryWriter Writer(Entity.Data);
		Furnace.Serialize(Writer);
	}
	TestTrue(TEXT("the furnace has bytes"), Entity.Data.Num() > 0);

	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded()))
	{
		return false;
	}
	FMcBlockWorld World{FMcDimensionType()};
	FMcChunk& Chunk = World.AddChunk(World.MakeChunk(FIntPoint(0, 0)));
	Chunk.SetBlockEntity(3, 10, 4, Entity);
	const TArray<uint8> Bytes = Chunk.EncodeBlockEntities();
	FMcChunk& Other = World.AddChunk(World.MakeChunk(FIntPoint(1, 0)));
	if (!TestTrue(TEXT("decodes"), Other.DecodeBlockEntities(Bytes)))
	{
		return false;
	}
	const FMcBlockEntity* Back = Other.FindBlockEntity(3, 10, 4);
	if (!TestNotNull(TEXT("the block entity is there"), Back))
	{
		return false;
	}
	TestTrue(TEXT("a furnace"), Back->Kind == EMcBlockEntityKind::Furnace);
	TestTrue(TEXT("with the same bytes"), Back->Data == Entity.Data);
	FMcFurnace Read(EMcFurnaceKind::Furnace);
	FMemoryReaderView Reader(Back->Data);
	if (TestTrue(TEXT("they read"), Read.Serialize(Reader)))
	{
		TestEqual(TEXT("the input"), Read.GetItems()->Get(FMcFurnace::InputSlot).Count, 5);
		TestEqual(TEXT("the fuel"), Read.GetItems()->Get(FMcFurnace::FuelSlot).Count, 3);
	}
	return true;
}

// Chests and furnaces in a world actor: what breaking them drops, and what a save keeps.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcContainerBlocksTest, "McClient.McWorld.ChestDropsAndFurnaceSave",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcContainerBlocksTest::RunTest(const FString& Parameters)
{
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!TestTrue(TEXT("registries load"), Registry.EnsureLoaded() && FMcBlockShapes::Get().EnsureLoaded() && FMcItemRegistry::Get().EnsureLoaded()) || !GEngine)
	{
		return false;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	AMcWorldActor* Actor = nullptr;
	const auto Finish = [&]()
	{
		// Ended while the world is up, as in a game, so its entity actors go before the world does.
		if (Actor)
		{
			Actor->Destroy();
		}
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};

	Actor = World->SpawnActor<AMcWorldActor>();
	// With no game mode the world never counts as begun, so the actor has to be started by hand.
	if (Actor && !Actor->HasActorBegunPlay())
	{
		Actor->DispatchBeginPlay();
	}
	UMcBlockUseSubsystem* Use = World->GetSubsystem<UMcBlockUseSubsystem>();
	if (!TestNotNull(TEXT("a world actor"), Actor) || !TestNotNull(TEXT("the block use subsystem"), Use) || !TestNotNull(TEXT("a block world"), Actor ? Actor->GetBlockWorld() : nullptr)
		|| !TestNotNull(TEXT("an entity host"), Actor ? Actor->GetEntityHost() : nullptr))
	{
		Finish();
		return false;
	}
	FMcBlockWorld& Blocks = *Actor->GetBlockWorld();
	if (!Blocks.HasChunk(FIntPoint(0, 0)))
	{
		Blocks.AddChunk(Blocks.MakeChunk(FIntPoint(0, 0)));
	}
	FMcChunk* Chunk = Blocks.FindChunk(FIntPoint(0, 0));
	const FIntVector ChestPos(2, 64, 2);
	const FIntVector FurnacePos(6, 64, 6);
	Blocks.SetBlock(ChestPos, Registry.ParseState(TEXT("minecraft:chest")).Get(0));
	Blocks.SetBlock(FurnacePos, Registry.ParseState(TEXT("minecraft:furnace")).Get(0));

	// A chest drops what it holds when it is broken, once.
	FMcBlockEntity Chest = FMcBlockEntity::MakeContainer(27);
	Chest.Slots[0] = FMcItemStack(TEXT("minecraft:diamond"), 3);
	Chest.Slots[5] = FMcItemStack(TEXT("minecraft:stick"), 10);
	Chunk->SetBlockEntity(ChestPos.X, ChestPos.Y, ChestPos.Z, Chest);
	TArray<FMcItemStack> Contents;
	UMcBlockUseSubsystem::GetContents(*Actor, ChestPos, Contents);
	int32 Held = 0;
	for (const FMcItemStack& Stack : Contents)
	{
		Held += Stack.IsEmpty() ? 0 : 1;
	}
	TestEqual(TEXT("the chest holds two stacks"), Held, 2);
	const int32 Before = Actor->GetEntityHost()->GetManager().GetEntities().Num();
	Use->ReleaseBlock(*Actor, ChestPos);
	TestEqual(TEXT("both stacks are dropped"), Actor->GetEntityHost()->GetManager().GetEntities().Num() - Before, 2);
	TestNull(TEXT("the block entity is gone"), Chunk->FindBlockEntity(ChestPos.X, ChestPos.Y, ChestPos.Z));
	Use->ReleaseBlock(*Actor, ChestPos);
	TestEqual(TEXT("and nothing drops twice"), Actor->GetEntityHost()->GetManager().GetEntities().Num() - Before, 2);

	// A furnace keeps what is in it through a save and a restart.
	const TSharedPtr<FMcFurnace> Furnace = Use->GetFurnace(*Actor, FurnacePos, true);
	if (TestTrue(TEXT("a furnace"), Furnace.IsValid()))
	{
		Furnace->GetItems()->Set(FMcFurnace::InputSlot, FMcItemStack(TEXT("minecraft:raw_iron"), 7));
		Furnace->GetItems()->Set(FMcFurnace::FuelSlot, FMcItemStack(TEXT("minecraft:coal"), 2));
		Use->SaveFurnaces(*Actor);
		const FMcBlockEntity* Saved = Chunk->FindBlockEntity(FurnacePos.X, FurnacePos.Y, FurnacePos.Z);
		if (TestNotNull(TEXT("saved as a block entity"), Saved))
		{
			TestTrue(TEXT("of the furnace kind"), Saved->Kind == EMcBlockEntityKind::Furnace);
		}
		Use->ForgetAll();
		const TSharedPtr<FMcFurnace> Loaded = Use->GetFurnace(*Actor, FurnacePos, false);
		if (TestTrue(TEXT("it loads again"), Loaded.IsValid()))
		{
			TestEqual(TEXT("the input is kept"), Loaded->GetItems()->Get(FMcFurnace::InputSlot).Count, 7);
			TestEqual(TEXT("the fuel is kept"), Loaded->GetItems()->Get(FMcFurnace::FuelSlot).Count, 2);
		}
		// Breaking it drops them.
		const int32 BeforeFurnace = Actor->GetEntityHost()->GetManager().GetEntities().Num();
		Use->ReleaseBlock(*Actor, FurnacePos);
		TestEqual(TEXT("a broken furnace drops its two stacks"), Actor->GetEntityHost()->GetManager().GetEntities().Num() - BeforeFurnace, 2);
	}

	// A hopper under a chest and over another moves the items down, one every 8 ticks; one under open sky takes an
	// item lying on it.
	Use->Tick(0.0f);
	const FIntVector Top(10, 65, 10);
	const FIntVector HopperPos(10, 64, 10);
	const FIntVector Bottom(10, 63, 10);
	Blocks.SetBlock(Top, Registry.ParseState(TEXT("minecraft:chest")).Get(0));
	Blocks.SetBlock(Bottom, Registry.ParseState(TEXT("minecraft:chest")).Get(0));
	FMcBlockEntity Full = FMcBlockEntity::MakeContainer(27);
	Full.Slots[4] = FMcItemStack(TEXT("minecraft:cobblestone"), 3);
	Chunk->SetBlockEntity(Top.X, Top.Y, Top.Z, Full);
	Blocks.SetBlock(HopperPos, Registry.ParseState(TEXT("minecraft:hopper[facing=down]")).Get(0));
	TestEqual(TEXT("the hopper is at work"), Use->NumHoppers(), 1);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Use->Tick(0.05f + 1e-4f);
	}
	TArray<FMcItemStack> Moved;
	UMcBlockUseSubsystem::GetContents(*Actor, Bottom, Moved);
	int32 Cobble = 0;
	for (const FMcItemStack& Stack : Moved)
	{
		Cobble += Stack.Item == TEXT("minecraft:cobblestone") ? Stack.Count : 0;
	}
	TestEqual(TEXT("all three reach the lower chest"), Cobble, 3);
	TArray<FMcItemStack> Left;
	UMcBlockUseSubsystem::GetContents(*Actor, Top, Left);
	TestTrue(TEXT("the upper one is empty"), !Left.ContainsByPredicate([](const FMcItemStack& Stack) { return !Stack.IsEmpty(); }));

	const FIntVector Open(12, 64, 12);
	Blocks.SetBlock(Open, Registry.ParseState(TEXT("minecraft:hopper[facing=down]")).Get(0));
	FMcItemEntity& Lying = Actor->GetEntityHost()->GetManager().DropItem(FVector(Open) + FVector(0.5, 1.1, 0.5), FMcItemStack(TEXT("minecraft:apple"), 4));
	Lying.Body.Position = FVector(Open) + FVector(0.5, 1.0, 0.5);
	for (int32 Step = 0; Step < 3; ++Step)
	{
		Use->Tick(0.05f + 1e-4f);
	}
	TArray<FMcItemStack> Caught;
	UMcBlockUseSubsystem::GetContents(*Actor, Open, Caught);
	TestTrue(TEXT("the apples are in the hopper"), Caught.ContainsByPredicate([](const FMcItemStack& Stack) { return Stack.Item == TEXT("minecraft:apple") && Stack.Count == 4; }));

	// A dropper puts an item into the chest it faces; a dispenser throws one out, or uses bone meal.
	const FIntVector DropperPos(14, 64, 14);
	const FIntVector Into(15, 64, 14);
	Blocks.SetBlock(Into, Registry.ParseState(TEXT("minecraft:chest")).Get(0));
	Blocks.SetBlock(DropperPos, Registry.ParseState(TEXT("minecraft:dropper[facing=east]")).Get(0));
	FMcBlockEntity Loaded = FMcBlockEntity::MakeContainer(9);
	Loaded.Slots[2] = FMcItemStack(TEXT("minecraft:flint"), 2);
	Chunk->SetBlockEntity(DropperPos.X, DropperPos.Y, DropperPos.Z, Loaded);
	Use->Dispense(*Actor, DropperPos);
	TArray<FMcItemStack> Received;
	UMcBlockUseSubsystem::GetContents(*Actor, Into, Received);
	TestTrue(TEXT("a flint went into the chest"), Received.ContainsByPredicate([](const FMcItemStack& Stack) { return Stack.Item == TEXT("minecraft:flint") && Stack.Count == 1; }));

	const FIntVector DispenserPos(14, 64, 4);
	Blocks.SetBlock(DispenserPos, Registry.ParseState(TEXT("minecraft:dispenser[facing=east]")).Get(0));
	FMcBlockEntity Armed = FMcBlockEntity::MakeContainer(9);
	Armed.Slots[0] = FMcItemStack(TEXT("minecraft:flint"), 1);
	Chunk->SetBlockEntity(DispenserPos.X, DispenserPos.Y, DispenserPos.Z, Armed);
	const int32 BeforeThrow = Actor->GetEntityHost()->GetManager().GetEntities().Num();
	Use->Dispense(*Actor, DispenserPos);
	TestEqual(TEXT("the flint is thrown out"), Actor->GetEntityHost()->GetManager().GetEntities().Num() - BeforeThrow, 1);
	TArray<FMcItemStack> Emptied;
	UMcBlockUseSubsystem::GetContents(*Actor, DispenserPos, Emptied);
	TestTrue(TEXT("and the dispenser is empty"), !Emptied.ContainsByPredicate([](const FMcItemStack& Stack) { return !Stack.IsEmpty(); }));
	Use->Dispense(*Actor, DispenserPos);
	TestEqual(TEXT("an empty one throws nothing"), Actor->GetEntityHost()->GetManager().GetEntities().Num() - BeforeThrow, 1);

	// Buckets: a water bucket in a dispenser pours a source in front and leaves an empty bucket; dispensed again, the
	// empty bucket takes the source back.
	const FIntVector Pourer(14, 64, 8);
	const FIntVector Front(15, 64, 8);
	Blocks.SetBlock(Pourer, Registry.ParseState(TEXT("minecraft:dispenser[facing=east]")).Get(0));
	FMcBlockEntity Bucketed = FMcBlockEntity::MakeContainer(9);
	Bucketed.Slots[0] = FMcItemStack(TEXT("minecraft:water_bucket"));
	Chunk->SetBlockEntity(Pourer.X, Pourer.Y, Pourer.Z, Bucketed);
	Use->Dispense(*Actor, Pourer);
	TestEqual(TEXT("water is poured"), Registry.GetBlockOf(Blocks.GetBlock(Front)).Name.ToString(), FString(TEXT("minecraft:water")));
	TArray<FMcItemStack> AfterPour;
	UMcBlockUseSubsystem::GetContents(*Actor, Pourer, AfterPour);
	TestEqual(TEXT("an empty bucket is left"), AfterPour[0].Item, FName(TEXT("minecraft:bucket")));
	Use->Dispense(*Actor, Pourer);
	TestEqual(TEXT("the source is taken back"), Registry.GetBlockOf(Blocks.GetBlock(Front)).Name.ToString(), FString(TEXT("minecraft:air")));
	TArray<FMcItemStack> AfterFill;
	UMcBlockUseSubsystem::GetContents(*Actor, Pourer, AfterFill);
	TestEqual(TEXT("and the bucket is full again"), AfterFill[0].Item, FName(TEXT("minecraft:water_bucket")));
	TestFalse(TEXT("a full bucket won't go into a furnace"), McBuckets::Pour(*Actor, TEXT("minecraft:lava_bucket"), FurnacePos));

	// Fire and shulker boxes: each in a dispenser of its own, so the random slot doesn't matter.
	const auto Fire = [&](int32 Z, const TCHAR* ItemName, const TCHAR* FrontBlock, FMcItemStack& Remain)
	{
		const FIntVector Gun(14, 64, Z);
		const FIntVector Muzzle(15, 64, Z);
		Blocks.SetBlock(Gun, Registry.ParseState(TEXT("minecraft:dispenser[facing=east]")).Get(0));
		Blocks.SetBlock(Muzzle, Registry.ParseState(FrontBlock).Get(0));
		FMcBlockEntity Loaded2 = FMcBlockEntity::MakeContainer(9);
		Loaded2.Slots[0] = FMcItemStack(ItemName);
		Chunk->SetBlockEntity(Gun.X, Gun.Y, Gun.Z, Loaded2);
		Use->Dispense(*Actor, Gun);
		TArray<FMcItemStack> After;
		UMcBlockUseSubsystem::GetContents(*Actor, Gun, After);
		Remain = After[0];
		return Registry.GetBlockOf(Blocks.GetBlock(Muzzle)).Name.ToString();
	};
	FMcItemStack Remain;
	TestEqual(TEXT("a fire charge lights the air in front"), Fire(0, TEXT("minecraft:fire_charge"), TEXT("minecraft:air"), Remain), FString(TEXT("minecraft:fire")));
	TestTrue(TEXT("and is used up"), Remain.IsEmpty());
	TestEqual(TEXT("a flint and steel lights it too"), Fire(2, TEXT("minecraft:flint_and_steel"), TEXT("minecraft:air"), Remain), FString(TEXT("minecraft:fire")));
	TestEqual(TEXT("and is worn by one"), Remain.GetDamage(), 1);
	TestEqual(TEXT("no fire on stone"), Fire(6, TEXT("minecraft:fire_charge"), TEXT("minecraft:stone"), Remain), FString(TEXT("minecraft:stone")));
	TestEqual(TEXT("and the charge is kept"), Remain.Count, 1);
	TestEqual(TEXT("a shulker box is placed"), Fire(10, TEXT("minecraft:red_shulker_box"), TEXT("minecraft:air"), Remain), FString(TEXT("minecraft:red_shulker_box")));
	TestTrue(TEXT("and leaves the dispenser"), Remain.IsEmpty());
	// Double chests: facing north, the left half is east of the right one. Placing a chest beside a single one
	// joins them, and taking one away leaves the other single again.
	TestEqual(TEXT("a right chest's partner is clockwise"), McChestLink::GetPartnerOffset(TEXT("north"), TEXT("right")), FIntVector(1, 0, 0));
	TestEqual(TEXT("a left chest's partner is counter-clockwise"), McChestLink::GetPartnerOffset(TEXT("east"), TEXT("left")), FIntVector(0, 0, -1));
	TestEqual(TEXT("a single chest has none"), McChestLink::GetPartnerOffset(TEXT("north"), TEXT("single")), FIntVector::ZeroValue);
	const FIntVector Lone(3, 64, 13);
	const FIntVector Joiner(4, 64, 13);
	Actor->SetBlock(Lone, Registry.ParseState(TEXT("minecraft:chest[facing=north,type=single]")).Get(0));
	const auto Join = McChestLink::ChooseJoin(Blocks, Joiner, TEXT("minecraft:chest"), TEXT("north"), Lone);
	TestTrue(TEXT("a chest east of a single one joins as its left half"), Join.IsSet() && Join->Key == TEXT("left") && Join->Value == Lone);
	TestFalse(TEXT("one facing another way can't"), McChestLink::ChooseJoin(Blocks, Joiner, TEXT("minecraft:chest"), TEXT("south"), Lone).IsSet());
	Actor->SetBlock(Joiner, Registry.ParseState(TEXT("minecraft:chest[facing=north,type=left]")).Get(0));
	TestEqual(TEXT("the neighbour becomes the right half"), Registry.GetPropertyValue(Blocks.GetBlock(Lone), TEXT("type")), FName(TEXT("right")));
	TestTrue(TEXT("and each finds the other"), McChestLink::FindPartner(Blocks, Joiner).Get(FIntVector::ZeroValue) == Lone && McChestLink::FindPartner(Blocks, Lone).Get(FIntVector::ZeroValue) == Joiner);
	Actor->SetBlock(Joiner, Registry.FindDefaultState(TEXT("minecraft:air")).Get(0));
	TestEqual(TEXT("taking one away leaves the other single"), Registry.GetPropertyValue(Blocks.GetBlock(Lone), TEXT("type")), FName(TEXT("single")));
	TestEqual(TEXT("and it keeps its facing"), Registry.GetPropertyValue(Blocks.GetBlock(Lone), TEXT("facing")), FName(TEXT("north")));
	Finish();
	return true;
}

#endif
	// Rails join up as they are placed: a line, a curve, a slope; a powered rail stays straight; taking one away straightens the rest.
	{
		const auto Shape = [&](const FIntVector& At) { return Registry.GetPropertyValue(Blocks.GetBlock(At), TEXT("shape")); };
		const auto Place = [&](const FIntVector& At, const TCHAR* Block) { Actor->SetBlock(At, Registry.ParseState(Block).Get(0)); };
		const FIntVector A(3, 64, 2), B(4, 64, 2), C(4, 64, 3);
		Place(A, TEXT("minecraft:rail"));
		TestEqual(TEXT("a lone rail runs north-south"), Shape(A), FName(TEXT("north_south")));
		Place(B, TEXT("minecraft:rail"));
		TestEqual(TEXT("a rail east of it joins and turns it"), Shape(A), FName(TEXT("east_west")));
		TestEqual(TEXT("and runs the same way"), Shape(B), FName(TEXT("east_west")));
		Place(C, TEXT("minecraft:rail"));
		TestEqual(TEXT("one to the south makes a curve"), Shape(B), FName(TEXT("south_west")));
		TestEqual(TEXT("and runs north-south itself"), Shape(C), FName(TEXT("north_south")));
		Actor->SetBlock(C, Registry.FindDefaultState(TEXT("minecraft:air")).Get(0));
		TestEqual(TEXT("taking it away straightens the curve"), Shape(B), FName(TEXT("east_west")));
		const FIntVector Low(8, 64, 5), High(9, 65, 5);
		Place(Low, TEXT("minecraft:rail"));
		Place(High, TEXT("minecraft:rail"));
		TestEqual(TEXT("a rail a block up makes a slope"), Shape(Low), FName(TEXT("ascending_east")));
		TestEqual(TEXT("and the upper one is level"), Shape(High), FName(TEXT("east_west")));
		McRails::FTrack Track;
		FIntVector Next;
		TestTrue(TEXT("the slope's track"), McRails::GetTrack(Blocks.GetBlock(Low), Track) && Track.bRiseA && Track.EndA == FIntVector(1, 0, 0) && Track.EndB == FIntVector(-1, 0, 0));
		TestTrue(TEXT("a cart goes up it to the next rail"), McRails::FindNext(Blocks, Low, Track.EndA, Track.bRiseA, Next) && Next == High);
		TestTrue(TEXT("and down again"), McRails::FindNext(Blocks, High, FIntVector(-1, 0, 0), false, Next) && Next == Low);
		const FIntVector P(8, 64, 9), Q(9, 64, 9), R(9, 64, 10);
		Place(P, TEXT("minecraft:powered_rail"));
		Place(Q, TEXT("minecraft:powered_rail"));
		Place(R, TEXT("minecraft:powered_rail"));
		TestEqual(TEXT("a powered rail doesn't curve"), Shape(Q), FName(TEXT("north_south")));
	}
