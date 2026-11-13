#include "McJavaExport.h"
#include "McJavaImport.h"
#include "McChunkSave.h"
#include "McRegionFile.h"
#include "McWorldSave.h"
#include "McChunk.h"
#include "McNbt.h"
#include "McBlockRegistry.h"
#include "McBiomeRegistry.h"
#include "McDimensionType.h"
#include "McGenRegion.h"
#include "McBlockTags.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McBlockLight.h"
#include "McLightEngine.h"
#include "McItemStack.h"
#include "McEntityManager.h"
#include "McMob.h"
#include "McVillager.h"
#include "McSign.h"
#include "Dom/JsonObject.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

namespace
{
	using EType = FMcNbt::EType;

	FMcNbt MakeTag(EType Type)
	{
		FMcNbt Tag;
		Tag.Type = Type;
		return Tag;
	}

	FMcNbt Int(int64 Value, EType Type = EType::Int)
	{
		FMcNbt Tag = MakeTag(Type);
		Tag.Integer = Value;
		return Tag;
	}

	FMcNbt Real(double Value, EType Type)
	{
		FMcNbt Tag = MakeTag(Type);
		Tag.Real = Value;
		return Tag;
	}

	FMcNbt Str(const FString& Value)
	{
		FMcNbt Tag = MakeTag(EType::String);
		Tag.String = Value;
		return Tag;
	}

	void Add(FMcNbt& Compound, const TCHAR* Key, FMcNbt Value)
	{
		Compound.Keys.Add(Key);
		Compound.Values.Add(MoveTemp(Value));
	}

	// "minecraft:oak_stairs[facing=north,half=top]" as the game's palette entry: id, and properties when there are any (26.3
	// saves write a plain string for a state with none; the game reads this compound too, observed).
	FMcNbt MakeBlockEntry(const FString& Described)
	{
		FMcNbt Entry = MakeTag(EType::Compound);
		FString Name = Described, Props;
		int32 Bracket;
		if (Described.FindChar(TEXT('['), Bracket))
		{
			Name = Described.Left(Bracket);
			Props = Described.Mid(Bracket + 1);
			Props.RemoveFromEnd(TEXT("]"));
		}
		Add(Entry, TEXT("id"), Str(Name));
		if (!Props.IsEmpty())
		{
			FMcNbt Properties = MakeTag(EType::Compound);
			TArray<FString> Pairs;
			Props.ParseIntoArray(Pairs, TEXT(","), true);
			for (const FString& Pair : Pairs)
			{
				FString Key, Value;
				if (Pair.Split(TEXT("="), &Key, &Value))
				{
					Properties.Keys.Add(Key);
					Properties.Values.Add(Str(Value));
				}
			}
			Add(Entry, TEXT("properties"), MoveTemp(Properties));
		}
		return Entry;
	}

	FMcNbt LongArray(const TArray<int64>& Longs)
	{
		FMcNbt Tag = MakeTag(EType::LongArray);
		for (int64 Value : Longs)
		{
			Tag.Values.Add(Int(Value, EType::Long));
		}
		return Tag;
	}

