#include "McPortal.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"

namespace
{
	const FIntVector Directions[6] = { FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0) };

	FMcStateId GetState(const TCHAR* Name, const TCHAR* Property = nullptr, const TCHAR* Value = nullptr)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		if (!Property)
		{
			return Registry.FindDefaultState(Name).Get(0);
		}
		TMap<FName, FName> Properties;
		Properties.Add(Property, Value);
		return Registry.FindState(Name, Properties).Get(0);
	}

	FMcStateId Air() { return GetState(TEXT("minecraft:air")); }
	FMcStateId Obsidian() { return GetState(TEXT("minecraft:obsidian")); }
	FMcStateId Portal(bool bAlongX) { return GetState(TEXT("minecraft:nether_portal"), TEXT("axis"), bAlongX ? TEXT("x") : TEXT("z")); }

	FName NameOf(FMcStateId State)
	{
		return FMcBlockRegistry::Get().GetBlockOf(State).Name;
	}

	bool IsObsidian(FMcStateId State)
	{
		return NameOf(State) == TEXT("minecraft:obsidian");
	}

	// Air or fire (or a portal already, when checking one that is lit).
	bool IsOpen(FMcStateId State, bool bAllowPortal)
	{
		if (FMcBlockRegistry::Get().IsAir(State))
		{
			return true;
		}
		const FName Name = NameOf(State);
		return Name == TEXT("minecraft:fire") || (bAllowPortal && Name == TEXT("minecraft:nether_portal"));
	}

	FIntVector Along(bool bAlongX)
	{
		return bAlongX ? FIntVector(1, 0, 0) : FIntVector(0, 0, 1);
	}

	FIntVector Across(bool bAlongX)
	{
		return bAlongX ? FIntVector(0, 0, 1) : FIntVector(1, 0, 0);
	}

	bool IsSolid(const FMcBlockWorld& World, const FIntVector& Pos)
	{
		return FMcBlockShapes::Get().HasCollision(World.GetBlock(Pos));
	}

	// Whether a frame of 4 along and `Across` blocks across (centred on the portal's line) can be built with its lowest inside block at Origin: ground
	// beneath all of it, 4 blocks of air over it.
	bool CanHostFrame(const FMcBlockWorld& World, const FIntVector& Origin, bool bAlongX, int32 Across1)
	{
		const FIntVector Dir = Along(bAlongX);
		const FIntVector Cross = Across(bAlongX);
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		for (int32 I = -1; I <= 2; ++I)
		{
			for (int32 J = -Across1; J <= Across1; ++J)
			{
				const FIntVector Column = Origin + Dir * I + Cross * J;
				if (!IsSolid(World, Column + FIntVector(0, -1, 0)))
				{
					return false;
				}
				for (int32 K = 0; K <= 3; ++K)
				{
					if (!Registry.IsAir(World.GetBlock(Column + FIntVector(0, K, 0))))
					{
						return false;
					}
				}
			}
		}
		return true;
	}
}

bool McPortal::IsFrameSize(int32 Width, int32 Height)
{
	return Width >= MinInnerWidth && Width <= MaxInner && Height >= MinInnerHeight && Height <= MaxInner;
}

bool McPortal::IsPortalBlock(FMcStateId State)
{
	return NameOf(State) == TEXT("minecraft:nether_portal");
}

bool McPortal::IsAlongX(FMcStateId State)
{
	return FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("axis")) == TEXT("x");
}

