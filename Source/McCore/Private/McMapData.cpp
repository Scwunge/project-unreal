#include "McMapData.h"

int32 FMcMapData::CenterOf(int32 Coordinate, int32 Scale)
{
	const int32 Span = 128 << Scale;
	return FMath::FloorToInt32((Coordinate + 64.0) / Span) * Span + Span / 2 - 64;
}

void FMcMapData::CenterOn(int32 X, int32 Z)
{
	CenterX = CenterOf(X, Scale);
	CenterZ = CenterOf(Z, Scale);
}

int8 FMcMapData::MarkerByte(double Position, int32 Centre, bool& bOffMap) const
{
	const double Raw = 2.0 * (Position - Centre) / BlocksPerPixel() + 0.5;
	const double Truncated = FMath::TruncToDouble(Raw);
	bOffMap = Truncated < -127.0 || Truncated > 127.0;
	return static_cast<int8>(FMath::Clamp(Truncated, -128.0, 127.0));
}

uint8 FMcMapData::MarkerRotation(double YawDegrees)
{
	const int32 Step = FMath::FloorToInt32((YawDegrees + 180.0) / 22.5 + 8.5);
	return static_cast<uint8>(((Step % 16) + 16) % 16);
}

int32 FMcMapData::LandBrightness(double HeightMinusNorth)
{
	// Scale 0 (mapexp4): up HIGH, same NORMAL, down LOW. Averaged heights at larger scales use the same sign (unfitted).
	return HeightMinusNorth > 0.0 ? 2 : (HeightMinusNorth < 0.0 ? 0 : 1);
}

int32 FMcMapData::WaterBrightness(int32 Depth, int32 Parity)
{
	// Fitted at scale 0: depth 1-2 HIGH, 3-4 HIGH/NORMAL by parity, 5-6 NORMAL, 7-9 NORMAL/LOW, 10+ LOW.
	if (Depth <= 2) return 2;
	if (Depth <= 4) return Parity == 0 ? 2 : 1;
	if (Depth <= 6) return 1;
	if (Depth <= 9) return Parity == 0 ? 1 : 0;
	return 0;
}

int32 FMcMapData::UpdateRow(int32 PixelZ, TFunctionRef<FColumn(int32, int32)> Sample)
{
	if (PixelZ < 0 || PixelZ >= Size)
	{
		return 0;
	}
	const int32 N = BlocksPerPixel();
	const int32 OriginX = CenterX - 64 * N;
	const int32 OriginZ = CenterZ - 64 * N;
	int32 Changed = 0;
	for (int32 PixelX = 0; PixelX < Size; ++PixelX)
	{
		auto Average = [&](int32 Row, int32& OutDepth, FMcStateId& OutTop)
		{
			double Sum = 0.0;
			int32 Depth = 0;
			for (int32 Dz = 0; Dz < N; ++Dz)
			{
				for (int32 Dx = 0; Dx < N; ++Dx)
				{
					const FColumn Column = Sample(OriginX + PixelX * N + Dx, OriginZ + Row * N + Dz);
					Sum += Column.Height;
					Depth += Column.WaterDepth;
					OutTop = Column.Top;
				}
			}
			OutDepth = Depth / (N * N);
			return Sum / (N * N);
		};
		int32 Depth = 0, NorthDepth = 0;
		FMcStateId Top = 0, NorthTop = 0;
		const double Here = Average(PixelZ, Depth, Top);
		const double North = Average(PixelZ - 1, NorthDepth, NorthTop);
		const uint8 Color = McMapColors::ColorOf(Top);
		uint8 Pixel = 0;
		if (Color != 0)
		{
			const int32 Brightness = Depth > 0 ? WaterBrightness(Depth, (PixelX + PixelZ) & 1) : LandBrightness(Here - North);
			Pixel = McMapColors::Pack(Color, Brightness);
		}
		uint8& Slot = Colors[PixelZ * Size + PixelX];
		if (Slot != Pixel)
		{
			Slot = Pixel;
			++Changed;
		}
	}
	return Changed;
}
