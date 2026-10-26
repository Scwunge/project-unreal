#include "McStructureTemplate.h"
#include "McBlockPredicate.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McGen.h"
#include "McGenRegion.h"
#include "McRandom.h"
#include "McStateProvider.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// Directions as 0 north, 1 east, 2 south, 3 west.
	int32 DirectionIndex(FName Name)
	{
		static const FName Names[4] = { "north", "east", "south", "west" };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			if (Names[Index] == Name)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}
	FName DirectionName(int32 Index)
	{
		static const FName Names[4] = { "north", "east", "south", "west" };
		return Names[Index & 3];
	}

	int32 Quarters(EMcRotation Rotation)
	{
		switch (Rotation)
		{
		case EMcRotation::Clockwise90: return 1;
		case EMcRotation::Clockwise180: return 2;
		case EMcRotation::CounterClockwise90: return 3;
		default: return 0;
		}
	}

	// A direction after mirroring, then turning.
	int32 MapDirection(int32 Direction, EMcRotation Rotation, EMcMirror Mirror)
	{
		if (Mirror == EMcMirror::LeftRight && (Direction & 1) == 0)
		{
			Direction ^= 2;
		}
		else if (Mirror == EMcMirror::FrontBack && (Direction & 1) == 1)
		{
			Direction ^= 2;
		}
		return (Direction + Quarters(Rotation)) & 3;
	}

	// "north_south", "ascending_east", "south_east" ...: the rail shape after the same.
	FName MapRailShape(FName Shape, EMcRotation Rotation, EMcMirror Mirror)
	{
		FString Text = Shape.ToString();
		FString Ascending;
		if (Text.StartsWith(TEXT("ascending_")))
		{
			Ascending = TEXT("ascending_");
			Text.RightChopInline(10);
		}
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT("_"));
		TArray<int32> Directions;
		for (const FString& Part : Parts)
		{
			const int32 Direction = DirectionIndex(FName(*Part));
			if (Direction == INDEX_NONE)
			{
				return Shape;
			}
			Directions.Add(MapDirection(Direction, Rotation, Mirror));
		}
		if (!Ascending.IsEmpty() && Directions.Num() == 1)
		{
			return FName(*(Ascending + DirectionName(Directions[0]).ToString()));
		}
		if (Directions.Num() != 2)
		{
			return Shape;
		}
		// Canonical names: north_south, east_west; corners south_east, south_west, north_west, north_east.
		const int32 A = Directions[0];
		const int32 B = Directions[1];
		if ((A & 1) == (B & 1))
		{
			return (A & 1) == 0 ? FName("north_south") : FName("east_west");
		}
		const bool bNorth = A == 0 || B == 0;
		const bool bEast = A == 1 || B == 1;
		if (bNorth)
		{
			return bEast ? FName("north_east") : FName("north_west");
		}
		return bEast ? FName("south_east") : FName("south_west");
	}

	FName Swapped(FName Value, const TCHAR* A, const TCHAR* B)
	{
		FString Text = Value.ToString();
		if (Text.EndsWith(A))
		{
			return FName(*(Text.LeftChop(FCString::Strlen(A)) + B));
		}
		if (Text.EndsWith(B))
		{
			return FName(*(Text.LeftChop(FCString::Strlen(B)) + A));
		}
		return Value;
	}
}

FIntVector McTemplate::TransformPos(const FIntVector& Pos, EMcRotation Rotation, EMcMirror Mirror, const FIntVector& Pivot)
{
	int32 X = Pos.X - Pivot.X;
	int32 Z = Pos.Z - Pivot.Z;
	if (Mirror == EMcMirror::LeftRight)
	{
		Z = -Z;
	}
	else if (Mirror == EMcMirror::FrontBack)
	{
		X = -X;
	}
	int32 TurnedX = X;
	int32 TurnedZ = Z;
	switch (Rotation)
	{
	case EMcRotation::Clockwise90:
		TurnedX = -Z;
		TurnedZ = X;
		break;
	case EMcRotation::Clockwise180:
		TurnedX = -X;
		TurnedZ = -Z;
		break;
	case EMcRotation::CounterClockwise90:
		TurnedX = Z;
		TurnedZ = -X;
		break;
	default:
		break;
	}
	return FIntVector(TurnedX + Pivot.X, Pos.Y, TurnedZ + Pivot.Z);
}

