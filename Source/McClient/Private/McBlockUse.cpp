#include "McBlockUse.h"
#include "McLeash.h"
#include "McRecipe.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "McBed.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McBell.h"
#include "McBook.h"
#include "McLecternMenu.h"
#include "McChunk.h"
#include "McChestLink.h"
#include "McCompost.h"
#include "McEnchanting.h"
#include "McEntityHost.h"
#include "McTnt.h"
#include "McFurnace.h"
#include "McBrewingStand.h"
#include "McAnvilMenu.h"
#include "McArrow.h"
#include "McBuckets.h"
#include "McJukebox.h"
#include "McSound.h"

#include "McShulker.h"
#include "McShulkerLids.h"
#include "McPot.h"
#include "McFont.h"
#include "McSign.h"
#include "McSimulation.h"
#include "McPlayerData.h"
#include "McPlayerPawn.h"
#include "McVillager.h"
#include "McWorldActor.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "McPerf.h"

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
		if (Name == TEXT("minecraft:jukebox"))
		{
			return 1;
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
	Jukeboxes.Reset();
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
				FHopperEntry& Entry = Hoppers.FindOrAdd(MakeKey(*Actor, Pos));
				if (!Entry.World.IsValid())
				{
					Entry.Order = NextHopperOrder++;
				}
				Entry.World = Actor;
			}
		});
		// So do the hoppers and part-done furnaces of a chunk as it loads.
		Blocks->OnChunkAdded.AddWeakLambda(this, [this, Weak = TWeakObjectPtr<AMcWorldActor>(&WorldActor)](FIntPoint ChunkPos)
		{
			if (AMcWorldActor* Actor = Weak.Get(); Actor && Actor->HasAuthority())
			{
				StartChunk(*Actor, ChunkPos);
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
			Sim->OnCraft = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&WorldActor)](const FIntVector& Pos)
			{
				AMcWorldActor* Actor = Weak.Get();
				return Actor && Craft(*Actor, Pos);
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
	MC_PERF_SCOPE(Misc);
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
			if (Pair.Value.Kind == EMcBlockEntityKind::BrewingStand && StaticCastSharedPtr<FMcBrewingStand>(Pair.Value.Machine)->TakeFinished())
			{
				// The reference (sounds2 run): block.brewing_stand.brew, volume 1, pitch 1, at the block's centre, once.
				if (AMcWorldActor* World = Pair.Value.World.Get())
				{
					if (FMcEntityHost* Host = World->GetEntityHost())
					{
						Host->GetManager().PlaySound(FName(TEXT("block.brewing_stand.brew")), FVector(Pair.Key.Pos) + FVector(0.5), 1.0f, 1.0f);
					}
				}
			}
		}
		TickHoppers();
		TickCampfires();
		TickJukeboxes();
		TickShulkers();
		if (++CleanTimer >= 100)
		{
			CleanTimer = 0;
			Sweep();
		}
	}
}

void UMcBlockUseSubsystem::TickShulkers()
{
	for (TActorIterator<AMcWorldActor> It(GetWorld()); It; ++It)
	{
		const FMcBlockWorld* Blocks = It->HasAuthority() ? It->GetBlockWorld() : nullptr;
		if (!Blocks)
		{
			continue;
		}
		TMap<FIntVector, int32> Openers;
		for (FConstPlayerControllerIterator Controller = GetWorld()->GetPlayerControllerIterator(); Controller; ++Controller)
		{
			const AMcPlayerPawn* Pawn = Controller->IsValid() && (!It->PlayerFilter || It->PlayerFilter(*Controller->Get())) ? Cast<AMcPlayerPawn>(Controller->Get()->GetPawn()) : nullptr;
			const UMcPlayerDataComponent* Items = Pawn ? Pawn->Data.Get() : nullptr;
			if (Items && Items->HasWindow() && Items->GetWindow().Kind == EMcWindowKind::Chest && McShulker::IsBox(GetBlockName(*Blocks, Items->GetWindow().Pos)))
			{
				++Openers.FindOrAdd(Items->GetWindow().Pos);
			}
		}
		It->SetShulkerOpeners(Openers);
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
		TArray<FIntPoint> Loaded;
		Blocks->GetChunks().GetKeys(Loaded);
		for (const FIntPoint& ChunkPos : Loaded)
		{
			StartChunk(*World, ChunkPos);
		}
	}
}

void UMcBlockUseSubsystem::StartChunk(AMcWorldActor& WorldActor, FIntPoint ChunkPos)
{
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	const FMcChunk* Chunk = Blocks ? Blocks->FindChunk(ChunkPos) : nullptr;
	if (!Chunk)
	{
		return;
	}
	// Hoppers by their block, whether or not they hold anything yet.
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const auto IsHopper = [&Registry](FMcStateId State) { return Registry.IsValidState(State) && Registry.GetBlockOf(State).Name == TEXT("minecraft:hopper"); };
	TArray<FMcStateId> States;
	for (int32 Section = 0; Section < Chunk->NumSections(); ++Section)
	{
		const FMcChunkSection& Part = Chunk->GetSection(Section);
		if (Part.IsEmpty() || !Part.Blocks.MayContain(IsHopper))
		{
			continue;
		}
		States.SetNumUninitialized(FMcPalettedContainer::NumEntries);
		Part.Blocks.CopyTo(States);
		for (int32 Index = 0; Index < States.Num(); ++Index)
		{
			if (IsHopper(States[Index]))
			{
				const FIntVector Pos(ChunkPos.X * 16 + (Index & 15), Chunk->SectionMinY(Section) + (Index >> 8), ChunkPos.Y * 16 + ((Index >> 4) & 15));
				if (!Hoppers.Contains(MakeKey(WorldActor, Pos)))
				{
					Hoppers.Add(MakeKey(WorldActor, Pos), FHopperEntry{ &WorldActor, 0, NextHopperOrder++ });
				}
			}
		}
	}
	// Furnaces and brewing stands saved mid-work go on with it.
	TArray<FIntVector> ToLoad;
	for (const TPair<FIntVector, FMcBlockEntity>& Entity : Chunk->GetBlockEntities())
	{
		const FIntVector Pos(ChunkPos.X * 16 + Entity.Key.X, Entity.Key.Y, ChunkPos.Y * 16 + Entity.Key.Z);
		if ((Entity.Value.Kind == EMcBlockEntityKind::Furnace || Entity.Value.Kind == EMcBlockEntityKind::BrewingStand)
			&& !Machines.Contains(MakeKey(WorldActor, Pos)))
		{
			ToLoad.Add(Pos);
		}
	}
	for (const FIntVector& Pos : ToLoad)
	{
		GetMachine(WorldActor, Pos, false);
	}
}