	// A JSON value of an item component as NBT: whole numbers as ints, other numbers as doubles, true and false as
	// bytes, objects as compounds, arrays as lists. (The component's own NBT types are not kept, so a long or a byte
	// comes back as an int.)
	FMcNbt JsonToNbt(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return MakeTag(EType::Compound);
		}
		switch (Value->Type)
		{
		case EJson::Number:
		{
			const double Number = Value->AsNumber();
			if (Number == FMath::FloorToDouble(Number) && FMath::Abs(Number) < 2147483647.0)
			{
				return Int(static_cast<int64>(Number));
			}
			return Real(Number, EType::Double);
		}
		case EJson::Boolean:
			return Int(Value->AsBool() ? 1 : 0, EType::Byte);
		case EJson::String:
			return Str(Value->AsString());
		case EJson::Array:
		{
			FMcNbt List = MakeTag(EType::List);
			for (const TSharedPtr<FJsonValue>& Element : Value->AsArray())
			{
				List.Values.Add(JsonToNbt(Element));
			}
			return List;
		}
		case EJson::Object:
		{
			FMcNbt Compound = MakeTag(EType::Compound);
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Value->AsObject()->Values)
			{
				Compound.Keys.Add(Pair.Key);
				Compound.Values.Add(JsonToNbt(Pair.Value));
			}
			return Compound;
		}
		default:
			return MakeTag(EType::Compound);
		}
	}

	// An item as the game saves one: id, count and its components.
	FMcNbt ItemToNbt(const FMcItemStack& Stack)
	{
		FMcNbt Item = MakeTag(EType::Compound);
		Add(Item, TEXT("id"), Str(Stack.Item.ToString()));
		Add(Item, TEXT("count"), Int(Stack.Count));
		if (!Stack.Components.IsEmpty())
		{
			FMcNbt Components = MakeTag(EType::Compound);
			for (const TPair<FName, TSharedPtr<FJsonValue>>& Pair : Stack.Components)
			{
				Components.Keys.Add(Pair.Key.ToString());
				Components.Values.Add(JsonToNbt(Pair.Value));
			}
			Add(Item, TEXT("components"), MoveTemp(Components));
		}
		return Item;
	}

	// "Items": the stacks that are there, each with its slot.
	FMcNbt ItemsToNbt(TConstArrayView<FMcItemStack> Slots)
	{
		FMcNbt Items = MakeTag(EType::List);
		for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
		{
			if (!Slots[Slot].IsEmpty())
			{
				FMcNbt Item = ItemToNbt(Slots[Slot]);
				Item.Keys.Insert(TEXT("Slot"), 0);
				Item.Values.Insert(Int(Slot, EType::Byte), 0);
				Items.Values.Add(MoveTemp(Item));
			}
		}
		return Items;
	}

	FMcNbt DoubleList(std::initializer_list<double> Values)
	{
		FMcNbt List = MakeTag(EType::List);
		for (const double Value : Values)
		{
			List.Values.Add(Real(Value, EType::Double));
		}
		return List;
	}

	// A mob of a chunk's entity save (its own bytes, its trailer, and the mob riding it, as FMcEntityHost::SaveLive wrote them)
	// as the game's entity NBT, the rider under Passengers. False for a kind this version doesn't have.
	bool MakeMobNbt(const FString& Kind, TConstArrayView<uint8> Data, FMcNbt& Out)
	{
		const FMcMobType* Type = FMcMobType::Find(FName(*Kind));
		if (!Type)
		{
			return false;
		}
		TUniquePtr<FMcMob> Mob;
		if (Type->Name == FMcVillager::StaticType())
		{
			Mob = MakeUnique<FMcVillager>(*Type);
		}
		else if (Type->Name == FMcZombieVillager::StaticType())
		{
			Mob = MakeUnique<FMcZombieVillager>(*Type);
		}
		else
		{
			Mob = MakeUnique<FMcMob>(*Type);
		}
		FMemoryReaderView Reader(Data);
		if (!Mob->Serialize(Reader))
		{
			return false;
		}
		Mob->LoadTrailer(Reader);
		Out = MakeTag(EType::Compound);
		Add(Out, TEXT("id"), Str(Kind));
		Add(Out, TEXT("Pos"), DoubleList({ Mob->Body.Position.X, Mob->Body.Position.Y, Mob->Body.Position.Z }));
		Add(Out, TEXT("Motion"), DoubleList({ Mob->Body.Velocity.X, Mob->Body.Velocity.Y, Mob->Body.Velocity.Z }));
		FMcNbt Rotation = MakeTag(EType::List);
		Rotation.Values.Add(Real(Mob->Yaw, EType::Float));
		Rotation.Values.Add(Real(Mob->Pitch, EType::Float));
		Add(Out, TEXT("Rotation"), MoveTemp(Rotation));
		Add(Out, TEXT("OnGround"), Int(Mob->Body.bOnGround ? 1 : 0, EType::Byte));
		Add(Out, TEXT("Health"), Real(Mob->Health, EType::Float));
		Add(Out, TEXT("PersistenceRequired"), Int(1, EType::Byte));
		if (Mob->bNoAI)
		{
			Add(Out, TEXT("NoAI"), Int(1, EType::Byte));
		}
		if (!Mob->CustomName.IsEmpty())
		{
			Add(Out, TEXT("CustomName"), Str(Mob->CustomName));
		}
		if (Mob->IsYoung())
		{
			// The zombies and piglins say IsBaby, the animals count their age up from a negative number.
			if (Kind.Contains(TEXT("zombie")) || Kind.Contains(TEXT("husk")) || Kind.Contains(TEXT("drowned")) || Kind.Contains(TEXT("piglin")))
			{
				Add(Out, TEXT("IsBaby"), Int(1, EType::Byte));
			}
			else
			{
				Add(Out, TEXT("Age"), Int(-Mob->YoungTicks));
			}
		}
		if (Type->bHops)
		{
			Add(Out, TEXT("Size"), Int(Mob->Size >= 4 ? 3 : Mob->Size - 1));
		}
		if (Kind == TEXT("minecraft:sheep") && Mob->DyeColor >= 0)
		{
			Add(Out, TEXT("Color"), Int(Mob->DyeColor, EType::Byte));
		}
		const auto Worn = [](FName Item)
		{
			FMcNbt Slot = MakeTag(EType::Compound);
			Add(Slot, TEXT("id"), Str(Item.ToString()));
			Add(Slot, TEXT("count"), Int(1));
			return Slot;
		};
		FMcNbt Equipment = MakeTag(EType::Compound);
		static const TCHAR* const Slots[4] = { TEXT("head"), TEXT("chest"), TEXT("legs"), TEXT("feet") };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (!Mob->Armor[Index].IsNone())
			{
				Add(Equipment, Slots[Index], Worn(Mob->Armor[Index]));
			}
		}
		if (!Mob->HeldItem.IsNone())
		{
			Add(Equipment, TEXT("mainhand"), Worn(Mob->HeldItem));
		}
		if (!Equipment.Keys.IsEmpty())
		{
			Add(Out, TEXT("equipment"), MoveTemp(Equipment));
		}
		int32 Riders = 0;
		if (!Reader.AtEnd() && !Reader.IsError())
		{
			Reader << Riders;
		}
		if (Riders == 1 && !Reader.IsError())
		{
			FString RiderKind;
			TArray<uint8> RiderData;
			Reader << RiderKind << RiderData;
			FMcNbt Rider;
			if (!Reader.IsError() && MakeMobNbt(RiderKind, RiderData, Rider))
			{
				FMcNbt Passengers = MakeTag(EType::List);
				Passengers.Values.Add(MoveTemp(Rider));
				Add(Out, TEXT("Passengers"), MoveTemp(Passengers));
			}
		}
		return true;
	}

	// The entities of one chunk's save (FMcEntityHost::CaptureChunk's bytes) that the game has a counterpart for: the mobs
	// (riders nested under their mounts) and dropped items. The rest (vehicles, tnt, banners, frames) are left out.
	FMcNbt EntitiesToNbt(TConstArrayView<uint8> Bytes)
	{
		FMcNbt List = MakeTag(EType::List);
		FMemoryReaderView Reader(Bytes);
		int32 Count = 0;
		Reader << Count;
		for (int32 Index = 0; Index < Count && !Reader.IsError(); ++Index)
		{
			FString Kind;
			TArray<uint8> Data;
			Reader << Kind << Data;
			if (Reader.IsError())
			{
				break;
			}
			FMcNbt Entity;
			if (Kind == FMcItemEntity::StaticType().ToString())
			{
				FMcItemEntity Item;
				FMemoryReaderView ItemReader(Data);
				if (!Item.Serialize(ItemReader))
				{
					continue;
				}
				Entity = MakeTag(EType::Compound);
				Add(Entity, TEXT("id"), Str(Kind));
				Add(Entity, TEXT("Pos"), DoubleList({ Item.Body.Position.X, Item.Body.Position.Y, Item.Body.Position.Z }));
				Add(Entity, TEXT("Motion"), DoubleList({ Item.Body.Velocity.X, Item.Body.Velocity.Y, Item.Body.Velocity.Z }));
				Add(Entity, TEXT("Item"), ItemToNbt(Item.Stack));
				Add(Entity, TEXT("Age"), Int(Item.Age, EType::Short));
				Add(Entity, TEXT("PickupDelay"), Int(Item.PickupDelay, EType::Short));
			}
			else if (!MakeMobNbt(Kind, Data, Entity))
			{
				continue;
			}
			List.Values.Add(MoveTemp(Entity));
		}
		return List;
	}

	FMcNbt MakeIntArray(const int32* Values, int32 Num)
	{
		FMcNbt Array = MakeTag(EType::IntArray);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			Array.Values.Add(Int(Values[Index]));
		}
		return Array;
	}

	// The stacks at the start of what FMcFurnace and FMcBrewingStand save (see McJavaImport's MakeMachineBytes).
	TArray<FMcItemStack> ReadMachineStacks(const TArray<uint8>& Data, int32 NumSlots)
	{
		TArray<FMcItemStack> Slots;
		Slots.SetNum(NumSlots);
		FMemoryReader Reader(Data);
		for (FMcItemStack& Slot : Slots)
		{
			Slot.Serialize(Reader);
		}
		return Slots;
	}

	// The block entity id the game uses for the block at a position, from the block's name.
	FString BlockEntityId(const FString& BlockName)
	{
		if (BlockName.EndsWith(TEXT("shulker_box")))
		{
			return TEXT("minecraft:shulker_box");
		}
		if (BlockName.EndsWith(TEXT("_hanging_sign")))
		{
			return TEXT("minecraft:hanging_sign");
		}
		if (BlockName.EndsWith(TEXT("_sign")))
		{
			return TEXT("minecraft:sign");
		}
		if (BlockName == TEXT("minecraft:spawner"))
		{
			return TEXT("minecraft:mob_spawner");
		}
		return BlockName;
	}

	FMcNbt MakeSignSide(const FMcSignSide& In)
	{
		FMcNbt Text = MakeTag(EType::Compound);
		FMcNbt Messages = MakeTag(EType::List);
		for (int32 Line = 0; Line < FMcSignSide::NumLines; ++Line)
		{
			Messages.Values.Add(Str(In.Lines[Line]));
		}
		Add(Text, TEXT("messages"), MoveTemp(Messages));
		Add(Text, TEXT("color"), Str(In.Color.ToString()));
		Add(Text, TEXT("has_glowing_text"), Int(In.bGlowing ? 1 : 0, EType::Byte));
		return Text;
	}

	// The block entities of a chunk as the game's list: each with its position, id and data.
	FMcNbt MakeBlockEntityList(const FMcChunk& Chunk)
	{
		const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
		FMcNbt List = MakeTag(EType::List);
		for (const TPair<FIntVector, FMcBlockEntity>& Pair : Chunk.GetBlockEntities())
		{
			const FMcBlockEntity& Source = Pair.Value;
			FString Described = Blocks.Describe(Chunk.GetBlock(Pair.Key.X, Pair.Key.Y, Pair.Key.Z));
			int32 Bracket;
			if (Described.FindChar(TEXT('['), Bracket))
			{
				Described.LeftInline(Bracket);
			}
			FMcNbt Entity = MakeTag(EType::Compound);
			bool bKnown = true;
			FString Id = BlockEntityId(Described);
			switch (Source.Kind)
			{
			case EMcBlockEntityKind::Container:
				Add(Entity, TEXT("Items"), ItemsToNbt(Source.Slots));
				if (!Source.LootTable.IsNone())
				{
					Add(Entity, TEXT("LootTable"), Str(Source.LootTable.ToString()));
					Add(Entity, TEXT("LootTableSeed"), Int(Source.LootSeed, EType::Long));
				}
				break;
			case EMcBlockEntityKind::Spawner:
			{
				FMcNbt Mob = MakeTag(EType::Compound);
				Add(Mob, TEXT("id"), Str(Source.EntityId.ToString()));
				FMcNbt SpawnData = MakeTag(EType::Compound);
				Add(SpawnData, TEXT("entity"), MoveTemp(Mob));
				Add(Entity, TEXT("SpawnData"), MoveTemp(SpawnData));
				break;
			}
			case EMcBlockEntityKind::Furnace:
				Add(Entity, TEXT("Items"), ItemsToNbt(ReadMachineStacks(Source.Data, 3)));
				break;
			case EMcBlockEntityKind::BrewingStand:
				Add(Entity, TEXT("Items"), ItemsToNbt(ReadMachineStacks(Source.Data, 5)));
				break;
			case EMcBlockEntityKind::Sign:
				Add(Entity, TEXT("front_text"), MakeSignSide(Source.SignSides[0]));
				Add(Entity, TEXT("back_text"), MakeSignSide(Source.SignSides[1]));
				Add(Entity, TEXT("is_waxed"), Int(Source.bWaxed ? 1 : 0, EType::Byte));
				break;
			case EMcBlockEntityKind::Lectern:
				if (Source.Slots.IsEmpty() || Source.Slots[0].IsEmpty())
				{
					bKnown = false;
					break;
				}
				Add(Entity, TEXT("Book"), ItemToNbt(Source.Slots[0]));
				Add(Entity, TEXT("Page"), Int(Source.Page));
				break;
			case EMcBlockEntityKind::Pot:
			{
				FMcNbt Sherds = MakeTag(EType::List);
				for (int32 Side = 0; Side < 4; ++Side)
				{
					Sherds.Values.Add(Str(Source.Slots.IsValidIndex(Side) && !Source.Slots[Side].IsEmpty() ? Source.Slots[Side].Item.ToString() : FString(TEXT("minecraft:brick"))));
				}
				Add(Entity, TEXT("sherds"), MoveTemp(Sherds));
				break;
			}
			case EMcBlockEntityKind::Campfire:
				Add(Entity, TEXT("Items"), ItemsToNbt(Source.Slots));
				Add(Entity, TEXT("CookingTimes"), MakeIntArray(Source.CookingTimes, FMcBlockEntity::NumCampfireSpots));
				Add(Entity, TEXT("CookingTotalTimes"), MakeIntArray(Source.CookingTotals, FMcBlockEntity::NumCampfireSpots));
				break;
			case EMcBlockEntityKind::Raw:
			{
				// Kept as it was read: its own compound, with its position put right.
				FMcNbt Kept;
				if (!FMcNbt::Parse(Source.Data, Kept) || Kept.Type != EType::Compound)
				{
					bKnown = false;
					break;
				}
				for (const TCHAR* Key : { TEXT("x"), TEXT("y"), TEXT("z"), TEXT("id") })
				{
					const int32 At = Kept.Keys.IndexOfByKey(Key);
					if (At != INDEX_NONE)
					{
						Kept.Keys.RemoveAt(At);
						Kept.Values.RemoveAt(At);
					}
				}
				Entity = MoveTemp(Kept);
				Id = Source.EntityId.ToString();
				break;
			}
			default:
				bKnown = false;
				break;
			}
			if (!bKnown)
			{
				continue;
			}
			Add(Entity, TEXT("id"), Str(Id));
			Add(Entity, TEXT("keepPacked"), Int(0, EType::Byte));
			Add(Entity, TEXT("x"), Int(Chunk.GetPos().X * 16 + Pair.Key.X));
			Add(Entity, TEXT("y"), Int(Pair.Key.Y));
			Add(Entity, TEXT("z"), Int(Chunk.GetPos().Y * 16 + Pair.Key.Z));
			List.Values.Add(MoveTemp(Entity));
		}
		return List;
	}

	// One section's light as the game saves it: 4096 nibbles, y then z then x, the even one in the low half of a byte.
	FMcNbt MakeLightArray(const FMcLightRegion& Light, int32 MinWorldY, bool bSky, bool& bAnyLight)
	{
		FMcNbt Tag = MakeTag(EType::ByteArray);
		Tag.Bytes.SetNumZeroed(2048);
		bAnyLight = false;
		for (int32 Y = 0; Y < 16; ++Y)
		{
			for (int32 Z = 0; Z < 16; ++Z)
			{
				for (int32 X = 0; X < 16; ++X)
				{
					const int32 Level = bSky ? Light.GetSkyLight(FMcLightRegion::Border + X, MinWorldY + Y, FMcLightRegion::Border + Z)
						: Light.GetBlockLight(FMcLightRegion::Border + X, MinWorldY + Y, FMcLightRegion::Border + Z);
					const int32 Index = (Y * 16 + Z) * 16 + X;
					Tag.Bytes[Index >> 1] |= static_cast<uint8>((Index & 1) ? Level << 4 : Level);
					bAnyLight |= Level != 0;
				}
			}
		}
		return Tag;
	}

	void WriteBig32(TArray<uint8>& Out, uint32 Value)
	{
		Out.Add(static_cast<uint8>(Value >> 24));
		Out.Add(static_cast<uint8>(Value >> 16));
		Out.Add(static_cast<uint8>(Value >> 8));
		Out.Add(static_cast<uint8>(Value));
	}

	bool Compress(FName Format, TConstArrayView<uint8> In, TArray<uint8>& Out)
	{
		int32 Size = FCompression::CompressMemoryBound(Format, In.Num()) + 64;
		Out.SetNumUninitialized(Size);
		if (!FCompression::CompressMemory(Format, Out.GetData(), Size, In.GetData(), In.Num()))
		{
			return false;
		}
		Out.SetNum(Size);
		return true;
	}
}

