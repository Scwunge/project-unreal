#include "McNoise.h"
#include "McCache.h"
#include "McGen.h"
#include "McRandom.h"

namespace
{
	// Perlin's 12 cube-edge gradients, padded to 16 so a hash picks one with a mask.
	constexpr int8 Gradients[16][3] = {
		{ 1, 1, 0 }, { -1, 1, 0 }, { 1, -1, 0 }, { -1, -1, 0 },
		{ 1, 0, 1 }, { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 },
		{ 0, 1, 1 }, { 0, -1, 1 }, { 0, 1, -1 }, { 0, -1, -1 },
		{ 1, 1, 0 }, { 0, -1, 1 }, { -1, 1, 0 }, { 0, -1, -1 },
	};

	double Grad(int32 Hash, double X, double Y, double Z)
	{
		const int8* G = Gradients[Hash & 15];
		return G[0] * X + G[1] * Y + G[2] * Z;
	}

	double Fade(double T)
	{
		return T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
	}

	// The classic terrain noise's base scale, in blocks per noise unit.
	constexpr double BlendedBaseScale = 684.412;
}

FMcImprovedNoise::FMcImprovedNoise(FMcRandom& Random)
{
	OriginX = Random.NextDouble() * 256.0;
	OriginY = Random.NextDouble() * 256.0;
	OriginZ = Random.NextDouble() * 256.0;
	for (int32 Index = 0; Index < 256; ++Index)
	{
		Permutation[Index] = static_cast<uint8>(Index);
	}
	for (int32 Index = 255; Index > 0; --Index)
	{
		Swap(Permutation[Index], Permutation[Random.NextInt(Index + 1)]);
	}
	FMemory::Memcpy(Permutation + 256, Permutation, 256);
}

double FMcImprovedNoise::Sample(double X, double Y, double Z) const
{
	X += OriginX;
	Y += OriginY;
	Z += OriginZ;
	const double FloorX = FMath::FloorToDouble(X);
	const double FloorY = FMath::FloorToDouble(Y);
	const double FloorZ = FMath::FloorToDouble(Z);
	const int32 CellX = static_cast<int32>(static_cast<int64>(FloorX) & 255);
	const int32 CellY = static_cast<int32>(static_cast<int64>(FloorY) & 255);
	const int32 CellZ = static_cast<int32>(static_cast<int64>(FloorZ) & 255);
	X -= FloorX;
	Y -= FloorY;
	Z -= FloorZ;
	const double U = Fade(X);
	const double V = Fade(Y);
	const double W = Fade(Z);

	const int32 A = Permutation[CellX] + CellY;
	const int32 AA = Permutation[A] + CellZ;
	const int32 AB = Permutation[A + 1] + CellZ;
	const int32 B = Permutation[CellX + 1] + CellY;
	const int32 BA = Permutation[B] + CellZ;
	const int32 BB = Permutation[B + 1] + CellZ;

	const double Near = FMath::Lerp(
		FMath::Lerp(Grad(Permutation[AA], X, Y, Z), Grad(Permutation[BA], X - 1, Y, Z), U),
		FMath::Lerp(Grad(Permutation[AB], X, Y - 1, Z), Grad(Permutation[BB], X - 1, Y - 1, Z), U), V);
	const double Far = FMath::Lerp(
		FMath::Lerp(Grad(Permutation[AA + 1], X, Y, Z - 1), Grad(Permutation[BA + 1], X - 1, Y, Z - 1), U),
		FMath::Lerp(Grad(Permutation[AB + 1], X, Y - 1, Z - 1), Grad(Permutation[BB + 1], X - 1, Y - 1, Z - 1), U), V);
	return FMath::Lerp(Near, Far, W);
}

bool FMcNoiseParams::Load(const FString& Id, FMcNoiseParams& Out)
{
	const FString Path = McCache::Data(TEXT("worldgen/noise/") + McCache::PathOf(Id) + TEXT(".json"));
	const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(Path);
	if (!Json.IsValid())
	{
		UE_LOG(LogMcGen, Error, TEXT("Noise %s not found"), *Id);
		return false;
	}
	FMcNoiseParams Result;
	Result.BaseOctave = static_cast<int32>(Json->GetNumberField(TEXT("base_octave")));
	Result.BaseAmplitude = Json->GetNumberField(TEXT("base_amplitude"));
	int32 Count = 1;
	Json->TryGetNumberField(TEXT("octave_count"), Count);
	const TArray<TSharedPtr<FJsonValue>>* Modifiers;
	if (Json->TryGetArrayField(TEXT("amplitude_modifiers"), Modifiers))
	{
		for (const TSharedPtr<FJsonValue>& Value : *Modifiers)
		{
			Result.Amplitudes.Add(Value->AsNumber());
		}
	}
	// Octaves without a modifier count fully.
	while (Result.Amplitudes.Num() < Count)
	{
		Result.Amplitudes.Add(1.0);
	}
	Out = MoveTemp(Result);
	return true;
}

