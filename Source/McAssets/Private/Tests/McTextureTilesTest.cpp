#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McBlockTextures.h"
#include "McCache.h"
#include "McTextureTiles.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	// Where on the whole picture a point of a rectangular quad is: its UVs blended by how far along its two edges it is.
	FVector2f WholeUVAt(const FMcBakedQuad& Quad, const FVector3f& Point)
	{
		const FVector3f Down = Quad.Positions[1] - Quad.Positions[0];
		const FVector3f Across = Quad.Positions[3] - Quad.Positions[0];
		const float A = FVector3f::DotProduct(Point - Quad.Positions[0], Down) / Down.SizeSquared();
		const float B = FVector3f::DotProduct(Point - Quad.Positions[0], Across) / Across.SizeSquared();
		return Quad.UVs[0] + (Quad.UVs[1] - Quad.UVs[0]) * A + (Quad.UVs[3] - Quad.UVs[0]) * B;
	}

	float Area(const FMcBakedQuad& Quad)
	{
		return FVector3f::CrossProduct(Quad.Positions[1] - Quad.Positions[0], Quad.Positions[3] - Quad.Positions[0]).Size();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMcTextureTilesTest, "McClient.McAssets.TextureTiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ServerContext | EAutomationTestFlags::EngineFilter)

bool FMcTextureTilesTest::RunTest(const FString& Parameters)
{
	// ---- Which pictures are cut ----
	TestEqual(TEXT("a 16-pixel picture is one tile"), McTextureTiles::GetGrid(16, 16, false), FIntPoint(1, 1));
	TestEqual(TEXT("a 32-pixel one is two by two"), McTextureTiles::GetGrid(32, 32, false), FIntPoint(2, 2));
	TestEqual(TEXT("a 64-pixel one four by four"), McTextureTiles::GetGrid(64, 64, false), FIntPoint(4, 4));
	TestEqual(TEXT("an animated one isn't cut"), McTextureTiles::GetGrid(32, 32, true), FIntPoint(1, 1));
	TestEqual(TEXT("nor one that isn't whole tiles"), McTextureTiles::GetGrid(24, 24, false), FIntPoint(1, 1));
	TestEqual(TEXT("the sign's picture is two by two"), McTextureTiles::GetGrid(FName("minecraft:block/oak_sign")), FIntPoint(2, 2));
	TestEqual(TEXT("stone is one tile"), McTextureTiles::GetGrid(FName("minecraft:block/stone")), FIntPoint(1, 1));

	const FName Id = McTextureTiles::MakeTileId(FName("minecraft:block/oak_sign"), FIntPoint(1, 0), FIntPoint(2, 2));
	FName Picture;
	FIntPoint Tile, Grid;
	TestTrue(TEXT("a tile's id parses"), McTextureTiles::ParseTileId(Id, Picture, Tile, Grid));
	TestEqual(TEXT("back to its picture"), Picture, FName("minecraft:block/oak_sign"));
	TestEqual(TEXT("and its tile"), Tile, FIntPoint(1, 0));
	TestEqual(TEXT("and grid"), Grid, FIntPoint(2, 2));
	TestFalse(TEXT("a plain id is no tile"), McTextureTiles::ParseTileId(FName("minecraft:block/stone"), Picture, Tile, Grid));
	TestFalse(TEXT("nor is a fluid's side"), McTextureTiles::ParseTileId(FName("minecraft:block/water_flow#side"), Picture, Tile, Grid));

	// ---- A 32-pixel face across tiles: the sign board's front, pixels 0..24 by 2..14, on a block's south face ----
	FMcBakedQuad Board;
	Board.Texture = FName("minecraft:block/oak_sign");
	Board.Positions[0] = FVector3f(0, 1, 1);
	Board.Positions[1] = FVector3f(0, 0.5f, 1);
	Board.Positions[2] = FVector3f(1, 0.5f, 1);
	Board.Positions[3] = FVector3f(1, 1, 1);
	// UV corners: top left, bottom left, bottom right, top right; and the same face turned a quarter (uv rotation 90).
	const FVector2f TopLeft(0, 2 / 32.0f), BottomRight(24 / 32.0f, 14 / 32.0f);
	Board.UVs[0] = TopLeft;
	Board.UVs[1] = FVector2f(TopLeft.X, BottomRight.Y);
	Board.UVs[2] = BottomRight;
	Board.UVs[3] = FVector2f(BottomRight.X, TopLeft.Y);
	FMcBakedQuad Turned = Board;
	Turned.UVs[0] = Board.UVs[1];
	Turned.UVs[1] = Board.UVs[2];
	Turned.UVs[2] = Board.UVs[3];
	Turned.UVs[3] = Board.UVs[0];
	// Straddling all four tiles.
	FMcBakedQuad Middle = Board;
	Middle.UVs[0] = FVector2f(8 / 32.0f, 8 / 32.0f);
	Middle.UVs[1] = FVector2f(8 / 32.0f, 24 / 32.0f);
	Middle.UVs[2] = FVector2f(24 / 32.0f, 24 / 32.0f);
	Middle.UVs[3] = FVector2f(24 / 32.0f, 8 / 32.0f);

	struct FCase { const TCHAR* Name; const FMcBakedQuad* Quad; int32 Pieces; };
	for (const FCase& Case : { FCase{ TEXT("board"), &Board, 2 }, FCase{ TEXT("turned board"), &Turned, 2 }, FCase{ TEXT("middle"), &Middle, 4 } })
	{
		TArray<FMcBakedQuad> Pieces;
		McTextureTiles::SplitQuad(*Case.Quad, FIntPoint(2, 2), Pieces);
		TestEqual(FString::Printf(TEXT("%s: one piece a tile it touches"), Case.Name), Pieces.Num(), Case.Pieces);
		float Total = 0.0f;
		bool bContinuous = true, bInTile = true, bFacing = true;
		TSet<FName> Tiles;
		for (const FMcBakedQuad& Piece : Pieces)
		{
			Total += Area(Piece);
			bInTile &= McTextureTiles::ParseTileId(Piece.Texture, Picture, Tile, Grid) && Picture == Case.Quad->Texture;
			Tiles.Add(Piece.Texture);
			const FVector3f Normal = FVector3f::CrossProduct(Piece.Positions[1] - Piece.Positions[0], Piece.Positions[3] - Piece.Positions[0]);
			const FVector3f Original = FVector3f::CrossProduct(Case.Quad->Positions[1] - Case.Quad->Positions[0], Case.Quad->Positions[3] - Case.Quad->Positions[0]);
			bFacing &= FVector3f::DotProduct(Normal, Original) > 0.0f;
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				// Each corner shows, from its tile, the very pixel the whole quad shows at that point.
				const FVector2f Shown = (Piece.UVs[Corner] + FVector2f(Tile)) / FVector2f(Grid);
				const FVector2f Expected = WholeUVAt(*Case.Quad, Piece.Positions[Corner]);
				bInTile &= Piece.UVs[Corner].X >= 0.0f && Piece.UVs[Corner].X <= 1.0f && Piece.UVs[Corner].Y >= 0.0f && Piece.UVs[Corner].Y <= 1.0f;
				if (!Shown.Equals(Expected, 1e-5f))
				{
					bContinuous = false;
					AddError(FString::Printf(TEXT("%s: corner %d of %s shows %s, not %s"), Case.Name, Corner, *Piece.Texture.ToString(), *Shown.ToString(), *Expected.ToString()));
				}
			}
		}
		TestEqual(FString::Printf(TEXT("%s: each piece its own tile"), Case.Name), Tiles.Num(), Pieces.Num());
		TestTrue(FString::Printf(TEXT("%s: the UVs run on across the tiles' edge"), Case.Name), bContinuous);
		TestTrue(FString::Printf(TEXT("%s: UVs within each tile"), Case.Name), bInTile);
		TestTrue(FString::Printf(TEXT("%s: the pieces face the same way"), Case.Name), bFacing);
		TestTrue(FString::Printf(TEXT("%s: the pieces cover the quad"), Case.Name), FMath::IsNearlyEqual(Total, Area(*Case.Quad), 1e-5f));
	}
	// The board's two pieces meet where the picture crosses from tile 0 to tile 1, 16 of its 24 pixels along.
	TArray<FMcBakedQuad> Halves;
	McTextureTiles::SplitQuad(Board, FIntPoint(2, 2), Halves);
	if (Halves.Num() == 2)
	{
		TestTrue(TEXT("the left piece ends two thirds across"), FMath::IsNearlyEqual(Halves[0].Positions[3].X, 2 / 3.0f, 1e-5f));
		TestTrue(TEXT("where the right one starts"), FMath::IsNearlyEqual(Halves[1].Positions[0].X, 2 / 3.0f, 1e-5f));
		TestTrue(TEXT("at the left tile's right edge"), FMath::IsNearlyEqual(Halves[0].UVs[3].X, 1.0f, 1e-5f));
		TestTrue(TEXT("and the right tile's left edge"), FMath::IsNearlyEqual(Halves[1].UVs[0].X, 0.0f, 1e-5f));
	}
	TArray<FMcBakedQuad> Whole;
	McTextureTiles::SplitQuad(Board, FIntPoint(1, 1), Whole);
	TestTrue(TEXT("a picture of one tile keeps the quad as it is"), Whole.Num() == 1 && Whole[0].Texture == Board.Texture);

	// ---- The baker and the texture array ----
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	FMcBlockTextures& Textures = FMcBlockTextures::Get();
	if (!TestTrue(TEXT("registry loads"), Registry.EnsureLoaded()) || !TestTrue(TEXT("textures load"), Textures.EnsureLoaded()))
	{
		return false;
	}
	const FMcBakedModel& Sign = FMcBlockModels::Get().GetModel(Registry.ParseState(TEXT("minecraft:oak_sign[rotation=0]")).Get(0));
	bool bAllTiles = Sign.Quads.Num() > 0;
	for (const FMcBakedQuad& Quad : Sign.Quads)
	{
		bAllTiles &= McTextureTiles::ParseTileId(Quad.Texture, Picture, Tile, Grid) && Picture == FName("minecraft:block/oak_sign");
		bAllTiles &= Textures.GetLayer(Quad.Texture) != 0;
	}
	TestTrue(TEXT("a sign's quads are all drawn from loaded tiles of its picture"), bAllTiles);
	TestTrue(TEXT("the textures have tiles"), Textures.GetStats().Tiles > 0);

	// Each tile holds its picture's pixels unscaled.
	TArray<uint8> File;
	IImageWrapperModule& Images = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Png = Images.CreateImageWrapper(EImageFormat::PNG);
	TArray64<uint8> Raw;
	if (TestTrue(TEXT("the sign's picture reads"), FFileHelper::LoadFileToArray(File, *McCache::Asset(TEXT("textures/block/oak_sign.png")))
		&& Png->SetCompressed(File.GetData(), File.Num()) && Png->GetRaw(ERGBFormat::BGRA, 8, Raw) && Png->GetWidth() == 32))
	{
		const FColor* Pixels = reinterpret_cast<const FColor*>(Raw.GetData());
		bool bSame = true;
		for (int32 TileY = 0; TileY < 2; ++TileY)
		{
			for (int32 TileX = 0; TileX < 2; ++TileX)
			{
				const int32 Layer = Textures.GetLayer(McTextureTiles::MakeTileId(FName("minecraft:block/oak_sign"), FIntPoint(TileX, TileY), FIntPoint(2, 2)));
				if (Layer == 0)
				{
					continue;  // a tile no quad uses isn't loaded
				}
				const TConstArrayView<FColor> Layered = Textures.GetPixels(Layer);
				for (int32 Y = 0; Y < 16; ++Y)
				{
					for (int32 X = 0; X < 16; ++X)
					{
						bSame &= Layered[Y * 16 + X] == Pixels[(TileY * 16 + Y) * 32 + TileX * 16 + X];
					}
				}
			}
		}
		TestTrue(TEXT("every tile's pixels are the picture's"), bSame);
	}
	return true;
}

#endif
