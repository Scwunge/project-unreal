#include "McBlockUse.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McChestLink.h"
#include "McEnchanting.h"
#include "McEntityHost.h"
#include "McFurnace.h"
#include "McBrewingStand.h"
#include "McAnvilMenu.h"
#include "McArrow.h"
#include "McBuckets.h"
#include "McShulker.h"
#include "McSimulation.h"
#include "McPlayerData.h"
#include "McPlayerPawn.h"
#include "McWorldActor.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

namespace
{
	constexpr int32 ChestSlots = 27;

	FName GetBlockName(const FMcBlockWorld& Blocks, const FIntVector& Pos)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcStateId State = Blocks.GetBlock(Pos);
		return Registry.IsValidState(State) ? Registry.GetBlockOf(State).Name : NAME_None;
	}

	// Whether a block burns, for fire to stay beside it.
	bool IsFlammableName(FName Name)
	{
	const FString Path = Name.ToString();
	return Path.EndsWith(TEXT("_planks")) || Path.EndsWith(TEXT("_log")) || Path.EndsWith(TEXT("_wood")) || Path.EndsWith(TEXT("_leaves"))
		|| Path.EndsWith(TEXT("_wool")) || Path.EndsWith(TEXT("_carpet"))
		|| Path == TEXT("minecraft:hay_block") || Path == TEXT("minecraft:bookshelf") || Path == TEXT("minecraft:tnt") || Path == TEXT("minecraft:vine")
		|| Path == TEXT("minecraft:coal_block") || Path == TEXT("minecraft:dried_kelp_block") || Path == TEXT("minecraft:short_grass") || Path == TEXT("minecraft:tall_grass")
		|| Path == TEXT("minecraft:scaffolding");
	}

	// Fire stays on something solid under it, or against a block that burns.
	bool CanFireStay(const FMcBlockWorld& Blocks, const FIntVector& Pos)
	{
	const FMcStateId Below = Blocks.GetBlock(Pos - FIntVector(0, 1, 0));
	for (const FBox3f& Box : FMcBlockShapes::Get().GetCollision(Below))
	{
		if (Box.Max.Y >= 0.99f && Box.Min.X <= 0.01f && Box.Max.X >= 0.99f && Box.Min.Z <= 0.01f && Box.Max.Z >= 0.99f)
		{
			return true;
		}
	}
	for (const FIntVector& Step : { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1), FIntVector(0, 1, 0), FIntVector(0, -1, 0) })
	{
		if (IsFlammableName(GetBlockName(Blocks, Pos + Step)))
		{
			return true;
		}
	}
	return false;
	}

	bool IsAnvilName(FName Name)
	{
		return Name == TEXT("minecraft:anvil") || Name == TEXT("minecraft:chipped_anvil") || Name == TEXT("minecraft:damaged_anvil");
	}

	bool IsChestName(FName Name)
	{
		return Name == TEXT("minecraft:chest") || Name == TEXT("minecraft:trapped_chest") || Name == TEXT("minecraft:barrel") || McShulker::IsBox(Name);
	}

	// How many slots the block keeps as a plain container (chests, barrels, hoppers); 0 for any other.
	int32 GetContainerSlots(FName Name)
	{
		if (Name == TEXT("minecraft:dispenser") || Name == TEXT("minecraft:dropper"))
		{
			return 9;
		}
		return IsChestName(Name) ? ChestSlots : Name == TEXT("minecraft:hopper") ? McHopper::NumSlots : 0;
	}

	// The step to the next block in a direction named by a facing property.
	FIntVector FacingStep(FName Facing)
	{
		if (Facing == TEXT("north")) return FIntVector(0, 0, -1);
		if (Facing == TEXT("south")) return FIntVector(0, 0, 1);
		if (Facing == TEXT("west")) return FIntVector(-1, 0, 0);
		if (Facing == TEXT("east")) return FIntVector(1, 0, 0);
		if (Facing == TEXT("up")) return FIntVector(0, 1, 0);
		return FIntVector(0, -1, 0);
	}

	// A shulker box opens at its face: a block in the way shuts it.
	bool IsLidBlocked(const FMcBlockWorld& Blocks, const FIntVector& Pos)
	{
		const FMcStateId State = Blocks.GetBlock(Pos);
		const FIntVector Step = FacingStep(FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("facing")));
		return FMcBlockShapes::Get().HasCollision(Blocks.GetBlock(Pos + Step));
	}

	bool GetFurnaceKind(FName Name, EMcFurnaceKind& OutKind)
	{
		if (Name == TEXT("minecraft:furnace"))
		{
			OutKind = EMcFurnaceKind::Furnace;
		}
		else if (Name == TEXT("minecraft:blast_furnace"))
		{
			OutKind = EMcFurnaceKind::BlastFurnace;
		}
		else if (Name == TEXT("minecraft:smoker"))
		{
			OutKind = EMcFurnaceKind::Smoker;
		}
		else
		{
			return false;
		}
		return true;
	}

	// The kind of machine the block is, if it is one; a furnace's kind too.
	bool GetMachineKind(FName Name, EMcBlockEntityKind& OutKind, EMcFurnaceKind& OutFurnace)
	{
		if (Name == TEXT("minecraft:brewing_stand"))
		{
			OutKind = EMcBlockEntityKind::BrewingStand;
			return true;
		}
		OutKind = EMcBlockEntityKind::Furnace;
		return GetFurnaceKind(Name, OutFurnace);
	}

	TSharedPtr<FMcMachine> MakeMachine(EMcBlockEntityKind Kind, EMcFurnaceKind FurnaceKind)
	{
		if (Kind == EMcBlockEntityKind::BrewingStand)
		{
			return MakeShared<FMcBrewingStand>();
		}
		return MakeShared<FMcFurnace>(FurnaceKind);
	}

	FMcChunk* FindChunkOf(AMcWorldActor& WorldActor, const FIntVector& Pos)
	{
		FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
		return Blocks ? Blocks->FindChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)) : nullptr;
	}

	void DropAll(AMcWorldActor& WorldActor, const FIntVector& Pos, const TArray<FMcItemStack>& Stacks)
	{
		FMcEntityHost* Host = WorldActor.GetEntityHost();
		if (!Host)
		{
			return;
		}
		for (const FMcItemStack& Stack : Stacks)
		{
			if (!Stack.IsEmpty())
			{
				Host->GetManager().DropItem(FVector(Pos) + FVector(0.5), Stack);
			}
		}
	}
}