bool McPortal::FindFrame(const FMcBlockWorld& World, const FIntVector& Pos, bool bAlongX, bool bAllowPortal, FFrame& OutFrame)
{
	const FIntVector Dir = Along(bAlongX);
	const FIntVector Up(0, 1, 0);
	const auto Open = [&](const FIntVector& At) { return IsOpen(World.GetBlock(At), bAllowPortal); };
	if (!Open(Pos))
	{
		return false;
	}
	// Down to the floor, then along to the left wall; the frame can't be bigger than the largest one, so stop looking there.
	FIntVector Corner = Pos;
	for (int32 Step = 0; Step <= MaxInner && Open(Corner - Up); ++Step)
	{
		Corner -= Up;
	}
	if (!IsObsidian(World.GetBlock(Corner - Up)))
	{
		return false;
	}
	for (int32 Step = 0; Step <= MaxInner && Open(Corner - Dir); ++Step)
	{
		Corner -= Dir;
	}
	if (!IsObsidian(World.GetBlock(Corner - Dir)))
	{
		return false;
	}
	int32 Width = 0;
	while (Width <= MaxInner && Open(Corner + Dir * Width))
	{
		++Width;
	}
	int32 Height = 0;
	while (Height <= MaxInner && Open(Corner + Up * Height))
	{
		++Height;
	}
	if (!IsFrameSize(Width, Height))
	{
		return false;
	}
	// The whole opening is empty, and obsidian runs along all four sides (the corners are free).
	for (int32 X = 0; X < Width; ++X)
	{
		if (!IsObsidian(World.GetBlock(Corner + Dir * X - Up)) || !IsObsidian(World.GetBlock(Corner + Dir * X + Up * Height)))
		{
			return false;
		}
		for (int32 Y = 0; Y < Height; ++Y)
		{
			if (!Open(Corner + Dir * X + Up * Y))
			{
				return false;
			}
		}
	}
	for (int32 Y = 0; Y < Height; ++Y)
	{
		if (!IsObsidian(World.GetBlock(Corner - Dir + Up * Y)) || !IsObsidian(World.GetBlock(Corner + Dir * Width + Up * Y)))
		{
			return false;
		}
	}
	OutFrame.Pos = Corner;
	OutFrame.bAlongX = bAlongX;
	OutFrame.Width = Width;
	OutFrame.Height = Height;
	return true;
}

int32 McPortal::Fill(FFrame Frame, const FSetBlock& SetBlock)
{
	const FMcStateId State = Portal(Frame.bAlongX);
	const FIntVector Dir = Along(Frame.bAlongX);
	for (int32 X = 0; X < Frame.Width; ++X)
	{
		for (int32 Y = 0; Y < Frame.Height; ++Y)
		{
			SetBlock(Frame.Pos + Dir * X + FIntVector(0, Y, 0), State);
		}
	}
	return Frame.Width * Frame.Height;
}

int32 McPortal::TryLight(FMcBlockWorld& World, const FIntVector& Pos, const FSetBlock& SetBlock)
{
	// There is no Nether portal in the End.
	if (World.GetDimension().Name == TEXT("minecraft:the_end"))
	{
		return 0;
	}
	FFrame Frame;
	for (const bool bAlongX : { true, false })
	{
		if (FindFrame(World, Pos, bAlongX, false, Frame))
		{
			return Fill(Frame, SetBlock);
		}
	}
	return 0;
}

