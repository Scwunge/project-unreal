#include "McCarver.h"
#include "Dom/JsonObject.h"
#include "McCache.h"
#include "McGen.h"
#include "McRandom.h"

struct FMcCarver::FWalk
{
	int32 ChunkX = 0;
	int32 ChunkZ = 0;
	TFunctionRef<void(int32, int32, int32)> CarveBlock;
};

bool FMcCarver::Load(const FString& InId, int32 InMinY, int32 InHeight)
{
	Id = McCache::Namespaced(InId);
	MinY = InMinY;
	Height = InHeight;
	const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(McCache::Data(TEXT("worldgen/carver/") + McCache::PathOf(Id) + TEXT(".json")));
	if (!Json.IsValid())
	{
		return false;
	}
	// Older files keep the fields in "config".
	TSharedPtr<FJsonObject> Config = Json;
	const TSharedPtr<FJsonObject>* Nested;
	if (Json->TryGetObjectField(TEXT("config"), Nested))
	{
		Config = *Nested;
	}
	const FString Type = McCache::Namespaced(Json->GetStringField(TEXT("type")));
	bCanyon = Type == TEXT("minecraft:canyon");
	if (!bCanyon && Type != TEXT("minecraft:cave"))
	{
		UE_LOG(LogMcGen, Error, TEXT("Carver %s: unknown type %s"), *Id, *Type);
		return false;
	}
	Probability = Config->GetNumberField(TEXT("probability"));
	bool bOk = Y.Parse(Config->TryGetField(TEXT("y")));
	if (bCanyon)
	{
		const TSharedPtr<FJsonObject>* Shape;
		bOk &= VerticalRotation.Parse(Config->TryGetField(TEXT("vertical_rotation")));
		bOk &= Config->TryGetObjectField(TEXT("shape"), Shape);
		if (bOk)
		{
			bOk &= DistanceFactor.Parse((*Shape)->TryGetField(TEXT("distance_factor")));
			bOk &= HorizontalRadiusFactor.Parse((*Shape)->TryGetField(TEXT("horizontal_radius_factor")));
			bOk &= Thickness.Parse((*Shape)->TryGetField(TEXT("thickness")));
			bOk &= YScale.Parse((*Shape)->TryGetField(TEXT("y_scale")));
			(*Shape)->TryGetNumberField(TEXT("vertical_radius_center_factor"), VerticalRadiusCenterFactor);
			(*Shape)->TryGetNumberField(TEXT("vertical_radius_default_factor"), VerticalRadiusDefaultFactor);
			(*Shape)->TryGetNumberField(TEXT("width_smoothness"), WidthSmoothness);
			WidthSmoothness = FMath::Max(WidthSmoothness, 1);
		}
	}
	else
	{
		bOk &= Count.Parse(Config->TryGetField(TEXT("count")));
		bOk &= Thickness.Parse(Config->TryGetField(TEXT("thickness")));
		bOk &= FloorLevel.Parse(Config->TryGetField(TEXT("floor_level")));
		bOk &= HorizontalRadiusMul.Parse(Config->TryGetField(TEXT("horizontal_radius_multiplier")));
		bOk &= VerticalRadiusMul.Parse(Config->TryGetField(TEXT("vertical_radius_multiplier")));
		bOk &= RoomVerticalRadius.Parse(Config->TryGetField(TEXT("room_vertical_radius_multiplier")));
		if (!StartVerticalRadius.Parse(Config->TryGetField(TEXT("start_vertical_radius_multiplier"))))
		{
			StartVerticalRadius = FMcFloatProvider();
			StartVerticalRadius.A = 1.0;
		}
		Config->TryGetBoolField(TEXT("weird_thickness_bias"), bWeirdThickness);
	}
	if (!bOk)
	{
		UE_LOG(LogMcGen, Error, TEXT("Carver %s: missing or bad fields"), *Id);
	}
	return bOk;
}