UMcBlockUseSubsystem::FKey UMcBlockUseSubsystem::MakeKey(const AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	return FKey{ FName(*WorldActor.Dimension), Pos };
}

void UMcBlockUseSubsystem::ForgetAll()
{
	Chests.Reset();
	Machines.Reset();
	Hoppers.Reset();
}

void UMcBlockUseSubsystem::WatchWorld(AMcWorldActor& WorldActor)
{
	FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	if (Blocks && !Watched.Contains(&WorldActor))
	{
		Watched.Add(&WorldActor);
		// A hopper set down starts work.
		Blocks->OnBlockChanged.AddWeakLambda(this, [this, Weak = TWeakObjectPtr<AMcWorldActor>(&WorldActor)](const FIntVector& Pos, FMcStateId State)
		{
			AMcWorldActor* Actor = Weak.Get();
			const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
			if (Actor && Actor->HasAuthority() && Registry.IsValidState(State) && Registry.GetBlockOf(State).Name == TEXT("minecraft:hopper"))
			{
				Hoppers.FindOrAdd(MakeKey(*Actor, Pos)).World = Actor;
			}
		});
		if (FMcSimulation* Sim = WorldActor.GetSimulation())
		{
			Sim->OnDispense = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&WorldActor)](const FIntVector& Pos)
			{
				if (AMcWorldActor* Actor = Weak.Get())
				{
					Dispense(*Actor, Pos);
				}
			};
		}
		// Furnaces go into their chunks whenever the world saves.
		WorldActor.OnSaving.AddWeakLambda(this, [this, Weak = TWeakObjectPtr<AMcWorldActor>(&WorldActor)]()
		{
			if (AMcWorldActor* Actor = Weak.Get())
			{
				SaveFurnaces(*Actor);
			}
		});
	}
}

void UMcBlockUseSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Twenty times a second, however fast frames come (but never a long catch-up after a stall).
	Accumulator = FMath::Min(Accumulator + DeltaSeconds, 0.25f);
	for (TActorIterator<AMcWorldActor> It(GetWorld()); It; ++It)
	{
		if (It->HasAuthority())
		{
			WatchWorld(**It);
		}
	}
	while (Accumulator >= 0.05f)
	{
		Accumulator -= 0.05f;
		for (TPair<FKey, FMachineEntry>& Pair : Machines)
		{
			Pair.Value.Machine->Tick();
		}
		TickHoppers();
		if (++CleanTimer >= 100)
		{
			CleanTimer = 0;
			Sweep();
		}
	}
}

void UMcBlockUseSubsystem::Sweep()
{
	// Furnaces, brewing stands and chests whose block has gone (broken by an explosion, or in creative) drop what
	// they held; machines that were saved in a chunk but have not been opened yet since it loaded start work; the
	// rest are written back.
	for (auto It = Machines.CreateIterator(); It; ++It)
	{
		AMcWorldActor* World = It.Value().World.Get();
		const FMcBlockWorld* Blocks = World ? World->GetBlockWorld() : nullptr;
		if (!Blocks)
		{
			It.RemoveCurrent();
			continue;
		}
		const FIntVector Pos = It.Key().Pos;
		if (!Blocks->HasChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)))
		{
			It.RemoveCurrent();
			continue;
		}
		EMcBlockEntityKind Kind;
		EMcFurnaceKind FurnaceKind;
		if (!GetMachineKind(GetBlockName(*Blocks, Pos), Kind, FurnaceKind) || Kind != It.Value().Kind)
		{
			DropAll(*World, Pos, It.Value().Machine->GetItems()->GetAll());
			It.RemoveCurrent();
			continue;
		}
		WriteMachine(It.Key(), It.Value());
	}
	for (auto It = Chests.CreateIterator(); It; ++It)
	{
		AMcWorldActor* World = It.Value().World.Get();
		const FMcBlockWorld* Blocks = World ? World->GetBlockWorld() : nullptr;
		const FIntVector Pos = It.Key().Pos;
		if (!Blocks || !Blocks->HasChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)))
		{
			It.RemoveCurrent();
		}
		else if (GetContainerSlots(GetBlockName(*Blocks, Pos)) == 0)
		{
			DropAll(*World, Pos, It.Value().Container->GetAll());
			It.RemoveCurrent();
		}
	}
	for (TActorIterator<AMcWorldActor> It(GetWorld()); It; ++It)
	{
		AMcWorldActor* World = *It;
		FMcBlockWorld* Blocks = World->GetBlockWorld();
		if (!Blocks || !World->HasAuthority())
		{
			continue;
		}
		TArray<FIntVector> ToLoad;
		for (const TPair<FIntPoint, TUniquePtr<FMcChunk>>& ChunkPair : Blocks->GetChunks())
		{
			for (const TPair<FIntVector, FMcBlockEntity>& Entity : ChunkPair.Value->GetBlockEntities())
			{
				const FIntVector Pos(ChunkPair.Key.X * 16 + Entity.Key.X, Entity.Key.Y, ChunkPair.Key.Y * 16 + Entity.Key.Z);
				if (Entity.Value.Kind == EMcBlockEntityKind::Container && !Hoppers.Contains(MakeKey(*World, Pos))
					&& GetBlockName(*Blocks, Pos) == TEXT("minecraft:hopper"))
				{
					Hoppers.Add(MakeKey(*World, Pos), FHopperEntry{ World, 0 });
				}
				if (Entity.Value.Kind == EMcBlockEntityKind::Furnace || Entity.Value.Kind == EMcBlockEntityKind::BrewingStand)
				{
					if (!Machines.Contains(MakeKey(*World, Pos)))
					{
						ToLoad.Add(Pos);
					}
				}
			}
		}
		for (const FIntVector& Pos : ToLoad)
		{
			GetMachine(*World, Pos, false);
		}
	}
}