int32 McPortal::RemoveBroken(FMcBlockWorld& World, const FIntVector& Pos, const FSetBlock& SetBlock)
{
	int32 Removed = 0;
	TSet<FIntVector> Seen;
	TArray<FIntVector> Starts;
	Starts.Add(Pos);
	for (const FIntVector& Offset : Directions)
	{
		Starts.Add(Pos + Offset);
	}
	for (const FIntVector& Start : Starts)
	{
		const FMcStateId StartState = World.GetBlock(Start);
		if (!IsPortalBlock(StartState) || Seen.Contains(Start))
		{
			continue;
		}
		// The portal this block is part of: the portal blocks joined to it.
		const bool bAlongX = IsAlongX(StartState);
		TArray<FIntVector> Blob;
		Blob.Add(Start);
		Seen.Add(Start);
		for (int32 Index = 0; Index < Blob.Num() && Index < 1000; ++Index)
		{
			const FIntVector At = Blob[Index];
			for (const FIntVector& Offset : Directions)
			{
				const FIntVector Next = At + Offset;
				const FMcStateId NextState = World.GetBlock(Next);
				if (!Seen.Contains(Next) && IsPortalBlock(NextState) && IsAlongX(NextState) == bAlongX)
				{
					Seen.Add(Next);
					Blob.Add(Next);
				}
			}
		}
		// Each block needs obsidian or portal on all four sides within the portal's plane.
		const FIntVector Dir = Along(bAlongX);
		bool bIntact = true;
		for (const FIntVector& At : Blob)
		{
			for (const FIntVector& Offset : { Dir, Dir * -1, FIntVector(0, 1, 0), FIntVector(0, -1, 0) })
			{
				const FMcStateId Beside = World.GetBlock(At + Offset);
				bIntact &= IsObsidian(Beside) || (IsPortalBlock(Beside) && IsAlongX(Beside) == bAlongX);
			}
		}
		if (!bIntact)
		{
			for (const FIntVector& At : Blob)
			{
				SetBlock(At, Air());
				++Removed;
			}
		}
	}
	return Removed;
}

FIntVector McPortal::ScalePosition(const FIntVector& Pos, double FromScale, double ToScale)
{
	const double Ratio = FromScale / ToScale;
	return FIntVector(FMath::FloorToInt(Pos.X * Ratio), Pos.Y, FMath::FloorToInt(Pos.Z * Ratio));
}

int32 McPortal::GetSearchRadius(double DestinationScale)
{
	return DestinationScale > 1.5 ? 16 : 128;
}

bool McPortal::FindPortal(const FMcBlockWorld& World, const FIntVector& Centre, int32 Radius, FFound& OutFound)
{
	const FMcStateId PortalX = Portal(true);
	const FMcStateId PortalZ = Portal(false);
	bool bFound = false;
	FIntVector Best = FIntVector::ZeroValue;
	int64 BestDistance = MAX_int64;
	const int32 MinChunkX = McCoords::BlockToChunk(Centre.X - Radius);
	const int32 MaxChunkX = McCoords::BlockToChunk(Centre.X + Radius);
	const int32 MinChunkZ = McCoords::BlockToChunk(Centre.Z - Radius);
	const int32 MaxChunkZ = McCoords::BlockToChunk(Centre.Z + Radius);
	for (int32 ChunkZ = MinChunkZ; ChunkZ <= MaxChunkZ; ++ChunkZ)
	{
		for (int32 ChunkX = MinChunkX; ChunkX <= MaxChunkX; ++ChunkX)
		{
			const FMcChunk* Chunk = World.FindChunk(FIntPoint(ChunkX, ChunkZ));
			if (!Chunk)
			{
				continue;
			}
			for (int32 Section = 0; Section < Chunk->NumSections(); ++Section)
			{
				const FMcChunkSection& Part = Chunk->GetSection(Section);
				if (Part.IsEmpty() || Part.Blocks.IsUniform())
				{
					continue;
				}
				for (int32 Index = 0; Index < FMcPalettedContainer::NumEntries; ++Index)
				{
					const FMcStateId State = Part.Blocks.Get(Index);
					if (State != PortalX && State != PortalZ)
					{
						continue;
					}
					const FIntVector At(ChunkX * 16 + (Index & 15), Chunk->SectionMinY(Section) + (Index >> 8), ChunkZ * 16 + ((Index >> 4) & 15));
					if (FMath::Abs(At.X - Centre.X) > Radius || FMath::Abs(At.Z - Centre.Z) > Radius)
					{
						continue;
					}
					const int64 DX = At.X - Centre.X, DY = At.Y - Centre.Y, DZ = At.Z - Centre.Z;
					const int64 Distance = DX * DX + DY * DY + DZ * DZ;
					// The nearest, and the lowest of equally near ones.
					if (Distance < BestDistance || (Distance == BestDistance && At.Y < Best.Y))
					{
						bFound = true;
						Best = At;
						BestDistance = Distance;
					}
				}
			}
		}
	}
	if (!bFound)
	{
		return false;
	}
	// Its bottom row, and how wide it is.
	const bool bAlongX = IsAlongX(World.GetBlock(Best));
	const FIntVector Dir = Along(bAlongX);
	const auto IsSame = [&](const FIntVector& At)
	{
		const FMcStateId State = World.GetBlock(At);
		return IsPortalBlock(State) && IsAlongX(State) == bAlongX;
	};
	while (IsSame(Best + FIntVector(0, -1, 0)))
	{
		Best += FIntVector(0, -1, 0);
	}
	while (IsSame(Best - Dir))
	{
		Best -= Dir;
	}
	int32 Width = 1;
	while (IsSame(Best + Dir * Width))
	{
		++Width;
	}
	OutFound.Pos = Best;
	OutFound.bAlongX = bAlongX;
	OutFound.Width = Width;
	return true;
}

