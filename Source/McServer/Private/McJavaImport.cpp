#include "McJavaImport.h"
#include "McGameRules.h"
#include "HAL/FileManager.h"
#include "McBiomeRegistry.h"
#include "McBannerEntity.h"
#include "McBlockEntity.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "McChunk.h"
#include "McEntityManager.h"
#include "McMob.h"
#include "McVehicle.h"
#include "McVillager.h"
#include "McChunkSave.h"
#include "McDecor.h"
#include "McDecorEntity.h"
#include "McDimensionType.h"
#include "McItemStack.h"
#include "McNbt.h"
#include "McPot.h"
#include "McServer.h"
#include "McWorldSave.h"
#include "McWorldStorage.h"
#include "Async/ParallelFor.h"
#include "HAL/Thread.h"
#include "HAL/Event.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "zlib.h"

namespace
{
	constexpr int32 SectorSize = 4096;
	constexpr int32 ImportSectionVolume = McCoords::SectionSize * McCoords::SectionSize * McCoords::SectionSize;
	constexpr int32 MaxInflated = 256 * 1024 * 1024;
	constexpr uint8 ExternalFlag = 128;

	int32 ReadInt32(TConstArrayView<uint8> Bytes, int32 At)
	{
		return static_cast<int32>((uint32(Bytes[At]) << 24) | (uint32(Bytes[At + 1]) << 16) | (uint32(Bytes[At + 2]) << 8) | uint32(Bytes[At + 3]));
	}

	// The slots a container block has, by block name; 0 for blocks that hold nothing we keep.
	int32 GetContainerSlots(const FString& Id)
	{
		static const TMap<FString, int32> Slots = {
			{ TEXT("minecraft:chest"), 27 }, { TEXT("minecraft:trapped_chest"), 27 }, { TEXT("minecraft:barrel"), 27 },
			{ TEXT("minecraft:hopper"), 5 }, { TEXT("minecraft:dispenser"), 9 }, { TEXT("minecraft:dropper"), 9 },
		};
		if (const int32* Found = Slots.Find(Id))
		{
			return *Found;
		}
		// Copper chests (all four weathering stages, waxed or not) hold 27 slots like a chest.
		return Id.EndsWith(TEXT("shulker_box")) || Id.EndsWith(TEXT("copper_chest")) ? 27 : 0;
	}

	bool GetPaletteEntry(const FMcNbt& Entry, FString& OutName, TMap<FString, FString>& OutProperties)
	{
		if (Entry.Type == FMcNbt::EType::String)
		{
			OutName = Entry.String;
			return true;
		}
		// A list of mixed entries holds the plain ones as a compound with the one key "" (a block with all-default
		// properties is just its name; the others are { id, properties }).
		if (const FMcNbt* Plain = Entry.Find(TEXT("")))
		{
			if (Plain->Type == FMcNbt::EType::String)
			{
				OutName = Plain->String;
				return true;
			}
		}
		const FMcNbt* Name = Entry.Find(TEXT("Name"));
		if (!Name)
		{
			Name = Entry.Find(TEXT("id"));
		}
		if (!Name || Name->Type != FMcNbt::EType::String)
		{
			return false;
		}
		OutName = Name->String;
		const FMcNbt* Properties = Entry.Find(TEXT("Properties"));
		if (!Properties)
		{
			Properties = Entry.Find(TEXT("properties"));
		}
		if (Properties && Properties->Type == FMcNbt::EType::Compound)
		{
			for (int32 Index = 0; Index < Properties->Num(); ++Index)
			{
				const FMcNbt& Value = Properties->Values[Index];
				// Properties are written as text; a number is kept as its text in case a version writes one.
				OutProperties.Add(Properties->Keys[Index], Value.Type == FMcNbt::EType::String ? Value.String : FString::FromInt(static_cast<int32>(Value.Integer)));
			}
		}
		return true;
	}

	TArray<int64> GetLongs(const FMcNbt* Array)
	{
		TArray<int64> Longs;
		if (Array && Array->Type == FMcNbt::EType::LongArray)
		{
			for (const FMcNbt& Value : Array->Values)
			{
				Longs.Add(Value.Integer);
			}
		}
		return Longs;
	}

	const FMcNbt* FindList(const FMcNbt& Root, const TCHAR* Key)
	{
		const FMcNbt* List = Root.Find(Key);
		return List && List->Type == FMcNbt::EType::List ? List : nullptr;
	}

	FString DescribeEntry(const FString& Name, const TMap<FString, FString>& Properties)
	{
		FString Text = Name;
		bool bFirst = true;
		for (const TPair<FString, FString>& Pair : Properties)
		{
			Text += FString::Printf(TEXT("%s%s=%s"), bFirst ? TEXT("[") : TEXT(","), *Pair.Key, *Pair.Value);
			bFirst = false;
		}
		return bFirst ? Text : Text + TEXT("]");
	}

	// The folder that holds a vanilla dimension's region files, whichever layout the world has; empty if it has none.
	FString FindRegionFolder(const FString& Source, const FString& Dimension)
	{
		FString Namespace, Path;
		Dimension.Split(TEXT(":"), &Namespace, &Path);
		IFileManager& Files = IFileManager::Get();
		const FString Current = Source / TEXT("dimensions") / Namespace / Path / TEXT("region");
		if (Files.DirectoryExists(*Current))
		{
			return Current;
		}
		const FString Legacy = Dimension == TEXT("minecraft:overworld") ? Source / TEXT("region")
			: Dimension == TEXT("minecraft:the_nether") ? Source / TEXT("DIM-1") / TEXT("region")
			: Dimension == TEXT("minecraft:the_end") ? Source / TEXT("DIM1") / TEXT("region") : FString();
		return !Legacy.IsEmpty() && Files.DirectoryExists(*Legacy) ? Legacy : FString();
	}

	TSharedPtr<FJsonValue> ToJson(const FMcNbt& Tag)
	{
		using EType = FMcNbt::EType;
		switch (Tag.Type)
		{
		case EType::Byte: case EType::Short: case EType::Int: case EType::Long:
			return MakeShared<FJsonValueNumber>(static_cast<double>(Tag.Integer));
		case EType::Float: case EType::Double:
			return MakeShared<FJsonValueNumber>(Tag.Real);
		case EType::String:
			return MakeShared<FJsonValueString>(Tag.String);
		case EType::Compound:
		{
			const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
			for (int32 Index = 0; Index < Tag.Num(); ++Index)
			{
				Object->SetField(Tag.Keys[Index], ToJson(Tag.Values[Index]));
			}
			return MakeShared<FJsonValueObject>(Object);
		}
		default:
		{
			// Lists and the three kinds of array.
			TArray<TSharedPtr<FJsonValue>> Array;
			for (const FMcNbt& Value : Tag.Values)
			{
				Array.Add(ToJson(Value));
			}
			return MakeShared<FJsonValueArray>(Array);
		}
		}
	}

	// An item as the game saves one: id, count and a compound of components (as data packs write them).
	FMcItemStack MakeStack(const FMcNbt& Item)
	{
		const FString ItemId = Item.GetString(TEXT("id"));
		const int32 Count = static_cast<int32>(Item.GetInt(TEXT("count"), Item.GetInt(TEXT("Count"), 1)));
		if (ItemId.IsEmpty() || Count <= 0)
		{
			return FMcItemStack();
		}
		FMcItemStack Stack(FName(*ItemId), Count);
		if (const FMcNbt* Components = Item.Find(TEXT("components")))
		{
			for (int32 Index = 0; Index < Components->Num(); ++Index)
			{
				Stack.Set(FName(*Components->Keys[Index]), ToJson(Components->Values[Index]));
			}
		}
		return Stack;
	}

	// The items of a block entity by slot.
	TArray<FMcItemStack> ReadItems(const FMcNbt& Entity, int32 NumSlots)
	{
		TArray<FMcItemStack> Slots;
		Slots.SetNum(NumSlots);
		if (const FMcNbt* Items = FindList(Entity, TEXT("Items")))
		{
			for (const FMcNbt& Item : Items->Values)
			{
				const int32 Slot = static_cast<int32>(Item.GetInt(TEXT("Slot"))) & 0xff;
				if (Slot < NumSlots)
				{
					Slots[Slot] = MakeStack(Item);
				}
			}
		}
		return Slots;
	}

