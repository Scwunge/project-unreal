#include "McTextureTiles.h"
#include "McBlockModels.h"
#include "McCache.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Serialization/Archive.h"

namespace
{
	constexpr int32 TileSize = 16;
	const TCHAR* const TileMark = TEXT("#tile:");

	// A PNG's size from its IHDR chunk, which always comes first: the 8-byte signature, the chunk's length and type, then
	// the width and height, big-endian.
	bool ReadPngSize(const FString& Path, int32& Width, int32& Height)
	{
		const TUniquePtr<FArchive> File(IFileManager::Get().CreateFileReader(*Path));
		if (!File || File->TotalSize() < 24)
		{
			return false;
		}
		uint8 Header[24];
		File->Serialize(Header, sizeof(Header));
		if (Header[1] != 'P' || Header[2] != 'N' || Header[3] != 'G' || FMemory::Memcmp(Header + 12, "IHDR", 4) != 0)
		{
			return false;
		}
		auto BigEndian = [&](int32 At) { return (Header[At] << 24) | (Header[At + 1] << 16) | (Header[At + 2] << 8) | Header[At + 3]; };
		Width = BigEndian(16);
		Height = BigEndian(20);
		return Width > 0 && Height > 0;
	}
}

FIntPoint McTextureTiles::GetGrid(int32 Width, int32 Height, bool bAnimated)
{
	if (bAnimated || Width % TileSize != 0 || Height % TileSize != 0 || (Width <= TileSize && Height <= TileSize))
	{
		return FIntPoint(1, 1);
	}
	return FIntPoint(Width / TileSize, Height / TileSize);
}

FIntPoint McTextureTiles::GetGrid(FName TextureId)
{
	// The grid follows the game's own picture, not a resource pack's: a pack's 1024-pixel stone is scaled to the layer size
	// like any other pack picture (cutting it into 64 by 64 tiles baked 160 million quads), while a 32-pixel sign stays
	// two tiles by two with a pack's finer picture cut to match. A picture only a pack has is read from the pack.
	const FString Relative = McCache::RelativeOf(TextureId.ToString(), TEXT("textures/"), TEXT(".png"));
	FString Path = McCache::Root() / TEXT("client/assets/minecraft") / Relative;
	if (Relative.Contains(TEXT(":")) || !FPaths::FileExists(Path))
	{
		Path = McCache::Asset(Relative);
	}
	int32 Width = 0, Height = 0;
	if (!ReadPngSize(Path, Width, Height))
	{
		return FIntPoint(1, 1);
	}
	return GetGrid(Width, Height, FPaths::FileExists(Path + TEXT(".mcmeta")));
}

FName McTextureTiles::MakeTileId(FName TextureId, FIntPoint Tile, FIntPoint Grid)
{
	return FName(*FString::Printf(TEXT("%s%s%d,%d:%d,%d"), *TextureId.ToString(), TileMark, Tile.X, Tile.Y, Grid.X, Grid.Y));
}

bool McTextureTiles::ParseTileId(FName Id, FName& OutTextureId, FIntPoint& OutTile, FIntPoint& OutGrid)
{
	const FString Text = Id.ToString();
	const int32 Mark = Text.Find(TileMark, ESearchCase::CaseSensitive);
	if (Mark == INDEX_NONE)
	{
		return false;
	}
	TArray<FString> Numbers;
	Text.Mid(Mark + FCString::Strlen(TileMark)).Replace(TEXT(":"), TEXT(",")).ParseIntoArray(Numbers, TEXT(","));
	if (Numbers.Num() != 4)
	{
		return false;
	}
	OutTextureId = FName(*Text.Left(Mark));
	OutTile = FIntPoint(FCString::Atoi(*Numbers[0]), FCString::Atoi(*Numbers[1]));
	OutGrid = FIntPoint(FCString::Atoi(*Numbers[2]), FCString::Atoi(*Numbers[3]));
	return OutGrid.X > 0 && OutGrid.Y > 0 && OutTile.X >= 0 && OutTile.Y >= 0 && OutTile.X < OutGrid.X && OutTile.Y < OutGrid.Y;
}

