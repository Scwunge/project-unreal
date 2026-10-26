// Seeded randomness for world generation. Xoroshiro128++ (Blackman and Vigna's public generator), seeded
// through SplitMix64 so that nearby seeds give unrelated streams. Every noise and feature gets its own
// stream, forked from the world seed by name or by position, so generating chunks in any order on any
// thread gives the same world.
//
// Not seed-matched with Minecraft: the same seed gives a different (but equally Minecraft-like) world.

#pragma once

#include "CoreMinimal.h"

class MCGEN_API FMcRandom
{
public:
	explicit FMcRandom(uint64 Seed);

	uint64 NextLong();
	// Uniform in [0, Bound). Bound must be positive.
	int32 NextInt(int32 Bound);
	// Uniform in [Min, Max], both inclusive.
	int32 NextIntBetween(int32 Min, int32 Max);
	// Uniform in [0, 1).
	double NextDouble();
	float NextFloat();
	bool NextBool();
	// Normally distributed, mean 0 and deviation 1.
	double NextGaussian();

	// A new stream for a named thing (a noise, a feature), independent of this one's position.
	FMcRandom Fork(const FString& Name) const;
	// A new stream for a block or chunk position, e.g. for decorating a chunk.
	FMcRandom Fork(int32 X, int32 Y, int32 Z) const;

	// SplitMix64's finaliser: a strong 64-bit mix.
	static uint64 Mix(uint64 Value);
	// A stable 64-bit hash of a string (FNV-1a, then mixed).
	static uint64 HashName(const FString& Name);

private:
	uint64 Lo = 0;
	uint64 Hi = 0;
	// The seed this stream was made from, for forking.
	uint64 Origin = 0;
};