	// A text component's words without their styling: a string, a list of components, or {"text": ..., "extra": [...]}.
	FString PlainText(const TSharedPtr<FJsonValue>& Component)
	{
		FString Text;
		if (!Component.IsValid())
		{
			return Text;
		}
		if (Component->TryGetString(Text))
		{
			return Text;
		}
		if (Component->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Part : Component->AsArray())
			{
				Text += PlainText(Part);
			}
			return Text;
		}
		if (Component->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>& Object = Component->AsObject();
			Object->TryGetStringField(TEXT("text"), Text);
			const TArray<TSharedPtr<FJsonValue>>* Extra = nullptr;
			if (Object->TryGetArrayField(TEXT("extra"), Extra))
			{
				for (const TSharedPtr<FJsonValue>& Part : *Extra)
				{
					Text += PlainText(Part);
				}
			}
		}
		return Text;
	}

	// A line of a sign as a save keeps it: older saves write the component as JSON in a string ("{\"text\":\"Hi\"}"),
	// newer ones as NBT (a plain string, or a compound like the JSON).
	FString ReadSignLine(const FMcNbt& Line)
	{
		if (Line.Type != FMcNbt::EType::String)
		{
			return PlainText(ToJson(Line));
		}
		const FString& String = Line.String;
		if (String.StartsWith(TEXT("{")) || String.StartsWith(TEXT("[")) || String.StartsWith(TEXT("\"")))
		{
			TSharedPtr<FJsonValue> Json;
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(String), Json) && Json.IsValid())
			{
				return PlainText(Json);
			}
		}
		return String;
	}

	// A sign's block entity (minecraft.wiki, "Sign"): front_text and back_text, each with four messages, a color and
	// has_glowing_text, and is_waxed; a sign saved before both sides could be written has Text1 to Text4, Color and
	// GlowingText for its front.
	FMcBlockEntity MakeSign(const FMcNbt& Entity)
	{
		FMcBlockEntity Sign = FMcBlockEntity::MakeSign();
		const auto ReadSide = [](const FMcNbt& Text, FMcSignSide& Side)
		{
			if (const FMcNbt* Messages = Text.Find(TEXT("messages")))
			{
				for (int32 Line = 0; Line < FMcSignSide::NumLines && Line < Messages->Num(); ++Line)
				{
					Side.Lines[Line] = ReadSignLine(Messages->Values[Line]);
				}
			}
			const FString Color = Text.GetString(TEXT("color"));
			if (!Color.IsEmpty())
			{
				Side.Color = FName(*Color);
			}
			Side.bGlowing = Text.GetInt(TEXT("has_glowing_text")) != 0;
		};
		const FMcNbt* Front = Entity.Find(TEXT("front_text"));
		if (Front)
		{
			ReadSide(*Front, Sign.SignSides[0]);
		}
		else
		{
			for (int32 Line = 0; Line < FMcSignSide::NumLines; ++Line)
			{
				if (const FMcNbt* Text = Entity.Find(*FString::Printf(TEXT("Text%d"), Line + 1)))
				{
					Sign.SignSides[0].Lines[Line] = ReadSignLine(*Text);
				}
			}
			const FString Color = Entity.GetString(TEXT("Color"));
			if (!Color.IsEmpty())
			{
				Sign.SignSides[0].Color = FName(*Color);
			}
			Sign.SignSides[0].bGlowing = Entity.GetInt(TEXT("GlowingText")) != 0;
		}
		if (const FMcNbt* Back = Entity.Find(TEXT("back_text")))
		{
			ReadSide(*Back, Sign.SignSides[1]);
		}
		Sign.bWaxed = Entity.GetInt(TEXT("is_waxed")) != 0;
		return Sign;
	}

	// A decorated pot (minecraft.wiki, "Decorated Pot"): sherds, the item on its back, left, right and front, a brick
	// where the list has none.
	FMcBlockEntity MakePot(const FMcNbt& Entity)
	{
		TArray<FName> Sides;
		Sides.Init(FName(TEXT("minecraft:brick")), McPot::NumSides);
		if (const FMcNbt* Sherds = Entity.Find(TEXT("sherds")))
		{
			for (int32 Side = 0; Side < McPot::NumSides && Side < Sherds->Num(); ++Side)
			{
				if (!Sherds->Values[Side].String.IsEmpty())
				{
					Sides[Side] = FName(*Sherds->Values[Side].String);
				}
			}
		}
		return FMcBlockEntity::MakePot(Sides);
	}

	// What FMcFurnace::Serialize and FMcBrewingStand::Serialize read: the stacks, then counters that start at rest (a
	// furnace's burn and cook time are not carried over).
	TArray<uint8> MakeMachineBytes(TArray<FMcItemStack> Slots, bool bFurnace)
	{
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		for (FMcItemStack& Slot : Slots)
		{
			Slot.Serialize(Writer);
		}
		if (bFurnace)
		{
			int32 LitTimeRemaining = 0, LitTotalTime = 0, CookingTotalTime = 0, NumRecipes = 0;
			float LitSpeed = 1.0f, CookingProgress = 0.0f;
			Writer << LitTimeRemaining << LitTotalTime << LitSpeed << CookingProgress << CookingTotalTime << NumRecipes;
		}
		else
		{
			int32 BrewTimeLeft = 0, Fuel = 0;
			FString Brewing;
			Writer << BrewTimeLeft << Fuel << Brewing;
		}
		return Bytes;
	}

	void AddField(FMcNbt& Compound, const TCHAR* Key, FMcNbt Value)
	{
		Compound.Keys.Add(Key);
		Compound.Values.Add(MoveTemp(Value));
	}

	FMcNbt MakeInt(int32 Value)
	{
		FMcNbt Tag;
		Tag.Type = FMcNbt::EType::Int;
		Tag.Integer = Value;
		return Tag;
	}

	// The decor (item frame, glow item frame, painting, armor stand) an entity of the game's NBT is, with where it stands
	// and which way it turns. False for any other entity, or one without what it needs.
	bool MakeDecor(const FMcNbt& Entity, McDecor::FDecor& Out, FVector& OutPos, float& OutYaw)
	{
		const FString Id = Entity.GetString(TEXT("id"));
		Out = McDecor::FDecor();
		const FMcNbt* Pos = FindList(Entity, TEXT("Pos"));
		if (!Pos || Pos->Num() != 3)
		{
			return false;
		}
		OutPos = FVector(Pos->Values[0].GetReal(), Pos->Values[1].GetReal(), Pos->Values[2].GetReal());
		OutYaw = 0.0f;
		if (const FMcNbt* Rotation = FindList(Entity, TEXT("Rotation")))
		{
			OutYaw = Rotation->Num() > 0 ? static_cast<float>(Rotation->Values[0].GetReal()) : 0.0f;
		}
		Out.bInvisible = Entity.GetInt(TEXT("Invisible")) != 0;
		// A direction is a byte in the game's order (0 down, 1 up, 2 north, 3 south, 4 west, 5 east), or its name.
		const auto GetFacing = [&Entity](const TCHAR* Key, int32 Default)
		{
			const FMcNbt* Tag = Entity.Find(Key);
			if (!Tag)
			{
				return Default;
			}
			if (Tag->Type == FMcNbt::EType::String)
			{
				static const TCHAR* Names[] = { TEXT("down"), TEXT("up"), TEXT("north"), TEXT("south"), TEXT("west"), TEXT("east") };
				for (int32 Index = 0; Index < 6; ++Index)
				{
					if (Tag->String == Names[Index])
					{
						return Index;
					}
				}
				return Default;
			}
			return static_cast<int32>(Tag->Integer);
		};
		if (Id == TEXT("minecraft:item_frame") || Id == TEXT("minecraft:glow_item_frame"))
		{
			Out.Kind = Id == TEXT("minecraft:item_frame") ? McDecor::EKind::ItemFrame : McDecor::EKind::GlowItemFrame;
			Out.Facing = GetFacing(TEXT("Facing"), 3);
			Out.ItemRotation = static_cast<int32>(Entity.GetInt(TEXT("ItemRotation")));
			if (const FMcNbt* Item = Entity.Find(TEXT("Item")))
			{
				Out.Item = Item->GetString(TEXT("id"));
			}
			return true;
		}
		if (Id == TEXT("minecraft:painting"))
		{
			Out.Kind = McDecor::EKind::Painting;
			// A painting's facing byte counts the horizontal ways only (wiki, "Painting": 0 south, 1 west, 2 north, 3 east).
			static const int32 FromPainting[4] = { 3, 4, 2, 5 };
			const int32 Facing = GetFacing(TEXT("facing"), 0);
			Out.Facing = Entity.Find(TEXT("facing")) && Entity.Find(TEXT("facing"))->Type == FMcNbt::EType::String ? Facing : FromPainting[Facing & 3];
			Out.Variant = McCache::PathOf(Entity.GetString(TEXT("variant")));
			return !Out.Variant.IsEmpty();
		}
		if (Id == TEXT("minecraft:armor_stand"))
		{
			Out.Kind = McDecor::EKind::ArmorStand;
			Out.Yaw = OutYaw;
			Out.bSmall = Entity.GetInt(TEXT("Small")) != 0;
			Out.bArms = Entity.GetInt(TEXT("ShowArms")) != 0;
			Out.bBasePlate = Entity.GetInt(TEXT("NoBasePlate")) == 0;
			if (const FMcNbt* Pose = Entity.Find(TEXT("Pose")))
			{
				static const TCHAR* Keys[] = { TEXT("Head"), TEXT("Body"), TEXT("LeftArm"), TEXT("RightArm"), TEXT("LeftLeg"), TEXT("RightLeg") };
				for (int32 Part = 0; Part < 6; ++Part)
				{
					const FMcNbt* Angles = FindList(*Pose, Keys[Part]);
					if (Angles && Angles->Num() == 3)
					{
						Out.Pose[Part] = FVector3f(Angles->Values[0].GetReal(), Angles->Values[1].GetReal(), Angles->Values[2].GetReal());
					}
				}
			}
			if (const FMcNbt* Equipment = Entity.Find(TEXT("equipment")))
			{
				static const TCHAR* Keys[] = { TEXT("head"), TEXT("chest"), TEXT("legs"), TEXT("feet"), TEXT("mainhand"), TEXT("offhand") };
				for (int32 Slot = 0; Slot < 6; ++Slot)
				{
					if (const FMcNbt* Stack = Equipment->Find(Keys[Slot]))
					{
						Out.Equipment[Slot] = Stack->GetString(TEXT("id"));
						// Dye is a component: a bare colour, or a compound holding it as "rgb".
						if (const FMcNbt* Components = Stack->Find(TEXT("components")))
						{
							if (const FMcNbt* Dye = Components->Find(TEXT("minecraft:dyed_color")))
							{
								const int64 Rgb = Dye->Type == FMcNbt::EType::Compound ? Dye->GetInt(TEXT("rgb")) : Dye->Integer;
								Out.Dye[Slot] = static_cast<int32>(Rgb & 0xFFFFFF);
							}
						}
					}
				}
			}
			return true;
		}
		return false;
	}

	// A moving entity of the game's NBT as the bytes of a chunk's entity save (the kind's name, then what its Serialize
	// writes): a dropped item, a mob the port has, a minecart or a boat. False for any other.
	bool MakeLive(const FMcNbt& Entity, FString& OutType, TArray<uint8>& OutData)
	{
		const FString Id = Entity.GetString(TEXT("id"));
		const FMcNbt* Pos = FindList(Entity, TEXT("Pos"));
		if (!Pos || Pos->Num() != 3)
		{
			return false;
		}
		const FVector Position(Pos->Values[0].GetReal(), Pos->Values[1].GetReal(), Pos->Values[2].GetReal());
		FVector Motion = FVector::ZeroVector;
		if (const FMcNbt* Motions = FindList(Entity, TEXT("Motion")))
		{
			if (Motions->Num() == 3)
			{
				Motion = FVector(Motions->Values[0].GetReal(), Motions->Values[1].GetReal(), Motions->Values[2].GetReal());
			}
		}
		float Yaw = 0.0f, Pitch = 0.0f;
		if (const FMcNbt* Rotation = FindList(Entity, TEXT("Rotation")))
		{
			if (Rotation->Num() == 2)
			{
				Yaw = static_cast<float>(Rotation->Values[0].GetReal());
				Pitch = static_cast<float>(Rotation->Values[1].GetReal());
			}
		}
		const auto Place = [&](FMcEntity& Live)
		{
			Live.Body.Position = Position;
			Live.Body.Velocity = Motion;
			Live.Body.bOnGround = Entity.GetInt(TEXT("OnGround")) != 0;
			Live.Yaw = Yaw;
			Live.Pitch = Pitch;
		};
		TUniquePtr<FMcEntity> Live;
		FMcMob* LiveMob = nullptr;
		if (Id == TEXT("minecraft:item"))
		{
			const FMcNbt* Held = Entity.Find(TEXT("Item"));
			if (!Held)
			{
				return false;
			}
			TUniquePtr<FMcItemEntity> Item = MakeUnique<FMcItemEntity>();
			Item->Stack = MakeStack(*Held);
			if (Item->Stack.IsEmpty())
			{
				return false;
			}
			Item->PickupDelay = static_cast<int32>(Entity.GetInt(TEXT("PickupDelay")));
			Item->Age = static_cast<int32>(Entity.GetInt(TEXT("Age")));
			Live = MoveTemp(Item);
		}
		else if (const FMcMobType* Type = FMcMobType::Find(FName(*Id)))
		{
			// A villager and a zombie villager have saves of their own on top of a mob's.
			FMcVillager* Villager = nullptr;
			TUniquePtr<FMcMob> Mob;
			if (Type->Name == FMcVillager::StaticType())
			{
				Villager = new FMcVillager(*Type);
				Mob.Reset(Villager);
			}
			else if (Type->Name == FMcZombieVillager::StaticType())
			{
				Mob = MakeUnique<FMcZombieVillager>(*Type);
			}
			else
			{
				Mob = MakeUnique<FMcMob>(*Type);
			}
			if (Type->bHops)
			{
				// The game's size counts from 0 (size 1, 2 or 4 is 0, 1 or 3).
				const int32 Wanted = static_cast<int32>(Entity.GetInt(TEXT("Size"))) + 1;
				Mob->SetSize(Wanted >= 4 ? 4 : (Wanted >= 2 ? 2 : 1), false);
			}
			if (const FMcNbt* Health = Entity.Find(TEXT("Health")))
			{
				Mob->Health = FMath::Clamp(static_cast<float>(Health->GetReal()), 1.0f, Type->bHops ? 64.0f : Type->MaxHealth);
			}
			else
			{
				Mob->Health = Type->MaxHealth;
			}
			const int32 EntityAge = static_cast<int32>(Entity.GetInt(TEXT("Age")));
			if (Villager)
			{
				// Type, profession, level and trades as the game keeps them (VillagerData: names, level, and the xp in Xp).
				if (const FMcNbt* Data = Entity.Find(TEXT("VillagerData")))
				{
					const FString Variant = McCache::PathOf(Data->GetString(TEXT("type")));
					if (!Variant.IsEmpty())
					{
						Villager->SetVariant(FName(*Variant));
					}
					const FString Profession = Data->GetString(TEXT("profession"));
					if (!Profession.IsEmpty())
					{
						Villager->SetProfession(Profession.Contains(TEXT(":")) ? FName(*Profession) : FName(*(TEXT("minecraft:") + Profession)));
					}
					Villager->SetLevelAndXp(static_cast<int32>(Data->GetInt(TEXT("level"), 1)), static_cast<int32>(Entity.GetInt(TEXT("Xp"))));
				}
				if (EntityAge < 0)
				{
					Villager->SetBaby(true);
					Villager->SetGrowthTicksLeft(-EntityAge);
				}
				Villager->SetFood(0);
				const FMcNbt* Offers = Entity.Find(TEXT("Offers"));
				const FMcNbt* Recipes = Offers ? FindList(*Offers, TEXT("Recipes")) : nullptr;
				if (Recipes && Recipes->Num() > 0 && Villager->HasProfession())
				{
					TArray<McTrading::FOffer> Imported;
					for (const FMcNbt& Recipe : Recipes->Values)
					{
						McTrading::FOffer Offer;
						if (const FMcNbt* Buy = Recipe.Find(TEXT("buy")))
						{
							Offer.Wants = MakeStack(*Buy);
						}
						if (const FMcNbt* BuyB = Recipe.Find(TEXT("buyB")))
						{
							Offer.Extra = MakeStack(*BuyB);
						}
						if (const FMcNbt* Sell = Recipe.Find(TEXT("sell")))
						{
							Offer.Gives = MakeStack(*Sell);
						}
						Offer.Uses = static_cast<int32>(Recipe.GetInt(TEXT("uses")));
						Offer.MaxUses = static_cast<int32>(Recipe.GetInt(TEXT("maxUses"), 12));
						Offer.Xp = static_cast<int32>(Recipe.GetInt(TEXT("xp"), 1));
						Offer.Demand = static_cast<int32>(Recipe.GetInt(TEXT("demand")));
						if (const FMcNbt* Multiplier = Recipe.Find(TEXT("priceMultiplier")))
						{
							Offer.Discount = static_cast<float>(Multiplier->GetReal());
						}
						if (!Offer.Wants.IsEmpty() && !Offer.Gives.IsEmpty())
						{
							Imported.Add(MoveTemp(Offer));
						}
					}
					if (!Imported.IsEmpty())
					{
						Villager->GetOffers() = MoveTemp(Imported);
					}
				}
			}
			else if (Type->bPanics && EntityAge < 0)
			{
				Mob->MakeBaby();
			}
			Mob->bNoAI = Entity.GetInt(TEXT("NoAI")) != 0;
			if (FMcMob::CanBeBabyZombie(Type->Name) && Entity.GetInt(TEXT("IsBaby")) != 0)
			{
				Mob->SetYoung(FMcMob::BabyForever);
			}
			if (Type->Name == FName(TEXT("minecraft:sheep")) && Entity.Find(TEXT("Color")))
			{
				Mob->DyeColor = FMath::Clamp(static_cast<int32>(Entity.GetInt(TEXT("Color"))), 0, 15);
				Mob->ArmPose = Mob->DyeColor << 8;
			}
			if (const FMcNbt* Name = Entity.Find(TEXT("CustomName")))
			{
				// A text component: a plain string, or a compound with the text in it.
				Mob->CustomName = Name->Type == FMcNbt::EType::String ? Name->String : Name->GetString(TEXT("text"));
			}
			// What it wears and holds: the equipment compound (head, chest, legs, feet, mainhand), or the older ArmorItems
			// (feet first) and HandItems lists.
			const auto ItemNamed = [](const FMcNbt* Slot) -> FName
			{
				const FString Id = Slot ? Slot->GetString(TEXT("id")) : FString();
				return Id.IsEmpty() ? FName() : FName(*Id);
			};
			if (const FMcNbt* Equipment = Entity.Find(TEXT("equipment")))
			{
				Mob->Armor[0] = ItemNamed(Equipment->Find(TEXT("head")));
				Mob->Armor[1] = ItemNamed(Equipment->Find(TEXT("chest")));
				Mob->Armor[2] = ItemNamed(Equipment->Find(TEXT("legs")));
				Mob->Armor[3] = ItemNamed(Equipment->Find(TEXT("feet")));
				const FName Main = ItemNamed(Equipment->Find(TEXT("mainhand")));
				if (!Main.IsNone())
				{
					Mob->HeldItem = Main;
				}
			}
			else
			{
				if (const FMcNbt* Worn = FindList(Entity, TEXT("ArmorItems")))
				{
					for (int32 Slot = 0; Slot < 4 && Slot < Worn->Num(); ++Slot)
					{
						Mob->Armor[3 - Slot] = ItemNamed(&Worn->Values[Slot]);
					}
				}
				if (const FMcNbt* Hands = FindList(Entity, TEXT("HandItems")))
				{
					const FName Main = Hands->Num() > 0 ? ItemNamed(&Hands->Values[0]) : FName();
					if (!Main.IsNone())
					{
						Mob->HeldItem = Main;
					}
				}
			}
			LiveMob = Mob.Get();
			Live = MoveTemp(Mob);
		}
		else if (Id.EndsWith(TEXT("minecart")) && Id != TEXT("minecraft:command_block_minecart") && Id != TEXT("minecraft:spawner_minecart"))
		{
			// The chest, furnace, hopper and TNT carts become plain ones; what they carried is kept in javaentities.
			Live = MakeUnique<FMcMinecartEntity>();
		}
		else if (Id.EndsWith(TEXT("_boat")) || Id.EndsWith(TEXT("_raft")))
		{
			TUniquePtr<FMcBoatEntity> Boat = MakeUnique<FMcBoatEntity>();
			FString Wood = Id;
			for (const TCHAR* Suffix : { TEXT("_chest_boat"), TEXT("_chest_raft"), TEXT("_boat"), TEXT("_raft") })
			{
				if (Wood.RemoveFromEnd(Suffix))
				{
					break;
				}
			}
			Boat->WoodName = FName(*Wood);
			Live = MoveTemp(Boat);
		}
		else
		{
			return false;
		}
		Place(*Live);
		OutType = Live->GetType().ToString();
		FMemoryWriter DataWriter(OutData);
		if (!Live->Serialize(DataWriter))
		{
			return false;
		}
		if (LiveMob)
		{
			// What a world's host keeps after a mob's own bytes (youth, flags, dye, name, gear).
			LiveMob->SaveTrailer(DataWriter);
			// The mob that sits on it (Passengers, the way the game keeps a chicken jockey's zombie), nested the way the host saves one.
			int32 Riders = 0;
			FString RiderType;
			TArray<uint8> RiderData;
			if (const FMcNbt* Passengers = FindList(Entity, TEXT("Passengers")))
			{
				for (const FMcNbt& Passenger : Passengers->Values)
				{
					if (FMcMobType::Find(FName(*Passenger.GetString(TEXT("id")))) && MakeLive(Passenger, RiderType, RiderData))
					{
						Riders = 1;
						break;
					}
					RiderData.Reset();
				}
			}
			DataWriter << Riders;
			if (Riders == 1)
			{
				DataWriter << RiderType << RiderData;
			}
		}
		return true;
	}

	// What an entity file turned into scenery, by chunk: the entities' count and bytes as a chunk's save keeps them.
	struct FSceneryChunk
	{
		int32 Count = 0;
		TArray<uint8> Body;
	};

	// The entities of one region file of entities/ (the game keeps them apart from the blocks) as one NBT file under
	// <dimension>/javaentities: Chunks, a list of { xPos, zPos, entities }. Nothing reads it yet; item frames, paintings,
	// armor stands, villagers and the rest are kept as they were.
	void SaveEntities(const FString& RegionFile, const FString& RegionFolder, int32 RegionX, int32 RegionZ, const FString& OutFolder, McJavaImport::FStats& Stats, TMap<FIntPoint, FSceneryChunk>& Scenery)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *RegionFile))
		{
			return;
		}
		FMcNbt Chunks;
		Chunks.Type = FMcNbt::EType::List;
		for (int32 LocalZ = 0; LocalZ < 32; ++LocalZ)
		{
			for (int32 LocalX = 0; LocalX < 32; ++LocalX)
			{
				TArray<uint8> Raw;
				bool bMissing = false;
				FMcNbt Root;
				if (!McJavaImport::ReadChunkBytes(Bytes, LocalX, LocalZ, RegionFolder, RegionX * 32 + LocalX, RegionZ * 32 + LocalZ, Raw, bMissing)
					|| !FMcNbt::Parse(Raw, Root))
				{
					continue;
				}
				const FMcNbt* Entities = FindList(Root, TEXT("entities"));
				if (!Entities || Entities->Num() == 0)
				{
					continue;
				}
				for (const FMcNbt& Entity : Entities->Values)
				{
					++Stats.Entities.FindOrAdd(Entity.GetString(TEXT("id")));
					McDecor::FDecor Decor;
					FVector Position;
					float Yaw = 0.0f;
					FString LiveType;
					TArray<uint8> LiveData;
					if (MakeLive(Entity, LiveType, LiveData))
					{
						FSceneryChunk& Chunk = Scenery.FindOrAdd(FIntPoint(RegionX * 32 + LocalX, RegionZ * 32 + LocalZ));
						FMemoryWriter BodyWriter(Chunk.Body, false, true);
						BodyWriter << LiveType << LiveData;
						++Chunk.Count;
						++Stats.Scenery;
					}
					else if (MakeDecor(Entity, Decor, Position, Yaw))
					{
						FMcDecorEntity Scenic;
						Scenic.Body.Position = Position;
						Scenic.Yaw = Yaw;
						Scenic.Look = McDecor::Encode(Decor);
						FString Type = FMcDecorEntity::StaticType().ToString();
						TArray<uint8> Data;
						FMemoryWriter DataWriter(Data);
						Scenic.Serialize(DataWriter);
						FSceneryChunk& Chunk = Scenery.FindOrAdd(FIntPoint(RegionX * 32 + LocalX, RegionZ * 32 + LocalZ));
						FMemoryWriter BodyWriter(Chunk.Body, false, true);
						BodyWriter << Type << Data;
						++Chunk.Count;
						++Stats.Scenery;
					}
				}
				FMcNbt Entry;
				Entry.Type = FMcNbt::EType::Compound;
				AddField(Entry, TEXT("xPos"), MakeInt(RegionX * 32 + LocalX));
				AddField(Entry, TEXT("zPos"), MakeInt(RegionZ * 32 + LocalZ));
				AddField(Entry, TEXT("entities"), *Entities);
				Chunks.Values.Add(MoveTemp(Entry));
			}
		}
		if (Chunks.Num() == 0)
		{
			return;
		}
		FMcNbt File;
		File.Type = FMcNbt::EType::Compound;
		AddField(File, TEXT("Chunks"), MoveTemp(Chunks));
		TArray<uint8> Out;
		File.WriteRoot(Out);
		FFileHelper::SaveArrayToFile(Out, *(OutFolder / FString::Printf(TEXT("r.%d.%d.nbt"), RegionX, RegionZ)));
	}

	void AddBlockEntities(const FMcNbt& Root, FMcChunk& Chunk, McJavaImport::FStats& Stats)
	{
		const FMcNbt* List = FindList(Root, TEXT("block_entities"));
		if (!List)
		{
			return;
		}
		for (const FMcNbt& Entity : List->Values)
		{
			const FString Id = Entity.GetString(TEXT("id"));
			const int32 X = static_cast<int32>(Entity.GetInt(TEXT("x"))) & 15;
			const int32 Z = static_cast<int32>(Entity.GetInt(TEXT("z"))) & 15;
			const int32 Y = static_cast<int32>(Entity.GetInt(TEXT("y")));
			if (!Chunk.IsInHeight(Y))
			{
				continue;
			}
			if (const int32 NumSlots = GetContainerSlots(Id))
			{
				FMcBlockEntity Container = FMcBlockEntity::MakeContainer(NumSlots);
				Container.Slots = ReadItems(Entity, NumSlots);
				const FString Loot = Entity.GetString(TEXT("LootTable"));
				if (!Loot.IsEmpty())
				{
					Container.LootTable = FName(*Loot);
					Container.LootSeed = Entity.GetInt(TEXT("LootTableSeed"));
				}
				Chunk.SetBlockEntity(X, Y, Z, MoveTemp(Container));
			}
			else if (Id == TEXT("minecraft:mob_spawner"))
			{
				FString Mob = TEXT("minecraft:pig");
				if (const FMcNbt* Data = Entity.Find(TEXT("SpawnData")))
				{
					if (const FMcNbt* Inner = Data->Find(TEXT("entity")))
					{
						const FString Found = Inner->GetString(TEXT("id"));
						if (!Found.IsEmpty())
						{
							Mob = Found;
						}
					}
				}
				Chunk.SetBlockEntity(X, Y, Z, FMcBlockEntity::MakeSpawner(FName(*Mob)));
			}
			else if (Id == TEXT("minecraft:furnace") || Id == TEXT("minecraft:blast_furnace") || Id == TEXT("minecraft:smoker")
				|| Id == TEXT("minecraft:brewing_stand"))
			{
				const bool bFurnace = Id != TEXT("minecraft:brewing_stand");
				FMcBlockEntity Machine;
				Machine.Kind = bFurnace ? EMcBlockEntityKind::Furnace : EMcBlockEntityKind::BrewingStand;
				Machine.Data = MakeMachineBytes(ReadItems(Entity, bFurnace ? 3 : 5), bFurnace);
				Chunk.SetBlockEntity(X, Y, Z, MoveTemp(Machine));
			}
			else if (Id == TEXT("minecraft:sign") || Id == TEXT("minecraft:hanging_sign"))
			{
				Chunk.SetBlockEntity(X, Y, Z, MakeSign(Entity));
			}
			else if (Id == TEXT("minecraft:decorated_pot"))
			{
				Chunk.SetBlockEntity(X, Y, Z, MakePot(Entity));
			}
			else if (Id == TEXT("minecraft:campfire") || Id == TEXT("minecraft:soul_campfire"))
			{
				FMcBlockEntity Campfire = FMcBlockEntity::MakeCampfire();
				Campfire.Slots = ReadItems(Entity, FMcBlockEntity::NumCampfireSpots);
				const auto ReadInts = [&Entity](const TCHAR* Key, int32* Out)
				{
					const FMcNbt* Array = Entity.Find(Key);
					for (int32 Spot = 0; Array && Array->Type == FMcNbt::EType::IntArray && Spot < FMcBlockEntity::NumCampfireSpots && Spot < Array->Num(); ++Spot)
					{
						Out[Spot] = static_cast<int32>(Array->Values[Spot].Integer);
					}
				};
				ReadInts(TEXT("CookingTimes"), Campfire.CookingTimes);
				ReadInts(TEXT("CookingTotalTimes"), Campfire.CookingTotals);
				Chunk.SetBlockEntity(X, Y, Z, MoveTemp(Campfire));
			}
			else if (Id == TEXT("minecraft:lectern"))
			{
				// Book, the item on it, and Page, the page it lies open at. A lectern without a book has no block entity of ours
				// (one is made when a book is put on).
				const FMcNbt* Book = Entity.Find(TEXT("Book"));
				const FMcItemStack Stack = Book ? MakeStack(*Book) : FMcItemStack();
				if (!Stack.IsEmpty())
				{
					FMcBlockEntity Lectern = FMcBlockEntity::MakeLectern(Stack);
					Lectern.Page = static_cast<int32>(Entity.GetInt(TEXT("Page")));
					Chunk.SetBlockEntity(X, Y, Z, MoveTemp(Lectern));
				}
			}
			else
			{
				// Banners, skulls and the rest: kept whole for when something reads them (skulls have no block entity of
				// ours yet).
				FMcBlockEntity Raw;
				Raw.Kind = EMcBlockEntityKind::Raw;
				Raw.EntityId = FName(*Id);
				Entity.WriteRoot(Raw.Data);
				if (Raw.Data.Num() > (1 << 20))
				{
					++Stats.DroppedBlockEntities.FindOrAdd(Id);
					continue;
				}
				Chunk.SetBlockEntity(X, Y, Z, MoveTemp(Raw));
				++Stats.KeptRawBlockEntities.FindOrAdd(Id);
				continue;
			}
			++Stats.BlockEntities;
		}
	}
}