McPortal::FFound McPortal::BuildPortal(FMcBlockWorld& World, const FIntVector& Centre, bool bAlongX, const FSetBlock& SetBlock)
{
	const FMcDimensionType& Dimension = World.GetDimension();
	const int32 Top = Dimension.MinY + Dimension.LogicalHeight;
	const FIntVector Dir = Along(bAlongX);
	const FIntVector Cross = Across(bAlongX);

	// Roomy patches first (4 along, 3 across), then narrow ones (4 by 1): the nearest, in 3 dimensions.
	FFound Result;
	Result.bAlongX = bAlongX;
	Result.Width = 2;
	for (const int32 Wide : { 1, 0 })
	{
		bool bFound = false;
		int64 BestDistance = MAX_int64;
		FIntVector Best = FIntVector::ZeroValue;
		for (int32 DZ = -16; DZ <= 16; ++DZ)
		{
			for (int32 DX = -16; DX <= 16; ++DX)
			{
				for (int32 Y = Top - 5; Y > Dimension.MinY; --Y)
				{
					const FIntVector Origin(Centre.X + DX, Y, Centre.Z + DZ);
					const int64 DY = Y - Centre.Y;
					const int64 Distance = static_cast<int64>(DX) * DX + static_cast<int64>(DZ) * DZ + DY * DY;
					if (Distance >= BestDistance || !IsSolid(World, Origin + FIntVector(0, -1, 0)) || !CanHostFrame(World, Origin, bAlongX, Wide))
					{
						continue;
					}
					bFound = true;
					BestDistance = Distance;
					Best = Origin;
				}
			}
		}
		if (bFound)
		{
			Result.Pos = Best;
			break;
		}
		if (Wide == 0)
		{
			// Nowhere to put it: make room at the centre, between 70 and 10 under the top, on a platform of its own.
			Result.Pos = FIntVector(Centre.X, FMath::Clamp(Centre.Y, 70, Top - 10), Centre.Z);
			const FMcStateId Stone = Obsidian();
			for (int32 I = 0; I <= 1; ++I)
			{
				for (int32 J = -1; J <= 1; ++J)
				{
					const FIntVector Column = Result.Pos + Dir * I + Cross * J;
					SetBlock(Column + FIntVector(0, -1, 0), Stone);
					for (int32 K = 0; K <= 2; ++K)
					{
						SetBlock(Column + FIntVector(0, K, 0), Air());
					}
				}
			}
		}
	}

	// The frame, 4 by 5 with its corners, round the portal.
	const FMcStateId Frame = Obsidian();
	const FMcStateId Fill = Portal(bAlongX);
	for (int32 I = -1; I <= 2; ++I)
	{
		for (int32 K = -1; K <= 3; ++K)
		{
			const bool bEdge = I == -1 || I == 2 || K == -1 || K == 3;
			SetBlock(Result.Pos + Dir * I + FIntVector(0, K, 0), bEdge ? Frame : Fill);
		}
	}
	return Result;
}

