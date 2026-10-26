#include "McLightEngine.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McChunk.h"

TSharedRef<const FMcLightPalette> FMcLightPalette::Build()
{
	const FMcBlockLight& Light = FMcBlockLight::Get();
	const int32 NumStates = FMcBlockRegistry::Get().NumStates();

	TSharedRef<FMcLightPalette> Palette = MakeShared<FMcLightPalette>();
	Palette->Emission.SetNumUninitialized(NumStates);
	Palette->Dampening.SetNumUninitialized(NumStates);
	Palette->SkyDown.Init(false, NumStates);
	for (int32 Id = 0; Id < NumStates; ++Id)
	{
		const FMcStateId State = static_cast<FMcStateId>(Id);
		Palette->Emission[Id] = Light.GetEmission(State);
		Palette->Dampening[Id] = Light.GetDampening(State);
		Palette->SkyDown[Id] = Light.PropagatesSkylightDown(State);
	}
	return Palette;
}

FMcLightRegion FMcLightRegion::Capture(const FMcBlockWorld& World, FIntPoint CenterChunk)
{
	FMcLightRegion Region;
	Region.CenterChunk = CenterChunk;
	Region.MinY = World.GetDimension().MinY;

	const FMcChunk* Columns[3][3];
	int32 TopSection = -1;
	for (int32 DZ = 0; DZ < 3; ++DZ)
	{
		for (int32 DX = 0; DX < 3; ++DX)
		{
			const FMcChunk* Chunk = World.FindChunk(CenterChunk + FIntPoint(DX - 1, DZ - 1));
			Columns[DZ][DX] = Chunk;
			for (int32 Section = Chunk ? Chunk->NumSections() - 1 : -1; Section > TopSection; --Section)
			{
				if (!Chunk->GetSection(Section).IsEmpty())
				{
					TopSection = Section;
					break;
				}
			}
		}
	}

	// One empty section of headroom above the highest blocks, for light spreading up and sideways over them.
	const int32 NumSections = World.GetDimension().NumSections();
	Region.Height = FMath::Min(TopSection + 2, NumSections) * McCoords::SectionSize;
	const int32 NumCells = Size * Size * Region.Height;
	Region.Blocks.SetNumZeroed(NumCells);

	for (int32 DZ = 0; DZ < 3; ++DZ)
	{
		for (int32 DX = 0; DX < 3; ++DX)
		{
			const FMcChunk* Chunk = Columns[DZ][DX];
			if (!Chunk)
			{
				continue;
			}
			for (int32 Section = 0; Section * McCoords::SectionSize < Region.Height; ++Section)
			{
				if (Chunk->GetSection(Section).IsEmpty())
				{
					continue;
				}
				for (int32 LocalY = 0; LocalY < McCoords::SectionSize; ++LocalY)
				{
					const int32 Y = Section * McCoords::SectionSize + LocalY;
					for (int32 Z = 0; Z < McCoords::SectionSize; ++Z)
					{
						for (int32 X = 0; X < McCoords::SectionSize; ++X)
						{
							Region.Blocks[Region.Index(DX * McCoords::SectionSize + X, Y, DZ * McCoords::SectionSize + Z)] = Chunk->GetBlock(X, Region.MinY + Y, Z);
						}
					}
				}
			}
		}
	}
	return Region;
}

FMcStateId FMcLightRegion::GetBlock(int32 X, int32 Y, int32 Z) const
{
	const int32 Layer = Y - MinY;
	return Layer >= 0 && Layer < Height ? Blocks[Index(X, Layer, Z)] : 0;
}

uint8 FMcLightRegion::GetSkyLight(int32 X, int32 Y, int32 Z) const
{
	const int32 Layer = Y - MinY;
	return Layer < 0 ? 0 : Layer >= Height ? 15 : Sky[Index(X, Layer, Z)];
}

uint8 FMcLightRegion::GetBlockLight(int32 X, int32 Y, int32 Z) const
{
	const int32 Layer = Y - MinY;
	return Layer >= 0 && Layer < Height ? BlockLight[Index(X, Layer, Z)] : 0;
}