FMcStateId McTemplate::TransformState(FMcStateId State, EMcRotation Rotation, EMcMirror Mirror)
{
	if ((Rotation == EMcRotation::None && Mirror == EMcMirror::None) || State == SkipState)
	{
		return State;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcBlock& Block = Registry.GetBlockOf(State);
	if (Block.Properties.IsEmpty())
	{
		return State;
	}
	const int32 Turns = Quarters(Rotation);
	const bool bOdd = (Turns & 1) != 0;

	// Collect what changes from the old values, then apply it all at once.
	TArray<TPair<FName, FName>, TInlineAllocator<8>> Changes;
	FName Connections[4] = {};
	bool bHasConnections[4] = {};
	for (const FMcBlockProperty& Property : Block.Properties)
	{
		const FName Name = Property.Name;
		const FName Value = Registry.GetPropertyValue(State, Name);
		const FString Text = Name.ToString();
		const int32 Direction = DirectionIndex(Name);
		if (Direction != INDEX_NONE)
		{
			const int32 To = MapDirection(Direction, Rotation, Mirror);
			Connections[To] = Value;
			bHasConnections[To] = true;
		}
		else if (Name == "facing" || Name == "horizontal_facing")
		{
			const int32 From = DirectionIndex(Value);
			if (From != INDEX_NONE)
			{
				Changes.Emplace(Name, DirectionName(MapDirection(From, Rotation, Mirror)));
			}
		}
		else if (Name == "axis")
		{
			if (bOdd && Value == "x")
			{
				Changes.Emplace(Name, FName("z"));
			}
			else if (bOdd && Value == "z")
			{
				Changes.Emplace(Name, FName("x"));
			}
		}
		else if (Name == "rotation")
		{
			int32 Steps = FCString::Atoi(*Value.ToString());
			if (Mirror == EMcMirror::LeftRight)
			{
				Steps = (8 - Steps + 16) % 16;
			}
			else if (Mirror == EMcMirror::FrontBack)
			{
				Steps = (16 - Steps) % 16;
			}
			Steps = (Steps + Turns * 4) % 16;
			Changes.Emplace(Name, FName(*FString::FromInt(Steps)));
		}
		else if (Name == "shape" && Block.Name.ToString().Contains(TEXT("rail")))
		{
			Changes.Emplace(Name, MapRailShape(Value, Rotation, Mirror));
		}
		else if (Mirror != EMcMirror::None && Name == "shape" && Block.Name.ToString().Contains(TEXT("stairs")))
		{
			Changes.Emplace(Name, Swapped(Value, TEXT("_left"), TEXT("_right")));
		}
		else if (Mirror != EMcMirror::None && (Name == "hinge" || Name == "type") && (Value == "left" || Value == "right"))
		{
			Changes.Emplace(Name, Value == "left" ? FName("right") : FName("left"));
		}
	}
	FMcStateId Result = State;
	for (const TPair<FName, FName>& Change : Changes)
	{
		Result = McWithProperty(Result, Change.Key, Change.Value);
	}
	for (int32 Direction = 0; Direction < 4; ++Direction)
	{
		if (bHasConnections[Direction])
		{
			Result = McWithProperty(Result, DirectionName(Direction), Connections[Direction]);
		}
	}
	return Result;
}

EMcRotation McTemplate::Compose(EMcRotation First, EMcRotation Second)
{
	return static_cast<EMcRotation>((Quarters(First) + Quarters(Second)) & 3);
}

EMcRotation McTemplate::RandomRotation(FMcRandom& Random)
{
	return static_cast<EMcRotation>(Random.NextInt(4));
}

const TCHAR* McTemplate::Name(EMcRotation Rotation)
{
	static const TCHAR* const Names[4] = { TEXT("none"), TEXT("clockwise_90"), TEXT("180"), TEXT("counterclockwise_90") };
	return Names[Quarters(Rotation)];
}

// ---- Processors ------------------------------------------------------------------------------------------------

namespace
{
	TSharedPtr<FJsonObject> ObjectOf(const TSharedPtr<FJsonValue>& Json)
	{
		return Json.IsValid() && Json->Type == EJson::Object ? Json->AsObject() : nullptr;
	}

	FString TagName(const FString& Text)
	{
		return Text.StartsWith(TEXT("#")) ? Text.RightChop(1) : Text;
	}

	void LogOnce(const FString& Message)
	{
		static FCriticalSection Lock;
		static TSet<FString> Seen;
		FScopeLock Guard(&Lock);
		if (!Seen.Contains(Message))
		{
			Seen.Add(Message);
			UE_LOG(LogMcGen, Warning, TEXT("%s"), *Message);
		}
	}

	using FProcessor = FMcProcessorList::FProcessor;

	// Replaces blocks by rules: the first whose input test accepts the template's block and whose location test
	// accepts what is in the world gives the output.
	struct FRuleProcessor : FProcessor
	{
		struct FRule
		{
			TSharedPtr<const FMcRuleTest> Input;
			TSharedPtr<const FMcRuleTest> Location;
			FMcStateId Output = McTemplate::SkipState;
		};
		TArray<FRule> Rules;

		virtual FMcStateId Process(FMcGenRegion&, FIntVector& Pos, int32, FMcStateId State, FMcStateId Existing, FMcRandom& Random) const override
		{
			for (const FRule& Rule : Rules)
			{
				if ((!Rule.Input || Rule.Input->Test(State, Pos, Random)) && (!Rule.Location || Rule.Location->Test(Existing, Pos, Random)))
				{
					return Rule.Output;
				}
			}
			return State;
		}
	};

	struct FBlockIgnoreProcessor : FProcessor
	{
		TArray<int32> BlockIndices;

		virtual FMcStateId Process(FMcGenRegion&, FIntVector&, int32, FMcStateId State, FMcStateId, FMcRandom&) const override
		{
			return BlockIndices.Contains(FMcBlockRegistry::Get().GetState(State).Block) ? McTemplate::SkipState : State;
		}
	};

	// Leaves what is in the world alone where it is in a tag.
	struct FProtectedBlocksProcessor : FProcessor
	{
		int32 Tag = INDEX_NONE;

		virtual FMcStateId Process(FMcGenRegion&, FIntVector&, int32, FMcStateId State, FMcStateId Existing, FMcRandom&) const override
		{
			return FMcBlockTags::Get().Has(Tag, Existing) ? McTemplate::SkipState : State;
		}
	};

	// Drops blocks at random so that the integrity (a probability) is what stays.
	struct FBlockRotProcessor : FProcessor
	{
		float Integrity = 1.0f;
		int32 Rottable = INDEX_NONE;

		virtual FMcStateId Process(FMcGenRegion&, FIntVector&, int32, FMcStateId State, FMcStateId, FMcRandom& Random) const override
		{
			if (Rottable != INDEX_NONE && !FMcBlockTags::Get().Has(Rottable, State))
			{
				return State;
			}
			return Random.NextFloat() <= Integrity ? State : McTemplate::SkipState;
		}
	};

	// The height of a column as it was before features: trees and leaves aren't counted, so every chunk that looks
	// at a column agrees on it.
	int32 GroundHeight(FMcGenRegion& Region, int32 X, int32 Z, EMcHeightmap Heightmap)
	{
		const FMcBlockTags& Tags = FMcBlockTags::Get();
		static const int32 Logs = Tags.Find(TEXT("minecraft:logs"));
		static const int32 Leaves = Tags.Find(TEXT("minecraft:leaves"));
		const bool bFloor = Heightmap == EMcHeightmap::OceanFloor || Heightmap == EMcHeightmap::OceanFloorWG;
		int32 Height = Region.GetHeight(bFloor ? Heightmap : EMcHeightmap::OceanFloor, X, Z);
		while (Height > Region.GetMinY() + 1)
		{
			const FMcStateId Below = Region.GetBlock(FIntVector(X, Height - 1, Z));
			if (!Tags.Has(Logs, Below) && !Tags.Has(Leaves, Below))
			{
				break;
			}
			--Height;
		}
		return Height;
	}

	// Sets blocks on the ground: each column is moved to a heightmap plus an offset.
	struct FGravityProcessor : FProcessor
	{
		EMcHeightmap Heightmap = EMcHeightmap::WorldSurfaceWG;
		int32 Offset = 0;

		virtual FMcStateId Process(FMcGenRegion& Region, FIntVector& Pos, int32 LocalY, FMcStateId State, FMcStateId, FMcRandom&) const override
		{
			Pos.Y = GroundHeight(Region, Pos.X, Pos.Z, Heightmap) + Offset + LocalY;
			return State;
		}
	};

	TSharedPtr<const FProcessor> ParseProcessor(const FJsonObject& Json)
	{
		FString Type;
		if (!Json.TryGetStringField(TEXT("processor_type"), Type) && !Json.TryGetStringField(TEXT("type"), Type))
		{
			return nullptr;
		}
		Type.RemoveFromStart(TEXT("minecraft:"));
		if (Type == TEXT("rule"))
		{
			TSharedRef<FRuleProcessor> Result = MakeShared<FRuleProcessor>();
			const TArray<TSharedPtr<FJsonValue>>* Rules = nullptr;
			Json.TryGetArrayField(TEXT("rules"), Rules);
			for (const TSharedPtr<FJsonValue>& Value : Rules ? *Rules : TArray<TSharedPtr<FJsonValue>>())
			{
				const TSharedPtr<FJsonObject> Object = ObjectOf(Value);
				if (!Object)
				{
					continue;
				}
				FRuleProcessor::FRule& Rule = Result->Rules.AddDefaulted_GetRef();
				if (Object->HasField(TEXT("input_predicate")))
				{
					Rule.Input = FMcRuleTest::Parse(Object->TryGetField(TEXT("input_predicate")));
				}
				if (Object->HasField(TEXT("location_predicate")))
				{
					Rule.Location = FMcRuleTest::Parse(Object->TryGetField(TEXT("location_predicate")));
				}
				const TOptional<FMcStateId> Output = McParseBlockState(Object->TryGetField(TEXT("output_state")));
				if (!Output)
				{
					Result->Rules.Pop();
					continue;
				}
				Rule.Output = *Output;
			}
			return Result;
		}
		if (Type == TEXT("block_ignore"))
		{
			TSharedRef<FBlockIgnoreProcessor> Result = MakeShared<FBlockIgnoreProcessor>();
			const TArray<TSharedPtr<FJsonValue>>* Blocks = nullptr;
			Json.TryGetArrayField(TEXT("blocks"), Blocks);
			for (const TSharedPtr<FJsonValue>& Value : Blocks ? *Blocks : TArray<TSharedPtr<FJsonValue>>())
			{
				if (const TOptional<FMcStateId> State = McParseBlockState(Value))
				{
					Result->BlockIndices.AddUnique(FMcBlockRegistry::Get().GetState(*State).Block);
				}
			}
			return Result;
		}
		if (Type == TEXT("protected_blocks"))
		{
			TSharedRef<FProtectedBlocksProcessor> Result = MakeShared<FProtectedBlocksProcessor>();
			Result->Tag = FMcBlockTags::Get().Find(TagName(Json.GetStringField(TEXT("value"))));
			return Result;
		}
		if (Type == TEXT("block_rot"))
		{
			TSharedRef<FBlockRotProcessor> Result = MakeShared<FBlockRotProcessor>();
			Result->Integrity = static_cast<float>(Json.GetNumberField(TEXT("integrity")));
			FString Rottable;
			if (Json.TryGetStringField(TEXT("rottable_blocks"), Rottable))
			{
				Result->Rottable = FMcBlockTags::Get().Find(TagName(Rottable));
			}
			return Result;
		}
		if (Type == TEXT("gravity"))
		{
			TSharedRef<FGravityProcessor> Result = MakeShared<FGravityProcessor>();
			Result->Offset = static_cast<int32>(Json.GetNumberField(TEXT("offset")));
			FString Heightmap;
			if (Json.TryGetStringField(TEXT("heightmap"), Heightmap))
			{
				Result->Heightmap = FMcGenRegion::ParseHeightmap(Heightmap).Get(EMcHeightmap::WorldSurfaceWG);
			}
			return Result;
		}
		LogOnce(FString::Printf(TEXT("Structure processor %s is not supported; blocks pass through it"), *Type));
		return nullptr;
	}
}

TSharedPtr<const FMcProcessorList> FMcProcessorList::Parse(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid())
	{
		return nullptr;
	}
	const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
	if (const TSharedPtr<FJsonObject> Object = ObjectOf(Json))
	{
		Object->TryGetArrayField(TEXT("processors"), Array);
	}
	else if (Json->Type == EJson::Array)
	{
		Array = &Json->AsArray();
	}
	if (!Array)
	{
		return nullptr;
	}
	TSharedRef<FMcProcessorList> Result = MakeShared<FMcProcessorList>();
	for (const TSharedPtr<FJsonValue>& Value : *Array)
	{
		if (const TSharedPtr<FJsonObject> Object = ObjectOf(Value))
		{
			if (const TSharedPtr<const FProcessor> Processor = ParseProcessor(*Object))
			{
				Result->Processors.Add(Processor);
			}
		}
	}
	return Result;
}