void UMcBlockUseSubsystem::TickHoppers()
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	// In the order they were set down (the reference client's: a hopper handed an item before its own turn counts its wait
	// from 8 down at once, so the next hop is 7 ticks on, and one handed it after its turn waits the full 8).
	TArray<FKey> Keys;
	Hoppers.GenerateKeyArray(Keys);
	Keys.Sort([this](const FKey& A, const FKey& B) { return Hoppers[A].Order < Hoppers[B].Order; });
	for (const FKey& Key : Keys)
	{
		FHopperEntry* Entry = Hoppers.Find(Key);
		if (!Entry)
		{
			continue;
		}
		AMcWorldActor* World = Entry->World.Get();
		const FMcBlockWorld* Blocks = World ? World->GetBlockWorld() : nullptr;
		const FIntVector Pos = Key.Pos;
		if (!Blocks || !Blocks->HasChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)))
		{
			Hoppers.Remove(Key);
			continue;
		}
		const FMcStateId State = Blocks->GetBlock(Pos);
		if (!Registry.IsValidState(State) || Registry.GetBlockOf(State).Name != TEXT("minecraft:hopper"))
		{
			// Breaking it drops its items (ReleaseBlock, or the chest sweep).
			Hoppers.Remove(Key);
			continue;
		}
		if (--Entry->Cooldown > 0)
		{
			continue;
		}
		Entry->Cooldown = 0;
		// A powered hopper (enabled=false) holds still.
		if (Registry.GetPropertyValue(State, TEXT("enabled")) != TEXT("false") && TickHopper(*World, Pos, State))
		{
			// TickHopper may have handed items to other hoppers; the entry is still ours.
			Hoppers[Key].Cooldown = McHopper::Cooldown;
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
	const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	const auto ComposterLevel = [&](const FIntVector& At) -> int32
	{
		const FMcBlockWorld* Block = WorldActor.GetBlockWorld();
		const FMcStateId At_ = Block->GetBlock(At);
		return GetBlockName(*Block, At) == TEXT("minecraft:composter") ? FCString::Atoi(*Blocks.GetPropertyValue(At_, TEXT("level")).ToString()) : -1;
	};
	if (!Items->IsEmpty() && Facing == TEXT("down") && ComposterLevel(Target) >= 0)
	{
		// From above a composter takes the first compostable item whole, and goes up a level by its chance (reference runs
		// lg_compost, lg_compost3: levels 0 to 6 take items, 7 and 8 refuse them; the hopper keeps trying each tick).
		const int32 Level = ComposterLevel(Target);
		for (int32 Slot = 0; Level < 7 && Slot < Items->Num(); ++Slot)
		{
			const int32 Chance = McCompostChance(Items->Get(Slot).Item);
			if (Items->Get(Slot).IsEmpty() || Chance == 0)
			{
				continue;
			}
			FMcItemStack Rest = Items->Get(Slot);
			Rest.Count -= 1;
			Items->Set(Slot, Rest.Count > 0 ? Rest : FMcItemStack());
			if (FMath::RandRange(1, 100) <= Chance)
			{
				WorldActor.SetBlock(Target, Blocks.ParseState(FString::Printf(TEXT("minecraft:composter[level=%d]"), Level + 1)).Get(0));
			}
			ContentsChanged(WorldActor, Pos);
			bMoved = true;
			break;
		}
	}
	else if (!Items->IsEmpty())
	{
		const FMcSlotAccess Into = GetAccess(WorldActor, Target, Facing == TEXT("down") ? EFace::Top : EFace::Side);
		if (McHopper::MoveOne(Self, Into))
		{
			ContentsChanged(WorldActor, Pos);
			ContentsChanged(WorldActor, Target);
			if (FHopperEntry* Receiver = Hoppers.Find(MakeKey(WorldActor, Target)))
			{
				// A receiver that has already had its turn this tick (earlier in Order) misses the decrement a later one gets in
				// this same pass, so it is given one less: a hopper pair passes an item on every 7 ticks both ways (reference run
				// log_lg_logi: moves at ticks 1, 8, 15, 22, ...).
				const FHopperEntry* Sender = Hoppers.Find(MakeKey(WorldActor, Pos));
				Receiver->Cooldown = McHopper::Cooldown - (Sender && Receiver->Order < Sender->Order ? 1 : 0);
			}
			bMoved = true;
		}
	}

	// In from above: out of the bottom of a container, or an item lying on the hopper.
	const FIntVector Above = Pos + FIntVector(0, 1, 0);
	const FMcSlotAccess From = GetAccess(WorldActor, Above, EFace::Bottom);
	if (ComposterLevel(Above) == 8)
	{
		// A ready composter gives a bone meal to the hopper under it and empties.
		FMcItemStack Meal(TEXT("minecraft:bone_meal"), 1);
		if (McHopper::Insert(Self, Meal))
		{
			WorldActor.SetBlock(Above, Blocks.ParseState(TEXT("minecraft:composter[level=0]")).Get(0));
			bMoved = true;
		}
	}
	else if (From.IsValid())
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
		FMcSlotAccess Access = McHopper::ForContainer(Container);
		if (Name == TEXT("minecraft:jukebox"))
		{
			// One music disc and nothing else.
			Access.CanInsert = [](int32, const FMcItemStack& Stack) { return McJukebox::IsDisc(Stack.Item); };
			Access.SlotLimit = [](int32) { return 1; };
		}
		if (McShulker::IsBox(Name))
		{
			// A shulker box never goes into a shulker box (reference run lg_hop, a hopper holding one beside a box).
			Access.CanInsert = [](int32, const FMcItemStack& Stack) { return !Stack.Item.ToString().EndsWith(TEXT("shulker_box")); };
		}
		return Access;
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

bool UMcBlockUseSubsystem::Craft(AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	using McHopper::EFace;
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	FMcEntityHost* Host = WorldActor.GetEntityHost();
	if (!Blocks || !Host || GetBlockName(*Blocks, Pos) != TEXT("minecraft:crafter"))
	{
		return false;
	}
	const TSharedPtr<FMcContainer> Items = GetChest(WorldActor, Pos, 9);
	if (!Items.IsValid())
	{
		return false;
	}
	TArray<FMcItemStack> Slots = Items->GetAll();
	FMcItemStack Product;
	if (!McCrafterCraft(Slots, Product))
	{
		return false;
	}
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		Items->Set(Index, Slots[Index]);
	}
	ContentsChanged(WorldActor, Pos);
	// The block's orientation reads front_top: the front is the first word.
	const FString Orientation = FMcBlockRegistry::Get().GetPropertyValue(Blocks->GetBlock(Pos), TEXT("orientation")).ToString();
	FString Front = Orientation;
	Orientation.Split(TEXT("_"), &Front, nullptr);
	const FName Facing(*Front);
	const FIntVector Step = FacingStep(Facing);
	const FMcSlotAccess Into = GetAccess(WorldActor, Pos + Step, Facing == TEXT("down") ? EFace::Top : Facing == TEXT("up") ? EFace::Bottom : EFace::Side);
	if (Into.IsValid())
	{
		McHopper::Insert(Into, Product);
		ContentsChanged(WorldActor, Pos + Step);
	}
	if (!Product.IsEmpty())
	{
		Host->GetManager().DropItem(FVector(Pos) + FVector(0.5) + FVector(Step) * 0.7, Product);
	}
	return true;
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

	if (bDropper && GetBlockName(*Blocks, Pos + Step) == TEXT("minecraft:composter"))
	{
		// A dropper facing a composter does nothing: the item stays (reference run lg_disp, wheat seeds).
		return;
	}
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
				// A hopper handed an item starts its wait (reference run log_dropper1: a dropper into a hopper, the hopper sends 7 on).
				if (FHopperEntry* Receiver = Hoppers.Find(MakeKey(WorldActor, Pos + Step)))
				{
					Receiver->Cooldown = McHopper::Cooldown;
				}
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
	else if (One.Item == TEXT("minecraft:fire_charge"))
	{
		// Launched as a small fireball: it does not light anything in front and is used up (reference run lg_disp: nothing in
		// front changed, with air, a campfire or anything else there).
		TakeOne();
		Manager.PlaySound(FName("block.dispenser.launch"), Center, 1.0f, 1.2f);
		return;
	}
	else if (One.Item == TEXT("minecraft:carved_pumpkin") || One.Item == TEXT("minecraft:brush") || One.Item.ToString().EndsWith(TEXT("_skull")) || One.Item.ToString().EndsWith(TEXT("_head")))
	{
		// Items that have a use at a dispenser (golems, mobs wearing it, brushing) and no other: when it does not apply the
		// dispenser fails and keeps the item (reference run lg_disp: nothing dropped, nothing placed).
		Manager.PlaySound(FName("block.dispenser.fail"), Center, 1.0f, 1.2f);
		return;
	}
	else if (One.Item == TEXT("minecraft:glass_bottle") || One.Item == TEXT("minecraft:shears") || One.Item == TEXT("minecraft:honeycomb"))
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FIntVector Front = Pos + Step;
		const FMcStateId FrontState = Blocks->GetBlock(Front);
		const FString FrontName = GetBlockName(*Blocks, Front).ToString();
		const bool bHive = (FrontName == TEXT("minecraft:beehive") || FrontName == TEXT("minecraft:bee_nest")) && Registry.GetPropertyValue(FrontState, TEXT("honey_level")) == TEXT("5");
		const auto EmptyHive = [&]()
		{
			TMap<FName, FName> Props;
			Props.Add(TEXT("facing"), Registry.GetPropertyValue(FrontState, TEXT("facing")));
			Props.Add(TEXT("honey_level"), TEXT("0"));
			WorldActor.SetBlock(Front, Registry.FindState(Registry.GetBlockOf(FrontState).Name, Props).Get(FrontState));
		};
		bool bDone = false;
		if (One.Item == TEXT("minecraft:glass_bottle") && bHive)
		{
			// The hive gives its honey: the bottle in the slot becomes a honey bottle (reference run lg_disp).
			EmptyHive();
			if (Items->Get(Slot).Count <= 1)
			{
				Items->Set(Slot, FMcItemStack(TEXT("minecraft:honey_bottle")));
				ContentsChanged(WorldActor, Pos);
			}
			else
			{
				TakeOne();
				FMcItemStack Honey(TEXT("minecraft:honey_bottle"));
				McHopper::Insert(McHopper::ForContainer(Items), Honey);
				if (!Honey.IsEmpty())
				{
					Manager.DropItem(Center + Dir * 0.7, Honey);
				}
				ContentsChanged(WorldActor, Pos);
			}
			bDone = true;
		}
		else if (One.Item == TEXT("minecraft:shears") && bHive)
		{
			// Sheared: three honeycombs fall in front, the shears are worn by one and stay (reference run lg_disp).
			EmptyHive();
			Manager.DropItem(FVector(Front) + FVector(0.5), FMcItemStack(TEXT("minecraft:honeycomb"), 3));
			FMcItemStack Worn = Items->Get(Slot);
			Items->Set(Slot, Worn.HurtAndBreak(1) ? FMcItemStack() : Worn);
			ContentsChanged(WorldActor, Pos);
			bDone = true;
		}
		else if (One.Item == TEXT("minecraft:honeycomb") && (FrontName.Contains(TEXT("copper")) || FrontName.EndsWith(TEXT("lightning_rod"))) && !FrontName.Contains(TEXT("waxed_")))
		{
			// Waxes the copper in front and is used up (reference run lg_disp: copper_block became waxed_copper_block).
			const TOptional<FMcStateId> Waxed = Registry.ParseState(Registry.Describe(FrontState).Replace(TEXT("minecraft:"), TEXT("minecraft:waxed_")));
			if (Waxed.IsSet())
			{
				WorldActor.SetBlock(Front, Waxed.GetValue());
				TakeOne();
				bDone = true;
			}
		}
		if (bDone)
		{
			Manager.PlaySound(FName("block.dispenser.dispense"), Center, 1.0f, 1.0f);
			return;
		}
		if (One.Item != TEXT("minecraft:glass_bottle"))
		{
			Manager.PlaySound(FName("block.dispenser.fail"), Center, 1.0f, 1.2f);
			return;
		}
	}
	else if (One.Item == TEXT("minecraft:flint_and_steel"))
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
		const bool bTnt = FMcPrimedTnt::IsTnt(Registry.GetBlockOf(FrontState).Name);
		if (bLightable || bFire || bTnt)
		{
			if (bTnt)
			{
				FMcPrimedTnt::Ignite(Manager, Front);
			}
			else if (bLightable)
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
		// Facing the way the dispenser does if the cell under the box is empty, otherwise up (reference run lg_disp).
		const bool bBelowEmpty = Registry.IsAir(Blocks->GetBlock(Pos + Step - FIntVector(0, 1, 0)));
		TOptional<FMcStateId> Box = Registry.ParseState(One.Item.ToString() + TEXT("[facing=") + (bBelowEmpty ? Facing.ToString() : FString(TEXT("up"))) + TEXT("]"));
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
	if (const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld(); Blocks && GetBlockName(*Blocks, Pos) == TEXT("minecraft:jukebox"))
	{
		SyncJukebox(WorldActor, Pos);
	}
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
	// Comparators that read it look again (after the write-back above).
	if (FMcSimulation* Sim = WorldActor.GetSimulation())
	{
		Sim->NotifyContainerChanged(Pos);
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
	if (Entity->Kind == EMcBlockEntityKind::Container || Entity->Kind == EMcBlockEntityKind::Lectern || Entity->Kind == EMcBlockEntityKind::Campfire)
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
	// A shulker box keeps its things for the item it drops, and a decorated pot its sides (PackBoxDrops).
	if (const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld(); Blocks && (McShulker::IsBox(GetBlockName(*Blocks, Pos)) || GetBlockName(*Blocks, Pos) == TEXT("minecraft:decorated_pot")))
	{
		return;
	}
	StopJukebox(WorldActor, Pos);
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
	if (Blocks && GetBlockName(*Blocks, Pos) == TEXT("minecraft:decorated_pot"))
	{
		PackPotDrops(WorldActor, Pos, Drops);
		return;
	}
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

void UMcBlockUseSubsystem::PackPotDrops(AMcWorldActor& WorldActor, const FIntVector& Pos, TArray<FMcItemStack>& Drops)
{
	FMcChunk* Chunk = FindChunkOf(WorldActor, Pos);
	const FMcBlockEntity* Entity = Chunk ? Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15) : nullptr;
	TArray<FName> Sides;
	Sides.Init(FName(TEXT("minecraft:brick")), McPot::NumSides);
	if (Entity && Entity->Kind == EMcBlockEntityKind::Pot)
	{
		for (int32 Side = 0; Side < McPot::NumSides && Side < Entity->Slots.Num(); ++Side)
		{
			Sides[Side] = Entity->Slots[Side].IsEmpty() ? Sides[Side] : Entity->Slots[Side].Item;
		}
	}
	if (Chunk && Chunk->RemoveBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15))
	{
		WorldActor.MarkChunkDirty(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
	}
	// The loot table copies the sides onto the pot it drops; a cracked pot (broken with a tool that breaks pots) drops
	// nothing from the table, but its "sherds": each side's sherd or brick.
	bool bWhole = false;
	for (FMcItemStack& Drop : Drops)
	{
		if (Drop.Item == TEXT("minecraft:decorated_pot"))
		{
			McPot::SetSides(Drop, Sides);
			bWhole = true;
		}
	}
	if (!bWhole && Drops.IsEmpty())
	{
		for (const FName Side : Sides)
		{
			Drops.Add(FMcItemStack(Side, 1));
		}
	}
}

void UMcBlockUseSubsystem::FillBox(AMcWorldActor& WorldActor, const FIntVector& Pos, const FMcItemStack& Box)
{
	if (Box.Item == TEXT("minecraft:decorated_pot"))
	{
		// A placed pot keeps the item's sides.
		if (FMcChunk* Chunk = FindChunkOf(WorldActor, Pos); Chunk && WorldActor.GetBlockWorld() && GetBlockName(*WorldActor.GetBlockWorld(), Pos) == Box.Item)
		{
			Chunk->SetBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15, FMcBlockEntity::MakePot(McPot::GetSides(Box)));
			WorldActor.MarkChunkDirty(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
			WorldActor.GetBlockWorld()->OnBlockEntityChanged.Broadcast(Pos);
		}
		return;
	}
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

void UMcBlockUseSubsystem::UseBed(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	UMcPlayerDataComponent* Data = Player.Data;
	FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	const TOptional<FIntVector> Head = Blocks ? McBed::GetHead(*Blocks, Pos) : TOptional<FIntVector>();
	if (!Data || !Head.IsSet() || Player.IsSleeping())
	{
		return;
	}
	FMcEntityHost* Host = WorldActor.GetEntityHost();
	// Out of the overworld a bed is a bomb (minecraft.wiki: "Bed", "Explosion").
	if (WorldActor.Dimension != TEXT("minecraft:overworld"))
	{
		if (Host)
		{
			Host->GetManager().Explode(FVector(Pos.X + 0.5, Pos.Y + 0.5, Pos.Z + 0.5), 5.0f);
		}
		return;
	}
	// Clicking a bed sets where they come back to, whether or not they can sleep in it now.
	if (!Data->SpawnBed.IsSet() || McBed::GetHead(*Blocks, Data->SpawnBed.GetValue()) != Head)
	{
		Data->TellOwner(TEXT("Respawn point set"), 0xFFFFFF);
	}
	Data->SpawnBed = Head.GetValue();
	const int64 DayTime = static_cast<int64>(WorldActor.GetDayTime());
	if (!McBed::CanSleepAt(DayTime, WorldActor.GetThunderLevel() > 0.9f))
	{
		Data->TellOwner(TEXT("You can only sleep at night"), 0xFFFFFF);
		return;
	}
	if (Host && McBed::HasMonstersNear(Host->GetManager(), Head.GetValue()))
	{
		Data->TellOwner(TEXT("You may not rest now, there are monsters nearby"), 0xFFFFFF);
		return;
	}
	Player.ServerStartSleep(Head.GetValue());
}

FMcBlockEntity* UMcBlockUseSubsystem::FindSign(AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	FMcChunk* Chunk = FindChunkOf(WorldActor, Pos);
	if (!Blocks || !Chunk || McSign::GetKind(Blocks->GetBlock(Pos)) == McSign::EKind::None)
	{
		return nullptr;
	}
	FMcBlockEntity* Entity = Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
	if (!Entity || Entity->Kind != EMcBlockEntityKind::Sign)
	{
		// A sign from before signs kept text, or one a structure put there.
		Chunk->SetBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15, FMcBlockEntity::MakeSign());
		Entity = Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
	}
	return Entity;
}

void UMcBlockUseSubsystem::SignChanged(AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	WorldActor.MarkChunkDirty(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
	if (FMcBlockWorld* Blocks = WorldActor.GetBlockWorld())
	{
		Blocks->OnBlockEntityChanged.Broadcast(Pos);
	}
}

void UMcBlockUseSubsystem::PlacedSign(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	if (!FindSign(WorldActor, Pos))
	{
		return;
	}
	SignChanged(WorldActor, Pos);
	EditSign(Player, WorldActor, Pos, false);
}

void UMcBlockUseSubsystem::EditSign(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos, bool bBack)
{
	const FMcBlockEntity* Sign = FindSign(WorldActor, Pos);
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	if (!Sign || !Blocks || !Player.Data)
	{
		return;
	}
	const FMcSignSide& Side = Sign->SignSides[bBack ? 1 : 0];
	const TArray<FString> Lines(Side.Lines, FMcSignSide::NumLines);
	const McSign::EKind Kind = McSign::GetKind(Blocks->GetBlock(Pos));
	Player.Data->OpenSignEditor(Pos, GetBlockName(*Blocks, Pos), static_cast<uint8>(Kind), bBack, McSign::GetTextColour(Side.Color), Lines,
		[WeakWorld = TWeakObjectPtr<AMcWorldActor>(&WorldActor), Pos, bBack, Kind](const TArray<FString>& Written)
		{
			AMcWorldActor* Actor = WeakWorld.Get();
			FMcBlockEntity* Entity = Actor ? FindSign(*Actor, Pos) : nullptr;
			// Gone, turned into another kind of sign, or waxed by someone else meanwhile: nothing is written.
			if (!Entity || Entity->bWaxed || McSign::GetKind(Actor->GetBlockWorld()->GetBlock(Pos)) != Kind)
			{
				return;
			}
			FMcSignSide& Into = Entity->SignSides[bBack ? 1 : 0];
			const FMcSignSide Before = Into;
			for (int32 Line = 0; Line < FMcSignSide::NumLines; ++Line)
			{
				// The screen keeps each line to the sign's width; this keeps a client that didn't to it too.
				Into.Lines[Line] = Written.IsValidIndex(Line) ? McFont::FitToWidth(Written[Line], McSign::GetMaxLineWidth(Kind)) : FString();
			}
			if (!(Into == Before))
			{
				SignChanged(*Actor, Pos);
			}
		});
}

bool UMcBlockUseSubsystem::UseSign(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	FMcBlockEntity* Sign = FindSign(WorldActor, Pos);
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	UMcPlayerDataComponent* Data = Player.Data;
	if (!Sign || !Blocks || !Data)
	{
		return false;
	}
	const FVector Centre = FVector(Pos) + FVector(0.5);
	if (Sign->bWaxed)
	{
		WorldActor.MulticastSound(FName("block.sign.waxed_interact_fail"), FVector_NetQuantize10(Centre), 1.0f, 1.0f);
		return true;
	}
	const bool bBack = !McSign::IsFacingFront(Blocks->GetBlock(Pos), Pos, Player.GetFeetPosition() + FVector(0.0, 1.62, 0.0));
	FMcSignSide& Side = Sign->SignSides[bBack ? 1 : 0];
	FMcItemStack& Held = Data->Inventory->GetMutable(Data->SelectedSlot);
	// What the held item does: a dye or glow ink on text not already so, an ink sac on glowing text, honeycomb on any sign.
	FName Sound;
	const FName Dye = McSign::GetDyeOfItem(Held.Item);
	if (!Dye.IsNone() && Side.HasText() && Side.Color != Dye)
	{
		Side.Color = Dye;
		Sound = FName("item.dye.use");
	}
	else if (Held.Item == TEXT("minecraft:glow_ink_sac") && Side.HasText() && !Side.bGlowing)
	{
		Side.bGlowing = true;
		Sound = FName("item.glow_ink_sac.use");
	}
	else if (Held.Item == TEXT("minecraft:ink_sac") && Side.HasText() && Side.bGlowing)
	{
		Side.bGlowing = false;
		Sound = FName("item.ink_sac.use");
	}
	else if (Held.Item == TEXT("minecraft:honeycomb"))
	{
		Sign->bWaxed = true;
		Sound = FName("item.honeycomb.wax_on");
	}
	if (!Sound.IsNone())
	{
		if (!Data->bCreative && --Held.Count <= 0)
		{
			Held.Clear();
		}
		WorldActor.MulticastSound(Sound, FVector_NetQuantize10(Centre), 1.0f, 1.0f);
		SignChanged(WorldActor, Pos);
		return true;
	}
	EditSign(Player, WorldActor, Pos, bBack);
	return true;
}

FMcBlockEntity* UMcBlockUseSubsystem::FindLectern(AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	FMcChunk* Chunk = FindChunkOf(WorldActor, Pos);
	if (!Blocks || !Chunk || GetBlockName(*Blocks, Pos) != TEXT("minecraft:lectern")
		|| FMcBlockRegistry::Get().GetPropertyValue(Blocks->GetBlock(Pos), TEXT("has_book")) != TEXT("true"))
	{
		return nullptr;
	}
	FMcBlockEntity* Entity = Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
	return Entity && Entity->Kind == EMcBlockEntityKind::Lectern && !Entity->Slots.IsEmpty() && !Entity->Slots[0].IsEmpty() ? Entity : nullptr;
}

namespace
{
	// The lectern state with its facing kept and the book and power as given.
	FMcStateId LecternState(FMcStateId State, bool bHasBook, bool bPowered)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		TMap<FName, FName> Properties;
		Properties.Add(TEXT("facing"), Registry.GetPropertyValue(State, TEXT("facing")));
		Properties.Add(TEXT("has_book"), bHasBook ? TEXT("true") : TEXT("false"));
		Properties.Add(TEXT("powered"), bPowered ? TEXT("true") : TEXT("false"));
		return Registry.FindState(TEXT("minecraft:lectern"), Properties).Get(State);
	}
}

bool UMcBlockUseSubsystem::UseCampfire(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	UMcPlayerDataComponent* Data = Player.Data;
	FMcChunk* Chunk = FindChunkOf(WorldActor, Pos);
	if (!Data || !Chunk)
	{
		return false;
	}
	FMcItemStack& Held = Data->Inventory->GetMutable(Data->SelectedSlot);
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	const FMcRecipe* Recipe = !Held.IsEmpty() && Book.EnsureLoaded() ? Book.FindCooking(EMcRecipeType::CampfireCooking, Held) : nullptr;
	if (!Recipe)
	{
		return false;
	}
	FMcBlockEntity* Campfire = Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
	if (!Campfire || Campfire->Kind != EMcBlockEntityKind::Campfire || Campfire->Slots.Num() != FMcBlockEntity::NumCampfireSpots)
	{
		Chunk->SetBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15, FMcBlockEntity::MakeCampfire());
		Campfire = Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
	}
	for (int32 Spot = 0; Spot < FMcBlockEntity::NumCampfireSpots; ++Spot)
	{
		if (Campfire->Slots[Spot].IsEmpty())
		{
			FMcItemStack One = Held;
			One.Count = 1;
			Campfire->Slots[Spot] = One;
			Campfire->CookingTimes[Spot] = 0;
			Campfire->CookingTotals[Spot] = Recipe->CookingTime;
			if (!Data->bCreative && --Held.Count <= 0)
			{
				Held.Clear();
			}
			WorldActor.MarkChunkDirty(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
			WorldActor.GetBlockWorld()->OnBlockEntityChanged.Broadcast(Pos);
			return true;
		}
	}
	return false;
}

void UMcBlockUseSubsystem::TickCampfires()
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	for (TActorIterator<AMcWorldActor> It(GetWorld()); It; ++It)
	{
		FMcBlockWorld* Blocks = It->HasAuthority() ? It->GetBlockWorld() : nullptr;
		if (!Blocks)
		{
			continue;
		}
		TArray<FIntPoint> Loaded;
		Blocks->GetChunks().GetKeys(Loaded);
		for (const FIntPoint& ChunkPos : Loaded)
		{
			FMcChunk* Chunk = Blocks->FindChunk(ChunkPos);
			if (!Chunk || Chunk->GetBlockEntities().IsEmpty())
			{
				continue;
			}
			TArray<FIntVector> Spots;
			for (const TPair<FIntVector, FMcBlockEntity>& Entity : Chunk->GetBlockEntities())
			{
				if (Entity.Value.Kind == EMcBlockEntityKind::Campfire)
				{
					Spots.Add(Entity.Key);
				}
			}
			for (const FIntVector& Local : Spots)
			{
				const FIntVector Pos(ChunkPos.X * 16 + Local.X, Local.Y, ChunkPos.Y * 16 + Local.Z);
				const FMcStateId State = Blocks->GetBlock(Pos);
				if (!Registry.IsValidState(State) || Registry.GetPropertyValue(State, TEXT("lit")) != TEXT("true"))
				{
					continue;
				}
				FMcBlockEntity* Campfire = Chunk->FindBlockEntity(Local.X, Local.Y, Local.Z);
				bool bChanged = false;
				for (int32 Spot = 0; Spot < FMcBlockEntity::NumCampfireSpots && Spot < Campfire->Slots.Num(); ++Spot)
				{
					FMcItemStack& Food = Campfire->Slots[Spot];
					if (Food.IsEmpty() || ++Campfire->CookingTimes[Spot] < Campfire->CookingTotals[Spot])
					{
						continue;
					}
					const FMcRecipe* Recipe = Book.EnsureLoaded() ? Book.FindCooking(EMcRecipeType::CampfireCooking, Food) : nullptr;
					if (Recipe)
					{
						if (FMcEntityHost* Host = It->GetEntityHost())
						{
							Host->GetManager().DropItem(FVector(Pos) + FVector(0.5, 0.7, 0.5), Recipe->Result);
						}
					}
					Food.Clear();
					Campfire->CookingTimes[Spot] = 0;
					Campfire->CookingTotals[Spot] = 0;
					bChanged = true;
				}
				if (bChanged)
				{
					It->MarkChunkDirty(ChunkPos);
					Blocks->OnBlockEntityChanged.Broadcast(Pos);
				}
			}
		}
	}
}

