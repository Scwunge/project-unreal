#include "McRails.h"
#include "McBlockWorld.h"

namespace
{
	// North, east, south, west (each the next clockwise).
	const FIntVector Steps[4] = { FIntVector(0, 0, -1), FIntVector(1, 0, 0), FIntVector(0, 0, 1), FIntVector(-1, 0, 0) };
	const TCHAR* const SideNames[4] = { TEXT("north"), TEXT("east"), TEXT("south"), TEXT("west") };
	const FIntVector Up(0, 1, 0);

	int32 Opposite(int32 Side)
	{
		return (Side + 2) % 4;
	}

	FName ShapeFor(int32 SideA, int32 SideB)
	{
		// Sides in clockwise order: the two of a curve in either order.
		if (SideA > SideB)
		{
			Swap(SideA, SideB);
		}
		if (SideA == 0 && SideB == 2) return TEXT("north_south");
		if (SideA == 1 && SideB == 3) return TEXT("east_west");
		if (SideA == 0 && SideB == 1) return TEXT("north_east");
		if (SideA == 1 && SideB == 2) return TEXT("south_east");
		if (SideA == 2 && SideB == 3) return TEXT("south_west");
		return TEXT("north_west");
	}

	bool IsRailAt(const FMcBlockWorld& World, const FIntVector& Pos)
	{
		return McRails::IsRail(World.GetBlock(Pos));
	}

	// The side (0 to 3) of a step, or -1.
	int32 SideOf(const FIntVector& Step)
	{
		for (int32 Side = 0; Side < 4; ++Side)
		{
			if (Steps[Side] == Step)
			{
				return Side;
			}
		}
		return -1;
	}

	// Whether the rail at Pos has an end that joins a rail.
	int32 CountLinks(const FMcBlockWorld& World, const FIntVector& Pos)
	{
		McRails::FTrack Track;
		if (!McRails::GetTrack(World.GetBlock(Pos), Track))
		{
			return 0;
		}
		int32 Links = 0;
		FIntVector Next;
		Links += McRails::FindNext(World, Pos, Track.EndA, Track.bRiseA, Next) ? 1 : 0;
		Links += McRails::FindNext(World, Pos, Track.EndB, Track.bRiseB, Next) ? 1 : 0;
		return Links;
	}

	// Whether the rail at Other would take a rail at Pos on its side Toward (a step from Other to Pos): it already
	// leads there, or it has an end free.
	bool WouldJoin(const FMcBlockWorld& World, const FIntVector& Other, const FIntVector& Pos)
	{
		McRails::FTrack Track;
		if (!McRails::GetTrack(World.GetBlock(Other), Track))
		{
			return false;
		}
		const FIntVector Flat(Pos.X - Other.X, 0, Pos.Z - Other.Z);
		if (Track.EndA == Flat || Track.EndB == Flat)
		{
			return true;
		}
		return CountLinks(World, Other) < 2;
	}
}

bool McRails::IsRail(FName BlockName)
{
	return BlockName == TEXT("minecraft:rail") || BlockName == TEXT("minecraft:powered_rail") || BlockName == TEXT("minecraft:detector_rail")
		|| BlockName == TEXT("minecraft:activator_rail");
}

bool McRails::IsRail(FMcStateId State)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	return Registry.IsValidState(State) && IsRail(Registry.GetBlockOf(State).Name);
}

bool McRails::CanCurve(FName BlockName)
{
	return BlockName == TEXT("minecraft:rail");
}

bool McRails::GetTrack(FName Shape, FTrack& OutTrack)
{
	const FString Name = Shape.ToString();
	OutTrack = FTrack();
	if (Name.StartsWith(TEXT("ascending_")))
	{
		const FString Toward = Name.Mid(10);
		for (int32 Side = 0; Side < 4; ++Side)
		{
			if (Toward == SideNames[Side])
			{
				OutTrack.EndA = Steps[Side];
				OutTrack.bRiseA = true;
				OutTrack.EndB = Steps[Opposite(Side)];
				return true;
			}
		}
		return false;
	}
	// "north_south", "south_east": two sides.
	FString First, Second;
	if (!Name.Split(TEXT("_"), &First, &Second))
	{
		return false;
	}
	int32 SideA = -1, SideB = -1;
	for (int32 Side = 0; Side < 4; ++Side)
	{
		SideA = First == SideNames[Side] ? Side : SideA;
		SideB = Second == SideNames[Side] ? Side : SideB;
	}
	if (SideA < 0 || SideB < 0)
	{
		return false;
	}
	OutTrack.EndA = Steps[SideA];
	OutTrack.EndB = Steps[SideB];
	return true;
}

bool McRails::GetTrack(FMcStateId State, FTrack& OutTrack)
{
	return IsRail(State) && GetTrack(FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("shape")), OutTrack);
}