void UMcBlockUseSubsystem::TickHoppers()
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	for (auto It = Hoppers.CreateIterator(); It; ++It)
	{
		AMcWorldActor* World = It.Value().World.Get();
		const FMcBlockWorld* Blocks = World ? World->GetBlockWorld() : nullptr;
		const FIntVector Pos = It.Key().Pos;
		if (!Blocks || !Blocks->HasChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)))
		{
			It.RemoveCurrent();
			continue;
		}
		const FMcStateId State = Blocks->GetBlock(Pos);
		if (!Registry.IsValidState(State) || Registry.GetBlockOf(State).Name != TEXT("minecraft:hopper"))
		{
			// Breaking it drops its items (ReleaseBlock, or the chest sweep).
			It.RemoveCurrent();
			continue;
		}
		if (--It.Value().Cooldown > 0)
		{
			continue;
		}
		It.Value().Cooldown = 0;
		// A powered hopper (enabled=false) holds still.
		if (Registry.GetPropertyValue(State, TEXT("enabled")) != TEXT("false") && TickHopper(*World, Pos, State))
		{
			It.Value().Cooldown = McHopper::Cooldown;
		}
	}
}

bool UMcBlockUseSubsystem::TickHopper(AMcWorldActor& WorldActor, const FIntVector& Pos, FMcStateId State)
{
	using McHopper::EFace;
	const TSharedPtr<FMcContainer> Items = GetChest(WorldActor, Pos, McHopper::NumSlots);
	if (!Items.IsValid())
	{
		return false;
	}
	const FMcSlotAccess Self = McHopper::ForContainer(Items);
	bool bMoved = false;

	// Out through the spout: into the top of the block below, or the side of the one beside.
	const FName Facing = FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("facing"));
	const FIntVector Target = Pos + FacingStep(Facing);
	if (!Items->IsEmpty())
	{
		const FMcSlotAccess Into = GetAccess(WorldActor, Target, Facing == TEXT("down") ? EFace::Top : EFace::Side);
		if (McHopper::MoveOne(Self, Into))
		{
			ContentsChanged(WorldActor, Target);
			bMoved = true;
		}
	}

	// In from above: out of the bottom of a container, or an item lying on the hopper.
	const FIntVector Above = Pos + FIntVector(0, 1, 0);
	const FMcSlotAccess From = GetAccess(WorldActor, Above, EFace::Bottom);
	if (From.IsValid())
	{
		if (McHopper::MoveOne(From, Self))
		{
			ContentsChanged(WorldActor, Above);
			bMoved = true;
		}
	}
	else if (FMcEntityHost* Host = WorldActor.GetEntityHost())
	{
		// The bowl and the block over it.
		TArray<FMcEntity*> Near;
		Host->GetManager().FindInBox(FBox(FVector(Pos) + FVector(0.0, 0.6875, 0.0), FVector(Pos) + FVector(1.0, 2.0, 1.0)), Near);
		for (FMcEntity* Entity : Near)
		{
			if (Entity->bRemoved || Entity->GetType() != FMcItemEntity::StaticType())
			{
				continue;
			}
			FMcItemEntity& Item = static_cast<FMcItemEntity&>(*Entity);
			if (McHopper::Insert(Self, Item.Stack))
			{
				Item.bRemoved = Item.Stack.IsEmpty();
				bMoved = true;
				break;
			}
		}
	}
	if (bMoved)
	{
		ContentsChanged(WorldActor, Pos);
	}
	return bMoved;
}

FMcSlotAccess UMcBlockUseSubsystem::GetAccess(AMcWorldActor& WorldActor, const FIntVector& Pos, McHopper::EFace Face)
{
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	if (!Blocks || !Blocks->HasChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)))
	{
		return FMcSlotAccess();
	}
	const FName Name = GetBlockName(*Blocks, Pos);
		if (const int32 Slots = GetContainerSlots(Name))
	{
		const TSharedPtr<FMcContainer> Container = GetChest(WorldActor, Pos, Slots);
		if (!Container.IsValid())
		{
			return FMcSlotAccess();
		}
		// A double chest is one 54-slot inventory, the right half's slots first.
		if (McChestLink::IsLinkable(Name))
		{
			if (const TOptional<FIntVector> Partner = McChestLink::FindPartner(*Blocks, Pos))
			{
				const TSharedPtr<FMcContainer> Other = GetChest(WorldActor, Partner.GetValue(), Slots);
				if (Other.IsValid())
				{
					const bool bRight = FMcBlockRegistry::Get().GetPropertyValue(Blocks->GetBlock(Pos), TEXT("type")) == TEXT("right");
					return bRight ? McHopper::ForContainer(Container, Other) : McHopper::ForContainer(Other, Container);
				}
			}
		}
		return McHopper::ForContainer(Container);
	}
	if (Name == TEXT("minecraft:brewing_stand"))
	{
		const TSharedPtr<FMcBrewingStand> Stand = GetBrewingStand(WorldActor, Pos, true);
		return Stand.IsValid() ? McHopper::ForBrewingStand(Stand, Face) : FMcSlotAccess();
	}
	EMcFurnaceKind FurnaceKind;
	if (GetFurnaceKind(Name, FurnaceKind))
	{
		const TSharedPtr<FMcFurnace> Furnace = GetFurnace(WorldActor, Pos, true);
		return Furnace.IsValid() ? McHopper::ForFurnace(Furnace, Face) : FMcSlotAccess();
	}
	return FMcSlotAccess();
}