bool UMcBlockUseSubsystem::UseLectern(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	UMcPlayerDataComponent* Data = Player.Data;
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	FMcChunk* Chunk = FindChunkOf(WorldActor, Pos);
	if (!Data || !Blocks || !Chunk)
	{
		return false;
	}
	const FMcStateId State = Blocks->GetBlock(Pos);
	if (FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("has_book")) != TEXT("true"))
	{
		// The book in hand goes on (one of it; creative keeps its own).
		FMcItemStack& Held = Data->Inventory->GetMutable(Data->SelectedSlot);
		if (!McBook::IsLecternBook(Held.Item))
		{
			return false;
		}
		FMcItemStack One = Held;
		One.Count = 1;
		Chunk->SetBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15, FMcBlockEntity::MakeLectern(One));
		if (!Data->bCreative && --Held.Count <= 0)
		{
			Held.Clear();
		}
		WorldActor.SetBlock(Pos, LecternState(State, true, false));
		WorldActor.MarkChunkDirty(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
		WorldActor.MulticastSound(FName("item.book.put"), FVector_NetQuantize10(FVector(Pos) + FVector(0.5)), 1.0f, 1.0f);
		return true;
	}
	const FMcBlockEntity* Lectern = FindLectern(WorldActor, Pos);
	if (!Lectern)
	{
		return false;
	}
	Data->OpenLectern(Pos, McBook::GetPages(Lectern->Slots[0]), Lectern->Page,
		[WeakPlayer = TWeakObjectPtr<AMcPlayerPawn>(&Player), WeakWorld = TWeakObjectPtr<AMcWorldActor>(&WorldActor), Pos](FMcLecternMenu& Menu)
		{
			AMcPlayerPawn* Reader = WeakPlayer.Get();
			AMcWorldActor* Actor = WeakWorld.Get();
			if (Reader && Actor)
			{
				LecternButton(*Reader, *Actor, Pos, Menu);
			}
		});
	return true;
}

