#include "McSectionMesher.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "Misc/Crc.h"

FMcSectionSnapshot FMcSectionSnapshot::Capture(const FMcBlockWorld& World, FIntPoint ChunkPos, int32 SectionIndex)
{
	FMcSectionSnapshot Snapshot;
	Snapshot.ChunkPos = ChunkPos;
	Snapshot.SectionIndex = SectionIndex;
	Snapshot.MinY = World.GetDimension().MinY + SectionIndex * McCoords::SectionSize;
	Snapshot.Blocks.SetNumZeroed(Padded * Padded * Padded);

	// The 3x3 chunk columns the border can reach into.
	const FMcChunk* Columns[3][3];
	for (int32 DZ = -1; DZ <= 1; ++DZ)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			Columns[DZ + 1][DX + 1] = World.FindChunk(ChunkPos + FIntPoint(DX, DZ));
		}
	}

	for (int32 Y = -1; Y <= McCoords::SectionSize; ++Y)
	{
		const int32 WorldY = Snapshot.MinY + Y;
		for (int32 Z = -1; Z <= McCoords::SectionSize; ++Z)
		{
			const int32 ColumnZ = Z < 0 ? 0 : (Z >= McCoords::SectionSize ? 2 : 1);
			for (int32 X = -1; X <= McCoords::SectionSize; ++X)
			{
				const int32 ColumnX = X < 0 ? 0 : (X >= McCoords::SectionSize ? 2 : 1);
				if (const FMcChunk* Chunk = Columns[ColumnZ][ColumnX])
				{
					Snapshot.Blocks[Index(X, Y, Z)] = Chunk->GetBlock(McCoords::BlockToLocal(X), WorldY, McCoords::BlockToLocal(Z));
				}
			}
		}
	}
	return Snapshot;
}

namespace
{
	// Greybox colours: a few familiar blocks get their usual colour, the rest a stable colour from the name.
	FColor GreyboxColor(const FString& Name)
	{
		struct FKnown { const TCHAR* Fragment; FColor Color; };
		static const FKnown Known[] = {
			{ TEXT("grass_block"), FColor(95, 159, 53) },
			{ TEXT("dirt"), FColor(134, 96, 67) },
			{ TEXT("bedrock"), FColor(60, 60, 60) },
			{ TEXT("stone"), FColor(125, 125, 125) },
			{ TEXT("sand"), FColor(219, 207, 163) },
			{ TEXT("water"), FColor(63, 118, 228) },
			{ TEXT("lava"), FColor(207, 92, 20) },
			{ TEXT("snow"), FColor(249, 254, 254) },
			{ TEXT("ice"), FColor(145, 183, 253) },
			{ TEXT("leaves"), FColor(59, 122, 33) },
			{ TEXT("log"), FColor(109, 85, 50) },
			{ TEXT("planks"), FColor(162, 130, 78) },
			{ TEXT("gravel"), FColor(131, 127, 126) },
			{ TEXT("glass"), FColor(200, 230, 235) },
		};
		for (const FKnown& Entry : Known)
		{
			if (Name.Contains(Entry.Fragment))
			{
				return Entry.Color;
			}
		}
		const uint32 Hash = FCrc::StrCrc32(*Name);
		return FLinearColor::MakeFromHSV8(static_cast<uint8>(Hash), static_cast<uint8>(90 + (Hash >> 8) % 80), static_cast<uint8>(150 + (Hash >> 16) % 80)).ToFColor(true);
	}

	struct FFace
	{
		FIntVector Normal;  // Unreal axes
		FIntVector U;       // U x V == Normal
		FIntVector V;
		float Shade;        // the flat per-direction shading Minecraft draws faces with
	};

