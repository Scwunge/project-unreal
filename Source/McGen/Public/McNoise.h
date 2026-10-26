// The noises behind world generation.
//
// - FMcImprovedNoise is Ken Perlin's 2002 improved noise: a shuffled permutation table, 16 edge gradients
//   and the quintic fade.
// - FMcNoise is the fractal noise a worldgen/noise JSON describes. Octave i (counting from base_octave)
//   samples at 2^(base_octave + i) times the position, weighted by its amplitude modifier and halved per
//   octave. The sum is normalised so that base_amplitude is the expected amplitude of the output, taken as
//   three standard deviations: values are roughly normal and rarely leave +-base_amplitude.
// - FMcBlendedNoise is the classic 3D terrain noise behind old_blended_noise: two 16-octave limit noises
//   blended by an 8-octave selector.

#pragma once

#include "CoreMinimal.h"

class FMcRandom;

class MCGEN_API FMcImprovedNoise
{
public:
	explicit FMcImprovedNoise(FMcRandom& Random);

	// Roughly -1..1, standard deviation about 0.27.
	double Sample(double X, double Y, double Z) const;

	// The noise's standard deviation over random 3D points, used to normalise octave sums.
	static constexpr double StdDev = 0.27;

private:
	uint8 Permutation[512];
	// A random origin, so octaves sharing a seed don't line up at 0.
	double OriginX = 0.0;
	double OriginY = 0.0;
	double OriginZ = 0.0;
};

struct MCGEN_API FMcNoiseParams
{
	int32 BaseOctave = 0;
	double BaseAmplitude = 1.0;
	// One per octave; 0 skips that octave.
	TArray<double> Amplitudes;

	// Reads worldgen/noise/<name>.json. Id is like "minecraft:ridge".
	static bool Load(const FString& Id, FMcNoiseParams& Out);
};

class MCGEN_API FMcNoise
{
public:
	FMcNoise(FMcRandom& Random, const FMcNoiseParams& Params);

	double Sample(double X, double Y, double Z) const;

	// The largest value the noise can reach, from the octave weights.
	double GetMaxValue() const { return MaxValue; }

private:
	struct FOctave
	{
		FMcImprovedNoise Noise;
		double Frequency = 1.0;
		double Weight = 0.0;
	};
	TArray<FOctave> Octaves;
	double MaxValue = 0.0;
};

class MCGEN_API FMcBlendedNoise
{
public:
	FMcBlendedNoise(FMcRandom& Random, double XzScale, double YScale, double XzFactor, double YFactor, double SmearScaleMultiplier);

	double Sample(double X, double Y, double Z) const;

private:
	TArray<FMcImprovedNoise> MinLimit;
	TArray<FMcImprovedNoise> MaxLimit;
	TArray<FMcImprovedNoise> Selector;
	double XzMultiplier = 0.0;
	double YMultiplier = 0.0;
	double XzFactor = 1.0;
	double YFactor = 1.0;
};