TSharedPtr<const FMcProcessorList> FMcProcessorList::ParseRef(const TSharedPtr<FJsonValue>& Json)
{
	if (Json.IsValid() && Json->Type == EJson::String)
	{
		FString Id = Json->AsString();
		Id.RemoveFromStart(TEXT("minecraft:"));
		FString Text;
		TSharedPtr<FJsonObject> Object;
		if (!FFileHelper::LoadFileToString(Text, *McCache::Data(TEXT("worldgen/processor_list/") + Id + TEXT(".json")))
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Object) || !Object.IsValid())
		{
			UE_LOG(LogMcGen, Warning, TEXT("Processor list %s can't be read"), *Id);
			return nullptr;
		}
		return Parse(MakeShared<FJsonValueObject>(Object));
	}
	return Parse(Json);
}

FMcStateId FMcProcessorList::Process(FMcGenRegion& Region, FIntVector& Pos, int32 LocalY, FMcStateId State, FMcRandom& Random) const
{
	FMcStateId Existing = Region.GetBlock(Pos);
	for (const TSharedPtr<const FProcessor>& Processor : Processors)
	{
		State = Processor->Process(Region, Pos, LocalY, State, Existing, Random);
		if (State == McTemplate::SkipState)
		{
			return State;
		}
	}
	return State;
}

