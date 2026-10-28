#include "McAcoustics.h"

namespace
{
	// Marches a ray in half-block steps; the distance to the first solid block, or Max if there is none.
	float CastRay(const FVector& Origin, const FVector& Direction, float Max, McAcoustics::FSolidQuery IsSolid)
	{
		constexpr float Step = 0.5f;
		for (float T = Step; T < Max; T += Step)
		{
			const FVector At = Origin + Direction * T;
			if (IsSolid(FIntVector(FMath::FloorToInt(At.X), FMath::FloorToInt(At.Y), FMath::FloorToInt(At.Z))))
			{
				return T;
			}
		}
		return Max;
	}

	float Smooth(float X)
	{
		X = FMath::Clamp(X, 0.0f, 1.0f);
		return X * X * (3.0f - 2.0f * X);
	}
}

const TArray<FVector>& McAcoustics::GetRayDirections()
{
	static const TArray<FVector> Directions = []
	{
		const float Phi = (1.0f + FMath::Sqrt(5.0f)) * 0.5f;
		TArray<FVector> Out;
		for (const float A : { -1.0f, 1.0f })
		{
			for (const float B : { -Phi, Phi })
			{
				Out.Add(FVector(0.0f, A, B).GetSafeNormal());
				Out.Add(FVector(A, B, 0.0f).GetSafeNormal());
				Out.Add(FVector(B, 0.0f, A).GetSafeNormal());
			}
		}
		return Out;
	}();
	return Directions;
}

McAcoustics::FEnclosure McAcoustics::Summarise(TConstArrayView<float> RayDistances)
{
	const TArray<FVector>& Directions = GetRayDirections();
	FEnclosure Result;
	if (RayDistances.IsEmpty())
	{
		return Result;
	}
	int32 Escaped = 0;
	int32 Up = 0;
	int32 UpEscaped = 0;
	float Sum = 0.0f;
	for (int32 Index = 0; Index < RayDistances.Num(); ++Index)
	{
		const bool bEscaped = RayDistances[Index] >= MaxRayDistance;
		Escaped += bEscaped;
		Sum += FMath::Min(RayDistances[Index], MaxRayDistance);
		if (Directions.IsValidIndex(Index) && Directions[Index].Y > 0.1)
		{
			++Up;
			UpEscaped += bEscaped;
		}
	}
	Result.Openness = static_cast<float>(Escaped) / RayDistances.Num();
	Result.SkyOpenness = Up > 0 ? static_cast<float>(UpEscaped) / Up : Result.Openness;
	Result.MeanDistance = Sum / RayDistances.Num();
	return Result;
}

McAcoustics::FEnclosure McAcoustics::EstimateEnclosure(const FVector& Origin, FSolidQuery IsSolid)
{
	TArray<float, TInlineAllocator<NumRays>> Distances;
	for (const FVector& Direction : GetRayDirections())
	{
		Distances.Add(CastRay(Origin, Direction, MaxRayDistance, IsSolid));
	}
	return Summarise(Distances);
}

McAcoustics::FReverb McAcoustics::ToReverb(const FEnclosure& Enclosure, bool bBigDark)
{
	FReverb Out;
	// 0 in the open, 1 sealed in; the sky counts for more than the sides.
	const float Closed = FMath::Clamp(1.0f - (0.55f * Enclosure.Openness + 0.45f * Enclosure.SkyOpenness), 0.0f, 1.0f);
	// 0 for a closet, 1 for a cavern.
	const float Size = Smooth((Enclosure.MeanDistance - 2.0f) / (MaxRayDistance - 6.0f));
	Out.Volume = Smooth(Closed * 1.2f) * 0.75f;
	Out.DecayTime = FMath::Lerp(0.35f, 5.0f, Size);
	Out.Diffusion = FMath::Lerp(0.6f, 1.0f, Size);
	Out.Density = FMath::Lerp(0.7f, 1.0f, Size);
	Out.GainHF = FMath::Lerp(0.85f, 0.55f, Size);
	Out.ReflectionsGain = FMath::Lerp(0.2f, 0.05f, Size);
	Out.ReflectionsDelay = FMath::Lerp(0.005f, 0.09f, Size);
	Out.LateGain = FMath::Lerp(0.8f, 1.6f, Size);
	Out.LateDelay = FMath::Lerp(0.01f, 0.05f, Size);
	if (bBigDark)
	{
		// A roofed hell: always a big dark hall.
		Out.Volume = FMath::Max(Out.Volume, 0.55f);
		Out.DecayTime = FMath::Max(Out.DecayTime * 1.5f, 4.5f);
		Out.GainHF = FMath::Min(Out.GainHF, 0.4f);
		Out.ReflectionsDelay = FMath::Max(Out.ReflectionsDelay, 0.07f);
		Out.LateGain = FMath::Max(Out.LateGain, 1.5f);
	}
	return Out;
}

