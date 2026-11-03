// Rails (minecraft.wiki, "Rail"): the shapes the four kinds of rail take, and how they join up with their
// neighbours as they are placed and taken away.
//
// A rail's `shape` names the two ends of its track. Straight ones run north_south or east_west; an ascending one
// rises toward the side it is named for (ascending_east is low in the west and high in the east); a plain rail
// can also curve (south_east joins its south and east sides). Powered, detector and activator rails stay
// straight. A rail joins a rail beside it at the same height, or one block up or down when that is a slope; one
// already joined at both ends is left alone.

#pragma once

#include "CoreMinimal.h"
#include "McBlockRegistry.h"

class FMcBlockWorld;

namespace McRails
{
	// The four blocks that are rails.
	MCWORLD_API bool IsRail(FName BlockName);
	MCWORLD_API bool IsRail(FMcStateId State);
	// Only the plain rail takes curves.
	MCWORLD_API bool CanCurve(FName BlockName);

	// The two ends of a track: where each leads (a step in x or z), and whether the end is the high end of a slope.
	struct FTrack
	{
		FIntVector EndA = FIntVector::ZeroValue;
		FIntVector EndB = FIntVector::ZeroValue;
		bool bRiseA = false;
		bool bRiseB = false;
		bool IsAscending() const { return bRiseA || bRiseB; }
	};
	// False for a name that isn't a rail shape.
	MCWORLD_API bool GetTrack(FName Shape, FTrack& OutTrack);
	// The track of the rail state; false if it isn't a rail.
	MCWORLD_API bool GetTrack(FMcStateId State, FTrack& OutTrack);

	// The same rail with another shape (power and waterlogging kept).
	MCWORLD_API FMcStateId WithShape(FMcStateId State, FName Shape);

	// The shape the rail at Pos (whose state is State) should take given the rails around it.
	MCWORLD_API FName ChooseShape(const FMcBlockWorld& World, const FIntVector& Pos, FMcStateId State);

	// Keeps the tracks joined after the block at Pos changed from Old to State: a rail that appeared picks its shape
	// and the rails it joins turn to meet it; one that went lets those it was joined to straighten out. Changes go
	// through SetBlock.
	MCWORLD_API void Sync(const FMcBlockWorld& World, const FIntVector& Pos, FMcStateId Old, FMcStateId State, const TFunction<void(const FIntVector&, FMcStateId)>& SetBlock);

	// The rail a cart at From moves on to through End (a step from the track's end), if there is one: at the same
	// height, one up, or one down. Gives its position.
	MCWORLD_API bool FindNext(const FMcBlockWorld& World, const FIntVector& From, const FIntVector& End, bool bRise, FIntVector& OutNext);
}
