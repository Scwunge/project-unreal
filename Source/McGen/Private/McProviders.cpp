#include "McProviders.h"
#include "Dom/JsonObject.h"
#include "McCache.h"
#include "McGen.h"
#include "McRandom.h"

namespace
{
	FString TypeOf(const FJsonObject& Json)
	{
		return McCache::Namespaced(Json.GetStringField(TEXT("type")));
	}

	// Uniform in [Min, Max].
	int32 Between(FMcRandom& Random, int32 Min, int32 Max)
	{
		return Max <= Min ? Min : Min + Random.NextInt(Max - Min + 1);
	}
}

bool FMcFloatProvider::Parse(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid())
	{
		return false;
	}
	if (Json->Type == EJson::Number)
	{
		Type = EType::Constant;
		A = Json->AsNumber();
		return true;
	}
	const TSharedPtr<FJsonObject> Object = Json->AsObject();
	if (!Object.IsValid())
	{
		return false;
	}
	const FString Kind = TypeOf(*Object);
	if (Kind == TEXT("minecraft:constant"))
	{
		Type = EType::Constant;
		A = Object->GetNumberField(TEXT("value"));
	}
	else if (Kind == TEXT("minecraft:uniform"))
	{
		Type = EType::Uniform;
		A = Object->GetNumberField(TEXT("min_inclusive"));
		B = Object->GetNumberField(TEXT("max_exclusive"));
	}
	else if (Kind == TEXT("minecraft:clamped_normal"))
	{
		Type = EType::ClampedNormal;
		A = Object->GetNumberField(TEXT("mean"));
		B = Object->GetNumberField(TEXT("deviation"));
		C = Object->GetNumberField(TEXT("min"));
		D = Object->GetNumberField(TEXT("max"));
	}
	else if (Kind == TEXT("minecraft:trapezoid"))
	{
		Type = EType::Trapezoid;
		A = Object->GetNumberField(TEXT("min"));
		B = Object->GetNumberField(TEXT("max"));
		C = Object->GetNumberField(TEXT("plateau"));
	}
	else
	{
		UE_LOG(LogMcGen, Error, TEXT("Unknown float provider %s"), *Kind);
		return false;
	}
	return true;
}

double FMcFloatProvider::Sample(FMcRandom& Random) const
{
	switch (Type)
	{
	case EType::Constant:
		return A;
	case EType::Uniform:
		return A + Random.NextDouble() * (B - A);
	case EType::ClampedNormal:
		return FMath::Clamp(A + Random.NextGaussian() * B, C, D);
	case EType::Trapezoid:
	{
		// The sum of two uniform numbers: flat across the plateau, sloping down to min and max.
		const double Span = B - A;
		const double Slope = (Span - C) * 0.5;
		return A + Random.NextDouble() * (Span - Slope) + Random.NextDouble() * Slope;
	}
	}
	return A;
}

bool FMcIntProvider::Parse(const TSharedPtr<FJsonValue>& Json)
{
	if (!Json.IsValid())
	{
		return false;
	}
	if (Json->Type == EJson::Number)
	{
		Type = EType::Constant;
		Min = Max = static_cast<int32>(Json->AsNumber());
		return true;
	}
	const TSharedPtr<FJsonObject> Object = Json->AsObject();
	if (!Object.IsValid())
	{
		return false;
	}
	const FString Kind = TypeOf(*Object);
	if (Kind == TEXT("minecraft:constant"))
	{
		Type = EType::Constant;
		Min = Max = Object->GetIntegerField(TEXT("value"));
	}
	else if (Kind == TEXT("minecraft:uniform") || Kind == TEXT("minecraft:biased_to_bottom") || Kind == TEXT("minecraft:very_biased_to_bottom"))
	{
		Type = Kind == TEXT("minecraft:uniform") ? EType::Uniform : (Kind == TEXT("minecraft:biased_to_bottom") ? EType::BiasedToBottom : EType::VeryBiasedToBottom);
		Min = Object->GetIntegerField(TEXT("min_inclusive"));
		Max = Object->GetIntegerField(TEXT("max_inclusive"));
	}
	else if (Kind == TEXT("minecraft:clamped"))
	{
		Type = EType::Clamped;
		Min = Object->GetIntegerField(TEXT("min_inclusive"));
		Max = Object->GetIntegerField(TEXT("max_inclusive"));
		FMcIntProvider Source;
		if (!Source.Parse(Object->TryGetField(TEXT("source"))))
		{
			return false;
		}
		Sources.Add(MoveTemp(Source));
	}
	else if (Kind == TEXT("minecraft:clamped_normal"))
	{
		Type = EType::ClampedNormal;
		Mean = Object->GetNumberField(TEXT("mean"));
		Deviation = Object->GetNumberField(TEXT("deviation"));
		Min = Object->GetIntegerField(TEXT("min_inclusive"));
		Max = Object->GetIntegerField(TEXT("max_inclusive"));
	}
	else if (Kind == TEXT("minecraft:weighted_list"))
	{
		Type = EType::WeightedList;
		for (const TSharedPtr<FJsonValue>& Entry : Object->GetArrayField(TEXT("distribution")))
		{
			FMcIntProvider Source;
			if (!Source.Parse(Entry->AsObject()->TryGetField(TEXT("data"))))
			{
				return false;
			}
			Sources.Add(MoveTemp(Source));
			Weights.Add(Entry->AsObject()->GetIntegerField(TEXT("weight")));
		}
	}
	else if (Kind == TEXT("minecraft:trapezoid"))
	{
		Type = EType::Trapezoid;
		Min = Object->GetIntegerField(TEXT("min"));
		Max = Object->GetIntegerField(TEXT("max"));
		Object->TryGetNumberField(TEXT("plateau"), Plateau);
	}
	else
	{
		UE_LOG(LogMcGen, Error, TEXT("Unknown int provider %s"), *Kind);
		return false;
	}
	return true;
}