void UMcBlockUseSubsystem::Dispense(AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	using McHopper::EFace;
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	FMcEntityHost* Host = WorldActor.GetEntityHost();
	if (!Blocks || !Host)
	{
		return;
	}
	const FName Name = GetBlockName(*Blocks, Pos);
	const bool bDropper = Name == TEXT("minecraft:dropper");
	if (!bDropper && Name != TEXT("minecraft:dispenser"))
	{
		return;
	}
	const TSharedPtr<FMcContainer> Items = GetChest(WorldActor, Pos, 9);
	if (!Items.IsValid())
	{
		return;
	}
	FMcEntityManager& Manager = Host->GetManager();
	const FVector Center = FVector(Pos) + FVector(0.5);
	// A slot picked at random among those with something in.
	int32 Slot = INDEX_NONE;
	int32 Seen = 0;
	for (int32 Index = 0; Index < Items->Num(); ++Index)
	{
		if (!Items->Get(Index).IsEmpty() && FMath::RandRange(0, Seen++) == 0)
		{
			Slot = Index;
		}
	}
	if (Slot == INDEX_NONE)
	{
		Manager.PlaySound(FName("block.dispenser.fail"), Center, 1.0f, 1.2f);
		return;
	}
	const FName Facing = FMcBlockRegistry::Get().GetPropertyValue(Blocks->GetBlock(Pos), TEXT("facing"));
	const FIntVector Step = FacingStep(Facing);
	const FVector Dir(Step);
	FMcItemStack One = Items->Get(Slot);
	One.Count = 1;
	const auto TakeOne = [&]()
	{
		FMcItemStack Rest = Items->Get(Slot);
		--Rest.Count;
		Items->Set(Slot, Rest.Count > 0 ? Rest : FMcItemStack());
		ContentsChanged(WorldActor, Pos);
	};

	if (bDropper)
	{
		// Into a container in front, through the face it meets; a full one takes nothing and nothing falls out.
		const FMcSlotAccess Into = GetAccess(WorldActor, Pos + Step, Facing == TEXT("down") ? EFace::Top : Facing == TEXT("up") ? EFace::Bottom : EFace::Side);
		if (Into.IsValid())
		{
			if (McHopper::Insert(Into, One))
			{
				TakeOne();
				ContentsChanged(WorldActor, Pos + Step);
			}
			return;
		}
	}
	else if (One.Item == TEXT("minecraft:arrow"))
	{
		// Shot at 1.1 blocks a tick, a little upward, spread by an inaccuracy of 6.
		TUniquePtr<FMcArrowEntity> Arrow = MakeUnique<FMcArrowEntity>();
		Arrow->Body.Position = Center + Dir * 0.7 - FVector(0.0, Step.Y == 0 ? 0.15 : 0.0, 0.0);
		const FVector Aim = (Dir + FVector(0.0, 0.1, 0.0)).GetSafeNormal();
		const auto Gauss = []() { return FMath::Sqrt(-2.0 * FMath::Loge(FMath::Max(static_cast<double>(FMath::FRand()), 1e-6))) * FMath::Cos(2.0 * PI * FMath::FRand()); };
		Arrow->Body.Velocity = (Aim + FVector(Gauss(), Gauss(), Gauss()) * 0.0075 * 6.0) * 1.1;
		Arrow->Damage = 3.0f;
		Manager.Add(MoveTemp(Arrow));
		TakeOne();
		Manager.PlaySound(FName("block.dispenser.launch"), Center, 1.0f, 1.2f);
		return;
	}
	else if ((One.Item == TEXT("minecraft:water_bucket") || One.Item == TEXT("minecraft:lava_bucket")) && McBuckets::Pour(WorldActor, One.Item, Pos + Step))
	{
		// Poured in front; the empty bucket stays in the slot.
		Items->Set(Slot, FMcItemStack(TEXT("minecraft:bucket")));
		ContentsChanged(WorldActor, Pos);
		return;
	}
	else if (One.Item == TEXT("minecraft:bucket"))
	{
		const FName Filled = McBuckets::Fill(WorldActor, Pos + Step);
		if (!Filled.IsNone())
		{
			// The full bucket takes the empty one's place, or another slot, or falls out.
			if (Items->Get(Slot).Count <= 1)
			{
				Items->Set(Slot, FMcItemStack(Filled));
				ContentsChanged(WorldActor, Pos);
				return;
			}
			TakeOne();
			FMcItemStack Full(Filled);
			McHopper::Insert(McHopper::ForContainer(Items), Full);
			if (!Full.IsEmpty())
			{
				Manager.DropItem(Center + Dir * 0.7, Full);
			}
			ContentsChanged(WorldActor, Pos);
			return;
		}
	}
	else if (One.Item == TEXT("minecraft:fire_charge") || One.Item == TEXT("minecraft:flint_and_steel"))
	{
		// Lights an unlit campfire or candle in front, or makes fire where the front is air and the fire can stay; a flint and
		// steel is worn by one, a fire charge is used up.
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FIntVector Front = Pos + Step;
		const FMcStateId FrontState = Blocks->GetBlock(Front);
		const FString FrontName = GetBlockName(*Blocks, Front).ToString();
		const bool bLightable = (FrontName == TEXT("minecraft:campfire") || FrontName == TEXT("minecraft:soul_campfire") || FrontName.EndsWith(TEXT("candle")))
			&& Registry.GetPropertyValue(FrontState, TEXT("lit")) == TEXT("false")
			&& Registry.GetPropertyValue(FrontState, TEXT("waterlogged")) != TEXT("true");
		const bool bFire = Registry.IsAir(FrontState) && CanFireStay(*Blocks, Front);
		if (bLightable || bFire)
		{
			if (bLightable)
			{
				TMap<FName, FName> Lit;
				for (const TCHAR* Kept : { TEXT("facing"), TEXT("signal_fire"), TEXT("waterlogged"), TEXT("candles") })
				{
					const FName Value = Registry.GetPropertyValue(FrontState, Kept);
					if (!Value.IsNone())
					{
						Lit.Add(Kept, Value);
					}
				}
				Lit.Add(TEXT("lit"), TEXT("true"));
				WorldActor.SetBlock(Front, Registry.FindState(Registry.GetBlockOf(FrontState).Name, Lit).Get(FrontState));
			}
			else
			{
				WorldActor.SetBlock(Front, Registry.FindDefaultState(TEXT("minecraft:fire")).Get(0));
			}
			if (One.Item == TEXT("minecraft:fire_charge"))
			{
				TakeOne();
			}
			else
			{
				FMcItemStack Worn = Items->Get(Slot);
				Items->Set(Slot, Worn.HurtAndBreak(1) ? FMcItemStack() : Worn);
				ContentsChanged(WorldActor, Pos);
			}
			Manager.PlaySound(FName("block.dispenser.launch"), Center, 1.0f, 1.2f);
		}
		else
		{
			Manager.PlaySound(FName("block.dispenser.fail"), Center, 1.0f, 1.2f);
		}
		return;
	}
	else if (One.Item.ToString().EndsWith(TEXT("shulker_box")))
	{
		// Placed in front, facing the way the dispenser does, if there's room.
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		TOptional<FMcStateId> Box = Registry.ParseState(One.Item.ToString() + TEXT("[facing=") + Facing.ToString() + TEXT("]"));
		if (Box.IsSet() && Registry.IsAir(Blocks->GetBlock(Pos + Step)))
		{
			WorldActor.SetBlock(Pos + Step, Box.GetValue());
			FillBox(WorldActor, Pos + Step, One);
			TakeOne();
			Manager.PlaySound(FName("block.dispenser.dispense"), Center, 1.0f, 1.0f);
		}
		else
		{
			Manager.PlaySound(FName("block.dispenser.fail"), Center, 1.0f, 1.2f);
		}
		return;
	}
	else if (One.Item == TEXT("minecraft:bone_meal"))
	{
		FMcSimulation* Sim = WorldActor.GetSimulation();
		if (Sim && Sim->Bonemeal(Pos + Step))
		{
			TakeOne();
			Manager.PlaySound(FName("block.dispenser.dispense"), Center, 1.0f, 1.0f);
		}
		else
		{
			Manager.PlaySound(FName("block.dispenser.fail"), Center, 1.0f, 1.2f);
		}
		return;
	}

	// Thrown out of the front.
	FMcItemEntity& Thrown = Manager.DropItem(Center, One);
	Thrown.Body.Position = Center + Dir * 0.7 - FVector(0.0, Step.Y == 0 ? 0.15625 : 0.125, 0.0);
	const double Speed = FMath::FRand() * 0.1 + 0.2;
	const auto Jitter = []() { return FMath::FRandRange(-1.0f, 1.0f) * 0.0172275 * 6.0; };
	Thrown.Body.Velocity = FVector(Dir.X * Speed + Jitter(), 0.2 + Jitter(), Dir.Z * Speed + Jitter());
	if (Step.Y != 0)
	{
		Thrown.Body.Velocity.Y = Dir.Y * Speed + Jitter();
	}
	TakeOne();
	Manager.PlaySound(FName("block.dispenser.dispense"), Center, 1.0f, 1.0f);
}

