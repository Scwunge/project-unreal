#include "McRandom.h"

namespace
{
	constexpr uint64 Golden = 0x9E3779B97F4A7C15ull;

	uint64 RotateLeft(uint64 Value, int32 Shift)
	{
		return (Value << Shift) | (Value >> (64 - Shift));
	}
}

uint64 FMcRandom::Mix(uint64 Value)
{
	Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
	Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
	return Value ^ (Value >> 31);
}

uint64 FMcRandom::HashName(const FString& Name)
{
	uint64 Hash = 0xCBF29CE484222325ull;
	for (const TCHAR Char : Name)
	{
		Hash ^= static_cast<uint64>(Char);
		Hash *= 0x100000001B3ull;
	}
	return Mix(Hash);
}

FMcRandom::FMcRandom(uint64 Seed)
	: Origin(Seed)
{
	Lo = Mix(Seed + Golden);
	Hi = Mix(Seed + 2 * Golden);
	if (Lo == 0 && Hi == 0)
	{
		Lo = Golden;
	}
}

uint64 FMcRandom::NextLong()
{
	const uint64 Result = RotateLeft(Lo + Hi, 17) + Lo;
	const uint64 Xor = Hi ^ Lo;
	Lo = RotateLeft(Lo, 49) ^ Xor ^ (Xor << 21);
	Hi = RotateLeft(Xor, 28);
	return Result;
}

int32 FMcRandom::NextInt(int32 Bound)
{
	check(Bound > 0);
	// Lemire's multiply-shift with rejection, so every value is equally likely.
	const uint32 Range = static_cast<uint32>(Bound);
	uint64 Product = (NextLong() >> 32) * Range;
	uint32 Low = static_cast<uint32>(Product);
	if (Low < Range)
	{
		const uint32 Threshold = (0u - Range) % Range;
		while (Low < Threshold)
		{
			Product = (NextLong() >> 32) * Range;
			Low = static_cast<uint32>(Product);
		}
	}
	return static_cast<int32>(Product >> 32);
}

int32 FMcRandom::NextIntBetween(int32 Min, int32 Max)
{
	return Min + NextInt(Max - Min + 1);
}

double FMcRandom::NextDouble()
{
	return static_cast<double>(NextLong() >> 11) * 0x1.0p-53;
}

float FMcRandom::NextFloat()
{
	return static_cast<float>(NextLong() >> 40) * 0x1.0p-24f;
}

bool FMcRandom::NextBool()
{
	return (NextLong() >> 63) != 0;
}

double FMcRandom::NextGaussian()
{
	// Box-Muller; the second value is thrown away to keep the stream stateless.
	const double U = 1.0 - NextDouble();
	const double V = NextDouble();
	return FMath::Sqrt(-2.0 * FMath::Loge(U)) * FMath::Cos(2.0 * UE_DOUBLE_PI * V);
}

FMcRandom FMcRandom::Fork(const FString& Name) const
{
	return FMcRandom(Mix(Origin ^ HashName(Name)));
}

FMcRandom FMcRandom::Fork(int32 X, int32 Y, int32 Z) const
{
	uint64 Hash = Mix(Origin ^ 0x51ED27F1A2B3C4D5ull);
	Hash = Mix(Hash ^ static_cast<uint32>(X));
	Hash = Mix(Hash ^ static_cast<uint32>(Y));
	Hash = Mix(Hash ^ static_cast<uint32>(Z));
	return FMcRandom(Hash);
}