// ---- Templates -------------------------------------------------------------------------------------------------

namespace
{
	FString IdPath(FString Id)
	{
		Id.RemoveFromStart(TEXT("minecraft:"));
		return Id;
	}

	TOptional<FMcStateId> ParsePaletteEntry(const FMcNbt& Entry)
	{
		FString Name = Entry.GetString(TEXT("id"));
		if (Name.IsEmpty())
		{
			Name = Entry.GetString(TEXT("Name"));
		}
		if (Name.IsEmpty())
		{
			return NullOpt;
		}
		if (!Name.Contains(TEXT(":")))
		{
			Name = TEXT("minecraft:") + Name;
		}
		const FMcNbt* Properties = Entry.Find(TEXT("properties"));
		if (!Properties)
		{
			Properties = Entry.Find(TEXT("Properties"));
		}
		TMap<FName, FName> Values;
		if (Properties && Properties->Type == FMcNbt::EType::Compound)
		{
			for (int32 Index = 0; Index < Properties->Keys.Num(); ++Index)
			{
				Values.Add(FName(*Properties->Keys[Index]), FName(*Properties->Values[Index].String));
			}
		}
		return FMcBlockRegistry::Get().FindState(FName(*Name), Values);
	}

	TSharedPtr<const FMcNbt> Share(const FMcNbt* Nbt)
	{
		return Nbt ? TSharedPtr<const FMcNbt>(MakeShared<const FMcNbt>(*Nbt)) : TSharedPtr<const FMcNbt>();
	}