FName McJavaImport::GetPlaceholderBlock()
{
	return TEXT("minecraft:magenta_concrete");
}

bool McJavaImport::Decompress(uint8 Compression, TConstArrayView<uint8> Data, TArray<uint8>& Out)
{
	Out.Reset();
	if (Compression == 3)
	{
		Out = Data;
		return true;
	}
	if (Compression != 1 && Compression != 2)
	{
		return false;
	}
	if (Compression == 1)
	{
		// A gzip stream ends with the size it inflates to, little endian.
		if (Data.Num() < 18)
		{
			return false;
		}
		const int32 Size = Data[Data.Num() - 4] | (Data[Data.Num() - 3] << 8) | (Data[Data.Num() - 2] << 16) | (Data[Data.Num() - 1] << 24);
		if (Size <= 0 || Size > MaxInflated)
		{
			return false;
		}
		Out.SetNumUninitialized(Size);
		return FCompression::UncompressMemory(NAME_Gzip, Out.GetData(), Size, Data.GetData(), Data.Num());
	}
	// Zlib does not say its size, so inflate in pieces until the stream ends.
	z_stream Stream = {};
	if (inflateInit(&Stream) != Z_OK)
	{
		return false;
	}
	Stream.next_in = const_cast<Bytef*>(Data.GetData());
	Stream.avail_in = static_cast<uInt>(Data.Num());
	TArray<uint8> Chunk;
	Chunk.SetNumUninitialized(64 * 1024);
	int Result = Z_OK;
	while (Result == Z_OK && Out.Num() < MaxInflated)
	{
		Stream.next_out = Chunk.GetData();
		Stream.avail_out = Chunk.Num();
		Result = inflate(&Stream, Z_NO_FLUSH);
		Out.Append(Chunk.GetData(), Chunk.Num() - static_cast<int32>(Stream.avail_out));
	}
	inflateEnd(&Stream);
	if (Result != Z_STREAM_END)
	{
		Out.Reset();
		return false;
	}
	return true;
}

