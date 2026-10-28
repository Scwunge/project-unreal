#include "McBlockUse.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McEnchanting.h"
#include "McEntityHost.h"
#include "McFurnace.h"
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

	bool IsAnvilName(FName Name)
	{
		return Name == TEXT("minecraft:anvil") || Name == TEXT("minecraft:chipped_anvil") || Name == TEXT("minecraft:damaged_anvil");
	}

	bool IsChestName(FName Name)
	{
		return Name == TEXT("minecraft:chest") || Name == TEXT("minecraft:trapped_chest") || Name == TEXT("minecraft:barrel");
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
	Furnaces.Reset();
}

void UMcBlockUseSubsystem::WatchWorld(AMcWorldActor& WorldActor)
{
	if (!Watched.Contains(&WorldActor))
	{
		Watched.Add(&WorldActor);
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
	while (Accumulator >= 0.05f)
	{
		Accumulator -= 0.05f;
		for (TPair<FKey, FFurnaceEntry>& Pair : Furnaces)
		{
			Pair.Value.Furnace->Tick();
		}
		if (++CleanTimer >= 100)
		{
			CleanTimer = 0;
			Sweep();
		}
	}
}

void UMcBlockUseSubsystem::Sweep()
{
	// Furnaces and chests whose block has gone (broken by an explosion, or in creative) drop what they held; furnaces
	// that were saved in a chunk but have not been opened yet since it loaded start work; the rest are written back.
	for (auto It = Furnaces.CreateIterator(); It; ++It)
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
		EMcFurnaceKind Kind;
		if (!GetFurnaceKind(GetBlockName(*Blocks, Pos), Kind))
		{
			DropAll(*World, Pos, It.Value().Furnace->GetItems()->GetAll());
			It.RemoveCurrent();
			continue;
		}
		WriteFurnace(It.Key(), It.Value());
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
		else if (!IsChestName(GetBlockName(*Blocks, Pos)))
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
				if (Entity.Value.Kind == EMcBlockEntityKind::Furnace)
				{
					const FIntVector Pos(ChunkPair.Key.X * 16 + Entity.Key.X, Entity.Key.Y, ChunkPair.Key.Y * 16 + Entity.Key.Z);
					if (!Furnaces.Contains(MakeKey(*World, Pos)))
					{
						ToLoad.Add(Pos);
					}
				}
			}
		}
		for (const FIntVector& Pos : ToLoad)
		{
			GetFurnace(*World, Pos, false);
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

TSharedPtr<FMcFurnace> UMcBlockUseSubsystem::GetFurnace(AMcWorldActor& WorldActor, const FIntVector& Pos, bool bCreate)
{
	const FKey Key = MakeKey(WorldActor, Pos);
	if (const FFurnaceEntry* Found = Furnaces.Find(Key))
	{
		return Found->Furnace;
	}
	const FMcBlockWorld* Blocks = WorldActor.GetBlockWorld();
	EMcFurnaceKind Kind = EMcFurnaceKind::Furnace;
	if (!Blocks || !GetFurnaceKind(GetBlockName(*Blocks, Pos), Kind))
	{
		return nullptr;
	}
	FFurnaceEntry Entry;
	Entry.World = &WorldActor;
	Entry.Furnace = MakeShared<FMcFurnace>(Kind);
	bool bSaved = false;
	if (const FMcChunk* Chunk = FindChunkOf(WorldActor, Pos))
	{
		const FMcBlockEntity* Entity = Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15);
		if (Entity && Entity->Kind == EMcBlockEntityKind::Furnace && !Entity->Data.IsEmpty())
		{
			FMemoryReaderView Reader(Entity->Data);
			bSaved = Entry.Furnace->Serialize(Reader);
			if (!bSaved)
			{
				Entry.Furnace = MakeShared<FMcFurnace>(Kind);
			}
			Entry.Saved = Entity->Data;
		}
	}
	if (!bSaved && !bCreate)
	{
		return nullptr;
	}
	WatchWorld(WorldActor);
	Furnaces.Add(Key, Entry);
	return Entry.Furnace;
}