void McJavaExport::PackIndices(TConstArrayView<int32> Indexes, int32 Bits, TArray<int64>& OutLongs)
{
	const int32 PerLong = 64 / Bits;
	OutLongs.Init(0, FMath::DivideAndRoundUp(Indexes.Num(), PerLong));
	for (int32 Index = 0; Index < Indexes.Num(); ++Index)
	{
		OutLongs[Index / PerLong] |= static_cast<int64>(static_cast<uint64>(Indexes[Index]) << ((Index % PerLong) * Bits));
	}
}

void McJavaExport::MakeChunk(const FMcChunk& Chunk, FMcNbt& Out, int64 LastUpdate, const FMcLightRegion* Light)
{
	const FMcBlockRegistry& Blocks = FMcBlockRegistry::Get();
	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	Out = MakeTag(EType::Compound);
	Add(Out, TEXT("DataVersion"), Int(DataVersion));
	Add(Out, TEXT("xPos"), Int(Chunk.GetPos().X));
	Add(Out, TEXT("zPos"), Int(Chunk.GetPos().Y));
	Add(Out, TEXT("yPos"), Int(Chunk.GetMinY() / McCoords::SectionSize));
	Add(Out, TEXT("Status"), Str(TEXT("minecraft:full")));
	Add(Out, TEXT("LastUpdate"), Int(LastUpdate, EType::Long));
	Add(Out, TEXT("InhabitedTime"), Int(0, EType::Long));
	// The game relights a chunk that says its light isn't on.
	Add(Out, TEXT("isLightOn"), Int(Light ? 1 : 0, EType::Byte));
	FMcNbt Sections = MakeTag(EType::List);
	if (Light)
	{
		// The section below the world holds only light (no sky reaches it).
		FMcNbt Below = MakeTag(EType::Compound);
		Add(Below, TEXT("Y"), Int(Chunk.GetMinY() / McCoords::SectionSize - 1, EType::Byte));
		bool bAny;
		Add(Below, TEXT("SkyLight"), MakeLightArray(*Light, Chunk.GetMinY() - McCoords::SectionSize, true, bAny));
		Sections.Values.Add(MoveTemp(Below));
	}
	for (int32 Section = 0; Section < Chunk.NumSections(); ++Section)
	{
		FMcNbt Entry = MakeTag(EType::Compound);
		Add(Entry, TEXT("Y"), Int(Chunk.GetMinY() / McCoords::SectionSize + Section, EType::Byte));
		// Blocks: a palette of the states used, and an index for each of the 4096 (y, then z, then x).
		// (A flat array by state with a stamp per section stands in for a map: this runs for 4096 blocks of every section.)
		static thread_local TArray<int32> Slot;
		static thread_local TArray<uint32> Stamp;
		static thread_local uint32 Epoch = 0;
		if (Slot.Num() != 65536)
		{
			Slot.SetNumZeroed(65536);
			Stamp.SetNumZeroed(65536);
		}
		++Epoch;
		TArray<FMcStateId> Order;
		TArray<int32> Indexes;
		const FMcChunkSection& Source = Chunk.GetSection(Section);
		if (Source.Blocks.IsUniform())
		{
			Order.Add(Source.Blocks.Get(0));
		}
		else
		{
			Indexes.SetNumUninitialized(4096);
			for (int32 Block = 0; Block < 4096; ++Block)
			{
				const FMcStateId State = Source.Blocks.Get(Block);
				if (Stamp[State] != Epoch)
				{
					Stamp[State] = Epoch;
					Slot[State] = Order.Num();
					Order.Add(State);
				}
				Indexes[Block] = Slot[State];
			}
		}
		FMcNbt BlockStates = MakeTag(EType::Compound);
		FMcNbt Palette = MakeTag(EType::List);
		for (FMcStateId State : Order)
		{
			Palette.Values.Add(MakeBlockEntry(Blocks.Describe(State)));
		}
		Add(BlockStates, TEXT("palette"), MoveTemp(Palette));
		if (Order.Num() > 1)
		{
			TArray<int64> Longs;
			PackIndices(Indexes, McJavaImport::GetBitsPerEntry(Order.Num(), 4), Longs);
			Add(BlockStates, TEXT("data"), LongArray(Longs));
		}
		Add(Entry, TEXT("block_states"), MoveTemp(BlockStates));
		// Biomes: the same, for 4 x 4 x 4 cells (64 per section, y then z then x).
		TMap<FMcBiomeId, int32> BiomeUsed;
		TArray<FMcBiomeId> BiomeOrder;
		TArray<int32> Cells;
		Cells.SetNumUninitialized(64);
		for (int32 Cell = 0; Cell < 64; ++Cell)
		{
			const FMcBiomeId Biome = Chunk.GetBiomes()[Section * 64 + Cell];
			int32* Found = BiomeUsed.Find(Biome);
			if (!Found)
			{
				Found = &BiomeUsed.Add(Biome, BiomeOrder.Num());
				BiomeOrder.Add(Biome);
			}
			Cells[Cell] = *Found;
		}
		FMcNbt BiomeTag = MakeTag(EType::Compound);
		FMcNbt BiomePalette = MakeTag(EType::List);
		for (FMcBiomeId Biome : BiomeOrder)
		{
			BiomePalette.Values.Add(Str(Biomes.GetName(Biome).ToString()));
		}
		Add(BiomeTag, TEXT("palette"), MoveTemp(BiomePalette));
		if (BiomeOrder.Num() > 1)
		{
			TArray<int64> Longs;
			PackIndices(Cells, McJavaImport::GetBitsPerEntry(BiomeOrder.Num(), 1), Longs);
			Add(BiomeTag, TEXT("data"), LongArray(Longs));
		}
		Add(Entry, TEXT("biomes"), MoveTemp(BiomeTag));
		if (Light)
		{
			// As in real saves: sky light for every section, block light only where there is some.
			bool bAny;
			Add(Entry, TEXT("SkyLight"), MakeLightArray(*Light, Chunk.GetMinY() + Section * McCoords::SectionSize, true, bAny));
			FMcNbt BlockLight = MakeLightArray(*Light, Chunk.GetMinY() + Section * McCoords::SectionSize, false, bAny);
			if (bAny)
			{
				Add(Entry, TEXT("BlockLight"), MoveTemp(BlockLight));
			}
		}
		Sections.Values.Add(MoveTemp(Entry));
	}
	Add(Out, TEXT("sections"), MoveTemp(Sections));
	Add(Out, TEXT("block_entities"), MakeBlockEntityList(Chunk));
	// Heightmaps (observed in a real 26.3 save): four long arrays, each column's height above the world's bottom in
	// 9 bits, 7 to a long, columns in z then x order.
	FMcNbt Heightmaps = MakeTag(EType::Compound);
	FMcBlockTags::Get().EnsureLoaded();
	FMcBlockShapes::Get().EnsureLoaded();
	FMcChunk* Middle = const_cast<FMcChunk*>(&Chunk);
	FMcChunk* Nine[9] = {};
	Nine[4] = Middle;
	FMcGenRegion Region(Chunk.GetPos(), Nine);
	const struct { const TCHAR* Name; EMcHeightmap Map; } Maps[] = {
		{ TEXT("OCEAN_FLOOR"), EMcHeightmap::OceanFloor },
		{ TEXT("MOTION_BLOCKING_NO_LEAVES"), EMcHeightmap::MotionBlockingNoLeaves },
		{ TEXT("MOTION_BLOCKING"), EMcHeightmap::MotionBlocking },
		{ TEXT("WORLD_SURFACE"), EMcHeightmap::WorldSurface },
	};
	for (const auto& Entry : Maps)
	{
		TArray<int32> Columns;
		Columns.SetNumUninitialized(256);
		for (int32 Z = 0; Z < 16; ++Z)
		{
			for (int32 X = 0; X < 16; ++X)
			{
				Columns[Z * 16 + X] = Region.GetHeight(Entry.Map, Chunk.GetPos().X * 16 + X, Chunk.GetPos().Y * 16 + Z) - Chunk.GetMinY();
			}
		}
		TArray<int64> Longs;
		PackIndices(Columns, 9, Longs);
		Add(Heightmaps, Entry.Name, LongArray(Longs));
	}
	Add(Out, TEXT("Heightmaps"), MoveTemp(Heightmaps));
	// Structures: none are kept; the game wants both keys. Pending ticks: none are kept. PostProcessing: one list per
	// section, empty.
	FMcNbt Structures = MakeTag(EType::Compound);
	Add(Structures, TEXT("References"), MakeTag(EType::Compound));
	Add(Structures, TEXT("starts"), MakeTag(EType::Compound));
	Add(Out, TEXT("structures"), MoveTemp(Structures));
	Add(Out, TEXT("block_ticks"), MakeTag(EType::List));
	Add(Out, TEXT("fluid_ticks"), MakeTag(EType::List));
	FMcNbt PostProcessing = MakeTag(EType::List);
	for (int32 Section = 0; Section < Chunk.NumSections(); ++Section)
	{
		PostProcessing.Values.Add(MakeTag(EType::List));
	}
	Add(Out, TEXT("PostProcessing"), MoveTemp(PostProcessing));
}

