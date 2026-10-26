// Carvers from worldgen/carver: the old tunnel caves and canyons cut through the finished terrain
// (minecraft.wiki, "Carver", "Carver definition").
//
// A chunk is carved by every carver start within 8 chunks of it. A start (by the carver's probability) walks
// tunnels: worms that drift in yaw and pitch, carving an ellipsoid per step whose radius swells to the
// middle of the tunnel and pinches here and there. Caves sometimes begin in a room and branch once into a
// T; canyons are one long, tall, narrow cut with uneven walls. The wiki gives the sizes and shapes; the walk
// itself (drift, branching point, step skipping) is our own.

#pragma once

#include "CoreMinimal.h"
#include "McProviders.h"

class MCGEN_API FMcCarver
{
public:
	// Reads worldgen/carver/<name>.json, e.g. "minecraft:cave".
	bool Load(const FString& Id, int32 InMinY, int32 InHeight);

	const FString& GetId() const { return Id; }

	// Carves the chunk at ChunkX, ChunkZ. Allowed(SourceChunkX, SourceChunkZ) says whether a start chunk's biome
	// has this carver; Carve(LocalX, Y, LocalZ) carves one block of the target chunk.
	void Carve(int32 ChunkX, int32 ChunkZ, uint64 Seed, TFunctionRef<bool(int32, int32)> Allowed, TFunctionRef<void(int32, int32, int32)> CarveBlock) const;

	static constexpr int32 Range = 8;

private:
	struct FWalk;

	void CaveTunnel(FWalk& Walk, uint64 TunnelSeed, FVector Pos, double HorizontalMul, double VerticalMul, double Thickness, double Yaw,
		double Pitch, int32 Start, int32 Length, double StartVerticalMul, double Floor) const;
	void CanyonTunnel(FWalk& Walk, uint64 TunnelSeed, FVector Pos, double Thickness, double Yaw, double Pitch, int32 Length) const;
	// Carves the blocks of the target chunk inside the ellipsoid; WidthTable (by y, or null) widens layers.
	void CarveEllipsoid(FWalk& Walk, const FVector& Centre, double HorizontalRadius, double VerticalRadius, double Floor, const TArray<float>* WidthTable) const;

	FString Id;
	bool bCanyon = false;
	int32 MinY = -64;
	int32 Height = 384;
	double Probability = 0.0;
	FMcHeightProvider Y;
	// Caves.
	FMcIntProvider Count;
	FMcFloatProvider Thickness;
	FMcFloatProvider FloorLevel;
	FMcFloatProvider HorizontalRadiusMul;
	FMcFloatProvider VerticalRadiusMul;
	FMcFloatProvider RoomVerticalRadius;
	FMcFloatProvider StartVerticalRadius;
	bool bWeirdThickness = false;
	// Canyons.
	FMcFloatProvider VerticalRotation;
	FMcFloatProvider DistanceFactor;
	FMcFloatProvider HorizontalRadiusFactor;
	double VerticalRadiusCenterFactor = 0.0;
	double VerticalRadiusDefaultFactor = 1.0;
	int32 WidthSmoothness = 3;
	FMcFloatProvider YScale;
};