void UMcBlockUseSubsystem::WriteFurnace(const FKey& Key, FFurnaceEntry& Entry)
{
	AMcWorldActor* World = Entry.World.Get();
	FMcChunk* Chunk = World ? FindChunkOf(*World, Key.Pos) : nullptr;
	if (!Chunk)
	{
		return;
	}
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Entry.Furnace->Serialize(Writer);
	if (Bytes == Entry.Saved && Chunk->FindBlockEntity(Key.Pos.X & 15, Key.Pos.Y, Key.Pos.Z & 15))
	{
		return;
	}
	FMcBlockEntity Entity;
	Entity.Kind = EMcBlockEntityKind::Furnace;
	Entity.Data = Bytes;
	Chunk->SetBlockEntity(Key.Pos.X & 15, Key.Pos.Y, Key.Pos.Z & 15, MoveTemp(Entity));
	World->MarkChunkDirty(FIntPoint(Key.Pos.X >> 4, Key.Pos.Z >> 4));
	Entry.Saved = MoveTemp(Bytes);
}

void UMcBlockUseSubsystem::SaveFurnaces(AMcWorldActor& WorldActor)
{
	for (TPair<FKey, FFurnaceEntry>& Pair : Furnaces)
	{
		if (Pair.Value.World.Get() == &WorldActor)
		{
			WriteFurnace(Pair.Key, Pair.Value);
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
	else if (Entity->Kind == EMcBlockEntityKind::Furnace && !Entity->Data.IsEmpty())
	{
		FMcFurnace Furnace;
		FMemoryReaderView Reader(Entity->Data);
		if (Furnace.Serialize(Reader))
		{
			Out.Append(Furnace.GetItems()->GetAll());
		}
	}
}

void UMcBlockUseSubsystem::ReleaseBlock(AMcWorldActor& WorldActor, const FIntVector& Pos)
{
	const FKey Key = MakeKey(WorldActor, Pos);
	TArray<FMcItemStack> Contents;
	if (const FChestEntry* Chest = Chests.Find(Key))
	{
		Contents = Chest->Container->GetAll();
		Chests.Remove(Key);
	}
	else if (const FFurnaceEntry* Furnace = Furnaces.Find(Key))
	{
		Contents = Furnace->Furnace->GetItems()->GetAll();
	}
	else
	{
		GetContents(WorldActor, Pos, Contents);
	}
	Furnaces.Remove(Key);
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
	if (IsAnvilName(Name))
	{
		Data->OpenBlockWindow(EMcWindowKind::Anvil, Pos, TEXT("Repair & Name"), nullptr, nullptr);
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
	if (IsChestName(Name))
	{
		const TSharedPtr<FMcContainer> Container = GetChest(WorldActor, Pos, ChestSlots);
		if (!Container.IsValid())
		{
			return false;
		}
		const FString Title = Name == TEXT("minecraft:barrel") ? TEXT("Barrel") : TEXT("Chest");
		Data->OpenBlockWindow(EMcWindowKind::Chest, Pos, Title, Container, nullptr,
			[this, WeakWorld = TWeakObjectPtr<AMcWorldActor>(&WorldActor), Pos]()
			{
				if (AMcWorldActor* Actor = WeakWorld.Get())
				{
					WriteChest(*Actor, Pos);
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
				if (FFurnaceEntry* Entry = Furnaces.Find(Key))
				{
					WriteFurnace(Key, *Entry);
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
		return IsChestName(Name);
	case EMcWindowKind::Furnace:
		return GetFurnaceKind(Name, Kind);
	case EMcWindowKind::EnchantingTable:
		return Name == TEXT("minecraft:enchanting_table");
	case EMcWindowKind::Stonecutter:
		return Name == TEXT("minecraft:stonecutter");
	case EMcWindowKind::SmithingTable:
		return Name == TEXT("minecraft:smithing_table");
	case EMcWindowKind::Grindstone:
		return Name == TEXT("minecraft:grindstone");
	case EMcWindowKind::Anvil:
		return IsAnvilName(Name);
	default:
		return false;
	}
}
