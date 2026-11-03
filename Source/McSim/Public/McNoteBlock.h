// Note blocks (minecraft.wiki, "Note Block"; the instrument of each block, volume and range measured in the reference
// client, scripts note1.txt to note4.txt with `log sounds`; see PLAN.md). The block below picks the instrument, a mob head
// on top picks a mob's voice instead, the space above must be air (or a head) for a note to sound, and each use steps
// the note 0 to 24 round again. A note is played by a punch (survival), by a use that steps it, or by redstone.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcBlockWorld;
struct FMcSoundEvent;

namespace McNoteBlock
{
	constexpr int32 NumNotes = 25;

	// The instrument a block of this name gives a note block above it: "harp" (the default) or one of the other 15 in
	// the note_block state's `instrument`, and the four copper trumpets. Measured block by block; stairs, buttons and
	// plates answer as the block they are made of.
	MCSIM_API FName GetInstrumentBelow(FName Block);
	// The instrument a head on top gives ("zombie", "skeleton", "creeper", "dragon", "wither_skeleton", "piglin",
	// "custom_head" for a player's), or none.
	MCSIM_API FName GetInstrumentAbove(FName Block);

	// 2^((note - 12) / 12): 0.5 at note 0, 1 at 12, 2 at 24.
	MCSIM_API float GetPitch(int32 Note);
	// What the instrument plays: the sound event, volume 3 (range 48 blocks; the piglin's own sound makes it 1.98) and,
	// for a head, a fixed pitch (the creeper's 0.5, the others' 1) rather than the note's. Empty name for a player's head.
	MCSIM_API FMcSoundEvent GetSound(FName Instrument, int32 Note, const FVector& Position);
	// Whether the instrument is a head's (no note particle, no pitch).
	MCSIM_API bool IsHeadInstrument(FName Instrument);
	// The colour of the note particle for a note (the wiki's table: a wheel of hue from green at 0 through blue at 12 to
	// green again at 24).
	MCSIM_API FColor GetNoteColour(int32 Note);
}