void McJavaExport::MakeRegion(const TMap<FIntPoint, TArray<uint8>>& Chunks, TArray<uint8>& OutFile)
{
	// Two sectors of header (where each chunk is, and when it was saved), then each chunk on a sector boundary: its
	// length, the compression (2: zlib) and the bytes.
	OutFile.Init(0, 8192);
	const int32 Now = static_cast<int32>(FDateTime::UtcNow().ToUnixTimestamp());
	for (const TPair<FIntPoint, TArray<uint8>>& Pair : Chunks)
	{
		TArray<uint8> Packed;
		if (!Compress(NAME_Zlib, Pair.Value, Packed))
		{
			continue;
		}
		const int32 Sector = OutFile.Num() / 4096;
		TArray<uint8> Record;
		WriteBig32(Record, Packed.Num() + 1);
		Record.Add(2);
		Record.Append(Packed);
		const int32 Sectors = FMath::DivideAndRoundUp(Record.Num(), 4096);
		Record.AddZeroed(Sectors * 4096 - Record.Num());
		OutFile.Append(Record);
		const int32 Slot = ((Pair.Key.Y & 31) * 32 + (Pair.Key.X & 31)) * 4;
		const uint32 Location = (static_cast<uint32>(Sector) << 8) | static_cast<uint32>(Sectors);
		for (int32 Byte = 0; Byte < 4; ++Byte)
		{
			OutFile[Slot + Byte] = static_cast<uint8>(Location >> (24 - 8 * Byte));
			OutFile[4096 + Slot + Byte] = static_cast<uint8>(static_cast<uint32>(Now) >> (24 - 8 * Byte));
		}
	}
}