void UMcBlockUseSubsystem::ContentsChanged(AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	// Furnaces and brewing stands are written back at each sweep and save.
	if (Chests.Contains(MakeKey(WorldActor, Pos)))
	{
		WriteChest(WorldActor, Pos);
		// The other half of a double chest may have changed too.
		const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
		if (Blocks && McChestLink::IsLinkable(GetBlockName(*Blocks, Pos)))
		{
			if (const TOptional<FIntVector> Partner = McChestLink::FindPartner(*Blocks, Pos))
			{
				WriteChest(WorldActor, Partner.GetValue());
			}
		}
	}
}

TSharedPtr<FMcContainer> UMcBlockUseSubsystem::GetChest(AMcWorldActor& WorldActor, const FIntVector& Pos, int32 NumSlots)
{
	const FKey Key = MakeKey(WorldActor, Pos);
	if (const FChestEntry* Found = Chests.Find(Key))
	{
		return Found->Container;
	}
	FMcChunk* Chunk = FindChunkOf(WorldActor, Pos);
	if (!Chunk)
	{
		return nullptr;
	}
	// What the chunk has saved for the block, or a new empty one.
	FMcBlockEntity* Entity = Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
	if (!Entity || Entity->Kind != EMcBlockEntityKind::Container)
	{
		Chunk->SetBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15, FMcBlockEntity::MakeContainer(NumSlots));
		Entity = Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
		if (!Entity)
		{
			return nullptr;
		}
	}
	TSharedPtr<FMcContainer> Container = MakeShared<FMcContainer>(NumSlots);
	for (int32 Slot = 0; Slot < NumSlots && Slot < Entity->Slots.Num(); ++Slot)
	{
		Container->Set(Slot, Entity->Slots[Slot]);
	}
	Chests.Add(Key, FChestEntry{ &WorldActor, Container });
	return Container;
}

void UMcBlockUseSubsystem::WriteChest(AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	const FChestEntry* Found = Chests.Find(MakeKey(WorldActor, Pos));
	FMcChunk* Chunk = FindChunkOf(WorldActor, Pos);
	FMcBlockEntity* Entity = Chunk ? Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15) : nullptr;
	if (!Found || !Entity)
	{
		return;
	}
	Entity->Slots = Found->Container->GetAll();
	WorldActor.MarkChunkDirty(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
}

UMcBlockUseSubsystem::FMachineEntry* UMcBlockUseSubsystem::GetMachine(AMcWorldActor& WorldActor, const FIntVector& Pos, bool bCreate)
{
	const FKey Key = MakeKey(WorldActor, Pos);
	if (FMachineEntry* Found = Machines.Find(Key))
	{
		return Found;
	}
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	EMcBlockEntityKind Kind;
	EMcFurnaceKind FurnaceKind = EMcFurnaceKind::Furnace;
	if (!Blocks || !GetMachineKind(GetBlockName(*Blocks, Pos), Kind, FurnaceKind))
	{
		return nullptr;
	}
	FMachineEntry Entry;
	Entry.World = &WorldActor;
	Entry.Kind = Kind;
	Entry.Machine = MakeMachine(Kind, FurnaceKind);
	bool bSaved = false;
	if (const FMcChunk* Chunk = FindChunkOf(WorldActor, Pos))
	{
		const FMcBlockEntity* Entity = Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
		if (Entity && Entity->Kind == Kind && !Entity->Data.IsEmpty())
		{
			FMemoryReaderView Reader(Entity->Data);
			bSaved = Entry.Machine->Serialize(Reader);
			if (!bSaved)
			{
				Entry.Machine = MakeMachine(Kind, FurnaceKind);
			}
			Entry.Saved = Entity->Data;
		}
	}
	if (!bSaved && !bCreate)
	{
		return nullptr;
	}
	WatchWorld(WorldActor);
	return &Machines.Add(Key, MoveTemp(Entry));
}