bool McJavaImport::ReadChunkBytes(TConstArrayView<uint8> RegionFile, int32 LocalX, int32 LocalZ, const FString& RegionFolder,
	int32 ChunkX, int32 ChunkZ, TArray<uint8>& OutNbt, bool& bOutMissing)
{
	bOutMissing = true;
	if (RegionFile.Num() < 2 * SectorSize || LocalX < 0 || LocalX > 31 || LocalZ < 0 || LocalZ > 31)
	{
		return false;
	}
	const int32 Entry = ReadInt32(RegionFile, 4 * (LocalX + 32 * LocalZ));
	const int64 Offset = static_cast<int64>((Entry >> 8) & 0xffffff) * SectorSize;
	const int32 Sectors = Entry & 0xff;
	if (Entry == 0 || Sectors == 0)
	{
		return false;
	}
	bOutMissing = false;
	if (Offset < 2 * SectorSize || Offset + 5 > RegionFile.Num())
	{
		return false;
	}
	const int32 Length = ReadInt32(RegionFile, static_cast<int32>(Offset));
	uint8 Compression = RegionFile[static_cast<int32>(Offset) + 4];
	if (Length < 1 || Offset + 4 + Length > RegionFile.Num())
	{
		return false;
	}
	if (Compression & ExternalFlag)
	{
		Compression &= ~ExternalFlag;
		TArray<uint8> External;
		if (!FFileHelper::LoadFileToArray(External, *(RegionFolder / FString::Printf(TEXT("c.%d.%d.mcc"), ChunkX, ChunkZ))))
		{
			return false;
		}
		return Decompress(Compression, External, OutNbt);
	}
	return Decompress(Compression, MakeArrayView(RegionFile.GetData() + Offset + 5, Length - 1), OutNbt);
}

int32 McJavaImport::GetBitsPerEntry(int32 PaletteSize, int32 MinBits)
{
	return FMath::Max(MinBits, static_cast<int32>(FMath::CeilLogTwo(static_cast<uint32>(FMath::Max(PaletteSize, 1)))));
}

bool McJavaImport::UnpackIndices(TConstArrayView<int64> Longs, int32 Bits, int32 Count, TArray<int32>& Out)
{
	Out.Reset();
	if (Bits < 1 || Bits > 32 || Count < 0)
	{
		return false;
	}
	const int32 PerLong = 64 / Bits;
	if (Longs.Num() < (Count + PerLong - 1) / PerLong)
	{
		return false;
	}
	const uint64 Mask = (uint64(1) << Bits) - 1;
	Out.SetNumUninitialized(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const uint64 Word = static_cast<uint64>(Longs[Index / PerLong]);
		Out[Index] = static_cast<int32>((Word >> ((Index % PerLong) * Bits)) & Mask);
	}
	return true;
}

