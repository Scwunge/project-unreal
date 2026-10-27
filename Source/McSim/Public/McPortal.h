// Portals (minecraft.wiki, "Nether portal", "End portal"): the rules that need only the blocks. Where a frame is, lighting
// it, what breaks it, where a portal in the other dimension is (or is made), and how coordinates change between
// dimensions. Taking the player there is McClient's.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcBlockWorld;

namespace McPortal
{
	// Changes a block; given by the caller so the change reaches players and the save (a world actor's SetBlock).
	using FSetBlock = TFunction<void(const FIntVector&, FMcStateId)>;

	// The open space inside an obsidian frame. Pos is its lowest corner along the axis (the smaller x or z); AlongX is
	// whether the portal's width runs along x (its blocks then have axis=x).
	struct FFrame
	{
		FIntVector Pos = FIntVector::ZeroValue;
		bool bAlongX = true;
		int32 Width = 0;
		int32 Height = 0;
	};

	// The inside of a frame may be 2 to 21 wide and 3 to 21 tall (so 4 by 5 to 23 by 23 outside).
	constexpr int32 MinInnerWidth = 2;
	constexpr int32 MinInnerHeight = 3;
	constexpr int32 MaxInner = 21;

	MCSIM_API bool IsFrameSize(int32 Width, int32 Height);

	// Looks for the obsidian frame around the empty space (air or fire) at Pos, whose portal runs along x or along z. The
	// corners of the frame are not needed. Portal blocks count as empty when bAllowPortal, to check a portal that exists.
	MCSIM_API bool FindFrame(const FMcBlockWorld& World, const FIntVector& Pos, bool bAlongX, bool bAllowPortal, FFrame& OutFrame);

	// Fills the frame with portal blocks. Returns how many.
	MCSIM_API int32 Fill(FFrame Frame, const FSetBlock& SetBlock);

	// Fire has been put at Pos: if it is inside a complete frame (either way round), the frame lights. Returns the number of
	// portal blocks made, 0 if there was no frame. Nothing is lit in a dimension without a Nether portal (the End).
	MCSIM_API int32 TryLight(FMcBlockWorld& World, const FIntVector& Pos, const FSetBlock& SetBlock);

	// A block next to Pos changed: any portal beside it that no longer has its frame is cleared. Returns the blocks removed.
	MCSIM_API int32 RemoveBroken(FMcBlockWorld& World, const FIntVector& Pos, const FSetBlock& SetBlock);

	MCSIM_API bool IsPortalBlock(FMcStateId State);
	// The axis of a portal block: true for x.
	MCSIM_API bool IsAlongX(FMcStateId State);

	// Where Pos goes when it crosses from a dimension of one coordinate scale to another (8 for the Nether, 1 for the others):
	// x and z are divided by the ratio and rounded down, y stays.
	MCSIM_API FIntVector ScalePosition(const FIntVector& Pos, double FromScale, double ToScale);

	// How far from the scaled position an existing portal is looked for in a dimension: 128 blocks in the others, 16 in the Nether.
	MCSIM_API int32 GetSearchRadius(double DestinationScale);

	// A portal found in the loaded chunks: the lowest block in the middle of its bottom row, and its direction. The nearest to
	// Centre (by distance in 3 dimensions) within Radius blocks in x and z. False if there is none.
	struct FFound
	{
		FIntVector Pos = FIntVector::ZeroValue;
		bool bAlongX = true;
		int32 Width = 0;
	};
	MCSIM_API bool FindPortal(const FMcBlockWorld& World, const FIntVector& Centre, int32 Radius, FFound& OutFound);

	// Makes a portal near Centre (where the arriving player is) and says where it is. Looks within 16 blocks for a 4 by 3
	// patch of solid ground with 4 blocks of air above (then for a 4 by 1 one), builds the frame there, and failing both
	// puts it at Centre, within the heights portals are made at, on a fresh obsidian platform.
	MCSIM_API FFound BuildPortal(FMcBlockWorld& World, const FIntVector& Centre, bool bAlongX, const FSetBlock& SetBlock);

	// The portal at the destination for a player entering at Centre: an existing one in range, or a new one. bBuilt says which.
	MCSIM_API FFound FindOrBuildPortal(FMcBlockWorld& World, const FIntVector& Centre, bool bAlongX, const FSetBlock& SetBlock, bool& bOutBuilt);

	// Where a player stands in a found portal, in block coordinates: the middle of the opening, on its floor.
	MCSIM_API FVector GetStandingPoint(const FFound& Portal);

	// ---- The End ----
	// The end portal frame ring: 12 frames round a 3 by 3 hole. Eye is set on the frame at Pos; if that makes all 12 hold
	// an eye, the 3 by 3 inside fills with end portal blocks. Returns true if the portal opened.
	MCSIM_API bool TryOpenEndPortal(FMcBlockWorld& World, const FIntVector& FramePos, const FSetBlock& SetBlock);
	MCSIM_API bool IsEndPortalFrame(FMcStateId State);
	MCSIM_API bool HasEye(FMcStateId State);
	// The state of the frame with an eye in it.
	MCSIM_API FMcStateId WithEye(FMcStateId State);

	// The platform players arrive on in the End: 5 by 5 obsidian at y 48 centred on (100, 0), with air above for 3 blocks.
	constexpr int32 EndPlatformX = 100;
	constexpr int32 EndPlatformY = 48;
	constexpr int32 EndPlatformZ = 0;
	MCSIM_API void BuildEndPlatform(FMcBlockWorld& World, const FSetBlock& SetBlock);
}