TSharedPtr<FMcFurnace> UMcBlockUseSubsystem::GetFurnace(AMcWorldActor& WorldActor, const FIntVector& Pos, bool bCreate)
{
	const FMachineEntry* Entry = GetMachine(WorldActor, Pos, bCreate);
	return Entry && Entry->Kind == EMcBlockEntityKind::Furnace ? StaticCastSharedPtr<FMcFurnace>(Entry->Machine) : nullptr;
}

TSharedPtr<FMcBrewingStand> UMcBlockUseSubsystem::GetBrewingStand(AMcWorldActor& WorldActor, const FIntVector& Pos, bool bCreate)
{
	const FMachineEntry* Entry = GetMachine(WorldActor, Pos, bCreate);
	return Entry && Entry->Kind == EMcBlockEntityKind::BrewingStand ? StaticCastSharedPtr<FMcBrewingStand>(Entry->Machine) : nullptr;
}

void UMcBlockUseSubsystem::WriteMachine(const FKey& Key, FMachineEntry& Entry)
{
	AMcWorldActor* World = Entry.World.Get();
	FMcChunk* Chunk = World ? FindChunkOf(*World, Key.Pos) : nullptr;
	if (!Chunk)
	{
		return;
	}
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Entry.Machine->Serialize(Writer);
	if (Bytes == Entry.Saved && Chunk->FindBlockEntity(Key.Pos.X & 15, Key.Pos.Y, Key.Pos.Z & 15))
	{
		return;
	}
	FMcBlockEntity Entity;
	Entity.Kind = Entry.Kind;
	Entity.Data = Bytes;
	Chunk->SetBlockEntity(Key.Pos.X & 15, Key.Pos.Y, Key.Pos.Z & 15, MoveTemp(Entity));
	World->MarkChunkDirty(FIntPoint(Key.Pos.X >> 4, Key.Pos.Z >> 4));
	Entry.Saved = MoveTemp(Bytes);
}

void UMcBlockUseSubsystem::SaveFurnaces(AMcWorldActor& WorldActor)
{
	for (TPair<FKey, FMachineEntry>& Pair : Machines)
	{
		if (Pair.Value.World.Get() == &WorldActor)
		{
			WriteMachine(Pair.Key, Pair.Value);
		}
	}
}

void UMcBlockUseSubsystem::GetContents(const AMcWorldActor& WorldActor, const FIntVector& Pos, TArray<FMcItemStack>& Out)
{
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	const FMcChunk* Chunk = Blocks ? Blocks->FindChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)) : nullptr;
	const FMcBlockEntity* Entity = Chunk ? Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15) : nullptr;
	if (!Entity)
	{
		return;
	}
	if (Entity->Kind == EMcBlockEntityKind::Container)
	{
		Out.Append(Entity->Slots);
	}
	else if ((Entity->Kind == EMcBlockEntityKind::Furnace || Entity->Kind == EMcBlockEntityKind::BrewingStand) && !Entity->Data.IsEmpty())
	{
		const TSharedPtr<FMcMachine> Machine = MakeMachine(Entity->Kind, EMcFurnaceKind::Furnace);
		FMemoryReaderView Reader(Entity->Data);
		if (Machine->Serialize(Reader))
		{
			Out.Append(Machine->GetItems()->GetAll());
		}
	}
}

void UMcBlockUseSubsystem::ReleaseBlock(AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	// A shulker box keeps its things for the item it drops (PackBoxDrops).
	if (const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld(); Blocks && McShulker::IsBox(GetBlockName(*Blocks, Pos)))
	{
		return;
	}
	const FKey Key = MakeKey(WorldActor, Pos);
	TArray<FMcItemStack> Contents;
	if (const FChestEntry* Chest = Chests.Find(Key))
	{
		Contents = Chest->Container->GetAll();
		Chests.Remove(Key);
	}
	else if (const FMachineEntry* Machine = Machines.Find(Key))
	{
		Contents = Machine->Machine->GetItems()->GetAll();
	}
	else
	{
		GetContents(WorldActor, Pos, Contents);
	}
	Machines.Remove(Key);
	DropAll(WorldActor, Pos, Contents);
	// With the contents out, the block entity goes too, so that nothing drops twice.
	if (FMcChunk* Chunk = FindChunkOf(WorldActor, Pos))
	{
		if (Chunk->RemoveBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15))
		{
			WorldActor.MarkChunkDirty(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
		}
	}
}

void UMcBlockUseSubsystem::PackBoxDrops(AMcWorldActor& WorldActor, const FIntVector& Pos, TArray<FMcItemStack>& Drops)
{
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	if (!Blocks || !McShulker::IsBox(GetBlockName(*Blocks, Pos)))
	{
		return;
	}
	const FKey Key = MakeKey(WorldActor, Pos);
	TArray<FMcItemStack> Contents;
	if (const FChestEntry* Chest = Chests.Find(Key))
	{
		Contents = Chest->Container->GetAll();
		Chests.Remove(Key);
	}
	else
	{
		GetContents(WorldActor, Pos, Contents);
	}
	if (FMcChunk* Chunk = FindChunkOf(WorldActor, Pos))
	{
		if (Chunk->RemoveBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15))
		{
			WorldActor.MarkChunkDirty(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
		}
	}
	const TSharedPtr<FJsonValue> Packed = McShulker::Pack(Contents);
	for (FMcItemStack& Drop : Drops)
	{
		if (Packed.IsValid() && McShulker::IsBox(Drop.Item))
		{
			Drop.Set(TEXT("minecraft:container"), Packed);
			return;
		}
	}
	// No box to carry them (nothing dropped): they spill.
	DropAll(WorldActor, Pos, Contents);
}