FMcStateId McJavaImport::MapState(const FString& Name, const TMap<FString, FString>& Properties, FStats& Stats, int64 Uses)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FName Block(*(Name.Contains(TEXT(":")) ? Name : TEXT("minecraft:") + Name));
	if (Registry.FindBlock(Block))
	{
		// Only properties the block has: FindState does not look past one it does not know.
		const FMcBlock& BlockInfo = *Registry.FindBlock(Block);
		TMap<FName, FName> Wanted, Fitting;
		for (const TPair<FString, FString>& Pair : Properties)
		{
			Wanted.Add(FName(*Pair.Key), FName(*Pair.Value));
			if (BlockInfo.FindProperty(FName(*Pair.Key)) != INDEX_NONE)
			{
				Fitting.Add(FName(*Pair.Key), FName(*Pair.Value));
			}
		}
		if (const TOptional<FMcStateId> State = Registry.FindState(Block, Fitting))
		{
			// Properties the block does not have (or values it does not allow) are left out by FindState, so check that
			// every property asked for was kept; if not it still is the block, which beats the placeholder.
			for (const TPair<FName, FName>& Pair : Wanted)
			{
				if (Registry.GetPropertyValue(*State, Pair.Key) != Pair.Value)
				{
					Stats.UnknownStates.FindOrAdd(DescribeEntry(Name, Properties) + TEXT(" (properties differ)")) += Uses;
					break;
				}
			}
			return *State;
		}
		if (const TOptional<FMcStateId> Default = Registry.FindDefaultState(Block))
		{
			Stats.UnknownStates.FindOrAdd(DescribeEntry(Name, Properties) + TEXT(" (properties differ)")) += Uses;
			return *Default;
		}
	}
	Stats.UnknownStates.FindOrAdd(DescribeEntry(Name, Properties)) += Uses;
	return Registry.FindDefaultState(GetPlaceholderBlock()).Get(0);
}

bool McJavaImport::GetChunkPos(const FMcNbt& Root, FIntPoint& OutPos)
{
	const FMcNbt* X = Root.Find(TEXT("xPos"));
	const FMcNbt* Z = Root.Find(TEXT("zPos"));
	if (!X || !Z || !X->IsNumber() || !Z->IsNumber())
	{
		return false;
	}
	OutPos = FIntPoint(static_cast<int32>(X->Integer), static_cast<int32>(Z->Integer));
	return true;
}

bool McJavaImport::ConvertChunk(const FMcNbt& Root, FMcChunk& Chunk, FStats& Stats)
{
	const FMcNbt* Sections = FindList(Root, TEXT("sections"));
	if (!Sections)
	{
		return false;
	}
	const FMcBiomeRegistry& Biomes = FMcBiomeRegistry::Get();
	TArray<FMcStateId> States;
	States.SetNumUninitialized(ImportSectionVolume);
	TArray<FMcBiomeId> BiomeCells;
	BiomeCells.Init(Biomes.GetPlains(), 64 * Chunk.NumSections());
	for (const FMcNbt& Section : Sections->Values)
	{
		const int32 Index = static_cast<int32>(Section.GetInt(TEXT("Y"))) - Chunk.GetMinY() / McCoords::SectionSize;
		if (Index < 0 || Index >= Chunk.NumSections())
		{
			continue;
		}
		if (const FMcNbt* BlockStates = Section.Find(TEXT("block_states")))
		{
			const FMcNbt* Palette = FindList(*BlockStates, TEXT("palette"));
			if (Palette && Palette->Num() > 0)
			{
				TArray<int32> Indexes;
				if (Palette->Num() > 1 && !UnpackIndices(GetLongs(BlockStates->Find(TEXT("data"))), GetBitsPerEntry(Palette->Num(), 4), ImportSectionVolume, Indexes))
				{
					continue;
				}
				// How often each entry is used, so unknown ones are counted in blocks and not in sections.
				TArray<int64> Uses;
				Uses.Init(0, Palette->Num());
				if (Palette->Num() == 1)
				{
					Uses[0] = ImportSectionVolume;
				}
				else
				{
					for (int32 Entry : Indexes)
					{
						if (Entry < Palette->Num())
						{
							++Uses[Entry];
						}
					}
				}
				TArray<FMcStateId> Mapped;
				for (int32 Entry = 0; Entry < Palette->Num(); ++Entry)
				{
					FString Name;
					TMap<FString, FString> Properties;
					if (!GetPaletteEntry(Palette->Values[Entry], Name, Properties))
					{
						// Counted, not turned into air without a word.
						Name = TEXT("<unreadable palette entry>");
					}
					Mapped.Add(MapState(Name, Properties, Stats, Uses[Entry]));
				}
				for (int32 Block = 0; Block < ImportSectionVolume; ++Block)
				{
					const int32 Entry = Palette->Num() == 1 ? 0 : Indexes[Block];
					States[Block] = Mapped.IsValidIndex(Entry) ? Mapped[Entry] : Mapped[0];
				}
				Chunk.LoadSection(Index, States);
				Stats.Blocks += ImportSectionVolume;
				Stats.NonAirBlocks += Chunk.GetSection(Index).NonAirCount;
			}
		}
		if (const FMcNbt* BiomeTag = Section.Find(TEXT("biomes")))
		{
			const FMcNbt* Palette = FindList(*BiomeTag, TEXT("palette"));
			if (!Palette || Palette->Num() == 0)
			{
				continue;
			}
			TArray<FMcBiomeId> Mapped;
			for (const FMcNbt& Entry : Palette->Values)
			{
				FString Name;
				TMap<FString, FString> Unused;
				GetPaletteEntry(Entry, Name, Unused);
				Mapped.Add(Biomes.Find(FName(*Name)).Get(Biomes.GetPlains()));
			}
			TArray<int32> Indexes;
			if (Palette->Num() > 1 && !UnpackIndices(GetLongs(BiomeTag->Find(TEXT("data"))), GetBitsPerEntry(Palette->Num(), 1), 64, Indexes))
			{
				continue;
			}
			for (int32 Cell = 0; Cell < 64; ++Cell)
			{
				const int32 Entry = Palette->Num() == 1 ? 0 : Indexes[Cell];
				BiomeCells[Index * 64 + Cell] = Mapped.IsValidIndex(Entry) ? Mapped[Entry] : Mapped[0];
			}
		}
	}
	Chunk.LoadBiomes(BiomeCells);
	AddBlockEntities(Root, Chunk, Stats);
	return true;
}

bool McJavaImport::IsJavaWorld(const FString& Folder)
{
	return IFileManager::Get().FileExists(*(Folder / TEXT("level.dat")));
}

bool McJavaImport::ConvertPlayer(const FMcNbt& Root, FMcPlayerData& Out)
{
	const FMcNbt* Pos = FindList(Root, TEXT("Pos"));
	if (!Pos || Pos->Num() != 3)
	{
		return false;
	}
	Out = FMcPlayerData();
	Out.Position = FVector(Pos->Values[0].GetReal(), Pos->Values[1].GetReal(), Pos->Values[2].GetReal());
	if (Out.Position.ContainsNaN())
	{
		return false;
	}
	if (const FMcNbt* Rotation = FindList(Root, TEXT("Rotation")))
	{
		if (Rotation->Num() >= 2)
		{
			Out.Yaw = static_cast<float>(Rotation->Values[0].GetReal());
			Out.Pitch = static_cast<float>(Rotation->Values[1].GetReal());
		}
	}
	const FString Dimension = Root.GetString(TEXT("Dimension"));
	if (!Dimension.IsEmpty())
	{
		Out.Dimension = Dimension;
	}
	const FMcNbt* Abilities = Root.Find(TEXT("abilities"));
	Out.bFlying = Abilities && Abilities->GetInt(TEXT("flying")) != 0;
	Out.bHasSurvival = true;
	Out.bCreative = Root.GetInt(TEXT("playerGameType")) == 1;
	Out.SelectedSlot = FMath::Clamp(static_cast<int32>(Root.GetInt(TEXT("SelectedItemSlot"))), 0, 8);
	const auto Real = [&Root](const TCHAR* Key, double Default)
	{
		const FMcNbt* Tag = Root.Find(Key);
		return Tag && Tag->IsNumber() ? Tag->GetReal() : Default;
	};
	Out.Stats.Add(TEXT("health"), FMath::Clamp(Real(TEXT("Health"), 20.0), 1.0, 20.0));
	Out.Stats.Add(TEXT("food"), FMath::Clamp(Real(TEXT("foodLevel"), 20.0), 0.0, 20.0));
	Out.Stats.Add(TEXT("saturation"), FMath::Max(0.0, Real(TEXT("foodSaturationLevel"), 5.0)));
	Out.Stats.Add(TEXT("exhaustion"), FMath::Max(0.0, Real(TEXT("foodExhaustionLevel"), 0.0)));
	Out.Stats.Add(TEXT("absorption"), FMath::Max(0.0, Real(TEXT("AbsorptionAmount"), 0.0)));
	Out.Stats.Add(TEXT("xp_level"), FMath::Max(0.0, Real(TEXT("XpLevel"), 0.0)));
	Out.Stats.Add(TEXT("xp_progress"), FMath::Clamp(Real(TEXT("XpP"), 0.0), 0.0, 1.0));
	// The inventory's slots are the game's own numbers (the off hand is -106 there, 150 here); the ender chest's 0 to 26 follow
	// the game's numbering at 200.
	const auto AddItems = [&Out](const FMcNbt* List, int32 Base, bool bInventory)
	{
		if (!List)
		{
			return;
		}
		for (const FMcNbt& Entry : List->Values)
		{
			int32 Slot = static_cast<int32>(Entry.GetInt(TEXT("Slot")));
			if (bInventory)
			{
				Slot = Slot == -106 ? 150 : Slot;
				if (!((Slot >= 0 && Slot <= 35) || (Slot >= 100 && Slot <= 103) || Slot == 150))
				{
					continue;
				}
			}
			else if (Slot < 0 || Slot > 26)
			{
				continue;
			}
			const FMcItemStack Stack = MakeStack(Entry);
			if (!Stack.IsEmpty())
			{
				Out.Slots.Add({ Base + Slot, Stack });
			}
		}
	};
	AddItems(FindList(Root, TEXT("Inventory")), 0, true);
	// The current layout keeps what is worn and the off hand in an equipment compound, not in the inventory list.
	if (const FMcNbt* Equipment = Root.Find(TEXT("equipment")))
	{
		const struct { const TCHAR* Key; int32 Slot; } Pieces[] = { { TEXT("feet"), 100 }, { TEXT("legs"), 101 }, { TEXT("chest"), 102 }, { TEXT("head"), 103 }, { TEXT("offhand"), 150 } };
		for (const auto& Piece : Pieces)
		{
			const FMcNbt* Item = Equipment->Find(Piece.Key);
			const FMcItemStack Stack = Item ? MakeStack(*Item) : FMcItemStack();
			if (!Stack.IsEmpty())
			{
				Out.Slots.RemoveAll([&Piece](const FMcSavedSlot& Saved) { return Saved.Slot == Piece.Slot; });
				Out.Slots.Add({ Piece.Slot, Stack });
			}
		}
	}
	AddItems(FindList(Root, TEXT("EnderItems")), 200, false);
	// The recipe book: the unlocked recipes (value 1), those among them not yet shown (value 2). Exact keys, as the lookup
	// is case-insensitive.
	if (const FMcNbt* Book = Root.Find(TEXT("recipeBook")))
	{
		const FMcNbt* Unlocked = FindList(*Book, TEXT("recipes"));
		const FMcNbt* Display = FindList(*Book, TEXT("toBeDisplayed"));
		TSet<FString> Pending;
		if (Display)
		{
			for (const FMcNbt& Entry : Display->Values)
			{
				Pending.Add(Entry.String);
			}
		}
		if (Unlocked)
		{
			for (const FMcNbt& Entry : Unlocked->Values)
			{
				const FString Id = Entry.String;
				if (!Id.IsEmpty())
				{
					Out.Stats.Add(TEXT("recipe:") + Id, Pending.Contains(Id) ? 2.0 : 1.0);
				}
			}
		}
	}
	return true;
}