void FMcCarver::Carve(int32 ChunkX, int32 ChunkZ, uint64 Seed, TFunctionRef<bool(int32, int32)> Allowed, TFunctionRef<void(int32, int32, int32)> CarveBlock) const
{
	FWalk Walk{ ChunkX, ChunkZ, CarveBlock };
	const FMcRandom CarverRandom = FMcRandom(Seed).Fork(Id);
	for (int32 SourceZ = ChunkZ - Range; SourceZ <= ChunkZ + Range; ++SourceZ)
	{
		for (int32 SourceX = ChunkX - Range; SourceX <= ChunkX + Range; ++SourceX)
		{
			FMcRandom Random = CarverRandom.Fork(SourceX, 0, SourceZ);
			if (Random.NextDouble() >= Probability || !Allowed(SourceX, SourceZ))
			{
				continue;
			}
			if (bCanyon)
			{
				const FVector Pos(SourceX * 16 + Random.NextInt(16), Y.Sample(Random, MinY, Height), SourceZ * 16 + Random.NextInt(16));
				const double Yaw = Random.NextDouble() * UE_DOUBLE_TWO_PI;
				const double Pitch = VerticalRotation.Sample(Random);
				const double CanyonThickness = Thickness.Sample(Random);
				const int32 Length = FMath::FloorToInt32((Range * 2 - 1) * 16 * DistanceFactor.Sample(Random));
				CanyonTunnel(Walk, Random.NextLong(), Pos, CanyonThickness, Yaw, Pitch, Length);
				continue;
			}
			const int32 Starts = Count.Sample(Random);
			for (int32 Start = 0; Start < Starts; ++Start)
			{
				const FVector Pos(SourceX * 16 + Random.NextInt(16), Y.Sample(Random, MinY, Height), SourceZ * 16 + Random.NextInt(16));
				const double HorizontalMul = HorizontalRadiusMul.Sample(Random);
				const double VerticalMul = VerticalRadiusMul.Sample(Random);
				const double Floor = FloorLevel.Sample(Random);
				int32 Tunnels = 1;
				if (Random.NextInt(4) == 0)
				{
					// A room the tunnels leave from: 5 to 15 across.
					const double RoomRadius = 1.5 + 1.0 + Random.NextDouble() * 6.0;
					CarveEllipsoid(Walk, Pos + FVector(1.0, 0.0, 0.0), RoomRadius, RoomRadius * RoomVerticalRadius.Sample(Random), Floor, nullptr);
					Tunnels += Random.NextInt(4);
				}
				for (int32 Tunnel = 0; Tunnel < Tunnels; ++Tunnel)
				{
					const double Yaw = Random.NextDouble() * UE_DOUBLE_TWO_PI;
					const double Pitch = (Random.NextDouble() - 0.5) / 4.0;
					double TunnelThickness = Thickness.Sample(Random);
					if (bWeirdThickness && Random.NextInt(10) == 0)
					{
						TunnelThickness *= Random.NextDouble() * Random.NextDouble() * 3.0 + 1.0;
					}
					// 85 to 112 steps from root to tip.
					const int32 Length = 112 - Random.NextInt(28);
					CaveTunnel(Walk, Random.NextLong(), Pos, HorizontalMul, VerticalMul, TunnelThickness, Yaw, Pitch, 0, Length, StartVerticalRadius.Sample(Random), Floor);
				}
			}
		}
	}
}

void FMcCarver::CaveTunnel(FWalk& Walk, uint64 TunnelSeed, FVector Pos, double HorizontalMul, double VerticalMul, double TunnelThickness, double Yaw,
	double Pitch, int32 Start, int32 Length, double StartVerticalMul, double Floor) const
{
	FMcRandom Random(TunnelSeed);
	const int32 Branch = Random.NextInt(FMath::Max(Length / 2, 1)) + Length / 4;
	const bool bSteep = Random.NextInt(6) == 0;
	const FVector2D Target(Walk.ChunkX * 16 + 8.0, Walk.ChunkZ * 16 + 8.0);
	double YawDrift = 0.0;
	double PitchDrift = 0.0;
	for (int32 Step = Start; Step < Length; ++Step)
	{
		const double Radius = 1.5 + FMath::Sin(UE_DOUBLE_PI * Step / Length) * TunnelThickness;
		const double VerticalRadius = Radius * (Step == Start ? StartVerticalMul : 1.0);
		const double CosPitch = FMath::Cos(Pitch);
		Pos += FVector(FMath::Cos(Yaw) * CosPitch, FMath::Sin(Pitch), FMath::Sin(Yaw) * CosPitch);
		Pitch = Pitch * (bSteep ? 0.92 : 0.7) + PitchDrift * 0.1;
		Yaw += YawDrift * 0.1;
		PitchDrift = PitchDrift * 0.9 + (Random.NextDouble() - Random.NextDouble()) * Random.NextDouble() * 2.0;
		YawDrift = YawDrift * 0.75 + (Random.NextDouble() - Random.NextDouble()) * Random.NextDouble() * 4.0;

		if (Step == Branch && TunnelThickness > 1.0)
		{
			// Split into a T: two thinner branches at right angles.
			CaveTunnel(Walk, Random.NextLong(), Pos, HorizontalMul, VerticalMul, Random.NextDouble() * 0.5 + 0.5, Yaw - UE_DOUBLE_HALF_PI, Pitch / 3.0, Step, Length, 1.0, Floor);
			CaveTunnel(Walk, Random.NextLong(), Pos, HorizontalMul, VerticalMul, Random.NextDouble() * 0.5 + 0.5, Yaw + UE_DOUBLE_HALF_PI, Pitch / 3.0, Step, Length, 1.0, Floor);
			return;
		}
		// Pinches.
		if (Random.NextInt(4) == 0)
		{
			continue;
		}
		// Give up once the rest of the tunnel can't reach the chunk.
		const double Remaining = Length - Step;
		const double Reach = TunnelThickness + 2.0 + 16.0;
		if (FVector2D::DistSquared(FVector2D(Pos.X, Pos.Z), Target) - Remaining * Remaining > Reach * Reach)
		{
			return;
		}
		CarveEllipsoid(Walk, Pos, Radius * HorizontalMul, VerticalRadius * VerticalMul, Floor, nullptr);
	}
}