McPortal::FFound McPortal::FindOrBuildPortal(FMcBlockWorld& World, const FIntVector& Centre, bool bAlongX, const FSetBlock& SetBlock, bool& bOutBuilt)
{
	FFound Found;
	bOutBuilt = false;
	if (FindPortal(World, Centre, GetSearchRadius(World.GetDimension().CoordinateScale), Found))
	{
		return Found;
	}
	bOutBuilt = true;
	return BuildPortal(World, Centre, bAlongX, SetBlock);
}

FVector McPortal::GetStandingPoint(const FFound& Portal)
{
	const double Middle = Portal.Width / 2.0;
	return Portal.bAlongX ? FVector(Portal.Pos.X + Middle, Portal.Pos.Y, Portal.Pos.Z + 0.5) : FVector(Portal.Pos.X + 0.5, Portal.Pos.Y, Portal.Pos.Z + Middle);
}

bool McPortal::IsEndPortalFrame(FMcStateId State)
{
	return NameOf(State) == TEXT("minecraft:end_portal_frame");
}

bool McPortal::HasEye(FMcStateId State)
{
	return FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("eye")) == TEXT("true");
}

FMcStateId McPortal::WithEye(FMcStateId State)
{
	TMap<FName, FName> Properties;
	Properties.Add(TEXT("eye"), TEXT("true"));
	Properties.Add(TEXT("facing"), FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("facing")));
	return FMcBlockRegistry::Get().FindState(TEXT("minecraft:end_portal_frame"), Properties).Get(State);
}

bool McPortal::TryOpenEndPortal(FMcBlockWorld& World, const FIntVector& FramePos, const FSetBlock& SetBlock)
{
	// The twelve frames round a 3 by 3 hole: three along each side, none at the corners, each facing in.
	struct FSpot { int32 DX, DZ; const TCHAR* Facing; };
	TArray<FSpot> Ring;
	for (int32 Offset = -1; Offset <= 1; ++Offset)
	{
		Ring.Add({ Offset, -2, TEXT("south") });
		Ring.Add({ Offset, 2, TEXT("north") });
		Ring.Add({ -2, Offset, TEXT("east") });
		Ring.Add({ 2, Offset, TEXT("west") });
	}
	for (const FSpot& Mine : Ring)
	{
		// This frame as the Mine'th spot of a ring: where its middle would be.
		const FIntVector Centre(FramePos.X - Mine.DX, FramePos.Y, FramePos.Z - Mine.DZ);
		bool bComplete = true;
		for (const FSpot& Spot : Ring)
		{
			const FMcStateId State = World.GetBlock(Centre + FIntVector(Spot.DX, 0, Spot.DZ));
			bComplete &= IsEndPortalFrame(State) && HasEye(State) && FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("facing")) == Spot.Facing;
			if (!bComplete)
			{
				break;
			}
		}
		if (bComplete)
		{
			const FMcStateId Fill = GetState(TEXT("minecraft:end_portal"));
			for (int32 DZ = -1; DZ <= 1; ++DZ)
			{
				for (int32 DX = -1; DX <= 1; ++DX)
				{
					SetBlock(Centre + FIntVector(DX, 0, DZ), Fill);
				}
			}
			return true;
		}
	}
	return false;
}

void McPortal::BuildEndPlatform(FMcBlockWorld& World, const FSetBlock& SetBlock)
{
	const FMcStateId Stone = Obsidian();
	for (int32 DZ = -2; DZ <= 2; ++DZ)
	{
		for (int32 DX = -2; DX <= 2; ++DX)
		{
			SetBlock(FIntVector(EndPlatformX + DX, EndPlatformY, EndPlatformZ + DZ), Stone);
			for (int32 K = 1; K <= 3; ++K)
			{
				SetBlock(FIntVector(EndPlatformX + DX, EndPlatformY + K, EndPlatformZ + DZ), Air());
			}
		}
	}
}