namespace
{
	// "2026-11-13 07:00:15 +0100" as Unix seconds; false for any other text.
	bool ParseAdvancementDate(const FString& Text, double& OutSeconds)
	{
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(" "));
		TArray<FString> Day, Clock;
		if (Parts.Num() < 3)
		{
			return false;
		}
		Parts[0].ParseIntoArray(Day, TEXT("-"));
		Parts[1].ParseIntoArray(Clock, TEXT(":"));
		if (Day.Num() != 3 || Clock.Num() != 3 || Parts[2].Len() != 5 || !FDateTime::Validate(FCString::Atoi(*Day[0]), FCString::Atoi(*Day[1]), FCString::Atoi(*Day[2]), FCString::Atoi(*Clock[0]), FCString::Atoi(*Clock[1]), FCString::Atoi(*Clock[2]), 0))
		{
			return false;
		}
		const TCHAR Sign = Parts[2][0];
		const int32 Zone = FCString::Atoi(*Parts[2].Mid(1));
		const int32 Y = FCString::Atoi(*Day[0]), Mo = FCString::Atoi(*Day[1]), D = FCString::Atoi(*Day[2]);
		const int32 H = FCString::Atoi(*Clock[0]), Mi = FCString::Atoi(*Clock[1]), S = FCString::Atoi(*Clock[2]);
		const int32 OffsetMinutes = (Zone / 100) * 60 + Zone % 100;
		OutSeconds = static_cast<double>(FDateTime(Y, Mo, D, H, Mi, S).ToUnixTimestamp()) - (Sign == TEXT('-') ? -1 : 1) * OffsetMinutes * 60.0;
		return true;
	}
}

bool McJavaImport::ConvertAdvancements(const FString& Json, FMcPlayerData& Out)
{
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
	{
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>> Entry : Root->Values)
	{
		const TSharedPtr<FJsonObject>* Made = nullptr;
		if (!Entry.Value.IsValid() || !Entry.Value->TryGetObject(Made))
		{
			continue;
		}
		const FString Id = Entry.Key.Contains(TEXT(":")) ? Entry.Key : TEXT("minecraft:") + Entry.Key;
		bool bDone = false;
		(*Made)->TryGetBoolField(TEXT("done"), bDone);
		if (bDone)
		{
			Out.Stats.Add(TEXT("advancement:") + Id, 1.0);
		}
		const TSharedPtr<FJsonObject>* Criteria = nullptr;
		if ((*Made)->TryGetObjectField(TEXT("criteria"), Criteria))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>> Criterion : (*Criteria)->Values)
			{
				const FString Key = Id + TEXT("/") + Criterion.Key;
				Out.Stats.Add(TEXT("criterion:") + Key, 1.0);
				double Seconds = 0.0;
				FString Date;
				if (Criterion.Value.IsValid() && Criterion.Value->TryGetString(Date) && ParseAdvancementDate(Date, Seconds))
				{
					Out.Stats.Add(TEXT("ctime:") + Key, Seconds);
				}
			}
		}
	}
	return true;
}

bool McJavaImport::ConvertStats(const FString& Json, FMcPlayerData& Out)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Categories = nullptr;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid() || !Root->TryGetObjectField(TEXT("stats"), Categories))
	{
		return false;
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>> Category : (*Categories)->Values)
	{
		const TSharedPtr<FJsonObject>* Counts = nullptr;
		if (!Category.Value.IsValid() || !Category.Value->TryGetObject(Counts))
		{
			continue;
		}
		FString Kind = Category.Key;
		Kind.RemoveFromStart(TEXT("minecraft:"));
		for (const TPair<FString, TSharedPtr<FJsonValue>> Count : (*Counts)->Values)
		{
			double Value = 0.0;
			if (!Count.Value.IsValid() || !Count.Value->TryGetNumber(Value))
			{
				continue;
			}
			FString Name = Count.Key;
			if (Kind == TEXT("custom"))
			{
				Name.RemoveFromStart(TEXT("minecraft:"));
			}
			Out.Stats.Add(TEXT("stat:") + Kind + TEXT(":") + Name, Value);
		}
	}
	return true;
}

bool McJavaImport::ImportPlayer(const FString& Source, const FString& WorldDir)
{
	// Every player of the folder gets a file of their own, named by their UUID (the join looks for it: McWorldSave::LoadPlayer);
	// the one who played last is also "imported-player", which whoever has no file takes.
	const FString DataFolder = Source / TEXT("players") / TEXT("data");
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(DataFolder / TEXT("*.dat")), true, false);
	FString Newest;
	FDateTime NewestTime = FDateTime::MinValue();
	for (const FString& File : Files)
	{
		const FDateTime Time = IFileManager::Get().GetTimeStamp(*(DataFolder / File));
		if (Time > NewestTime)
		{
			NewestTime = Time;
			Newest = File;
		}
	}

	FMcPlayerData Last;
	bool bHaveLast = false;
	for (const FString& File : Files)
	{
		FMcNbt Root;
		FMcPlayerData Player;
		if (!FMcNbt::LoadFile(DataFolder / File, Root) || !ConvertPlayer(Root, Player))
		{
			continue;
		}
		Player.Uuid = FPaths::GetBaseFilename(File);
		// Their advancements and statistics, with the game's dates.
		FString Text;
		if (FFileHelper::LoadFileToString(Text, *(Source / TEXT("players") / TEXT("advancements") / (Player.Uuid + TEXT(".json")))))
		{
			ConvertAdvancements(Text, Player);
		}
		if (FFileHelper::LoadFileToString(Text, *(Source / TEXT("players") / TEXT("stats") / (Player.Uuid + TEXT(".json")))))
		{
			ConvertStats(Text, Player);
		}
		McWorldSave::SavePlayer(WorldDir, Player.Uuid, Player);
		if (File == Newest)
		{
			Last = Player;
			bHaveLast = true;
		}
	}
	if (!bHaveLast)
	{
		// The older layout kept the one player in level.dat.
		FMcNbt Level;
		const FMcNbt* Data = FMcNbt::LoadFile(Source / TEXT("level.dat"), Level) ? Level.Find(TEXT("Data")) : nullptr;
		const FMcNbt* Old = Data ? Data->Find(TEXT("Player")) : nullptr;
		bHaveLast = Old && ConvertPlayer(*Old, Last);
	}
	return bHaveLast && McWorldSave::SavePlayer(WorldDir, TEXT("imported-player"), Last);
}

bool McJavaImport::ReadLevel(const FString& Folder, FMcLevelData& OutLevel)
{
	FMcNbt Level;
	if (!FMcNbt::LoadFile(Folder / TEXT("level.dat"), Level))
	{
		return false;
	}
	const FMcNbt* Data = Level.Find(TEXT("Data"));
	if (!Data)
	{
		return false;
	}
	OutLevel.Name = Data->GetString(TEXT("LevelName"));
	OutLevel.GameMode = Data->GetInt(TEXT("GameType")) == 1 ? TEXT("creative") : TEXT("survival");
	OutLevel.DayTime = static_cast<double>(Data->GetInt(TEXT("DayTime"), Data->GetInt(TEXT("Time"))));
	// 26.3 keeps the time of day in data/minecraft/world_clocks.dat (the overworld's total_ticks), not in level.dat.
	FMcNbt Clocks;
	if (FMcNbt::LoadFile(Folder / TEXT("data") / TEXT("minecraft") / TEXT("world_clocks.dat"), Clocks))
	{
		const FMcNbt* ClockData = Clocks.Find(TEXT("data"));
		const FMcNbt* Overworld = ClockData ? ClockData->Find(TEXT("minecraft:overworld")) : nullptr;
		if (Overworld && Overworld->Find(TEXT("total_ticks")))
		{
			OutLevel.DayTime = static_cast<double>(Overworld->GetInt(TEXT("total_ticks")));
		}
	}
	// The spawn: a `spawn` compound with a `pos` int array, or the older SpawnX, SpawnY and SpawnZ.
	if (const FMcNbt* Spawn = Data->Find(TEXT("spawn")))
	{
		const FMcNbt* Pos = Spawn->Find(TEXT("pos"));
		if (Pos && Pos->Type == FMcNbt::EType::IntArray && Pos->Num() >= 3)
		{
			OutLevel.Spawn = FIntVector(static_cast<int32>(Pos->Values[0].Integer), static_cast<int32>(Pos->Values[1].Integer), static_cast<int32>(Pos->Values[2].Integer));
		}
		const FString Dimension = Spawn->GetString(TEXT("dimension"));
		if (!Dimension.IsEmpty())
		{
			OutLevel.Dimension = Dimension;
		}
	}
	else
	{
		OutLevel.Spawn = FIntVector(static_cast<int32>(Data->GetInt(TEXT("SpawnX"))), static_cast<int32>(Data->GetInt(TEXT("SpawnY"))), static_cast<int32>(Data->GetInt(TEXT("SpawnZ"))));
	}
	if (const FMcNbt* Difficulty = Data->Find(TEXT("difficulty_settings")))
	{
		static const TCHAR* Names[] = { TEXT("peaceful"), TEXT("easy"), TEXT("normal"), TEXT("hard") };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (Difficulty->GetString(TEXT("difficulty")) == Names[Index])
			{
				OutLevel.Difficulty = Index;
			}
		}
	}
	else if (Data->Find(TEXT("Difficulty")))
	{
		OutLevel.Difficulty = static_cast<int32>(Data->GetInt(TEXT("Difficulty"), 2));
	}
	// The seed: world_gen_settings.dat in the current layout, in level.dat before.
	FMcNbt Settings;
	if (FMcNbt::LoadFile(Folder / TEXT("data") / TEXT("minecraft") / TEXT("world_gen_settings.dat"), Settings))
	{
		if (const FMcNbt* Inner = Settings.Find(TEXT("data")))
		{
			OutLevel.Seed = Inner->GetInt(TEXT("seed"));
		}
	}
	else if (const FMcNbt* Old = Data->Find(TEXT("WorldGenSettings")))
	{
		OutLevel.Seed = Old->GetInt(TEXT("seed"));
	}
	else
	{
		OutLevel.Seed = Data->GetInt(TEXT("RandomSeed"));
	}
	FMcNbt Rules;
	if (FMcNbt::LoadFile(Folder / TEXT("data") / TEXT("minecraft") / TEXT("game_rules.dat"), Rules))
	{
		if (const FMcNbt* Inner = Rules.Find(TEXT("data")))
		{
			for (int32 Index = 0; Index < Inner->Num(); ++Index)
			{
				const FMcNbt& Value = Inner->Values[Index];
				FString Key = Inner->Keys[Index];
				Key.RemoveFromStart(TEXT("minecraft:"));
				OutLevel.GameRules.Add(Key, Value.Type == FMcNbt::EType::String ? Value.String
					: Value.Type == FMcNbt::EType::Byte ? (Value.Integer ? TEXT("true") : TEXT("false")) : FString::Printf(TEXT("%lld"), Value.Integer));
			}
			FMcGameRules::MigrateLegacyNames(OutLevel.GameRules);
		}
	}
	return true;
}

namespace
{
	enum class EChunkResult : uint8
	{
		Missing,
		Failed,
		Done,
	};