	// Unreal axes: X is Minecraft's east (+x), Y is south (+z), Z is up (+y).
	const FFace Faces[6] = {
		{ FIntVector(0, 0, 1), FIntVector(1, 0, 0), FIntVector(0, 1, 0), 1.0f },   // up
		{ FIntVector(0, 0, -1), FIntVector(0, 1, 0), FIntVector(1, 0, 0), 0.5f },  // down
		{ FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, 0, 1), 0.6f },   // east
		{ FIntVector(-1, 0, 0), FIntVector(0, 0, 1), FIntVector(0, 1, 0), 0.6f },  // west
		{ FIntVector(0, 1, 0), FIntVector(0, 0, 1), FIntVector(1, 0, 0), 0.8f },   // south
		{ FIntVector(0, -1, 0), FIntVector(1, 0, 0), FIntVector(0, 0, 1), 0.8f },  // north
	};

	FORCEINLINE FColor Shaded(FColor Color, float Shade)
	{
		return FColor(static_cast<uint8>(Color.R * Shade), static_cast<uint8>(Color.G * Shade), static_cast<uint8>(Color.B * Shade), 255);
	}
}

TSharedRef<const FMcGreyboxPalette> FMcGreyboxPalette::Build(const FMcBlockRegistry& Registry, bool bFlipWinding)
{
	TSharedRef<FMcGreyboxPalette> Palette = MakeShared<FMcGreyboxPalette>();
	Palette->bFlipWinding = bFlipWinding;
	Palette->StateColors.SetNumZeroed(Registry.NumStates());
	Palette->Solid.Init(false, Registry.NumStates());

	for (int32 BlockIndex = 0; BlockIndex < Registry.NumBlocks(); ++BlockIndex)
	{
		const FMcBlock& Block = Registry.GetBlock(BlockIndex);
		const FColor Color = GreyboxColor(Block.Name.ToString());
		for (const FMcStateId Id : Block.States)
		{
			Palette->StateColors[Id] = Color;
			Palette->Solid[Id] = !Registry.IsAir(Id);
		}
	}
	return Palette;
}

FMcSectionMesh McGreyboxMesher::Build(const FMcSectionSnapshot& Snapshot, const FMcGreyboxPalette& Palette)
{
	FMcSectionMesh Mesh;
	Mesh.SectionIndex = Snapshot.SectionIndex;

	auto IsSolid = [&](FMcStateId Id) { return Palette.Solid.IsValidIndex(Id) && Palette.Solid[Id]; };

	for (int32 Y = 0; Y < McCoords::SectionSize; ++Y)
	{
		for (int32 Z = 0; Z < McCoords::SectionSize; ++Z)
		{
			for (int32 X = 0; X < McCoords::SectionSize; ++X)
			{
				const FMcStateId State = Snapshot.Get(X, Y, Z);
				if (!IsSolid(State))
				{
					continue;
				}

				// The block's minimum corner in Unreal block units.
				const FIntVector Block(X, Z, Snapshot.MinY + Y);
				for (const FFace& Face : Faces)
				{
					// Unreal (X, Y, Z) offset back to Minecraft (x, y, z).
					if (IsSolid(Snapshot.Get(X + Face.Normal.X, Y + Face.Normal.Z, Z + Face.Normal.Y)))
					{
						continue;
					}

					const FIntVector Base = Block + FIntVector(FMath::Max(Face.Normal.X, 0), FMath::Max(Face.Normal.Y, 0), FMath::Max(Face.Normal.Z, 0));
					const FIntVector Corners[4] = { Base, Base + Face.U, Base + Face.U + Face.V, Base + Face.V };
					const FColor Color = Shaded(Palette.StateColors[State], Face.Shade);
					const FVector Normal(Face.Normal);

					const int32 First = Mesh.Positions.Num();
					for (const FIntVector& Corner : Corners)
					{
						Mesh.Positions.Add(FVector(Corner) * McCoords::UnitsPerBlock);
						Mesh.Normals.Add(Normal);
						Mesh.Colors.Add(Color);
					}

					// Corners go round anticlockwise seen from outside in the usual right-handed sense
					// (U x V == Normal). Unreal's front faces want the opposite order; bFlipWinding is the
					// escape hatch (mc.Render.FlipWinding) if that turns out wrong on first build.
					if (Palette.bFlipWinding)
					{
						Mesh.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
					}
					else
					{
						Mesh.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
					}
				}
			}
		}
	}
	return Mesh;
}