void UMcBlockUseSubsystem::LecternButton(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos, FMcLecternMenu& Menu)
{
	FMcBlockEntity* Lectern = FindLectern(WorldActor, Pos);
	UMcPlayerDataComponent* Data = Player.Data;
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	if (!Lectern || !Data || !Blocks)
	{
		// Gone, or someone else took the book meanwhile: the window closes as it is found invalid.
		return;
	}
	const FMcStateId State = Blocks->GetBlock(Pos);
	if (Menu.bTakeAsked)
	{
		// Into the reader's inventory, or at their feet if it is full; the lectern is left bare and unpowered.
		Menu.bTakeAsked = false;
		const FMcItemStack Rest = Data->Inventory->AddItem(Lectern->Slots[0]);
		if (!Rest.IsEmpty())
		{
			Data->DropFromWindow(Rest);
		}
		if (FMcChunk* Chunk = FindChunkOf(WorldActor, Pos))
		{
			Chunk->RemoveBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
		}
		WorldActor.SetBlock(Pos, LecternState(State, false, false));
		WorldActor.MarkChunkDirty(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
		return;
	}
	if (Menu.Page == Lectern->Page)
	{
		return;
	}
	Lectern->Page = Menu.Page;
	WorldActor.MarkChunkDirty(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
	WorldActor.MulticastSound(FName("item.book.page_turn"), FVector_NetQuantize10(FVector(Pos) + FVector(0.5)), 2.0f, FMath::FRandRange(0.9f, 1.0f));
	// Everyone else reading it turns with it.
	for (TActorIterator<AMcPlayerPawn> It(WorldActor.GetWorld()); It; ++It)
	{
		UMcPlayerDataComponent* Other = It->Data;
		if (Other && Other->GetWindow().Kind == EMcWindowKind::Lectern && Other->GetWindow().Pos == Pos && Other->GetWindow().Menu.Get() != &Menu)
		{
			static_cast<FMcLecternMenu*>(Other->GetWindow().Menu.Get())->Page = Menu.Page;
		}
	}
	// A full-strength pulse for 2 ticks (the redstone ends it), which also has comparators read the new page.
	WorldActor.SetBlock(Pos, LecternState(State, true, true));
	if (FMcSimulation* Sim = WorldActor.GetSimulation())
	{
		Sim->ScheduleTick(Pos, 2);
	}
}

namespace
{
	// The books a chiseled bookshelf holds (minecraft.wiki, "Chiseled Bookshelf": books, books and quills, written books,
	// enchanted books and knowledge books).
	bool IsShelfBook(FName Item)
	{
		return Item == TEXT("minecraft:book") || Item == TEXT("minecraft:writable_book") || Item == TEXT("minecraft:written_book")
			|| Item == TEXT("minecraft:enchanted_book") || Item == TEXT("minecraft:knowledge_book");
	}

	// Which of the six slots the player's look meets on the shelf's front, 0 to 5 (upper left, upper middle, upper right, then the
	// lower row, as the face is seen), or INDEX_NONE if it is not the front. UNVERIFIED: the wiki gives the slot numbers but not
	// where on the face each one is; this is the natural reading order.
	int32 GetShelfSlot(const AMcPlayerPawn& Player, FMcStateId State, const FIntVector& Pos)
	{
		const FName Facing = FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("facing"));
		const FVector Front = Facing == TEXT("north") ? FVector(0, 0, -1) : Facing == TEXT("south") ? FVector(0, 0, 1) : Facing == TEXT("east") ? FVector(1, 0, 0) : FVector(-1, 0, 0);
		const double YawRad = FMath::DegreesToRadians(static_cast<double>(Player.GetLookYaw()));
		const double PitchRad = FMath::DegreesToRadians(static_cast<double>(Player.GetLookPitch()));
		const FVector Look(-FMath::Sin(YawRad) * FMath::Cos(PitchRad), -FMath::Sin(PitchRad), FMath::Cos(YawRad) * FMath::Cos(PitchRad));
		const FVector Eye = Player.GetFeetPosition() + FVector(0.0, 1.62, 0.0);
		const FVector Centre = FVector(Pos) + FVector(0.5);
		const double Facing_Dot = FVector::DotProduct(Look, Front);
		if (Facing_Dot >= -1e-6)
		{
			return INDEX_NONE;
		}
		const double T = FVector::DotProduct(Centre + Front * 0.5 - Eye, Front) / Facing_Dot;
		if (T <= 0.0)
		{
			return INDEX_NONE;
		}
		const FVector Hit = Eye + Look * T;
		const FVector Right = FVector::CrossProduct(-Front, FVector(0, 1, 0));
		const double U = FVector::DotProduct(Hit - Centre, Right) + 0.5;
		const double V = Hit.Y - Pos.Y;
		if (U < 0.0 || U >= 1.0 || V < 0.0 || V >= 1.0)
		{
			return INDEX_NONE;
		}
		return (V >= 0.5 ? 0 : 3) + FMath::Min(2, FMath::FloorToInt32(U * 3.0));
	}

	// A click on a chiseled bookshelf: a book in hand goes into an empty slot, an occupied slot gives its book (also with a book
	// in hand, since 1.19.3). The slot is remembered for comparators (reference run bookshelf: they read slot + 1).
	bool UseBookshelf(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos)
	{
		UMcPlayerDataComponent* Data = Player.Data;
		FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
		FMcChunk* Chunk = Blocks ? Blocks->FindChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)) : nullptr;
		if (!Data || !Chunk)
		{
			return false;
		}
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcStateId State = Blocks->GetBlock(Pos);
		const int32 Slot = GetShelfSlot(Player, State, Pos);
		if (Slot == INDEX_NONE)
		{
			return false;
		}
		FMcBlockEntity* Entity = Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
		if (!Entity || Entity->Kind != EMcBlockEntityKind::Container || Entity->Slots.Num() != 6)
		{
			Chunk->SetBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15, FMcBlockEntity::MakeContainer(6));
			Entity = Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
		}
		const FName Occupied(*FString::Printf(TEXT("slot_%d_occupied"), Slot));
		FMcItemStack& Held = Data->Inventory->GetMutable(Data->SelectedSlot);
		FName Sound;
		bool bPlace = false;
		if (!Entity->Slots[Slot].IsEmpty())
		{
			const FMcItemStack Taken = Entity->Slots[Slot];
			Entity->Slots[Slot].Clear();
			const FMcItemStack Left = Data->Inventory->AddItem(Taken);
			if (!Left.IsEmpty())
			{
				DropAll(WorldActor, Pos, { Left });
			}
			Sound = Taken.Item == TEXT("minecraft:enchanted_book") ? FName("block.chiseled_bookshelf.pickup.enchanted") : FName("block.chiseled_bookshelf.pickup");
		}
		else if (!Held.IsEmpty() && IsShelfBook(Held.Item))
		{
			FMcItemStack One = Held;
			One.Count = 1;
			Entity->Slots[Slot] = One;
			if (!Data->bCreative && --Held.Count <= 0)
			{
				Held.Clear();
			}
			bPlace = true;
			Sound = One.Item == TEXT("minecraft:enchanted_book") ? FName("block.chiseled_bookshelf.insert.enchanted") : FName("block.chiseled_bookshelf.insert");
		}
		else
		{
			return false;
		}
		const FMcBlock& Block = Registry.GetBlockOf(State);
		const FMcBlockState& Data0 = Registry.GetState(State);
		TMap<FName, FName> Properties;
		for (int32 Index = 0; Index < Block.Properties.Num(); ++Index)
		{
			Properties.Add(Block.Properties[Index].Name, Block.Properties[Index].Values[Data0.Values[Index]]);
		}
		Properties.Add(Occupied, bPlace ? FName("true") : FName("false"));
		WorldActor.SetBlock(Pos, Registry.FindState(Block.Name, Properties).Get(State));
		WorldActor.MarkChunkDirty(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
		WorldActor.MulticastSound(Sound, FVector_NetQuantize10(FVector(Pos) + FVector(0.5)), 0.8f, 1.0f);
		if (FMcSimulation* Sim = WorldActor.GetSimulation())
		{
			Sim->BookshelfUsed(Pos, Slot);
		}
		return true;
	}
}