	// One chunk of a region file already in memory, as a save (McChunkSave) with the scenery its chunk had.
	EChunkResult ConvertToSave(TConstArrayView<uint8> Region, const FString& RegionFolder, FIntPoint Pos, const FMcDimensionType& Type,
		const TMap<FIntPoint, FSceneryChunk>& Scenery, McJavaImport::FStats& Stats, TArray<uint8>& OutSave)
	{
		TArray<uint8> Raw;
		bool bMissing = false;
		if (!McJavaImport::ReadChunkBytes(Region, Pos.X & 31, Pos.Y & 31, RegionFolder, Pos.X, Pos.Y, Raw, bMissing))
		{
			return bMissing ? EChunkResult::Missing : EChunkResult::Failed;
		}
		FMcNbt Root;
		FMcChunk Chunk(Pos, Type.MinY, Type.NumSections());
		Chunk.FillBiome(FMcBiomeRegistry::Get().GetPlains());
		if (!FMcNbt::Parse(Raw, Root) || !McJavaImport::ConvertChunk(Root, Chunk, Stats))
		{
			return EChunkResult::Failed;
		}
		FMcChunkSaveExtras Extras;
		Extras.bPopulated = true;
		if (const FSceneryChunk* Here = Scenery.Find(Pos))
		{
			FMemoryWriter CountWriter(Extras.Entities);
			int32 Count = Here->Count;
			CountWriter << Count;
			Extras.Entities.Append(Here->Body);
		}
		OutSave = McChunkSave::Encode(Chunk, Extras);
		++Stats.Chunks;
		return EChunkResult::Done;
	}

	// Every chunk of one region file into Storage, 32 rows at once on the thread pool. A chunk the storage has already (a
	// world picked up again, or one the game asked for) is left alone when bSkipSaved. Returns the chunks the file holds.
	int32 ImportRegion(const FString& RegionFolder, const FString& File, int32 RegionX, int32 RegionZ, const FMcDimensionType& Type,
		const TMap<FIntPoint, FSceneryChunk>& Scenery, FMcWorldStorage& Storage, bool bSkipSaved, const TAtomic<bool>* Stop, McJavaImport::FStats& Stats)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *(RegionFolder / File)))
		{
			return 0;
		}
		++Stats.Regions;
		TArray<McJavaImport::FStats> Rows;
		Rows.SetNum(32);
		TArray<int32> Held;
		Held.Init(0, 32);
		TBitArray<> Saved;
		if (bSkipSaved)
		{
			Storage.GetSavedMask(FIntPoint(RegionX, RegionZ), Saved);
		}
		ParallelFor(32, [&](int32 LocalZ)
		{
			for (int32 LocalX = 0; LocalX < 32; ++LocalX)
			{
				if (Stop && *Stop)
				{
					return;
				}
				const FIntPoint Pos(RegionX * 32 + LocalX, RegionZ * 32 + LocalZ);
				if (bSkipSaved && Saved[LocalX + 32 * LocalZ])
				{
					++Held[LocalZ];
					continue;
				}
				TArray<uint8> Save;
				switch (ConvertToSave(Bytes, RegionFolder, Pos, Type, Scenery, Rows[LocalZ], Save))
				{
				case EChunkResult::Done:
					++Held[LocalZ];
					if (bSkipSaved)
					{
						Storage.QueueSaveIfAbsent(Pos, MoveTemp(Save));
					}
					else
					{
						Storage.QueueSave(Pos, MoveTemp(Save));
					}
					break;
				case EChunkResult::Failed:
					++Held[LocalZ];
					++Rows[LocalZ].ChunksFailed;
					break;
				default:
					break;
				}
			}
		});
		int32 Total = 0;
		for (int32 Row = 0; Row < 32; ++Row)
		{
			McJavaImport::AddStats(Stats, Rows[Row]);
			Total += Held[Row];
		}
		return Total;
	}

	// r.<x>.<z>.mca -> x, z.
	bool ParseRegionName(const FString& File, int32& OutX, int32& OutZ)
	{
		TArray<FString> Parts;
		File.ParseIntoArray(Parts, TEXT("."));
		if (Parts.Num() != 4 || Parts[0] != TEXT("r"))
		{
			return false;
		}
		OutX = FCString::Atoi(*Parts[1]);
		OutZ = FCString::Atoi(*Parts[2]);
		return true;
	}

	// The entities/ files of a dimension into scenery by chunk (and the javaentities files).
	void CollectEntities(const FString& RegionFolder, const FString& OutFolder, McJavaImport::FStats& Stats, TMap<FIntPoint, FSceneryChunk>& Scenery)
	{
		// entities/ is next to region/ (in both layouts).
		const FString EntityFolder = FPaths::GetPath(RegionFolder) / TEXT("entities");
		TArray<FString> EntityFiles;
		IFileManager::Get().FindFiles(EntityFiles, *(EntityFolder / TEXT("*.mca")), true, false);
		for (const FString& File : EntityFiles)
		{
			int32 X = 0, Z = 0;
			if (ParseRegionName(File, X, Z))
			{
				SaveEntities(EntityFolder / File, EntityFolder, X, Z, OutFolder, Stats, Scenery);
			}
		}
	}

	FString GetMarkerPath(const FString& WorldDir)
	{
		return WorldDir / TEXT("javaimport.txt");
	}

	// A new world's folder for a Java world, its level read into OutLevel; empty if it can't be.
	FString MakeWorldFolder(const FString& Source, const FString& Name, FMcLevelData& OutLevel)
	{
		if (!FMcBlockRegistry::Get().EnsureLoaded() || !FMcBiomeRegistry::Get().EnsureLoaded() || !McJavaImport::IsJavaWorld(Source))
		{
			UE_LOG(LogMcServer, Error, TEXT("Cannot import '%s': the registries did not load or it has no level.dat."), *Source);
			return FString();
		}
		if (!McJavaImport::ReadLevel(Source, OutLevel))
		{
			UE_LOG(LogMcServer, Error, TEXT("Cannot import '%s': level.dat is unreadable."), *Source);
			return FString();
		}
		if (!Name.IsEmpty())
		{
			OutLevel.Name = Name;
		}
		if (OutLevel.Name.IsEmpty())
		{
			OutLevel.Name = FPaths::GetCleanFilename(Source);
		}
		return McWorldSave::MakeNewFolder(OutLevel.Name);
	}
}

void McJavaImport::AddStats(FStats& Stats, const FStats& Other)
{
	Stats.Regions += Other.Regions;
	Stats.Chunks += Other.Chunks;
	Stats.ChunksFailed += Other.ChunksFailed;
	Stats.Blocks += Other.Blocks;
	Stats.NonAirBlocks += Other.NonAirBlocks;
	Stats.Scenery += Other.Scenery;
	Stats.BlockEntities += Other.BlockEntities;
	for (const TPair<FString, int64>& Pair : Other.UnknownStates)
	{
		Stats.UnknownStates.FindOrAdd(Pair.Key) += Pair.Value;
	}
	for (const TPair<FString, int32>& Pair : Other.DroppedBlockEntities)
	{
		Stats.DroppedBlockEntities.FindOrAdd(Pair.Key) += Pair.Value;
	}
	for (const TPair<FString, int32>& Pair : Other.KeptRawBlockEntities)
	{
		Stats.KeptRawBlockEntities.FindOrAdd(Pair.Key) += Pair.Value;
	}
	for (const TPair<FString, int32>& Pair : Other.Entities)
	{
		Stats.Entities.FindOrAdd(Pair.Key) += Pair.Value;
	}
}

FString McJavaImport::ImportWorld(const FString& Source, const FString& Name, FStats& OutStats)
{
	FMcLevelData Level;
	const FString Folder = MakeWorldFolder(Source, Name, Level);
	if (Folder.IsEmpty())
	{
		return FString();
	}
	const FString WorldDir = McWorldSave::GetWorldDir(Folder);
	const double Start = FPlatformTime::Seconds();
	for (const TCHAR* Dimension : { TEXT("minecraft:overworld"), TEXT("minecraft:the_nether"), TEXT("minecraft:the_end") })
	{
		const FString RegionFolder = FindRegionFolder(Source, Dimension);
		FMcDimensionType Type;
		if (RegionFolder.IsEmpty() || !FMcDimensionType::Load(Dimension, Type))
		{
			continue;
		}
		FMcWorldStorage Storage(WorldDir / McWorldSave::GetDimensionFolder(Dimension));
		Storage.SetBulk(true);
		TMap<FIntPoint, FSceneryChunk> Scenery;
		const double EntitiesStart = FPlatformTime::Seconds();
		CollectEntities(RegionFolder, WorldDir / McWorldSave::GetDimensionFolder(Dimension) / TEXT("javaentities"), OutStats, Scenery);
		UE_LOG(LogMcServer, Display, TEXT("Import %s: entities %.1f s"), Dimension, FPlatformTime::Seconds() - EntitiesStart);

		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(RegionFolder / TEXT("*.mca")), true, false);
		for (const FString& File : Names)
		{
			int32 RegionX = 0, RegionZ = 0;
			if (!ParseRegionName(File, RegionX, RegionZ))
			{
				continue;
			}
			const double RegionStart = FPlatformTime::Seconds();
			const int32 Held = ImportRegion(RegionFolder, File, RegionX, RegionZ, Type, Scenery, Storage, false, nullptr, OutStats);
			UE_LOG(LogMcServer, Display, TEXT("Import %s %s: %d chunks, %.2f s"), Dimension, *File, Held, FPlatformTime::Seconds() - RegionStart);
		}
		Storage.Flush();
	}
	if (!McWorldSave::SaveLevel(WorldDir, Level))
	{
		UE_LOG(LogMcServer, Error, TEXT("Could not write %s."), *WorldDir);
		return FString();
	}
	ImportPlayer(Source, WorldDir);
	UE_LOG(LogMcServer, Log, TEXT("Imported '%s' as '%s' in %.1f s: %s"), *Source, *Folder, FPlatformTime::Seconds() - Start, *Describe(OutStats));
	return Folder;
}

FString McJavaImport::BeginImport(const FString& Source, const FString& Name)
{
	FMcLevelData Level;
	const FString Folder = MakeWorldFolder(Source, Name, Level);
	if (Folder.IsEmpty())
	{
		return FString();
	}
	const FString WorldDir = McWorldSave::GetWorldDir(Folder);
	// The game reads this when it opens the world; the chunks are made from the folder it names, read only.
	if (!McWorldSave::SaveLevel(WorldDir, Level) || !FFileHelper::SaveStringToFile(FPaths::ConvertRelativePathToFull(Source), *GetMarkerPath(WorldDir)))
	{
		UE_LOG(LogMcServer, Error, TEXT("Could not write %s."), *WorldDir);
		return FString();
	}
	ImportPlayer(Source, WorldDir);
	UE_LOG(LogMcServer, Log, TEXT("Began importing '%s' as '%s'; its chunks come in while it is played."), *Source, *Folder);
	return Folder;
}

// ---- The import that goes on while the world is played ----

struct McJavaImport::FLiveState
{
	FString Source, RegionFolder, WorldDir, DimensionFolder, Dimension;
	FMcDimensionType Type;
	TSharedPtr<FMcWorldStorage, ESPMode::ThreadSafe> Storage;
	FIntPoint SpawnChunk = FIntPoint::ZeroValue;
	TMap<FIntPoint, FSceneryChunk> Scenery;
	FEventRef Ready{ EEventMode::ManualReset };
	TAtomic<bool> bStop{ false };
	TAtomic<bool> bFinished{ false };
	TAtomic<int32> Done{ 0 };
	TAtomic<int32> Total{ 0 };
	McJavaImport::FStats Stats;
	FCriticalSection CacheLock;
	TArray<TPair<FIntPoint, TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe>>> Cache;
	TUniquePtr<FThread> Thread;

