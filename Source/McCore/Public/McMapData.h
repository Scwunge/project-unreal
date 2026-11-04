// The data of one map: a 128 by 128 grid of colour bytes (see McMapColors), the scale, where it is centred and the markers.
// The rules are fitted to the logged vanilla runs mapexp1 to mapexp4 (centre, marker byte, land and water shading at
// scale 0). Land shading at larger scales and the fill order while held are NOT fitted yet: the scaled rule below is the
// scale 0 rule applied to averaged heights.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"
#include "McMapColors.h"

struct MCCORE_API FMcMapMarker
{
	FName Type = TEXT("player");
	int8 X = 0;      // -128 .. 127, half a pixel each
	int8 Z = 0;
	uint8 Rotation = 0;  // 0 .. 15
};

struct MCCORE_API FMcMapData
{
	static constexpr int32 Size = 128;

	int32 Scale = 0;
	int32 CenterX = 0;
	int32 CenterZ = 0;
	FName Dimension = TEXT("minecraft:overworld");
	bool bLocked = false;
	// Counts the changes to the pixels, so a picture of the map knows when to be drawn again.
	int32 Revision = 0;
	TArray<uint8> Colors;
	TArray<FMcMapMarker> Markers;

	FMcMapData() { Colors.SetNumZeroed(Size * Size); }

	int32 BlocksPerPixel() const { return 1 << Scale; }

	// The centre of the map made at a block coordinate: the middle of the 128 << Scale cell that holds (C + 64)... (run mapexp1 to 4).
	static int32 CenterOf(int32 Coordinate, int32 Scale);
	void CenterOn(int32 X, int32 Z);

	// The marker position byte of a place: trunc(2 * (Pos - Centre) / BlocksPerPixel + 0.5), clamped to -128..127.
	// bOffMap is set when the place is beyond the edge (the marker is then drawn at the edge as the off-map kind).
	int8 MarkerByte(double Position, int32 Centre, bool& bOffMap) const;
	// Rotation 0..15 of a yaw in degrees (yaw -180 is 8, one more per 22.5 degrees).
	static uint8 MarkerRotation(double YawDegrees);

	// Brightness (0 low, 1 normal, 2 high) of land for the height step to the north neighbour (parity = (px + pz) & 1).
	// Scale 0 is the sign of the step; scales 1 and 2 (run mapexp5, flat rows) are the sign with a one step dither:
	// step -1 low/normal, 0 normal, +1 normal/high by parity, beyond that saturated.
	static int32 LandBrightness(double HeightMinusNorth, int32 Scale, int32 Parity);
	// Brightness of water of a depth at a pixel (parity = (px + pz) & 1).
	static int32 WaterBrightness(int32 Depth, int32 Parity);

	// What the sky sees in one block column.
	struct FColumn
	{
		FMcStateId Top = 0;   // the top block that has a map colour
		int32 Height = 0;     // y of that block
		int32 WaterDepth = 0; // blocks of water over the floor (Top is then the water)
	};

	// Rebuild the pixels of one map row range from a column sampler: Sample(BlockX, BlockZ). Returns pixels changed.
	int32 UpdateRow(int32 PixelZ, TFunctionRef<FColumn(int32, int32)> Sample);
	// The same for one column of pixels (the held map fills by columns, see ServerTickMaps).
	int32 UpdateColumn(int32 PixelX, TFunctionRef<FColumn(int32, int32)> Sample);

private:
	int32 UpdatePixel(int32 PixelX, int32 PixelZ, TFunctionRef<FColumn(int32, int32)> Sample);

public:
};