	FCriticalSection TemplateLock;
	TMap<FString, TSharedPtr<const FMcStructureTemplate>> Loaded;
}

bool FMcStructureTemplate::Load(const FString& Id, FMcStructureTemplate& Out)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	FMcNbt Root;
	if (!FMcNbt::LoadFile(McCache::Data(TEXT("structure/") + IdPath(Id) + TEXT(".nbt")), Root))
	{
		return false;
	}
	const FMcNbt* Size = Root.Find(TEXT("size"));
	const FMcNbt* Blocks = Root.Find(TEXT("blocks"));
	if (!Size || Size->Num() != 3 || !Blocks)
	{
		return false;
	}
	Out = FMcStructureTemplate();
	Out.Size = FIntVector(Size->Values[0].Integer, Size->Values[1].Integer, Size->Values[2].Integer);

	TArray<const FMcNbt*> PaletteLists;
	if (const FMcNbt* Palettes = Root.Find(TEXT("palettes")))
	{
		for (const FMcNbt& Palette : Palettes->Values)
		{
			PaletteLists.Add(&Palette);
		}
	}
	else if (const FMcNbt* Palette = Root.Find(TEXT("palette")))
	{
		PaletteLists.Add(Palette);
	}
	if (PaletteLists.IsEmpty())
	{
		return false;
	}
	for (const FMcNbt* List : PaletteLists)
	{
		TArray<FMcStateId>& States = Out.Palettes.AddDefaulted_GetRef();
		for (const FMcNbt& Entry : List->Values)
		{
			FString Name = Entry.GetString(TEXT("id"));
			if (Name.IsEmpty())
			{
				Name = Entry.GetString(TEXT("Name"));
			}
			if (Name == TEXT("minecraft:structure_void") || Name == TEXT("minecraft:structure_block"))
			{
				States.Add(McTemplate::SkipState);
				continue;
			}
			const TOptional<FMcStateId> State = ParsePaletteEntry(Entry);
			if (!State)
			{
				UE_LOG(LogMcGen, Warning, TEXT("Template %s: unknown block %s"), *Id, *Name);
			}
			States.Add(State.Get(McTemplate::SkipState));
		}
	}

	for (const FMcNbt& Entry : Blocks->Values)
	{
		const FMcNbt* Pos = Entry.Find(TEXT("pos"));
		if (!Pos || Pos->Num() != 3)
		{
			continue;
		}
		FBlock& Block = Out.Blocks.AddDefaulted_GetRef();
		Block.Pos = FIntVector(Pos->Values[0].Integer, Pos->Values[1].Integer, Pos->Values[2].Integer);
		Block.Palette = static_cast<int32>(Entry.GetInt(TEXT("state")));
		if (const FMcNbt* Nbt = Entry.Find(TEXT("nbt")))
		{
			Block.Nbt = Share(Nbt);
			if (const FMcNbt* Final = Nbt->Find(TEXT("final_state")))
			{
				Block.bJigsaw = true;
				// A structure void final state leaves the block as it was.
				const TOptional<FMcStateId> State = Final->String.StartsWith(TEXT("minecraft:structure_void")) ? TOptional<FMcStateId>() : Registry.ParseState(Final->String);
				if (State)
				{
					Block.Final = *State;
				}
			}
		}
		if (!Out.Palettes[0].IsValidIndex(Block.Palette))
		{
			Out.Blocks.Pop();
		}
	}
	if (const FMcNbt* Entities = Root.Find(TEXT("entities")))
	{
		for (const FMcNbt& Entry : Entities->Values)
		{
			FEntity& Entity = Out.Entities.AddDefaulted_GetRef();
			if (const FMcNbt* Pos = Entry.Find(TEXT("pos")))
			{
				if (Pos->Num() == 3)
				{
					Entity.Pos = FVector(Pos->Values[0].GetReal(), Pos->Values[1].GetReal(), Pos->Values[2].GetReal());
				}
			}
			if (const FMcNbt* Pos = Entry.Find(TEXT("blockPos")))
			{
				if (Pos->Num() == 3)
				{
					Entity.BlockPos = FIntVector(Pos->Values[0].Integer, Pos->Values[1].Integer, Pos->Values[2].Integer);
				}
			}
			Entity.Nbt = Share(Entry.Find(TEXT("nbt")));
		}
	}
	return true;
}