	FString GetDonePath() const { return WorldDir / DimensionFolder / TEXT("javaimport.done"); }

	// A region file's bytes, the last few kept so a walk across one doesn't read it again for each chunk.
	TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> GetRegion(FIntPoint Region)
	{
		{
			FScopeLock Guard(&CacheLock);
			for (const TPair<FIntPoint, TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe>>& Entry : Cache)
			{
				if (Entry.Key == Region)
				{
					return Entry.Value;
				}
			}
		}
		TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> Bytes = MakeShared<TArray<uint8>, ESPMode::ThreadSafe>();
		if (!FFileHelper::LoadFileToArray(*Bytes, *(RegionFolder / FString::Printf(TEXT("r.%d.%d.mca"), Region.X, Region.Y))))
		{
			return nullptr;
		}
		FScopeLock Guard(&CacheLock);
		if (Cache.Num() >= 6)
		{
			Cache.RemoveAt(0);
		}
		Cache.Emplace(Region, Bytes);
		return Bytes;
	}

	bool MakeChunk(FIntPoint Pos, TArray<uint8>& OutSave)
	{
		Ready->Wait();
		const TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> Region = GetRegion(FIntPoint(Pos.X >> 5, Pos.Y >> 5));
		McJavaImport::FStats Unused;
		return Region && ConvertToSave(*Region, RegionFolder, Pos, Type, Scenery, Unused, OutSave) == EChunkResult::Done;
	}

	void Run()
	{
		const double Start = FPlatformTime::Seconds();
		CollectEntities(RegionFolder, WorldDir / DimensionFolder / TEXT("javaentities"), Stats, Scenery);
		Ready->Trigger();

		struct FRegion
		{
			FString File;
			int32 X = 0, Z = 0, Chunks = 0;
			int64 Distance = 0;
		};
		TArray<FRegion> Regions;
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(RegionFolder / TEXT("*.mca")), true, false);
		for (const FString& File : Names)
		{
			FRegion Region;
			Region.File = File;
			if (!ParseRegionName(File, Region.X, Region.Z))
			{
				continue;
			}
			// The chunks the file holds, from its table of locations alone.
			TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*(RegionFolder / File)));
			if (Reader && Reader->TotalSize() >= 4096)
			{
				TArray<uint8> Table;
				Table.SetNumUninitialized(4096);
				Reader->Serialize(Table.GetData(), Table.Num());
				for (int32 Slot = 0; Slot < 1024; ++Slot)
				{
					Region.Chunks += (Table[Slot * 4] | Table[Slot * 4 + 1] | Table[Slot * 4 + 2] | Table[Slot * 4 + 3]) != 0;
				}
			}
			const int64 DX = Region.X * 32 + 16 - SpawnChunk.X, DZ = Region.Z * 32 + 16 - SpawnChunk.Y;
			Region.Distance = DX * DX + DZ * DZ;
			Total += Region.Chunks;
			Regions.Add(MoveTemp(Region));
		}
		Regions.Sort([](const FRegion& A, const FRegion& B) { return A.Distance < B.Distance; });
		for (const FRegion& Region : Regions)
		{
			// Not so far ahead of the disk that the queue eats memory.
			while (!bStop && Storage->NumQueued() > 3000)
			{
				FPlatformProcess::Sleep(0.02f);
			}
			if (bStop)
			{
				break;
			}
			const double RegionStart = FPlatformTime::Seconds();
			const int32 Held = ImportRegion(RegionFolder, Region.File, Region.X, Region.Z, Type, Scenery, *Storage, true, &bStop, Stats);
			Done += Region.Chunks;
			UE_LOG(LogMcServer, Display, TEXT("Import %s %s: %d chunks, %.2f s"), *Dimension, *Region.File, Held, FPlatformTime::Seconds() - RegionStart);
		}
		if (!bStop)
		{
			Storage->Flush();
			Storage->SetBulk(false);
			FFileHelper::SaveStringToFile(TEXT("done"), *GetDonePath());
			bFinished = true;
			UE_LOG(LogMcServer, Log, TEXT("Imported the %s of '%s' in %.1f s: %s"), *Dimension, *Source, FPlatformTime::Seconds() - Start, *McJavaImport::Describe(Stats));
		}
	}
};

namespace
{
	FCriticalSection LiveLock;
	TArray<TWeakPtr<McJavaImport::FLiveState, ESPMode::ThreadSafe>> LiveStates;
}

McJavaImport::FLiveImport::FLiveImport() = default;

McJavaImport::FLiveImport::~FLiveImport()
{
	Stop();
}

TSharedPtr<McJavaImport::FLiveImport, ESPMode::ThreadSafe> McJavaImport::FLiveImport::Start(const FString& WorldDir, const FString& Dimension,
	TSharedPtr<FMcWorldStorage, ESPMode::ThreadSafe> Storage, FIntPoint SpawnChunk)
{
	FString Source;
	if (!Storage || !FFileHelper::LoadFileToString(Source, *GetMarkerPath(WorldDir)))
	{
		return nullptr;
	}
	Source = Source.TrimStartAndEnd();
	const FString RegionFolder = FindRegionFolder(Source, Dimension);
	TSharedRef<FLiveState, ESPMode::ThreadSafe> State = MakeShared<FLiveState, ESPMode::ThreadSafe>();
	if (RegionFolder.IsEmpty() || !FMcDimensionType::Load(Dimension, State->Type)
		|| IFileManager::Get().FileExists(*(WorldDir / McWorldSave::GetDimensionFolder(Dimension) / TEXT("javaimport.done"))))
	{
		return nullptr;
	}
	State->Source = Source;
	State->RegionFolder = RegionFolder;
	State->WorldDir = WorldDir;
	State->DimensionFolder = McWorldSave::GetDimensionFolder(Dimension);
	State->Dimension = Dimension;
	State->Storage = Storage;
	Storage->SetBulk(true);
	State->SpawnChunk = SpawnChunk;
	TSharedPtr<FLiveImport, ESPMode::ThreadSafe> Import = MakeShareable(new FLiveImport());
	Import->State = State;
	{
		FScopeLock Guard(&LiveLock);
		LiveStates.Add(State);
	}
	Storage->SetChunkSource([State](FIntPoint Pos, TArray<uint8>& OutBytes)
	{
		if (!State->MakeChunk(Pos, OutBytes))
		{
			return false;
		}
		State->Storage->QueueSaveIfAbsent(Pos, TArray<uint8>(OutBytes));
		return true;
	});
	State->Thread = MakeUnique<FThread>(TEXT("McJavaImport"), [State]() { State->Run(); });
	return Import;
}

void McJavaImport::FLiveImport::Stop()
{
	if (!State)
	{
		return;
	}
	State->bStop = true;
	if (State->Thread)
	{
		// The thread may be waiting for the entities nobody will read: let it through.
		State->Ready->Trigger();
		State->Thread->Join();
		State->Thread.Reset();
	}
	if (State->Storage)
	{
		State->Storage->SetChunkSource(nullptr);
		State->Storage->SetBulk(false);
	}
}

bool McJavaImport::FLiveImport::IsFinished() const
{
	return !State || State->bFinished;
}

bool McJavaImport::FLiveImport::GetProgress(int32& OutDone, int32& OutTotal)
{
	OutDone = 0;
	OutTotal = 0;
	FScopeLock Guard(&LiveLock);
	for (int32 Index = LiveStates.Num() - 1; Index >= 0; --Index)
	{
		if (const TSharedPtr<FLiveState, ESPMode::ThreadSafe> Live = LiveStates[Index].Pin())
		{
			OutDone += Live->Done;
			OutTotal += Live->Total;
		}
		else
		{
			LiveStates.RemoveAt(Index);
		}
	}
	return OutTotal > 0;
}

bool McJavaImport::GetSceneryLine(const FMcNbt& Entity, FString& OutLine, FVector& OutPos, float& OutYaw)
{
	McDecor::FDecor Decor;
	if (!MakeDecor(Entity, Decor, OutPos, OutYaw))
	{
		return false;
	}
	OutLine = McDecor::Encode(Decor);
	return true;
}

int32 McJavaImport::StandBanners(const FMcChunk& Chunk, FMcEntityManager& Manager)
{
	int32 Stood = 0;
	for (const TPair<FIntVector, FMcBlockEntity>& Pair : Chunk.GetBlockEntities())
	{
		const FMcBlockEntity& Entity = Pair.Value;
		if (Entity.Kind != EMcBlockEntityKind::Raw || Entity.EntityId != TEXT("minecraft:banner"))
		{
			continue;
		}
		const FIntVector Pos(Chunk.GetPos().X * McCoords::SectionSize + Pair.Key.X, Pair.Key.Y, Chunk.GetPos().Y * McCoords::SectionSize + Pair.Key.Z);
		if (FMcBannerEntity::FindAt(Manager, Pos))
		{
			continue;
		}
		TArray<McBanner::FLayer> Layers;
		FMcNbt Root;
		if (FMcNbt::Parse(Entity.Data, Root))
		{
			if (const FMcNbt* Patterns = FindList(Root, TEXT("patterns")))
			{
				for (const FMcNbt& Entry : Patterns->Values)
				{
					// A pattern written out in full (a data-pack one) has no name here.
					const FString Pattern = Entry.GetString(TEXT("pattern"));
					const FString Colour = Entry.GetString(TEXT("color"));
					if (!Pattern.IsEmpty() && !Colour.IsEmpty())
					{
						Layers.Add({ McCache::PathOf(Pattern), Colour });
					}
				}
			}
		}
		if (FMcBannerEntity::Stand(Manager, Pos, Layers))
		{
			++Stood;
		}
	}
	return Stood;
}

FString McJavaImport::Describe(const FStats& Stats)
{
	FString Out = FString::Printf(TEXT("%d regions, %d chunks (%d unreadable), %lld blocks, %d block entities kept"),
		Stats.Regions, Stats.Chunks, Stats.ChunksFailed, Stats.Blocks, Stats.BlockEntities);
	for (const TPair<FString, int64>& Unknown : Stats.UnknownStates)
	{
		Out += FString::Printf(TEXT("; unknown %s x%lld"), *Unknown.Key, Unknown.Value);
	}
	for (const TPair<FString, int32>& Kept : Stats.KeptRawBlockEntities)
	{
		Out += FString::Printf(TEXT("; kept raw %s x%d"), *Kept.Key, Kept.Value);
	}
	if (Stats.Scenery > 0)
	{
		Out += FString::Printf(TEXT("; %d frames, paintings and armor stands drawn"), Stats.Scenery);
	}
	for (const TPair<FString, int32>& Entity : Stats.Entities)
	{
		Out += FString::Printf(TEXT("; saved entity %s x%d"), *Entity.Key, Entity.Value);
	}
	for (const TPair<FString, int32>& Dropped : Stats.DroppedBlockEntities)
	{
		Out += FString::Printf(TEXT("; dropped %s x%d"), *Dropped.Key, Dropped.Value);
	}
	return Out;
}