void UMcBlockUseSubsystem::FillBox(AMcWorldActor& WorldActor, const FIntVector& Pos, const FMcItemStack& Box)
{
	if (!McShulker::IsBox(Box.Item) || !Box.Has(TEXT("minecraft:container")))
	{
		return;
	}
	const TSharedPtr<FMcContainer> Container = GetChest(WorldActor, Pos, McShulker::NumSlots);
	if (!Container.IsValid())
	{
		return;
	}
	const TArray<FMcItemStack> Slots = McShulker::Unpack(Box);
	for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
	{
		Container->Set(Slot, Slots[Slot]);
	}
	WriteChest(WorldActor, Pos);
}

bool UMcBlockUseSubsystem::Use(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	UMcPlayerDataComponent* Data = Player.Data;
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	if (!Data || !Blocks || Data->HasWindow())
	{
		return false;
	}
	// Levers, buttons, doors, trapdoors and gates move rather than open a window.
	if (WorldActor.UseBlock(Pos, Player.GetLookYaw()))
	{
		return true;
	}
	const FName Name = GetBlockName(*Blocks, Pos);
	EMcFurnaceKind FurnaceKind;
	if (Name == TEXT("minecraft:crafting_table"))
	{
		Data->OpenBlockWindow(EMcWindowKind::CraftingTable, Pos, TEXT("Crafting"), nullptr, nullptr);
		return true;
	}
	if (Name == TEXT("minecraft:stonecutter"))
	{
		Data->OpenBlockWindow(EMcWindowKind::Stonecutter, Pos, TEXT("Stonecutter"), nullptr, nullptr);
		return true;
	}
	if (Name == TEXT("minecraft:loom"))
	{
		Data->OpenBlockWindow(EMcWindowKind::Loom, Pos, TEXT("Loom"), nullptr, nullptr);
		return true;
	}
	if (IsAnvilName(Name))
	{
		// Each use has a 12% chance to wear it a step: chipped, damaged, then gone (minecraft.wiki, "Anvil").
		Data->OpenBlockWindow(EMcWindowKind::Anvil, Pos, TEXT("Repair & Name"), nullptr, nullptr,
			[WeakData = TWeakObjectPtr<UMcPlayerDataComponent>(Data), WeakWorld = TWeakObjectPtr<AMcWorldActor>(&WorldActor), Pos]()
			{
				UMcPlayerDataComponent* Owner = WeakData.Get();
				AMcWorldActor* Actor = WeakWorld.Get();
				const FMcBlockWorld* World = Actor ? Actor->GetBlockWorld() : nullptr;
				if (!Owner || !World || Owner->GetWindow().Kind != EMcWindowKind::Anvil)
				{
					return;
				}
				const int32 Uses = static_cast<FMcAnvilMenu*>(Owner->GetWindow().Menu.Get())->TakeUses();
				for (int32 Use = 0; Use < Uses; ++Use)
				{
					const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
					const FMcStateId State = World->GetBlock(Pos);
					const FName Current = GetBlockName(*World, Pos);
					if (!IsAnvilName(Current))
					{
						return;
					}
					if (FMath::FRand() >= 0.12f)
					{
						Actor->MulticastSound(FName("block.anvil.use"), FVector_NetQuantize10(FVector(Pos) + FVector(0.5)), 1.0f, 0.9f + FMath::FRand() * 0.1f);
						continue;
					}
					if (Current == TEXT("minecraft:damaged_anvil"))
					{
						Actor->SetBlock(Pos, Registry.ParseState(TEXT("minecraft:air")).Get(0));
						Actor->MulticastSound(FName("block.anvil.destroy"), FVector_NetQuantize10(FVector(Pos) + FVector(0.5)), 1.0f, 0.9f + FMath::FRand() * 0.1f);
						return;
					}
					const FString Next = FString::Printf(TEXT("%s[facing=%s]"), Current == TEXT("minecraft:anvil") ? TEXT("minecraft:chipped_anvil") : TEXT("minecraft:damaged_anvil"),
						*Registry.GetPropertyValue(State, TEXT("facing")).ToString());
					Actor->SetBlock(Pos, Registry.ParseState(Next).Get(0));
					Actor->MulticastSound(FName("block.anvil.use"), FVector_NetQuantize10(FVector(Pos) + FVector(0.5)), 1.0f, 0.9f + FMath::FRand() * 0.1f);
				}
			});
		return true;
	}
	if (Name == TEXT("minecraft:grindstone"))
	{
		Data->OpenBlockWindow(EMcWindowKind::Grindstone, Pos, TEXT("Repair & Disenchant"), nullptr, nullptr);
		return true;
	}
	if (Name == TEXT("minecraft:smithing_table"))
	{
		Data->OpenBlockWindow(EMcWindowKind::SmithingTable, Pos, TEXT("Upgrade Gear"), nullptr, nullptr);
		return true;
	}
	if (Name == TEXT("minecraft:enchanting_table"))
	{
		FMcBlockTags& Tags = FMcBlockTags::Get();
		Tags.EnsureLoaded();
		const int32 Provider = Tags.Find(TEXT("minecraft:enchantment_power_provider"));
		const int32 Transmitter = Tags.Find(TEXT("minecraft:enchantment_power_transmitter"));
		const int32 Bookshelves = McEnchanting::CountBookshelves(
			[&](const FIntVector& Offset) { return Tags.Has(Provider, Blocks->GetBlock(Pos + Offset)); },
			[&](const FIntVector& Offset) { return Tags.Has(Transmitter, Blocks->GetBlock(Pos + Offset)); });
		Data->OpenEnchantingTable(Pos, Bookshelves);
		return true;
	}
	if (const int32 Slots = GetContainerSlots(Name))
	{
		if (McShulker::IsBox(Name) && IsLidBlocked(*Blocks, Pos))
		{
			return true;
		}
		const TSharedPtr<FMcContainer> Container = GetChest(WorldActor, Pos, Slots);
		if (!Container.IsValid())
		{
			return false;
		}
		// A double chest opens as both halves, the right one's slots first.
		TSharedPtr<FMcContainer> First = Container;
		TSharedPtr<FMcContainer> Second;
		FIntVector SecondPos = Pos;
		if (McChestLink::IsLinkable(Name))
		{
			if (const TOptional<FIntVector> Partner = McChestLink::FindPartner(*Blocks, Pos))
			{
				SecondPos = Partner.GetValue();
				const TSharedPtr<FMcContainer> Other = GetChest(WorldActor, SecondPos, Slots);
				if (Other.IsValid())
				{
					const bool bRight = FMcBlockRegistry::Get().GetPropertyValue(Blocks->GetBlock(Pos), TEXT("type")) == TEXT("right");
					First = bRight ? Container : Other;
					Second = bRight ? Other : Container;
				}
			}
		}
		const bool bHopper = Slots == McHopper::NumSlots;
		const bool bDispenser = Slots == 9;
		const FString Title = bHopper ? TEXT("Hopper") : Name == TEXT("minecraft:dispenser") ? TEXT("Dispenser") : Name == TEXT("minecraft:dropper") ? TEXT("Dropper")
			: Name == TEXT("minecraft:barrel") ? TEXT("Barrel") : McShulker::IsBox(Name) ? TEXT("Shulker Box") : TEXT("Chest");
		const EMcWindowKind Kind = bHopper ? EMcWindowKind::Hopper : bDispenser ? EMcWindowKind::Dispenser : EMcWindowKind::Chest;
		Data->OpenBlockWindow(Kind, Pos, Second.IsValid() ? FString(TEXT("Large Chest")) : Title, First, nullptr,
			[this, WeakWorld = TWeakObjectPtr<AMcWorldActor>(&WorldActor), Pos, SecondPos]()
			{
				if (AMcWorldActor* Actor = WeakWorld.Get())
				{
					WriteChest(*Actor, Pos);
					if (SecondPos != Pos)
					{
						WriteChest(*Actor, SecondPos);
					}
				}
			}, Second);
		return true;
	}
	if (Name == TEXT("minecraft:brewing_stand"))
	{
		const TSharedPtr<FMcBrewingStand> Stand = GetBrewingStand(WorldActor, Pos, true);
		if (!Stand.IsValid())
		{
			return false;
		}
		Data->OpenBlockWindow(EMcWindowKind::BrewingStand, Pos, TEXT("Brewing Stand"), nullptr, Stand,
			[this, Key = MakeKey(WorldActor, Pos)]()
			{
				if (FMachineEntry* Entry = Machines.Find(Key))
				{
					WriteMachine(Key, *Entry);
				}
			});
		return true;
	}
	if (GetFurnaceKind(Name, FurnaceKind))
	{
		const TSharedPtr<FMcFurnace> Furnace = GetFurnace(WorldActor, Pos, true);
		if (!Furnace.IsValid())
		{
			return false;
		}
		const FString Title = FurnaceKind == EMcFurnaceKind::BlastFurnace ? TEXT("Blast Furnace") : FurnaceKind == EMcFurnaceKind::Smoker ? TEXT("Smoker") : TEXT("Furnace");
		Data->OpenBlockWindow(EMcWindowKind::Furnace, Pos, Title, nullptr, Furnace,
			[this, Key = MakeKey(WorldActor, Pos)]()
			{
				if (FMachineEntry* Entry = Machines.Find(Key))
				{
					WriteMachine(Key, *Entry);
				}
			});
		return true;
	}
	return false;
}

