// Signs (minecraft.wiki, "Sign", "Hanging Sign"): which kind a block is, which way its front faces, where its board is,
// how wide a line of text may be, and the colours the text is drawn in. The text itself is the block entity's
// (FMcSignSide in McBlockEntity.h). The boards' places come from the block models (template_sign_rot_0,
// template_wall_sign, template_hanging_sign_rot_0, template_wall_hanging_sign), whose front faces south unturned.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

struct FMcSignSide;

namespace McSign
{
	enum class EKind : uint8
	{
		None,
		Standing,
		Wall,
		// From the ceiling, on chains.
		Hanging,
		// From a bar out of a wall.
		WallHanging,
	};

	MCWORLD_API EKind GetKind(FName Block);
	MCWORLD_API EKind GetKind(FMcStateId State);
	inline bool IsHanging(EKind Kind) { return Kind == EKind::Hanging || Kind == EKind::WallHanging; }

	// The way the front faces, in Minecraft degrees turned from south towards west (0 south, 90 west, 180 north, 270
	// east): a standing or hanging sign's rotation times 22.5, a wall one's facing.
	MCWORLD_API float GetYaw(FMcStateId State);
	// Unturned (the front facing south), in blocks inside the block: the middle of the board's front-to-back middle
	// plane, and how far in front of that plane the front face is (the back face is as far behind).
	MCWORLD_API FVector GetBoardCentre(EKind Kind);
	MCWORLD_API double GetHalfThickness(EKind Kind);
	// Where a point of the unturned block (in blocks, inside the block) is once the sign at Pos is turned to Yaw: it
	// turns about the block's vertical middle.
	MCWORLD_API FVector Turn(const FVector& Local, float Yaw, const FIntVector& Pos);
	// Whether someone at Where (in blocks) is in front of the sign rather than behind: within a quarter turn of the way
	// its front faces, seen from the board's middle.
	MCWORLD_API bool IsFacingFront(FMcStateId State, const FIntVector& Pos, const FVector& Where);

	// The widest a line may be in the font's pixels, the distance from one line to the next, and the size of a font
	// pixel on the board in blocks. A hanging sign's board is narrower and its text a little bigger.
	MCWORLD_API int32 GetMaxLineWidth(EKind Kind);
	MCWORLD_API int32 GetLineHeight(EKind Kind);
	MCWORLD_API double GetTextScale(EKind Kind);

	// A dye's text colour ("light_blue" is 0x9AC0CD), which is not its banner colour; black for a name that isn't a dye.
	MCWORLD_API uint32 GetTextColour(FName Dye);
	MCWORLD_API bool IsDye(FName Dye);
	// The dye of a dye item ("minecraft:light_blue_dye" is "light_blue"); None for anything else.
	MCWORLD_API FName GetDyeOfItem(FName Item);
	// What a side's text is drawn in: its dye's colour if it glows, else that at four tenths (so white text is grey).
	MCWORLD_API uint32 GetDrawColour(const FMcSignSide& Side);
	// The outline glowing text has: the colour at four tenths, but a pale cream round black.
	MCWORLD_API uint32 GetOutlineColour(const FMcSignSide& Side);
	// Glowing text has its outline close up (within 16 blocks); black's shows from any distance.
	MCWORLD_API bool IsOutlineVisible(const FMcSignSide& Side, double Distance);
}