TSharedPtr<const FMcStructureTemplate> FMcStructureTemplate::Find(const FString& Id)
{
	const FString Key = IdPath(Id);
	FScopeLock Guard(&TemplateLock);
	if (const TSharedPtr<const FMcStructureTemplate>* Found = Loaded.Find(Key))
	{
		return *Found;
	}
	TSharedPtr<FMcStructureTemplate> Template = MakeShared<FMcStructureTemplate>();
	if (!FMcBlockRegistry::Get().IsLoaded() || !Load(Key, *Template))
	{
		Template.Reset();
		if (FMcBlockRegistry::Get().IsLoaded())
		{
			UE_LOG(LogMcGen, Warning, TEXT("Structure template %s can't be loaded"), *Key);
		}
	}
	Loaded.Add(Key, Template);
	return Template;
}

void FMcStructureTemplate::ListIds(TArray<FString>& Out)
{
	FString Root = McCache::Data(TEXT("structure/"));
	FPaths::NormalizeFilename(Root);
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.nbt"), true, false);
	for (FString File : Files)
	{
		FPaths::NormalizeFilename(File);
		if (File.StartsWith(Root))
		{
			Out.Add(File.Mid(Root.Len()).LeftChop(4));
		}
	}
	Out.Sort();
}

FIntVector FMcStructureTemplate::ToWorld(const FIntVector& Local, const FIntVector& Position, const FMcTemplatePlaceSettings& Settings) const
{
	return Position + McTemplate::TransformPos(Local, Settings.Rotation, Settings.Mirror, Settings.Pivot);
}

