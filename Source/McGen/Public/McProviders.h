// The jar's value providers: numbers that worldgen JSON gives as a constant or as a random distribution
// (minecraft.wiki, "Float provider", "Int provider", "Height provider").
//
// - Float providers: a number, constant, uniform [min, max), clamped_normal, trapezoid.
// - Int providers: a number, constant, uniform [min, max], biased_to_bottom, very_biased_to_bottom, clamped,
//   clamped_normal, weighted_list, trapezoid.
// - Height providers: constant, uniform, biased_to_bottom, very_biased_to_bottom, trapezoid, with vertical
//   anchors (absolute, above_bottom, below_top).
//
// The biased distributions draw nested uniform numbers, so low values are more likely; their exact shape is
// our own.

#pragma once

#include "CoreMinimal.h"

class FJsonValue;
class FMcRandom;

struct MCGEN_API FMcFloatProvider
{
	enum class EType : uint8 { Constant, Uniform, ClampedNormal, Trapezoid };
	EType Type = EType::Constant;
	double A = 0.0;  // value, min, mean
	double B = 0.0;  // max, deviation
	double C = 0.0;  // plateau, clamp min
	double D = 0.0;  // clamp max

	bool Parse(const TSharedPtr<FJsonValue>& Json);
	double Sample(FMcRandom& Random) const;
};

struct MCGEN_API FMcIntProvider
{
	enum class EType : uint8 { Constant, Uniform, BiasedToBottom, VeryBiasedToBottom, Clamped, ClampedNormal, WeightedList, Trapezoid };
	EType Type = EType::Constant;
	int32 Min = 0;
	int32 Max = 0;
	double Mean = 0.0;
	double Deviation = 0.0;
	int32 Plateau = 0;
	TArray<FMcIntProvider> Sources;  // clamped: the one source; weighted_list: the entries
	TArray<int32> Weights;

	bool Parse(const TSharedPtr<FJsonValue>& Json);
	int32 Sample(FMcRandom& Random) const;
};

// A y given as { "absolute": y }, { "above_bottom": n } or { "below_top": n }.
struct MCGEN_API FMcVerticalAnchor
{
	enum class EType : uint8 { Absolute, AboveBottom, BelowTop };
	EType Type = EType::Absolute;
	int32 Value = 0;

	bool Parse(const TSharedPtr<FJsonValue>& Json);
	int32 Resolve(int32 MinY, int32 Height) const;
};

struct MCGEN_API FMcHeightProvider
{
	enum class EType : uint8 { Constant, Uniform, BiasedToBottom, VeryBiasedToBottom, Trapezoid };
	EType Type = EType::Constant;
	FMcVerticalAnchor Min;
	FMcVerticalAnchor Max;
	int32 Inner = 1;
	int32 Plateau = 0;

	bool Parse(const TSharedPtr<FJsonValue>& Json);
	int32 Sample(FMcRandom& Random, int32 MinY, int32 Height) const;
};