McAcoustics::FOcclusion McAcoustics::GetOcclusion(int32 SolidBlocks)
{
	FOcclusion Out;
	if (SolidBlocks <= 0)
	{
		return Out;
	}
	// Each block takes a bit of the volume and most of what is left of the high end.
	Out.Volume = FMath::Max(0.35f, 1.0f - 0.2f * SolidBlocks);
	Out.LowpassHz = FMath::Max(500.0f, 20000.0f * FMath::Pow(0.35f, static_cast<float>(SolidBlocks)));
	return Out;
}

int32 McAcoustics::CountSolidBetween(const FVector& From, const FVector& To, FSolidQuery IsSolid)
{
	const FIntVector Start(FMath::FloorToInt(From.X), FMath::FloorToInt(From.Y), FMath::FloorToInt(From.Z));
	const FIntVector End(FMath::FloorToInt(To.X), FMath::FloorToInt(To.Y), FMath::FloorToInt(To.Z));
	const FVector Delta = To - From;
	const float Length = static_cast<float>(Delta.Size());
	if (Length < 0.01f)
	{
		return 0;
	}
	const FVector Direction = Delta / Length;
	int32 Count = 0;
	FIntVector Last = Start;
	for (float T = 0.25f; T < Length; T += 0.25f)
	{
		const FVector At = From + Direction * T;
		const FIntVector Cell(FMath::FloorToInt(At.X), FMath::FloorToInt(At.Y), FMath::FloorToInt(At.Z));
		if (Cell == Last)
		{
			continue;
		}
		Last = Cell;
		if (Cell != Start && Cell != End && IsSolid(Cell))
		{
			++Count;
		}
	}
	return Count;
}

bool McAcoustics::IsSkyOpen(const FIntVector& Cell, FSolidQuery IsSolid, int32 Up)
{
	for (int32 Y = 1; Y <= Up; ++Y)
	{
		if (IsSolid(Cell + FIntVector(0, Y, 0)))
		{
			return false;
		}
	}
	return true;
}

int32 McAcoustics::EstimateBlockLight(const FIntVector& Cell, TFunctionRef<int32(const FIntVector&)> EmissionAt, int32 Radius)
{
	int32 Best = 0;
	for (int32 X = -Radius; X <= Radius; ++X)
	{
		for (int32 Y = -Radius; Y <= Radius; ++Y)
		{
			for (int32 Z = -Radius; Z <= Radius; ++Z)
			{
				const int32 Distance = FMath::Abs(X) + FMath::Abs(Y) + FMath::Abs(Z);
				if (Distance >= 15 || 15 - Distance <= Best)
				{
					continue;
				}
				Best = FMath::Max(Best, EmissionAt(Cell + FIntVector(X, Y, Z)) - Distance);
			}
		}
	}
	return FMath::Clamp(Best, 0, 15);
}

bool McAcoustics::TickMood(float& Meter, int32 Light, int32 DelayTicks)
{
	const float Delay = static_cast<float>(FMath::Max(DelayTicks, 1));
	if (Light <= 7)
	{
		// The darker, the faster: full dark takes the whole delay.
		Meter += (8.0f - Light) / 8.0f / Delay;
	}
	else
	{
		Meter = FMath::Max(0.0f, Meter - (Light - 7.0f) / 8.0f / Delay);
	}
	if (Meter >= 1.0f)
	{
		Meter = 0.0f;
		return true;
	}
	return false;
}

McAcoustics::FEcho McAcoustics::GetEcho(float DistanceBlocks, const FEnclosure& Enclosure)
{
	FEcho Out;
	const float Closed = FMath::Clamp(1.0f - (0.55f * Enclosure.Openness + 0.45f * Enclosure.SkyOpenness), 0.0f, 1.0f);
	if (DistanceBlocks < 10.0f || Closed < 0.5f)
	{
		return Out;
	}
	const float Size = Smooth((Enclosure.MeanDistance - 2.0f) / (MaxRayDistance - 6.0f));
	// Sound goes 343 blocks a second: out and back over a wall that far away is a few tenths.
	Out.DelaySeconds = FMath::Clamp(DistanceBlocks / 40.0f + Size * 0.25f, 0.2f, 0.9f);
	Out.Volume = FMath::Clamp(0.18f + 0.3f * Size * Closed, 0.0f, 0.5f);
	return Out;
}
