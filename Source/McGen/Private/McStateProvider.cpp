#include "McStateProvider.h"
#include "Dom/JsonObject.h"
#include "McBlockPredicate.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McGen.h"
#include "McNoise.h"
#include "McProviders.h"
#include "McRandom.h"
#include "Misc/Paths.h"

FMcStateId McWithProperty(FMcStateId State, FName Property, FName Value)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcBlock& Block = Registry.GetBlockOf(State);
	const FMcBlockState& Info = Registry.GetState(State);
	TMap<FName, FName> Properties;
	for (int32 Index = 0; Index < Block.Properties.Num(); ++Index)
	{
		Properties.Add(Block.Properties[Index].Name, Block.Properties[Index].Values[Info.Values[Index]]);
	}
	if (!Properties.Contains(Property))
	{
		return State;
	}
	Properties.Add(Property, Value);
	return Registry.FindState(Block.Name, Properties).Get(State);
}

namespace
{
	class FSimple final : public FMcStateProvider
	{
	public:
		FMcStateId State = 0;
		virtual TOptional<FMcStateId> Get(FMcGenRegion&, FMcRandom&, const FIntVector&) const override { return State; }
	};

	class FWeighted final : public FMcStateProvider
	{
	public:
		TArray<TSharedPtr<const FMcStateProvider>> Entries;
		TArray<int32> Weights;
		int32 Total = 0;
		virtual TOptional<FMcStateId> Get(FMcGenRegion& Region, FMcRandom& Random, const FIntVector& Pos) const override
		{
			int32 Pick = Total > 0 ? Random.NextInt(Total) : 0;
			for (int32 Index = 0; Index < Entries.Num(); ++Index)
			{
				Pick -= Weights[Index];
				if (Pick < 0)
				{
					return Entries[Index]->Get(Region, Random, Pos);
				}
			}
			return {};
		}
	};

	class FRandomizedInt final : public FMcStateProvider
	{
	public:
		TSharedPtr<const FMcStateProvider> Source;
		FName Property;
		FMcIntProvider Values;
		virtual TOptional<FMcStateId> Get(FMcGenRegion& Region, FMcRandom& Random, const FIntVector& Pos) const override
		{
			const TOptional<FMcStateId> State = Source->Get(Region, Random, Pos);
			if (!State.IsSet())
			{
				return State;
			}
			return McWithProperty(State.GetValue(), Property, FName(*FString::FromInt(Values.Sample(Random))));
		}
	};

	class FRandomBlock final : public FMcStateProvider
	{
	public:
		TArray<FMcStateId> States;
		virtual TOptional<FMcStateId> Get(FMcGenRegion&, FMcRandom& Random, const FIntVector&) const override
		{
			return States[Random.NextInt(States.Num())];
		}
	};

	// A provider's state turned: to a given facing, or onto a random axis.
	class FRotated final : public FMcStateProvider
	{
	public:
		TSharedPtr<const FMcStateProvider> Source;
		FName Facing;
		virtual TOptional<FMcStateId> Get(FMcGenRegion& Region, FMcRandom& Random, const FIntVector& Pos) const override
		{
			const TOptional<FMcStateId> State = Source->Get(Region, Random, Pos);
			if (!State.IsSet())
			{
				return State;
			}
			if (!Facing.IsNone())
			{
				return McWithProperty(State.GetValue(), TEXT("facing"), Facing);
			}
			static const FName Axes[] = { TEXT("x"), TEXT("y"), TEXT("z") };
			return McWithProperty(State.GetValue(), TEXT("axis"), Axes[Random.NextInt(3)]);
		}
	};

	// The shared parts of the noise providers.
	class FNoiseBased : public FMcStateProvider
	{
	public:
		TUniquePtr<FMcNoise> Noise;
		double Scale = 1.0;
		TArray<FMcStateId> States;

		double Sample(const FMcNoise& Which, double WhichScale, const FIntVector& Pos) const
		{
			return Which.Sample(Pos.X * WhichScale, Pos.Y * WhichScale, Pos.Z * WhichScale);
		}
		// A noise value in -1..1 picks a state from the list.
		FMcStateId Pick(const TArray<FMcStateId>& List, double Value) const
		{
			const double Unit = FMath::Clamp((Value + 1.0) * 0.5, 0.0, 0.999999);
			return List[FMath::FloorToInt32(Unit * List.Num())];
		}
	};