void FMcCarver::CanyonTunnel(FWalk& Walk, uint64 TunnelSeed, FVector Pos, double CanyonThickness, double Yaw, double Pitch, int32 Length) const
{
	FMcRandom Random(TunnelSeed);
	// Uneven walls: a width per y, changing every few layers.
	TArray<float> Widths;
	Widths.SetNum(Height);
	float Width = 1.0f;
	for (int32 Layer = 0; Layer < Height; ++Layer)
	{
		if (Layer == 0 || Random.NextInt(WidthSmoothness) == 0)
		{
			const float Factor = 1.0f + static_cast<float>(Random.NextDouble() * Random.NextDouble());
			Width = Factor * Factor;
		}
		Widths[Layer] = Width;
	}
	const FVector2D Target(Walk.ChunkX * 16 + 8.0, Walk.ChunkZ * 16 + 8.0);
	const double Scale = YScale.Sample(Random);
	double YawDrift = 0.0;
	double PitchDrift = 0.0;
	for (int32 Step = 0; Step < Length; ++Step)
	{
		const double Middle = FMath::Sin(UE_DOUBLE_PI * Step / Length);
		const double Radius = (1.5 + Middle * CanyonThickness) * HorizontalRadiusFactor.Sample(Random);
		const double VerticalRadius = Radius * Scale * (VerticalRadiusDefaultFactor + VerticalRadiusCenterFactor * Middle);
		const double CosPitch = FMath::Cos(Pitch);
		Pos += FVector(FMath::Cos(Yaw) * CosPitch, FMath::Sin(Pitch), FMath::Sin(Yaw) * CosPitch);
		Pitch = Pitch * 0.7 + PitchDrift * 0.05;
		Yaw += YawDrift * 0.05;
		PitchDrift = PitchDrift * 0.8 + (Random.NextDouble() - Random.NextDouble()) * Random.NextDouble() * 2.0;
		YawDrift = YawDrift * 0.5 + (Random.NextDouble() - Random.NextDouble()) * Random.NextDouble() * 4.0;
		if (Random.NextInt(4) == 0)
		{
			continue;
		}
		const double Remaining = Length - Step;
		const double Reach = CanyonThickness + 2.0 + 16.0;
		if (FVector2D::DistSquared(FVector2D(Pos.X, Pos.Z), Target) - Remaining * Remaining > Reach * Reach)
		{
			return;
		}
		CarveEllipsoid(Walk, Pos, Radius, VerticalRadius, -1.0, &Widths);
	}
}

void FMcCarver::CarveEllipsoid(FWalk& Walk, const FVector& Centre, double HorizontalRadius, double VerticalRadius, double Floor, const TArray<float>* WidthTable) const
{
	const int32 BaseX = Walk.ChunkX * 16;
	const int32 BaseZ = Walk.ChunkZ * 16;
	if (Centre.X + HorizontalRadius < BaseX || Centre.X - HorizontalRadius > BaseX + 16
		|| Centre.Z + HorizontalRadius < BaseZ || Centre.Z - HorizontalRadius > BaseZ + 16)
	{
		return;
	}
	HorizontalRadius = FMath::Max(HorizontalRadius, 0.5);
	VerticalRadius = FMath::Max(VerticalRadius, 0.5);
	const int32 MinX = FMath::Max(FMath::FloorToInt32(Centre.X - HorizontalRadius) - BaseX, 0);
	const int32 MaxX = FMath::Min(FMath::FloorToInt32(Centre.X + HorizontalRadius) - BaseX, 15);
	const int32 MinZ = FMath::Max(FMath::FloorToInt32(Centre.Z - HorizontalRadius) - BaseZ, 0);
	const int32 MaxZ = FMath::Min(FMath::FloorToInt32(Centre.Z + HorizontalRadius) - BaseZ, 15);
	// Never the bottom layer (bedrock) or the top of the world.
	const int32 LowY = FMath::Max(FMath::FloorToInt32(Centre.Y - VerticalRadius), MinY + 1);
	const int32 HighY = FMath::Min(FMath::FloorToInt32(Centre.Y + VerticalRadius), MinY + Height - 2);
	for (int32 X = MinX; X <= MaxX; ++X)
	{
		const double DX = (BaseX + X + 0.5 - Centre.X) / HorizontalRadius;
		for (int32 Z = MinZ; Z <= MaxZ; ++Z)
		{
			const double DZ = (BaseZ + Z + 0.5 - Centre.Z) / HorizontalRadius;
			const double Flat = DX * DX + DZ * DZ;
			if (Flat >= 1.0)
			{
				continue;
			}
			for (int32 BlockY = LowY; BlockY <= HighY; ++BlockY)
			{
				const double DY = (BlockY + 0.5 - Centre.Y) / VerticalRadius;
				if (DY <= Floor)
				{
					continue;
				}
				const double Widen = WidthTable ? (*WidthTable)[BlockY - MinY] : 1.0;
				if (Flat * Widen + DY * DY < 1.0)
				{
					Walk.CarveBlock(X, BlockY, Z);
				}
			}
		}
	}
}