bool UMcBlockUseSubsystem::Use(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	UMcPlayerDataComponent* Data = Player.Data;
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	if (!Data || !Blocks || Data->HasWindow())
	{
		return false;
	}
	{
		// A lead on a fence post ties the mobs the player leads to a knot there; a knot already there hands them back.
		const FString FenceName = GetBlockName(*Blocks, Pos).ToString();
		FMcEntityHost* LeadHost = WorldActor.GetEntityHost();
		if (LeadHost && FenceName.EndsWith(TEXT("_fence")))
		{
			const FMcItemStack& HeldLead = Data->Inventory->Get(Data->SelectedSlot);
			if (McLeash::UseFence(LeadHost->GetManager(), Pos, Player.GetFeetPosition(), !HeldLead.IsEmpty() && HeldLead.Item == TEXT("minecraft:lead")))
			{
				return true;
			}
		}
	}
	if (GetBlockName(*Blocks, Pos) == TEXT("minecraft:note_block"))
	{
		return WorldActor.TuneNote(Pos);
	}
	if (GetBlockName(*Blocks, Pos) == TEXT("minecraft:jukebox"))
	{
		return UseJukebox(Player, WorldActor, Pos);
	}
	// Levers, buttons, doors, trapdoors and gates move rather than open a window.
	if (WorldActor.UseBlock(Pos, Player.GetLookYaw()))
	{
		return true;
	}
	const FName Name = GetBlockName(*Blocks, Pos);
	if (McSign::GetKind(Name) != McSign::EKind::None)
	{
		return UseSign(Player, WorldActor, Pos);
	}
	if (Name == TEXT("minecraft:lectern"))
	{
		return UseLectern(Player, WorldActor, Pos);
	}
	if (Name == TEXT("minecraft:campfire") || Name == TEXT("minecraft:soul_campfire"))
	{
		return UseCampfire(Player, WorldActor, Pos);
	}
	if (Name == TEXT("minecraft:chiseled_bookshelf"))
	{
		return UseBookshelf(Player, WorldActor, Pos);
	}
	if (McBell::IsBell(Blocks->GetBlock(Pos)))
	{
		// Struck from a side it can swing away from, it rings (and swings on every screen); from its bar's ends, nothing.
		const int32 Side = McBell::GetStruckSide(Blocks->GetBlock(Pos), Pos, Player.GetFeetPosition() + FVector(0.0, 1.62, 0.0));
		if (Side == INDEX_NONE)
		{
			return false;
		}
		WorldActor.MulticastSound(FName("block.bell.use"), FVector_NetQuantize10(FVector(Pos) + FVector(0.5)), 2.0f, 1.0f);
		WorldActor.MulticastBellRung(Pos, static_cast<uint8>(Side));
		return true;
	}
	EMcFurnaceKind FurnaceKind;
	if (McBed::IsBed(Name.ToString()))
	{
		UseBed(Player, WorldActor, Pos);
		return true;
	}
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
	if (Name == TEXT("minecraft:bell"))
	{
		// Ringing the bell is the alarm: the villagers within reach come and gather at it for a minute.
		WorldActor.MulticastSound(FName("block.bell.use"), FVector_NetQuantize10(FVector(Pos) + FVector(0.5)), 2.0f, 1.0f);
		if (FMcEntityHost* Host = WorldActor.GetEntityHost())
		{
			Host->GetManager().SoundAlarm(FVector(Pos.X + 0.5, Pos.Y, Pos.Z + 0.5), McVillager::AlarmTicks);
			// Any raider within 48 blocks shows through the walls for 3 seconds.
			Host->RingBell(FVector(Pos.X + 0.5, Pos.Y, Pos.Z + 0.5));
		}
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
	if (Name == TEXT("minecraft:cartography_table"))
	{
		Data->OpenBlockWindow(EMcWindowKind::CartographyTable, Pos, TEXT("Cartography Table"), nullptr, nullptr);
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
	if (Name == TEXT("minecraft:ender_chest"))
	{
		// Every ender chest shows the player's own 27 slots. The lid sounds are the reference's (echest run): volume
		// 0.35, pitch 0.9 to 1. A block above is not checked here, as for the chest.
		const auto LidSound = [Pos](AMcWorldActor& Actor, bool bOpen)
		{
			if (FMcEntityHost* Host = Actor.GetEntityHost())
			{
				Host->GetManager().PlaySound(FName(bOpen ? TEXT("block.ender_chest.open") : TEXT("block.ender_chest.close")), FVector(Pos) + FVector(0.5), 0.35f, FMath::FRandRange(0.9f, 1.0f));
			}
		};
		LidSound(WorldActor, true);
		Data->OpenBlockWindow(EMcWindowKind::Chest, Pos, TEXT("Ender Chest"), Data->EnderChest, nullptr,
			[WeakWorld = TWeakObjectPtr<AMcWorldActor>(&WorldActor), LidSound]()
			{
				if (AMcWorldActor* Actor = WeakWorld.Get())
				{
					LidSound(*Actor, false);
				}
			});
		return true;
	}
	if (const int32 Slots = GetContainerSlots(Name))
	{
		if (McShulker::IsBox(Name) && FMcShulkerLids::IsBlocked(*Blocks, Pos))
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
		// Chests, barrels and shulker boxes creak when they open and again when they shut.
		const TCHAR* Lid = Name == TEXT("minecraft:barrel") ? TEXT("barrel") : McShulker::IsBox(Name) ? TEXT("shulker_box") : IsChestName(Name) ? TEXT("chest") : nullptr;
		const auto LidSound = [Lid, Pos](AMcWorldActor& Actor, bool bOpen)
		{
			if (Lid)
			{
				if (FMcEntityHost* Host = Actor.GetEntityHost())
				{
					Host->GetManager().PlaySound(FName(*FString::Printf(TEXT("block.%s.%s"), Lid, bOpen ? TEXT("open") : TEXT("close"))), FVector(Pos) + FVector(0.5), 0.5f, FMath::FRandRange(0.9f, 1.0f));
				}
			}
		};
		LidSound(WorldActor, true);
		Data->OpenBlockWindow(Kind, Pos, Second.IsValid() ? FString(TEXT("Large Chest")) : Title, First, nullptr,
			[this, WeakWorld = TWeakObjectPtr<AMcWorldActor>(&WorldActor), Pos, SecondPos, LidSound]()
			{
				if (AMcWorldActor* Actor = WeakWorld.Get())
				{
					LidSound(*Actor, false);
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
	// A window on an animal or a cart or boat stays while that is within reach.
	if (Window.EntityId != 0)
	{
		FMcEntityHost* Host = const_cast<AMcWorldActor&>(WorldActor).GetEntityHost();
		const FMcEntity* Found = Host ? Host->GetManager().Find(Window.EntityId) : nullptr;
		return Found && !Found->bRemoved && FVector::Dist(Player.GetFeetPosition(), Found->Body.Position) < AMcPlayerPawn::ServerReach + 1.0;
	}
	const FVector Eye = Player.GetFeetPosition() + FVector(0.0, 1.62, 0.0);
	if (FVector::Dist(Eye, FVector(Window.Pos) + FVector(0.5)) > AMcPlayerPawn::ServerReach + 1.0)
	{
		return false;
	}
	// A trader's window is on whoever stands there, not on a block.
	if (Window.Kind == EMcWindowKind::Trading)
	{
		return true;
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
		return IsChestName(Name) || Name == TEXT("minecraft:ender_chest");
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
	case EMcWindowKind::CartographyTable:
		return Name == TEXT("minecraft:cartography_table");
	case EMcWindowKind::Anvil:
		return IsAnvilName(Name);
	case EMcWindowKind::BrewingStand:
		return Name == TEXT("minecraft:brewing_stand");
	case EMcWindowKind::Horse:
		// Only on an entity, which is checked above.
		return true;
	case EMcWindowKind::Hopper:
		return Name == TEXT("minecraft:hopper");
	case EMcWindowKind::Dispenser:
		return Name == TEXT("minecraft:dispenser") || Name == TEXT("minecraft:dropper");
	case EMcWindowKind::Sign:
		return McSign::GetKind(Name) != McSign::EKind::None;
	case EMcWindowKind::Lectern:
		return Name == TEXT("minecraft:lectern") && FMcBlockRegistry::Get().GetPropertyValue(Blocks->GetBlock(Window.Pos), TEXT("has_book")) == TEXT("true");
	default:
		return false;
	}
}

// ---- Jukebox ----

namespace
{
	FVector JukeboxCentre(const FIntVector& Pos)
	{
		return FVector(Pos) + FVector(0.5);
	}
}

bool UMcBlockUseSubsystem::IsJukeboxPlaying(const AMcWorldActor& WorldActor, const FIntVector& Pos) const
{
	return Jukeboxes.Contains(MakeKey(WorldActor, Pos));
}

bool UMcBlockUseSubsystem::UseJukebox(AMcPlayerPawn& Player, AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	const TSharedPtr<FMcContainer> Container = GetChest(WorldActor, Pos, 1);
	UMcPlayerDataComponent* Data = Player.Data;
	if (!Container.IsValid() || !Data)
	{
		return false;
	}
	if (!Container->Get(0).IsEmpty())
	{
		// Any hand takes the disc out; it pops up out of the top.
		const FMcItemStack Disc = Container->Get(0);
		Container->Set(0, FMcItemStack());
		SyncJukebox(WorldActor, Pos);
		ContentsChanged(WorldActor, Pos);
		DropAll(WorldActor, Pos, { Disc });
		return true;
	}
	FMcItemStack& Held = Data->Inventory->GetMutable(Data->SelectedSlot);
	if (!McJukebox::IsDisc(Held.Item))
	{
		return false;
	}
	FMcItemStack One = Held;
	One.Count = 1;
	Container->Set(0, One);
	if (!Data->bCreative && --Held.Count <= 0)
	{
		Held.Clear();
	}
	SyncJukebox(WorldActor, Pos);
	ContentsChanged(WorldActor, Pos);
	return true;
}

void UMcBlockUseSubsystem::SyncJukebox(AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	const TSharedPtr<FMcContainer> Container = GetChest(WorldActor, Pos, 1);
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	if (!Container.IsValid() || !Blocks)
	{
		return;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId State = Blocks->GetBlock(Pos);
	const bool bHad = Registry.GetPropertyValue(State, TEXT("has_record")) == TEXT("true");
	const bool bHas = !Container->Get(0).IsEmpty();
	if (bHas != bHad)
	{
		TMap<FName, FName> Properties;
		Properties.Add(TEXT("has_record"), bHas ? FName(TEXT("true")) : FName(TEXT("false")));
		if (const TOptional<FMcStateId> Next = Registry.FindState(Registry.GetBlockOf(State).Name, Properties))
		{
			WorldActor.SetBlock(Pos, Next.GetValue());
		}
	}
	if (!bHas)
	{
		StopJukebox(WorldActor, Pos);
		return;
	}
	FMcSong Song;
	if (!bHad && McJukebox::FindSong(Container->Get(0).Item, Song))
	{
		// A disc put in starts at once, and its title shows to those who can hear it.
		const FName Event = McJukebox::GetSoundEvent(Song);
		Jukeboxes.Add(MakeKey(WorldActor, Pos), FJukeboxPlay{ &WorldActor, Song.LengthTicks, 0, Event });
		WorldActor.MulticastRecord(Event, FVector_NetQuantize10(JukeboxCentre(Pos)), true);
		WorldActor.MulticastNote(FVector_NetQuantize10(FVector(Pos) + FVector(0.5, 1.2, 0.5)), FMath::RandRange(0, 24));
		const FString Text = McJukebox::GetNowPlaying(Song);
		const double Range = McSound::GetAudibleRange(McJukebox::Volume);
		for (TActorIterator<AMcPlayerPawn> It(GetWorld()); It; ++It)
		{
			if (It->Data && FVector::Dist(McCoords::UnrealToBlock(It->GetActorLocation()), JukeboxCentre(Pos)) <= Range)
			{
				It->Data->ServerOverlay(Text, true);
			}
		}
	}
}

void UMcBlockUseSubsystem::StopJukebox(AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	FJukeboxPlay Play;
	if (Jukeboxes.RemoveAndCopyValue(MakeKey(WorldActor, Pos), Play))
	{
		WorldActor.MulticastRecord(Play.Sound, FVector_NetQuantize10(JukeboxCentre(Pos)), false);
	}
}

void UMcBlockUseSubsystem::TickJukeboxes()
{
	for (auto It = Jukeboxes.CreateIterator(); It; ++It)
	{
		AMcWorldActor* World = It.Value().World.Get();
		const FMcBlockWorld* Blocks = World ? World->GetBlockWorld() : nullptr;
		const FIntVector Pos = It.Key().Pos;
		if (!Blocks || !Blocks->HasChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)) || GetBlockName(*Blocks, Pos) != TEXT("minecraft:jukebox"))
		{
			// Blown up or replaced: the sound goes with it.
			if (World)
			{
				World->MulticastRecord(It.Value().Sound, FVector_NetQuantize10(JukeboxCentre(Pos)), false);
			}
			It.RemoveCurrent();
		}
		else
		{
			if (++It.Value().Elapsed % 20 == 0 && It.Value().TicksLeft > 1)
			{
				// A note rises off the top every second of the song.
				World->MulticastNote(FVector_NetQuantize10(FVector(Pos) + FVector(0.5, 1.2, 0.5)), FMath::RandRange(0, 24));
			}
			if (--It.Value().TicksLeft <= 0)
			{
				// The song is over; the disc stays in.
				It.RemoveCurrent();
			}
		}
	}
}
