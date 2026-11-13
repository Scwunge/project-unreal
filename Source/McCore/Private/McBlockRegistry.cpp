#include "McBlockRegistry.h"
#include "McCache.h"
#include "HAL/PlatformTime.h"

int32 FMcBlock::FindProperty(FName PropertyName) const
{
	return Properties.IndexOfByPredicate([PropertyName](const FMcBlockProperty& Property) { return Property.Name == PropertyName; });
}

FMcBlockRegistry& FMcBlockRegistry::Get()
{
	static FMcBlockRegistry Registry;
	return Registry;
}

bool FMcBlockRegistry::EnsureLoaded()
{
	check(IsInGameThread());
	return IsLoaded() || LoadFromReport(McCache::Report(TEXT("blocks.json")), McCache::Data(TEXT("tags/block/air.json")));
}

bool FMcBlockRegistry::LoadFromReport(const FString& BlocksJsonPath, const FString& AirTagPath)
{
	const double StartTime = FPlatformTime::Seconds();

	const TSharedPtr<FJsonObject> Report = McCache::LoadJsonObject(BlocksJsonPath);
	if (!Report.IsValid())
	{
		return false;
	}

	TArray<FMcBlock> NewBlocks;
	TArray<FMcBlockState> NewStates;
	NewBlocks.Reserve(Report->Values.Num());

	for (const auto& Entry : Report->Values)
	{
		const TSharedPtr<FJsonObject>* BlockObject;
		if (!Entry.Value->TryGetObject(BlockObject))
		{
			UE_LOG(LogMcCore, Error, TEXT("blocks.json: %s is not an object"), *Entry.Key);
			return false;
		}

		FMcBlock& Block = NewBlocks.AddDefaulted_GetRef();
		Block.Name = FName(*Entry.Key);
		Block.Index = NewBlocks.Num() - 1;

		const TSharedPtr<FJsonObject>* PropertiesObject;
		if ((*BlockObject)->TryGetObjectField(TEXT("properties"), PropertiesObject))
		{
			for (const auto& PropertyEntry : (*PropertiesObject)->Values)
			{
				FMcBlockProperty& Property = Block.Properties.AddDefaulted_GetRef();
				Property.Name = FName(*PropertyEntry.Key);
				for (const TSharedPtr<FJsonValue>& Value : PropertyEntry.Value->AsArray())
				{
					Property.Values.Add(FName(*Value->AsString()));
				}
				if (Property.Values.Num() > MAX_uint8)
				{
					UE_LOG(LogMcCore, Error, TEXT("blocks.json: %s.%s has too many values"), *Entry.Key, *PropertyEntry.Key);
					return false;
				}
			}
		}

		bool bHasDefault = false;
		for (const TSharedPtr<FJsonValue>& StateValue : (*BlockObject)->GetArrayField(TEXT("states")))
		{
			const TSharedPtr<FJsonObject>& StateObject = StateValue->AsObject();
			const int32 Id = StateObject->GetIntegerField(TEXT("id"));
			if (Id < 0 || Id > MAX_uint16)
			{
				UE_LOG(LogMcCore, Error, TEXT("blocks.json: %s has state id %d, outside 16 bits"), *Entry.Key, Id);
				return false;
			}
			if (NewStates.Num() <= Id)
			{
				NewStates.SetNum(Id + 1);
			}

			FMcBlockState& State = NewStates[Id];
			if (State.Block != INDEX_NONE)
			{
				UE_LOG(LogMcCore, Error, TEXT("blocks.json: state id %d is used twice"), Id);
				return false;
			}
			State.Id = static_cast<FMcStateId>(Id);
			State.Block = Block.Index;
			State.Values.SetNumZeroed(Block.Properties.Num());

			const TSharedPtr<FJsonObject>* StateProperties;
			if (StateObject->TryGetObjectField(TEXT("properties"), StateProperties))
			{
				for (const auto& PropertyValue : (*StateProperties)->Values)
				{
					const int32 PropertyIndex = Block.FindProperty(FName(*PropertyValue.Key));
					const int32 ValueIndex = PropertyIndex == INDEX_NONE
						? INDEX_NONE
						: Block.Properties[PropertyIndex].Values.IndexOfByKey(FName(*PropertyValue.Value->AsString()));
					if (ValueIndex == INDEX_NONE)
					{
						UE_LOG(LogMcCore, Error, TEXT("blocks.json: state %d has unknown %s=%s"), Id, *PropertyValue.Key, *PropertyValue.Value->AsString());
						return false;
					}
					State.Values[PropertyIndex] = static_cast<uint8>(ValueIndex);
				}
			}

			Block.States.Add(State.Id);
			bool bIsDefault = false;
			if (StateObject->TryGetBoolField(TEXT("default"), bIsDefault) && bIsDefault)
			{
				Block.DefaultState = State.Id;
				bHasDefault = true;
			}
		}

		if (Block.States.Num() == 0)
		{
			UE_LOG(LogMcCore, Error, TEXT("blocks.json: %s has no states"), *Entry.Key);
			return false;
		}
		if (!bHasDefault)
		{
			Block.DefaultState = Block.States[0];
		}
	}

	Blocks = MoveTemp(NewBlocks);
	States = MoveTemp(NewStates);
	BlocksByName.Reset();
	for (const FMcBlock& Block : Blocks)
	{
		BlocksByName.Add(Block.Name, Block.Index);
	}

	AirStates.Init(false, States.Num());
	TArray<FString> AirBlocks;
	if (const TSharedPtr<FJsonObject> AirTag = McCache::LoadJsonObject(AirTagPath))
	{
		AirTag->TryGetStringArrayField(TEXT("values"), AirBlocks);
	}
	if (AirBlocks.Num() == 0)
	{
		UE_LOG(LogMcCore, Warning, TEXT("No #minecraft:air tag in cache/; treating only minecraft:air as air"));
		AirBlocks.Add(TEXT("minecraft:air"));
	}
	for (const FString& AirBlock : AirBlocks)
	{
		if (const FMcBlock* Block = FindBlock(FName(*AirBlock)))
		{
			for (const FMcStateId Id : Block->States)
			{
				AirStates[Id] = true;
			}
		}
	}

	UE_LOG(LogMcCore, Log, TEXT("Loaded %d blocks with %d states from %s in %.0f ms"),
		Blocks.Num(), States.Num(), *BlocksJsonPath, (FPlatformTime::Seconds() - StartTime) * 1000.0);
	return true;
}