int32 FMcIntProvider::Sample(FMcRandom& Random) const
{
	switch (Type)
	{
	case EType::Constant:
		return Min;
	case EType::Uniform:
		return Between(Random, Min, Max);
	case EType::BiasedToBottom:
		return Min + Random.NextInt(Random.NextInt(Max - Min + 1) + 1);
	case EType::VeryBiasedToBottom:
		return Min + Random.NextInt(Random.NextInt(Random.NextInt(Max - Min + 1) + 1) + 1);
	case EType::Clamped:
		return FMath::Clamp(Sources[0].Sample(Random), Min, Max);
	case EType::ClampedNormal:
		return FMath::Clamp(FMath::RoundToInt32(Mean + Random.NextGaussian() * Deviation), Min, Max);
	case EType::Trapezoid:
	{
		// Two uniform draws added: flat across the plateau, sloping down to min and max.
		const int32 Span = Max - Min;
		if (Plateau >= Span)
		{
			return Between(Random, Min, Max);
		}
		const int32 Slope = (Span - Plateau) / 2;
		return Min + Between(Random, 0, Span - Slope) + Between(Random, 0, Slope);
	}
	case EType::WeightedList:
	{
		int32 Total = 0;
		for (const int32 Weight : Weights)
		{
			Total += Weight;
		}
		if (Total <= 0)
		{
			return 0;
		}
		int32 Pick = Random.NextInt(Total);
		for (int32 Index = 0; Index < Weights.Num(); ++Index)
		{
			Pick -= Weights[Index];
			if (Pick < 0)
			{
				return Sources[Index].Sample(Random);
			}
		}
		return 0;
	}
	}
	return Min;
}

bool FMcVerticalAnchor::Parse(const TSharedPtr<FJsonValue>& Json)
{
	const TSharedPtr<FJsonObject> Object = Json.IsValid() ? Json->AsObject() : nullptr;
	if (!Object.IsValid())
	{
		return false;
	}
	if (Object->TryGetNumberField(TEXT("absolute"), Value))
	{
		Type = EType::Absolute;
	}
	else if (Object->TryGetNumberField(TEXT("above_bottom"), Value))
	{
		Type = EType::AboveBottom;
	}
	else if (Object->TryGetNumberField(TEXT("below_top"), Value))
	{
		Type = EType::BelowTop;
	}
	else
	{
		return false;
	}
	return true;
}

int32 FMcVerticalAnchor::Resolve(int32 MinY, int32 Height) const
{
	switch (Type)
	{
	case EType::AboveBottom:
		return MinY + Value;
	case EType::BelowTop:
		return MinY + Height - 1 - Value;
	default:
		return Value;
	}
}

bool FMcHeightProvider::Parse(const TSharedPtr<FJsonValue>& Json)
{
	const TSharedPtr<FJsonObject> Object = Json.IsValid() ? Json->AsObject() : nullptr;
	if (!Object.IsValid())
	{
		return false;
	}
	// A bare anchor is a constant.
	if (!Object->HasField(TEXT("type")))
	{
		Type = EType::Constant;
		return Min.Parse(Json);
	}
	const FString Kind = TypeOf(*Object);
	if (Kind == TEXT("minecraft:constant"))
	{
		Type = EType::Constant;
		return Min.Parse(Object->TryGetField(TEXT("value")));
	}
	if (Kind == TEXT("minecraft:uniform"))
	{
		Type = EType::Uniform;
	}
	else if (Kind == TEXT("minecraft:biased_to_bottom"))
	{
		Type = EType::BiasedToBottom;
	}
	else if (Kind == TEXT("minecraft:very_biased_to_bottom"))
	{
		Type = EType::VeryBiasedToBottom;
	}
	else if (Kind == TEXT("minecraft:trapezoid"))
	{
		Type = EType::Trapezoid;
		Object->TryGetNumberField(TEXT("plateau"), Plateau);
	}
	else
	{
		UE_LOG(LogMcGen, Error, TEXT("Unknown height provider %s"), *Kind);
		return false;
	}
	Object->TryGetNumberField(TEXT("inner"), Inner);
	Inner = FMath::Max(Inner, 1);
	return Min.Parse(Object->TryGetField(TEXT("min_inclusive"))) && Max.Parse(Object->TryGetField(TEXT("max_inclusive")));
}

int32 FMcHeightProvider::Sample(FMcRandom& Random, int32 MinY, int32 Height) const
{
	const int32 Low = Min.Resolve(MinY, Height);
	const int32 High = Max.Resolve(MinY, Height);
	if (Type == EType::Constant || High <= Low)
	{
		return Low;
	}
	switch (Type)
	{
	case EType::Uniform:
		return Between(Random, Low, High);
	case EType::BiasedToBottom:
		return Low + Random.NextInt(Random.NextInt(High - Low - Inner + 1 > 0 ? High - Low - Inner + 1 : 1) + Inner);
	case EType::VeryBiasedToBottom:
	{
		// Three nested draws: most results sit near the bottom.
		const int32 First = Between(Random, Low + Inner, High);
		const int32 Second = Between(Random, Low, First - 1 >= Low ? First - 1 : Low);
		return Between(Random, Low, Second - 1 + Inner >= Low ? Second - 1 + Inner : Low);
	}
	case EType::Trapezoid:
	{
		const int32 Span = High - Low;
		if (Plateau >= Span)
		{
			return Between(Random, Low, High);
		}
		const int32 Slope = (Span - Plateau) / 2;
		return Low + Between(Random, 0, Span - Slope) + Between(Random, 0, Slope);
	}
	default:
		return Low;
	}
}