void McTextureTiles::SplitQuad(const FMcBakedQuad& Quad, FIntPoint Grid, TArray<FMcBakedQuad>& Out)
{
	if (Grid.X <= 1 && Grid.Y <= 1)
	{
		Out.Add(Quad);
		return;
	}
	const FVector2f Scale(static_cast<float>(Grid.X), static_cast<float>(Grid.Y));
	const FVector2f* UV = Quad.UVs;

	// The corners as (A, B): A runs from corner 0 to corner 1, B from corner 0 to corner 3. A point of the quad is the
	// blend of its corners by those, and so is its UV.
	auto Blend = [](const auto (&Corners)[4], float A, float B)
	{
		return Corners[0] * ((1 - A) * (1 - B)) + Corners[1] * (A * (1 - B)) + Corners[2] * (A * B) + Corners[3] * ((1 - A) * B);
	};
	auto Piece = [&](FIntPoint Tile, float A0, float A1, float B0, float B1)
	{
		FMcBakedQuad& Made = Out.Add_GetRef(Quad);
		Made.Texture = MakeTileId(Quad.Texture, Tile, Grid);
		const float As[4] = { A0, A1, A1, A0 };
		const float Bs[4] = { B0, B0, B1, B1 };
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Made.Positions[Corner] = Blend(Quad.Positions, As[Corner], Bs[Corner]);
			const FVector2f Whole = Blend(Quad.UVs, As[Corner], Bs[Corner]);
			Made.UVs[Corner] = FVector2f(
				FMath::Clamp(Whole.X * Scale.X - Tile.X, 0.0f, 1.0f),
				FMath::Clamp(Whole.Y * Scale.Y - Tile.Y, 0.0f, 1.0f));
		}
	};

	const FVector2f Down = UV[1] - UV[0];
	const FVector2f Across = UV[3] - UV[0];
	const float Det = Down.X * Across.Y - Down.Y * Across.X;
	const bool bAligned = (FMath::IsNearlyZero(Down.X) && FMath::IsNearlyZero(Across.Y)) || (FMath::IsNearlyZero(Down.Y) && FMath::IsNearlyZero(Across.X));
	const FVector2f Middle = (UV[0] + UV[2]) * 0.5f;
	if (!bAligned || FMath::Abs(Det) < 1e-9f || !(UV[2] - (UV[0] + Down + Across)).IsNearlyZero(1e-4f))
	{
		const FIntPoint Tile(
			FMath::Clamp(FMath::FloorToInt32(Middle.X * Scale.X), 0, Grid.X - 1),
			FMath::Clamp(FMath::FloorToInt32(Middle.Y * Scale.Y), 0, Grid.Y - 1));
		Piece(Tile, 0.0f, 1.0f, 0.0f, 1.0f);
		return;
	}

	// Where a UV is, as (A, B).
	auto ToAB = [&](const FVector2f& Point)
	{
		const FVector2f R = Point - UV[0];
		return FVector2f((R.X * Across.Y - R.Y * Across.X) / Det, (Down.X * R.Y - Down.Y * R.X) / Det);
	};
	const FVector2f Min = FVector2f::Min(UV[0], UV[2]);
	const FVector2f Max = FVector2f::Max(UV[0], UV[2]);
	constexpr float Eps = 1e-5f;
	const int32 FirstX = FMath::Clamp(FMath::FloorToInt32(Min.X * Scale.X + Eps), 0, Grid.X - 1);
	const int32 LastX = FMath::Clamp(FMath::CeilToInt32(Max.X * Scale.X - Eps) - 1, FirstX, Grid.X - 1);
	const int32 FirstY = FMath::Clamp(FMath::FloorToInt32(Min.Y * Scale.Y + Eps), 0, Grid.Y - 1);
	const int32 LastY = FMath::Clamp(FMath::CeilToInt32(Max.Y * Scale.Y - Eps) - 1, FirstY, Grid.Y - 1);
	for (int32 TileY = FirstY; TileY <= LastY; ++TileY)
	{
		for (int32 TileX = FirstX; TileX <= LastX; ++TileX)
		{
			// The part of the quad's picture region in this tile (all of it along an edge of no width), as (A, B).
			const FVector2f CellMin(TileX / Scale.X, TileY / Scale.Y);
			const FVector2f CellMax((TileX + 1) / Scale.X, (TileY + 1) / Scale.Y);
			const FVector2f ClipMin(Min.X == Max.X ? Min.X : FMath::Max(Min.X, CellMin.X), Min.Y == Max.Y ? Min.Y : FMath::Max(Min.Y, CellMin.Y));
			const FVector2f ClipMax(Min.X == Max.X ? Max.X : FMath::Min(Max.X, CellMax.X), Min.Y == Max.Y ? Max.Y : FMath::Min(Max.Y, CellMax.Y));
			const FVector2f P = ToAB(ClipMin);
			const FVector2f Q = ToAB(ClipMax);
			const float A0 = FMath::Clamp(FMath::Min(P.X, Q.X), 0.0f, 1.0f);
			const float A1 = FMath::Clamp(FMath::Max(P.X, Q.X), 0.0f, 1.0f);
			const float B0 = FMath::Clamp(FMath::Min(P.Y, Q.Y), 0.0f, 1.0f);
			const float B1 = FMath::Clamp(FMath::Max(P.Y, Q.Y), 0.0f, 1.0f);
			Piece(FIntPoint(TileX, TileY), A0, A1, B0, B1);
		}
	}
}