bool McJavaExport::ExportWorld(const FString& WorldDir, const FString& OutFolder, FStats& OutStats, bool bWriteLight)
{
	FMcLevelData Level;
	if (!McWorldSave::LoadLevel(WorldDir, Level) || !FMcBlockRegistry::Get().EnsureLoaded())
	{
		return false;
	}
	IFileManager& Files = IFileManager::Get();
	TSharedPtr<const FMcLightPalette> LightPalette;
	if (bWriteLight)
	{
		FMcBlockLight::Get().EnsureLoaded();
		LightPalette = FMcLightPalette::Build();
	}
	for (const TCHAR* Dimension : { TEXT("minecraft:overworld"), TEXT("minecraft:the_nether"), TEXT("minecraft:the_end") })
	{
		const FString Source = WorldDir / McWorldSave::GetDimensionFolder(Dimension);
		FMcDimensionType Type;
		if (!Files.DirectoryExists(*Source) || !FMcDimensionType::Load(Dimension, Type))
		{
			continue;
		}
		FString Namespace, Path;
		FString(Dimension).Split(TEXT(":"), &Namespace, &Path);
		const FString Target = OutFolder / TEXT("dimensions") / Namespace / Path / TEXT("region");
		TArray<FString> Names;
		Files.FindFiles(Names, *(Source / TEXT("*.mcr")), true, false);
		TArray<FIntPoint> RegionKeys;
		for (const FString& Name : Names)
		{
			TArray<FString> Parts;
			Name.ParseIntoArray(Parts, TEXT("."), true);
			if (Parts.Num() == 4 && Parts[0] == TEXT("r") && Parts[1].IsNumeric() && Parts[2].IsNumeric())
			{
				RegionKeys.Add(FIntPoint(FCString::Atoi(*Parts[1]), FCString::Atoi(*Parts[2])));
			}
		}
		// Row by row, so that the regions around the one being written (light crosses their borders) are the ones held.
		RegionKeys.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.Y != B.Y ? A.Y < B.Y : A.X < B.X; });
		FMcBlockWorld World(Type);
		TMap<FIntPoint, TBitArray<>> Held;
		// The entities a held chunk's save kept, by chunk, for the entities folder.
		TMap<FIntPoint, TArray<uint8>> EntityBytes;
		const auto Hold = [&](FIntPoint Key)
		{
			if (Held.Contains(Key))
			{
				return;
			}
			TBitArray<>& Present = Held.Add(Key);
			const FString Path = Source / FString::Printf(TEXT("r.%d.%d.mcr"), Key.X, Key.Y);
			if (!Files.FileExists(*Path))
			{
				return;
			}
			FMcRegionFile Region(Path);
			Region.GetPresent(Present);
			for (int32 Index = 0; Index < Present.Num(); ++Index)
			{
				if (!Present[Index])
				{
					continue;
				}
				const FIntPoint Pos(Key.X * 32 + Index % 32, Key.Y * 32 + Index / 32);
				TArray<uint8> Payload;
				TUniquePtr<FMcChunk> Chunk = World.MakeChunk(Pos);
				FMcChunkSaveExtras Extras;
				if (Region.Read(Pos, Payload) && McChunkSave::Decode(Payload, *Chunk, Extras))
				{
					if (!Extras.Entities.IsEmpty())
					{
						EntityBytes.Add(Pos, MoveTemp(Extras.Entities));
					}
					World.AddChunk(MoveTemp(Chunk));
				}
			}
		};
		const double Started = FPlatformTime::Seconds();
		int32 RegionsHere = 0;
		for (const FIntPoint& Key : RegionKeys)
		{
			for (int32 DZ = -1; DZ <= 1; ++DZ)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					Hold(Key + FIntPoint(DX, DZ));
				}
			}
			for (TMap<FIntPoint, TBitArray<>>::TIterator It(Held); It; ++It)
			{
				if (FMath::Abs(It.Key().X - Key.X) > 1 || FMath::Abs(It.Key().Y - Key.Y) > 1)
				{
					for (int32 Index = 0; Index < It.Value().Num(); ++Index)
					{
						World.RemoveChunk(FIntPoint(It.Key().X * 32 + Index % 32, It.Key().Y * 32 + Index / 32));
						EntityBytes.Remove(FIntPoint(It.Key().X * 32 + Index % 32, It.Key().Y * 32 + Index / 32));
					}
					It.RemoveCurrent();
				}
			}
			const TBitArray<>& Present = Held.FindChecked(Key);
			TMap<FIntPoint, TArray<uint8>> Chunks;
			for (int32 Index = 0; Index < Present.Num(); ++Index)
			{
				if (!Present[Index])
				{
					continue;
				}
				const FIntPoint Pos(Key.X * 32 + Index % 32, Key.Y * 32 + Index / 32);
				const FMcChunk* Chunk = World.FindChunk(Pos);
				if (!Chunk)
				{
					++OutStats.Failed;
					continue;
				}
				const double MakeStarted = FPlatformTime::Seconds();
				FMcNbt Root;
				if (bWriteLight)
				{
					const double LightStarted = FPlatformTime::Seconds();
					FMcLightRegion Light = FMcLightRegion::Capture(World, Pos);
					Light.Compute(*LightPalette);
					OutStats.LightSeconds += FPlatformTime::Seconds() - LightStarted;
					MakeChunk(*Chunk, Root, static_cast<int64>(Level.DayTime), &Light);
				}
				else
				{
					MakeChunk(*Chunk, Root, static_cast<int64>(Level.DayTime));
				}
				const double WriteStarted = FPlatformTime::Seconds();
				OutStats.MakeSeconds += WriteStarted - MakeStarted;
				TArray<uint8> Bytes;
				Root.WriteRoot(Bytes);
				OutStats.WriteSeconds += FPlatformTime::Seconds() - WriteStarted;
				Chunks.Add(Pos, MoveTemp(Bytes));
				++OutStats.Chunks;
			}
			if (Chunks.IsEmpty())
			{
				continue;
			}
			TArray<uint8> File;
			const double RegionStarted = FPlatformTime::Seconds();
			MakeRegion(Chunks, File);
			OutStats.RegionSeconds += FPlatformTime::Seconds() - RegionStarted;
			Files.MakeDirectory(*Target, true);
			if (!FFileHelper::SaveArrayToFile(File, *(Target / FString::Printf(TEXT("r.%d.%d.mca"), Key.X, Key.Y))))
			{
				return false;
			}
			++OutStats.Regions;
			// Progress, so a long export shows whether time per chunk grows with the chunk count.
			UE_LOG(LogTemp, Display, TEXT("JavaExport progress: %s region %d of %d (this dimension), %d chunks so far in all, %.1f s (%.1f s lighting in all)"),
				Dimension, ++RegionsHere, RegionKeys.Num(), OutStats.Chunks, FPlatformTime::Seconds() - Started, OutStats.LightSeconds);
			// What lives in the region's chunks, in entities/ beside region/ (as the game keeps them).
			TMap<FIntPoint, TArray<uint8>> EntityChunks;
			for (int32 Index = 0; Index < Present.Num(); ++Index)
			{
				const FIntPoint Pos(Key.X * 32 + Index % 32, Key.Y * 32 + Index / 32);
				const TArray<uint8>* Saved = Present[Index] ? EntityBytes.Find(Pos) : nullptr;
				if (!Saved)
				{
					continue;
				}
				FMcNbt List = EntitiesToNbt(*Saved);
				if (List.Values.IsEmpty())
				{
					continue;
				}
				FMcNbt EntityRoot = MakeTag(EType::Compound);
				Add(EntityRoot, TEXT("DataVersion"), Int(DataVersion));
				FMcNbt Position = MakeTag(EType::IntArray);
				Position.Values = { Int(Pos.X), Int(Pos.Y) };
				Add(EntityRoot, TEXT("Position"), MoveTemp(Position));
				Add(EntityRoot, TEXT("entities"), MoveTemp(List));
				TArray<uint8> EntityNbt;
				EntityRoot.WriteRoot(EntityNbt);
				EntityChunks.Add(Pos, MoveTemp(EntityNbt));
				++OutStats.EntityChunks;
			}
			if (!EntityChunks.IsEmpty())
			{
				TArray<uint8> EntityFile;
				MakeRegion(EntityChunks, EntityFile);
				const FString EntityTarget = FPaths::GetPath(Target) / TEXT("entities");
				Files.MakeDirectory(*EntityTarget, true);
				FFileHelper::SaveArrayToFile(EntityFile, *(EntityTarget / FString::Printf(TEXT("r.%d.%d.mca"), Key.X, Key.Y)));
			}
		}
		OutStats.Seconds += FPlatformTime::Seconds() - Started;
	}
	// level.dat, gzip-compressed, in the current layout.
	FMcNbt Data = MakeTag(EType::Compound);
	Add(Data, TEXT("LevelName"), Str(Level.Name));
	Add(Data, TEXT("DataVersion"), Int(DataVersion));
	FMcNbt Version = MakeTag(EType::Compound);
	Add(Version, TEXT("Id"), Int(DataVersion));
	Add(Version, TEXT("Name"), Str(TEXT("26.3")));
	Add(Version, TEXT("Series"), Str(TEXT("main")));
	Add(Version, TEXT("Snapshot"), Int(0, EType::Byte));
	Add(Data, TEXT("Version"), MoveTemp(Version));
	Add(Data, TEXT("GameType"), Int(Level.GameMode == TEXT("creative") ? 1 : 0));
	Add(Data, TEXT("Time"), Int(static_cast<int64>(Level.DayTime), EType::Long));
	// Observed in a real 26.3 level.dat (ref save): no DayTime here, and a storage "version" of 19133.
	Add(Data, TEXT("version"), Int(19133));
	Add(Data, TEXT("LastPlayed"), Int(FDateTime::UtcNow().ToUnixTimestamp() * 1000, EType::Long));
	Add(Data, TEXT("initialized"), Int(1, EType::Byte));
	Add(Data, TEXT("allowCommands"), Int(1, EType::Byte));
	FMcNbt Spawn = MakeTag(EType::Compound);
	FMcNbt Pos = MakeTag(EType::IntArray);
	Pos.Values = { Int(Level.Spawn.X), Int(Level.Spawn.Y), Int(Level.Spawn.Z) };
	Add(Spawn, TEXT("pos"), MoveTemp(Pos));
	Add(Spawn, TEXT("dimension"), Str(Level.Dimension));
	Add(Spawn, TEXT("yaw"), Real(0.0, EType::Float));
	Add(Spawn, TEXT("pitch"), Real(0.0, EType::Float));
	Add(Data, TEXT("spawn"), MoveTemp(Spawn));
	FMcNbt Difficulty = MakeTag(EType::Compound);
	static const TCHAR* const DifficultyNames[] = { TEXT("peaceful"), TEXT("easy"), TEXT("normal"), TEXT("hard") };
	Add(Difficulty, TEXT("difficulty"), Str(DifficultyNames[FMath::Clamp(Level.Difficulty, 0, 3)]));
	Add(Difficulty, TEXT("hardcore"), Int(0, EType::Byte));
	Add(Difficulty, TEXT("locked"), Int(0, EType::Byte));
	Add(Data, TEXT("difficulty_settings"), MoveTemp(Difficulty));
	// Packs, as in a real 26.3 level.dat: the vanilla one enabled, the experimental ones disabled.
	FMcNbt Packs = MakeTag(EType::Compound);
	FMcNbt Enabled = MakeTag(EType::List);
	Enabled.Values.Add(Str(TEXT("vanilla")));
	FMcNbt Disabled = MakeTag(EType::List);
	for (const TCHAR* Name : { TEXT("minecart_improvements"), TEXT("redstone_experiments"), TEXT("trade_rebalance") })
	{
		Disabled.Values.Add(Str(Name));
	}
	Add(Packs, TEXT("Enabled"), MoveTemp(Enabled));
	Add(Packs, TEXT("Disabled"), MoveTemp(Disabled));
	Add(Data, TEXT("DataPacks"), MoveTemp(Packs));
	FMcNbt Root = MakeTag(EType::Compound);
	Add(Root, TEXT("Data"), MoveTemp(Data));
	TArray<uint8> Raw, Zipped;
	Root.WriteRoot(Raw);
	if (!Compress(NAME_Gzip, Raw, Zipped))
	{
		return false;
	}
	Files.MakeDirectory(*OutFolder, true);
	if (!FFileHelper::SaveArrayToFile(Zipped, *(OutFolder / TEXT("level.dat"))))
	{
		return false;
	}
	// data/minecraft/world_gen_settings.dat: the seed and each dimension's generator. Without it the game stops with
	// "Overworld settings missing" while loading the level (observed). Shape as in real 26.3 saves: every dimension has a
	// type and a generator; ours are the noise generators with the standard presets.
	auto Dimension = [](const TCHAR* Type, const TCHAR* Settings, const TCHAR* Preset)
	{
		FMcNbt Generator = MakeTag(EType::Compound);
		Add(Generator, TEXT("settings"), Str(Settings));
		FMcNbt Biomes = MakeTag(EType::Compound);
		if (Preset)
		{
			Add(Biomes, TEXT("preset"), Str(Preset));
		}
		Add(Biomes, TEXT("type"), Str(Preset ? TEXT("minecraft:multi_noise") : TEXT("minecraft:the_end")));
		Add(Generator, TEXT("biome_source"), MoveTemp(Biomes));
		Add(Generator, TEXT("type"), Str(TEXT("minecraft:noise")));
		FMcNbt Entry = MakeTag(EType::Compound);
		Add(Entry, TEXT("generator"), MoveTemp(Generator));
		Add(Entry, TEXT("type"), Str(Type));
		return Entry;
	};
	FMcNbt Dimensions = MakeTag(EType::Compound);
	Add(Dimensions, TEXT("minecraft:overworld"), Dimension(TEXT("minecraft:overworld"), TEXT("minecraft:overworld"), TEXT("minecraft:overworld")));
	Add(Dimensions, TEXT("minecraft:the_nether"), Dimension(TEXT("minecraft:the_nether"), TEXT("minecraft:nether"), TEXT("minecraft:nether")));
	Add(Dimensions, TEXT("minecraft:the_end"), Dimension(TEXT("minecraft:the_end"), TEXT("minecraft:end"), nullptr));
	FMcNbt Settings = MakeTag(EType::Compound);
	Add(Settings, TEXT("bonus_chest"), Int(0, EType::Byte));
	Add(Settings, TEXT("seed"), Int(Level.Seed, EType::Long));
	Add(Settings, TEXT("generate_structures"), Int(1, EType::Byte));
	Add(Settings, TEXT("dimensions"), MoveTemp(Dimensions));
	FMcNbt GenRoot = MakeTag(EType::Compound);
	Add(GenRoot, TEXT("data"), MoveTemp(Settings));
	Add(GenRoot, TEXT("DataVersion"), Int(DataVersion));
	TArray<uint8> GenRaw, GenZipped;
	GenRoot.WriteRoot(GenRaw);
	if (!Compress(NAME_Gzip, GenRaw, GenZipped))
	{
		return false;
	}
	const FString DataFolder = OutFolder / TEXT("data") / TEXT("minecraft");
	Files.MakeDirectory(*DataFolder, true);
	if (!FFileHelper::SaveArrayToFile(GenZipped, *(DataFolder / TEXT("world_gen_settings.dat"))))
	{
		return false;
	}
	// data/minecraft/world_clocks.dat: the time of day lives here in 26.3 (observed in the ref save: the overworld's total_ticks,
	// 77000 there), not in level.dat. The End's clock is left for the game to start.
	FMcNbt Clock = MakeTag(EType::Compound);
	Add(Clock, TEXT("total_ticks"), Int(static_cast<int64>(Level.DayTime), EType::Long));
	FMcNbt Clocks = MakeTag(EType::Compound);
	Add(Clocks, TEXT("minecraft:overworld"), MoveTemp(Clock));
	FMcNbt ClockRoot = MakeTag(EType::Compound);
	Add(ClockRoot, TEXT("data"), MoveTemp(Clocks));
	Add(ClockRoot, TEXT("DataVersion"), Int(DataVersion));
	TArray<uint8> ClockRaw, ClockZipped;
	ClockRoot.WriteRoot(ClockRaw);
	return Compress(NAME_Gzip, ClockRaw, ClockZipped) && FFileHelper::SaveArrayToFile(ClockZipped, *(DataFolder / TEXT("world_clocks.dat")));
}