bool UMcBlockUseSubsystem::IsWindowValid(const AMcPlayerPawn& Player, const AMcWorldActor& WorldActor, const FMcWindow& Window)
{
	if (Window.Kind == EMcWindowKind::Inventory || Window.Kind == EMcWindowKind::None)
	{
		return true;
	}
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	if (!Blocks)
	{
		return false;
	}
	const FVector Eye = Player.GetFeetPosition() + FVector(0.0, 1.62, 0.0);
	if (FVector::Dist(Eye, FVector(Window.Pos) + FVector(0.5)) > AMcPlayerPawn::ServerReach + 1.0)
	{
		return false;
	}
	const FName Name = GetBlockName(*Blocks, Window.Pos);
	EMcFurnaceKind Kind;
	switch (Window.Kind)
	{
	case EMcWindowKind::CraftingTable:
		return Name == TEXT("minecraft:crafting_table");
	case EMcWindowKind::Chest:
		// A double chest's window needs both halves (a broken one would let its items be taken twice).
		if (Window.Menu.IsValid() && Window.Menu->NumSlots() > 27 + 36 && !McChestLink::FindPartner(*Blocks, Window.Pos).IsSet())
		{
			return false;
		}
		return IsChestName(Name);
	case EMcWindowKind::Furnace:
		return GetFurnaceKind(Name, Kind);
	case EMcWindowKind::EnchantingTable:
		return Name == TEXT("minecraft:enchanting_table");
	case EMcWindowKind::Stonecutter:
		return Name == TEXT("minecraft:stonecutter");
	case EMcWindowKind::Loom:
		return Name == TEXT("minecraft:loom");
	case EMcWindowKind::SmithingTable:
		return Name == TEXT("minecraft:smithing_table");
	case EMcWindowKind::Grindstone:
		return Name == TEXT("minecraft:grindstone");
	case EMcWindowKind::Anvil:
		return IsAnvilName(Name);
	case EMcWindowKind::BrewingStand:
		return Name == TEXT("minecraft:brewing_stand");
	case EMcWindowKind::Hopper:
		return Name == TEXT("minecraft:hopper");
	case EMcWindowKind::Dispenser:
		return Name == TEXT("minecraft:dispenser") || Name == TEXT("minecraft:dropper");
	default:
		return false;
	}
}