FMcStateId McRails::WithShape(FMcStateId State, FName Shape)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	TMap<FName, FName> Properties;
	Properties.Add(TEXT("shape"), Shape);
	for (const TCHAR* Kept : { TEXT("powered"), TEXT("waterlogged") })
	{
		const FName Value = Registry.GetPropertyValue(State, Kept);
		if (!Value.IsNone())
		{
			Properties.Add(Kept, Value);
		}
	}
	return Registry.FindState(Registry.GetBlockOf(State).Name, Properties).Get(State);
}

bool McRails::FindNext(const FMcBlockWorld& World, const FIntVector& From, const FIntVector& End, bool bRise, FIntVector& OutNext)
{
	const FIntVector Across = From + End;
	// Level with where this end reaches (one up, from the high end of a slope), then one below it.
	const FIntVector Level = bRise ? Across + Up : Across;
	const FIntVector Back(-End.X, 0, -End.Z);
	for (const FIntVector& Candidate : { Level, Level - Up })
	{
		FTrack Track;
		if (!IsRailAt(World, Candidate) || !GetTrack(World.GetBlock(Candidate), Track))
		{
			continue;
		}
		// Its end toward us has to reach the same height: a low end if it is level with ours, the high end of a slope if it is below.
		const bool bBelow = Candidate != Level;
		const bool bMeets = (Track.EndA == Back && Track.bRiseA == bBelow) || (Track.EndB == Back && Track.bRiseB == bBelow);
		if (bMeets)
		{
			OutNext = Candidate;
			return true;
		}
	}
	return false;
}

FName McRails::ChooseShape(const FMcBlockWorld& World, const FIntVector& Pos, FMcStateId State)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const bool bCurves = CanCurve(Registry.GetBlockOf(State).Name);
	const FName Current = Registry.GetPropertyValue(State, TEXT("shape"));

	// The sides with a rail to join, and whether it is above.
	bool bJoin[4] = { false, false, false, false };
	bool bAbove[4] = { false, false, false, false };
	for (int32 Side = 0; Side < 4; ++Side)
	{
		const FIntVector Level = Pos + Steps[Side];
		if (IsRailAt(World, Level + Up) && WouldJoin(World, Level + Up, Pos))
		{
			bJoin[Side] = true;
			bAbove[Side] = true;
		}
		else if (IsRailAt(World, Level) && WouldJoin(World, Level, Pos))
		{
			bJoin[Side] = true;
		}
		else if (IsRailAt(World, Level - Up) && WouldJoin(World, Level - Up, Pos))
		{
			bJoin[Side] = true;
		}
	}
	const auto Ascending = [&](int32 Side) { return FName(*(FString(TEXT("ascending_")) + SideNames[Side])); };
	// Opposite sides: a straight run (rising toward whichever side has the higher rail).
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		const int32 A = Axis, B = Axis + 2;
		if (bJoin[A] && bJoin[B])
		{
			return bAbove[A] ? Ascending(A) : bAbove[B] ? Ascending(B) : ShapeFor(A, B);
		}
	}
	int32 First = -1, Second = -1;
	for (int32 Side = 0; Side < 4; ++Side)
	{
		if (bJoin[Side])
		{
			(First < 0 ? First : Second) = Side;
		}
	}
	if (First < 0)
	{
		return Current.IsNone() ? FName(TEXT("north_south")) : Current;
	}
	if (Second >= 0 && bCurves)
	{
		return ShapeFor(First, Second);
	}
	// One side to join (or a rail that can't curve): straight along that side's axis.
	const int32 Side = First;
	if (bAbove[Side])
	{
		return Ascending(Side);
	}
	return ShapeFor(Side % 2, Side % 2 + 2);
}

void McRails::Sync(const FMcBlockWorld& World, const FIntVector& Pos, FMcStateId Old, FMcStateId State, const TFunction<void(const FIntVector&, FMcStateId)>& SetBlock)
{
	const bool bNow = IsRail(State);
	if (bNow == IsRail(Old))
	{
		return;
	}
	if (bNow)
	{
		const FName Shape = ChooseShape(World, Pos, State);
		if (Shape != FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("shape")))
		{
			SetBlock(Pos, WithShape(State, Shape));
		}
	}
	// The rails around turn to meet it (or straighten when it goes).
	for (int32 Side = 0; Side < 4; ++Side)
	{
		for (int32 Rise = -1; Rise <= 1; ++Rise)
		{
			const FIntVector Beside = Pos + Steps[Side] + FIntVector(0, Rise, 0);
			const FMcStateId Rail = World.GetBlock(Beside);
			if (!IsRail(Rail))
			{
				continue;
			}
			const FName Shape = ChooseShape(World, Beside, Rail);
			if (Shape != FMcBlockRegistry::Get().GetPropertyValue(Rail, TEXT("shape")))
			{
				SetBlock(Beside, WithShape(Rail, Shape));
			}
		}
	}
}
