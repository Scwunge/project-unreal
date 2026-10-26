// Pool aliases (minecraft.wiki, "Structure": pool_aliases): a structure can have a pool name stand in for another,
// settled once when it starts. Trial chambers pick their spawner mobs this way.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "McRandom.h"

namespace McPoolAliases
{
	// Every pool a structure's aliases can stand in for.
	inline void CollectTargets(const TArray<TSharedPtr<FJsonValue>>& Aliases, TArray<FName>& Out)
	{
		for (const TSharedPtr<FJsonValue>& Value : Aliases)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			if (!Object.IsValid())
			{
				continue;
			}
			FString Target;
			if (Object->TryGetStringField(TEXT("target"), Target))
			{
				Out.AddUnique(FName(*Target));
			}
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (Object->TryGetArrayField(TEXT("targets"), List))
			{
				for (const TSharedPtr<FJsonValue>& Entry : *List)
				{
					FString Data;
					if (Entry->AsObject().IsValid() && Entry->AsObject()->TryGetStringField(TEXT("data"), Data))
					{
						Out.AddUnique(FName(*Data));
					}
				}
			}
			if (Object->TryGetArrayField(TEXT("groups"), List))
			{
				for (const TSharedPtr<FJsonValue>& Group : *List)
				{
					const TArray<TSharedPtr<FJsonValue>>* Data = nullptr;
					if (Group->AsObject().IsValid() && Group->AsObject()->TryGetArrayField(TEXT("data"), Data))
					{
						CollectTargets(*Data, Out);
					}
				}
			}
		}
	}

	// One of the {data, weight} entries, by weight.
	inline const FJsonObject* PickWeighted(const TArray<TSharedPtr<FJsonValue>>& List, FMcRandom& Random)
	{
		const auto WeightOf = [](const TSharedPtr<FJsonValue>& Entry)
		{
			double Weight = 1.0;
			Entry->AsObject()->TryGetNumberField(TEXT("weight"), Weight);
			return static_cast<int32>(Weight);
		};
		int32 Total = 0;
		for (const TSharedPtr<FJsonValue>& Entry : List)
		{
			Total += WeightOf(Entry);
		}
		if (Total <= 0)
		{
			return nullptr;
		}
		int32 Pick = Random.NextInt(Total);
		for (const TSharedPtr<FJsonValue>& Entry : List)
		{
			if (Pick < WeightOf(Entry))
			{
				return Entry->AsObject().Get();
			}
			Pick -= WeightOf(Entry);
		}
		return nullptr;
	}

	// Settles which pool each alias means: a direct alias always, a random one by weight, a group by picking one
	// of its sets of aliases.
	inline void Resolve(const TArray<TSharedPtr<FJsonValue>>& Aliases, FMcRandom& Random, TMap<FName, FName>& Out)
	{
		for (const TSharedPtr<FJsonValue>& Value : Aliases)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			if (!Object.IsValid())
			{
				continue;
			}
			FString Type = Object->GetStringField(TEXT("type"));
			Type.RemoveFromStart(TEXT("minecraft:"));
			FString Alias, Target;
			const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
			if (Type == TEXT("direct") && Object->TryGetStringField(TEXT("alias"), Alias) && Object->TryGetStringField(TEXT("target"), Target))
			{
				Out.Add(FName(*Alias), FName(*Target));
			}
			else if (Type == TEXT("random") && Object->TryGetStringField(TEXT("alias"), Alias) && Object->TryGetArrayField(TEXT("targets"), List))
			{
				const FJsonObject* Picked = PickWeighted(*List, Random);
				if (Picked && Picked->TryGetStringField(TEXT("data"), Target))
				{
					Out.Add(FName(*Alias), FName(*Target));
				}
			}
			else if (Type == TEXT("random_group") && Object->TryGetArrayField(TEXT("groups"), List))
			{
				const FJsonObject* Picked = PickWeighted(*List, Random);
				const TArray<TSharedPtr<FJsonValue>>* Data = nullptr;
				if (Picked && Picked->TryGetArrayField(TEXT("data"), Data))
				{
					Resolve(*Data, Random, Out);
				}
			}
		}
	}
}