void FMcLightRegion::Spread(TArray<uint8>& Light, TArray<int32>& Queue, const FMcLightPalette& Palette) const
{
	// Breadth first. Light entering a block drops by its dampening, and by at least one per block. A cell is
	// queued again whenever it gets brighter, so dampening above one can't leave a cell too dark.
	const int32 Layer = Size * Size;
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
	{
		const int32 Cell = Queue[Head];
		const uint8 Level = Light[Cell];
		if (Level <= 1)
		{
			continue;
		}
		const int32 X = Cell % Size;
		const int32 Z = (Cell / Size) % Size;
		const int32 Y = Cell / Layer;

		auto Visit = [&](int32 Neighbour)
		{
			const uint8 Dampening = FMath::Max<uint8>(1, Palette.Dampening[Blocks[Neighbour]]);
			if (Level > Dampening && Light[Neighbour] < Level - Dampening)
			{
				Light[Neighbour] = Level - Dampening;
				Queue.Add(Neighbour);
			}
		};
		if (X > 0) Visit(Cell - 1);
		if (X < Size - 1) Visit(Cell + 1);
		if (Z > 0) Visit(Cell - Size);
		if (Z < Size - 1) Visit(Cell + Size);
		if (Y > 0) Visit(Cell - Layer);
		if (Y < Height - 1) Visit(Cell + Layer);
	}
}

void FMcLightRegion::Compute(const FMcLightPalette& Palette)
{
	const int32 NumCells = Blocks.Num();
	Sky.SetNumZeroed(NumCells);
	BlockLight.SetNumZeroed(NumCells);
	TArray<int32> Queue;

	// Sky light: full strength straight down each column until something stops it.
	const int32 Layer = Size * Size;
	TArray<int32> SkyBottom;  // per column, the lowest layer at full sky light
	SkyBottom.SetNumUninitialized(Layer);
	for (int32 Column = 0; Column < Layer; ++Column)
	{
		int32 Y = Height - 1;
		for (; Y >= 0 && Palette.SkyDown[Blocks[Y * Layer + Column]]; --Y)
		{
			Sky[Y * Layer + Column] = 15;
		}
		SkyBottom[Column] = Y + 1;
	}

	// Only cells next to something darker need to spread: the bottom of each column, and the cells whose
	// neighbouring column is lit less far down.
	for (int32 Z = 0; Z < Size; ++Z)
	{
		for (int32 X = 0; X < Size; ++X)
		{
			const int32 Column = Z * Size + X;
			int32 Lowest = SkyBottom[Column];
			if (X > 0) Lowest = FMath::Max(Lowest, SkyBottom[Column - 1]);
			if (X < Size - 1) Lowest = FMath::Max(Lowest, SkyBottom[Column + 1]);
			if (Z > 0) Lowest = FMath::Max(Lowest, SkyBottom[Column - Size]);
			if (Z < Size - 1) Lowest = FMath::Max(Lowest, SkyBottom[Column + Size]);
			// Lowest is now the highest of the neighbours' bottoms: below it, some neighbour is darker.
			for (int32 Y = SkyBottom[Column]; Y < FMath::Min(Lowest + 1, Height); ++Y)
			{
				Queue.Add(Y * Layer + Column);
			}
		}
	}
	// Sky light also enters the blocks under each column's full-strength part, such as water and leaves.
	for (int32 Column = 0; Column < Layer; ++Column)
	{
		const int32 Below = SkyBottom[Column] - 1;
		if (Below >= 0)
		{
			const int32 Cell = Below * Layer + Column;
			const uint8 Dampening = FMath::Max<uint8>(1, Palette.Dampening[Blocks[Cell]]);
			if (Dampening < 15)
			{
				Sky[Cell] = 15 - Dampening;
				Queue.Add(Cell);
			}
		}
	}
	Spread(Sky, Queue, Palette);

	// Block light: every emitting block is a source.
	Queue.Reset();
	for (int32 Cell = 0; Cell < NumCells; ++Cell)
	{
		if (const uint8 Emission = Palette.Emission[Blocks[Cell]])
		{
			BlockLight[Cell] = Emission;
			Queue.Add(Cell);
		}
	}
	Spread(BlockLight, Queue, Palette);
}