const FMcBlock* FMcBlockRegistry::FindBlock(FName Name) const
{
	const int32* Index = BlocksByName.Find(Name);
	return Index ? &Blocks[*Index] : nullptr;
}

TOptional<FMcStateId> FMcBlockRegistry::FindDefaultState(FName BlockName) const
{
	const FMcBlock* Block = FindBlock(BlockName);
	return Block ? TOptional<FMcStateId>(Block->DefaultState) : TOptional<FMcStateId>();
}

TOptional<FMcStateId> FMcBlockRegistry::FindState(FName BlockName, const TMap<FName, FName>& Properties) const
{
	const FMcBlock* Block = FindBlock(BlockName);
	if (!Block)
	{
		return {};
	}

	TArray<uint8, TInlineAllocator<8>> Wanted = States[Block->DefaultState].Values;
	for (const TPair<FName, FName>& Property : Properties)
	{
		const int32 PropertyIndex = Block->FindProperty(Property.Key);
		if (PropertyIndex == INDEX_NONE)
		{
			return {};
		}
		const int32 ValueIndex = Block->Properties[PropertyIndex].Values.IndexOfByKey(Property.Value);
		if (ValueIndex == INDEX_NONE)
		{
			return {};
		}
		Wanted[PropertyIndex] = static_cast<uint8>(ValueIndex);
	}

	for (const FMcStateId Id : Block->States)
	{
		if (States[Id].Values == Wanted)
		{
			return Id;
		}
	}
	return {};
}

TOptional<FMcStateId> FMcBlockRegistry::ParseState(const FString& Text) const
{
	FString Name = Text.TrimStartAndEnd();
	TMap<FName, FName> Properties;

	int32 Open;
	if (Name.FindChar(TEXT('['), Open))
	{
		if (!Name.EndsWith(TEXT("]")))
		{
			return {};
		}
		const FString Inside = Name.Mid(Open + 1, Name.Len() - Open - 2);
		Name.LeftInline(Open);

		TArray<FString> Pairs;
		Inside.ParseIntoArray(Pairs, TEXT(","));
		for (const FString& Pair : Pairs)
		{
			FString Key, Value;
			if (!Pair.Split(TEXT("="), &Key, &Value))
			{
				return {};
			}
			Properties.Add(FName(*Key.TrimStartAndEnd()), FName(*Value.TrimStartAndEnd()));
		}
	}

	return FindState(FName(*McCache::Namespaced(Name)), Properties);
}

FName FMcBlockRegistry::GetPropertyValue(FMcStateId Id, FName PropertyName) const
{
	const FMcBlock& Block = GetBlockOf(Id);
	const int32 PropertyIndex = Block.FindProperty(PropertyName);
	return PropertyIndex == INDEX_NONE ? NAME_None : Block.Properties[PropertyIndex].Values[States[Id].Values[PropertyIndex]];
}

FString FMcBlockRegistry::Describe(FMcStateId Id) const
{
	if (!IsValidState(Id))
	{
		return FString::Printf(TEXT("<unknown state %d>"), Id);
	}

	const FMcBlock& Block = GetBlockOf(Id);
	FString Result = Block.Name.ToString();
	if (Block.Properties.Num() > 0)
	{
		TArray<FString> Parts;
		for (int32 Index = 0; Index < Block.Properties.Num(); ++Index)
		{
			const FMcBlockProperty& Property = Block.Properties[Index];
			Parts.Add(Property.Name.ToString() + TEXT("=") + Property.Values[States[Id].Values[Index]].ToString());
		}
		Parts.Sort();
		Result += TEXT("[") + FString::Join(Parts, TEXT(",")) + TEXT("]");
	}
	return Result;
}