	class FNoiseProvider final : public FNoiseBased
	{
	public:
		virtual TOptional<FMcStateId> Get(FMcGenRegion&, FMcRandom&, const FIntVector& Pos) const override
		{
			return Pick(States, Sample(*Noise, Scale, Pos));
		}
	};

	class FDualNoise final : public FNoiseBased
	{
	public:
		TUniquePtr<FMcNoise> SlowNoise;
		double SlowScale = 1.0;
		int32 VarietyMin = 1;
		int32 VarietyMax = 1;
		virtual TOptional<FMcStateId> Get(FMcGenRegion&, FMcRandom& Random, const FIntVector& Pos) const override
		{
			// The slow noise decides how many neighbouring states mix here; the fast one where in the list.
			const double Slow = FMath::Clamp((Sample(*SlowNoise, SlowScale, Pos) + 1.0) * 0.5, 0.0, 1.0);
			const int32 Variety = FMath::Clamp(FMath::RoundToInt32(FMath::Lerp(static_cast<double>(VarietyMin), static_cast<double>(VarietyMax), Slow)), 1, States.Num());
			const double Unit = FMath::Clamp((Sample(*Noise, Scale, Pos) + 1.0) * 0.5, 0.0, 0.999999);
			const int32 Base = FMath::FloorToInt32(Unit * States.Num());
			return States[(Base + Random.NextInt(Variety)) % States.Num()];
		}
	};

	class FNoiseThreshold final : public FNoiseBased
	{
	public:
		FMcStateId Default = 0;
		double Threshold = 0.0;
		double HighChance = 0.0;
		TArray<FMcStateId> Low;
		TArray<FMcStateId> High;
		virtual TOptional<FMcStateId> Get(FMcGenRegion&, FMcRandom& Random, const FIntVector& Pos) const override
		{
			if (Sample(*Noise, Scale, Pos) < Threshold)
			{
				return Low[Random.NextInt(Low.Num())];
			}
			if (Random.NextDouble() < HighChance)
			{
				return High[Random.NextInt(High.Num())];
			}
			return Default;
		}
	};

	class FRuleBased final : public FMcStateProvider
	{
	public:
		TArray<TSharedPtr<const FMcBlockPredicate>> Conditions;
		TArray<TSharedPtr<const FMcStateProvider>> Results;
		TSharedPtr<const FMcStateProvider> Fallback;
		virtual TOptional<FMcStateId> Get(FMcGenRegion& Region, FMcRandom& Random, const FIntVector& Pos) const override
		{
			for (int32 Index = 0; Index < Conditions.Num(); ++Index)
			{
				if (Conditions[Index]->Test(Region, Pos))
				{
					return Results[Index]->Get(Region, Random, Pos);
				}
			}
			return Fallback.IsValid() ? Fallback->Get(Region, Random, Pos) : TOptional<FMcStateId>();
		}
	};

	bool ReadStates(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, TArray<FMcStateId>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* List;
		if (!Object->TryGetArrayField(Field, List))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Value : *List)
		{
			const TOptional<FMcStateId> State = McParseBlockState(Value);
			if (!State.IsSet())
			{
				return false;
			}
			Out.Add(State.GetValue());
		}
		return !Out.IsEmpty();
	}

	bool ReadNoise(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int64 Seed, TUniquePtr<FMcNoise>& Out)
	{
		const TSharedPtr<FJsonObject>* NoiseObject;
		FMcNoiseParams Params;
		if (!Object->TryGetObjectField(Field, NoiseObject) || !FMcNoiseParams::Parse(**NoiseObject, Params))
		{
			return false;
		}
		FMcRandom Random = FMcRandom(static_cast<uint64>(Seed)).Fork(Field);
		Out = MakeUnique<FMcNoise>(Random, Params);
		return true;
	}

	bool ReadNoiseBase(const TSharedPtr<FJsonObject>& Object, FNoiseBased& Out, bool bStates)
	{
		int64 Seed = 0;
		Object->TryGetNumberField(TEXT("seed"), Seed);
		Object->TryGetNumberField(TEXT("scale"), Out.Scale);
		return ReadNoise(Object, TEXT("noise"), Seed, Out.Noise) && (!bStates || ReadStates(Object, TEXT("states"), Out.States));
	}
}