void FMcStructureTemplate::GetBounds(const FIntVector& Position, const FMcTemplatePlaceSettings& Settings, FIntVector& OutMin, FIntVector& OutMax) const
{
	OutMin = FIntVector(MAX_int32);
	OutMax = FIntVector(MIN_int32);
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FIntVector Local((Corner & 1) ? Size.X - 1 : 0, (Corner & 2) ? Size.Y - 1 : 0, (Corner & 4) ? Size.Z - 1 : 0);
		const FIntVector World = ToWorld(Local, Position, Settings);
		OutMin = FIntVector(FMath::Min(OutMin.X, World.X), FMath::Min(OutMin.Y, World.Y), FMath::Min(OutMin.Z, World.Z));
		OutMax = FIntVector(FMath::Max(OutMax.X, World.X), FMath::Max(OutMax.Y, World.Y), FMath::Max(OutMax.Z, World.Z));
	}
}

int32 FMcStructureTemplate::Place(FMcGenRegion& Region, const FIntVector& Position, const FMcTemplatePlaceSettings& Settings, FMcRandom& Random) const
{
	if (Palettes.IsEmpty())
	{
		return 0;
	}
	const int32 PaletteIndex = Settings.Palette >= 0 ? FMath::Min(Settings.Palette, Palettes.Num() - 1) : Random.NextInt(Palettes.Num());
	// Turn the palette's states once.
	TArray<FMcStateId> Turned;
	Turned.Reserve(Palettes[PaletteIndex].Num());
	for (const FMcStateId State : Palettes[PaletteIndex])
	{
		Turned.Add(McTemplate::TransformState(State, Settings.Rotation, Settings.Mirror));
	}
	int32 Placed = 0;
	for (const FBlock& Block : Blocks)
	{
		FMcStateId State = Block.bJigsaw ? McTemplate::TransformState(Block.Final, Settings.Rotation, Settings.Mirror) : Turned.IsValidIndex(Block.Palette) ? Turned[Block.Palette] : McTemplate::SkipState;
		if (State == McTemplate::SkipState || (Settings.bSkipAir && Region.IsAir(State)))
		{
			continue;
		}
		FIntVector Pos = ToWorld(Block.Pos, Position, Settings);
		if (!Region.Contains(Pos.X, Pos.Z) || (Settings.bClip && (Pos.X < Settings.ClipMin.X || Pos.X > Settings.ClipMax.X || Pos.Z < Settings.ClipMin.Y || Pos.Z > Settings.ClipMax.Y)))
		{
			continue;
		}
		if (Settings.Processors)
		{
			State = Settings.Processors->Process(Region, Pos, Block.Pos.Y, State, Random);
			if (State == McTemplate::SkipState)
			{
				continue;
			}
			if (!Region.Contains(Pos.X, Pos.Z))
			{
				continue;
			}
		}
		Placed += Region.SetBlock(Pos, State) ? 1 : 0;
	}
	return Placed;
}

void FMcProcessorList::AddGravity(EMcHeightmap Heightmap, int32 Offset)
{
	TSharedRef<FGravityProcessor> Gravity = MakeShared<FGravityProcessor>();
	Gravity->Heightmap = Heightmap;
	Gravity->Offset = Offset;
	Processors.Add(Gravity);
}