FMcNoise::FMcNoise(FMcRandom& Random, const FMcNoiseParams& Params)
{
	double SumSquares = 0.0;
	for (int32 Index = 0; Index < Params.Amplitudes.Num(); ++Index)
	{
		const double Weight = Params.Amplitudes[Index] * FMath::Pow(0.5, Index);
		if (Weight == 0.0)
		{
			continue;
		}
		FMcRandom OctaveRandom = Random.Fork(FString::Printf(TEXT("octave%d"), Params.BaseOctave + Index));
		Octaves.Add({ FMcImprovedNoise(OctaveRandom), FMath::Pow(2.0, Params.BaseOctave + Index), Weight });
		SumSquares += Weight * Weight;
	}
	if (SumSquares <= 0.0)
	{
		return;
	}
	// Expected amplitude is three standard deviations.
	const double Scale = Params.BaseAmplitude / 3.0 / (FMath::Sqrt(SumSquares) * FMcImprovedNoise::StdDev);
	for (FOctave& Octave : Octaves)
	{
		Octave.Weight *= Scale;
		MaxValue += FMath::Abs(Octave.Weight);
	}
}

double FMcNoise::Sample(double X, double Y, double Z) const
{
	double Sum = 0.0;
	for (const FOctave& Octave : Octaves)
	{
		Sum += Octave.Weight * Octave.Noise.Sample(X * Octave.Frequency, Y * Octave.Frequency, Z * Octave.Frequency);
	}
	return Sum;
}

FMcBlendedNoise::FMcBlendedNoise(FMcRandom& Random, double XzScale, double YScale, double InXzFactor, double InYFactor, double SmearScaleMultiplier)
	: XzMultiplier(BlendedBaseScale * XzScale)
	, YMultiplier(BlendedBaseScale * YScale)
	, XzFactor(InXzFactor)
	, YFactor(InYFactor)
{
	// The smear multiplier only stretched the old noise's vertical detail; this version ignores it.
	FMcRandom MinRandom = Random.Fork(TEXT("min_limit"));
	FMcRandom MaxRandom = Random.Fork(TEXT("max_limit"));
	FMcRandom SelectorRandom = Random.Fork(TEXT("selector"));
	for (int32 Index = 0; Index < 16; ++Index)
	{
		MinLimit.Emplace(MinRandom);
		MaxLimit.Emplace(MaxRandom);
	}
	for (int32 Index = 0; Index < 8; ++Index)
	{
		Selector.Emplace(SelectorRandom);
	}
}

double FMcBlendedNoise::Sample(double X, double Y, double Z) const
{
	const double SX = X * XzMultiplier;
	const double SY = Y * YMultiplier;
	const double SZ = Z * XzMultiplier;

	// The selector: low octaves count most, each octave half the frequency and twice the weight of the last.
	double Select = 0.0;
	double Frequency = 1.0;
	for (const FMcImprovedNoise& Octave : Selector)
	{
		Select += Octave.Sample(SX / XzFactor * Frequency, SY / YFactor * Frequency, SZ / XzFactor * Frequency) / Frequency;
		Frequency *= 0.5;
	}
	const double T = FMath::Clamp((Select / 10.0 + 1.0) / 2.0, 0.0, 1.0);

	double Min = 0.0;
	double Max = 0.0;
	Frequency = 1.0;
	for (int32 Index = 0; Index < 16; ++Index)
	{
		const double PX = SX * Frequency;
		const double PY = SY * Frequency;
		const double PZ = SZ * Frequency;
		if (T < 1.0)
		{
			Min += MinLimit[Index].Sample(PX, PY, PZ) / Frequency;
		}
		if (T > 0.0)
		{
			Max += MaxLimit[Index].Sample(PX, PY, PZ) / Frequency;
		}
		Frequency *= 0.5;
	}
	return FMath::Lerp(Min / 512.0, Max / 512.0, T) / 128.0;
}