TSharedPtr<const FMcStateProvider> FMcStateProvider::Simple(FMcStateId State)
{
	TSharedRef<FSimple> Result = MakeShared<FSimple>();
	Result->State = State;
	return Result;
}

TSharedPtr<const FMcStateProvider> FMcStateProvider::Parse(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid())
	{
		UE_LOG(LogMcGen, Error, TEXT("Missing block state provider"));
		return nullptr;
	}
	if (Json->Type == EJson::String)
	{
		// A named provider in worldgen/block_state_provider, else a block.
		const FString Id = McCache::Namespaced(Json->AsString());
		const FString Path = McCache::Data(TEXT("worldgen/block_state_provider/") + McCache::PathOf(Id) + TEXT(".json"));
		if (FPaths::FileExists(Path))
		{
			if (const TSharedPtr<FJsonObject> Named = McCache::LoadJsonObject(Path))
			{
				return Parse(MakeShared<FJsonValueObject>(Named));
			}
		}
	}
	const TSharedPtr<FJsonObject> Object = Json->Type == EJson::Object ? Json->AsObject() : nullptr;
	if (!Object.IsValid() || !Object->HasField(TEXT("type")))
	{
		const TOptional<FMcStateId> State = McParseBlockState(Json);
		if (!State.IsSet())
		{
			UE_LOG(LogMcGen, Error, TEXT("Block state provider: unknown state"));
			return nullptr;
		}
		return Simple(State.GetValue());
	}
	const FString Type = McCache::Namespaced(Object->GetStringField(TEXT("type")));
	if (Type == TEXT("minecraft:simple_state_provider"))
	{
		const TOptional<FMcStateId> State = McParseBlockState(Object->TryGetField(TEXT("state")));
		return State.IsSet() ? Simple(State.GetValue()) : nullptr;
	}
	if (Type == TEXT("minecraft:weighted") || Type == TEXT("minecraft:weighted_state_provider"))
	{
		TSharedRef<FWeighted> Result = MakeShared<FWeighted>();
		for (const TSharedPtr<FJsonValue>& Entry : Object->GetArrayField(TEXT("entries")))
		{
			const TSharedPtr<FJsonObject> EntryObject = Entry->AsObject();
			TSharedPtr<const FMcStateProvider> Data = Parse(EntryObject->TryGetField(TEXT("data")));
			if (!Data.IsValid())
			{
				return nullptr;
			}
			Result->Entries.Add(Data);
			Result->Weights.Add(EntryObject->GetIntegerField(TEXT("weight")));
			Result->Total += Result->Weights.Last();
		}
		return Result;
	}
	if (Type == TEXT("minecraft:randomized_int") || Type == TEXT("minecraft:randomized_int_state_provider"))
	{
		TSharedRef<FRandomizedInt> Result = MakeShared<FRandomizedInt>();
		Result->Source = Parse(Object->TryGetField(TEXT("source")));
		Result->Property = FName(*Object->GetStringField(TEXT("property")));
		if (!Result->Source.IsValid() || !Result->Values.Parse(Object->TryGetField(TEXT("values"))))
		{
			return nullptr;
		}
		return Result;
	}
	if (Type == TEXT("minecraft:random_block"))
	{
		// One of a list or tag of blocks, each in its default state.
		TSharedRef<FRandomBlock> Result = MakeShared<FRandomBlock>();
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const TSharedPtr<FJsonValue> Blocks = Object->TryGetField(TEXT("blocks"));
		TArray<FString> Names;
		if (Blocks.IsValid() && Blocks->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Name : Blocks->AsArray())
			{
				Names.Add(Name->AsString());
			}
		}
		else if (Blocks.IsValid())
		{
			Names.Add(Blocks->AsString());
		}
		for (const FString& Name : Names)
		{
			if (Name.StartsWith(TEXT("#")))
			{
				const FMcBlockTags& Tags = FMcBlockTags::Get();
				const int32 Tag = Tags.Find(Name.Mid(1));
				for (int32 Block = 0; Block < Registry.NumBlocks(); ++Block)
				{
					if (Tags.HasBlock(Tag, Block))
					{
						Result->States.Add(Registry.GetBlock(Block).DefaultState);
					}
				}
			}
			else if (const FMcBlock* Block = Registry.FindBlock(FName(*McCache::Namespaced(Name))))
			{
				Result->States.Add(Block->DefaultState);
			}
		}
		return Result->States.IsEmpty() ? TSharedPtr<FRandomBlock>() : Result;
	}
	if (Type == TEXT("minecraft:rotated") || Type == TEXT("minecraft:rotated_block_provider"))
	{
		TSharedRef<FRotated> Result = MakeShared<FRotated>();
		Result->Source = Parse(Object->TryGetField(TEXT("state")));
		FString Direction;
		if (Object->TryGetStringField(TEXT("direction"), Direction))
		{
			Result->Facing = FName(*Direction);
		}
		return Result->Source.IsValid() ? Result : TSharedPtr<FRotated>();
	}
	if (Type == TEXT("minecraft:noise") || Type == TEXT("minecraft:noise_provider"))
	{
		TSharedRef<FNoiseProvider> Result = MakeShared<FNoiseProvider>();
		return ReadNoiseBase(Object, *Result, true) ? Result : TSharedPtr<const FMcStateProvider>();
	}
	if (Type == TEXT("minecraft:dual_noise") || Type == TEXT("minecraft:dual_noise_provider"))
	{
		TSharedRef<FDualNoise> Result = MakeShared<FDualNoise>();
		int64 Seed = 0;
		Object->TryGetNumberField(TEXT("seed"), Seed);
		Object->TryGetNumberField(TEXT("slow_scale"), Result->SlowScale);
		const TArray<TSharedPtr<FJsonValue>>* Variety;
		if (Object->TryGetArrayField(TEXT("variety"), Variety) && Variety->Num() == 2)
		{
			Result->VarietyMin = static_cast<int32>((*Variety)[0]->AsNumber());
			Result->VarietyMax = static_cast<int32>((*Variety)[1]->AsNumber());
		}
		return ReadNoiseBase(Object, *Result, true) && ReadNoise(Object, TEXT("slow_noise"), Seed, Result->SlowNoise) ? Result : TSharedPtr<const FMcStateProvider>();
	}
	if (Type == TEXT("minecraft:noise_threshold") || Type == TEXT("minecraft:noise_threshold_provider"))
	{
		TSharedRef<FNoiseThreshold> Result = MakeShared<FNoiseThreshold>();
		const TOptional<FMcStateId> Default = McParseBlockState(Object->TryGetField(TEXT("default_state")));
		Object->TryGetNumberField(TEXT("threshold"), Result->Threshold);
		Object->TryGetNumberField(TEXT("high_chance"), Result->HighChance);
		if (!Default.IsSet() || !ReadNoiseBase(Object, *Result, false) || !ReadStates(Object, TEXT("low_states"), Result->Low) || !ReadStates(Object, TEXT("high_states"), Result->High))
		{
			return nullptr;
		}
		Result->Default = Default.GetValue();
		return Result;
	}
	if (Type == TEXT("minecraft:rule_based") || Type == TEXT("minecraft:rule_based_state_provider"))
	{
		TSharedRef<FRuleBased> Result = MakeShared<FRuleBased>();
		for (const TSharedPtr<FJsonValue>& Rule : Object->GetArrayField(TEXT("rules")))
		{
			const TSharedPtr<FJsonObject> RuleObject = Rule->AsObject();
			TSharedPtr<const FMcBlockPredicate> Condition = FMcBlockPredicate::Parse(RuleObject->TryGetField(TEXT("if_true")));
			TSharedPtr<const FMcStateProvider> Then = Parse(RuleObject->TryGetField(TEXT("then")));
			if (!Condition.IsValid() || !Then.IsValid())
			{
				return nullptr;
			}
			Result->Conditions.Add(Condition);
			Result->Results.Add(Then);
		}
		if (Object->HasField(TEXT("fallback")))
		{
			Result->Fallback = Parse(Object->TryGetField(TEXT("fallback")));
		}
		return Result;
	}
	UE_LOG(LogMcGen, Error, TEXT("Unknown block state provider %s"), *Type);
	return nullptr;
}
