// A player's survival state to and from the world's playerdata file (McWorldSave), which knows nothing of the
// player module: the stats go by name and the items by the game's slot numbers (see FMcSavedSlot).

#pragma once

#include "CoreMinimal.h"

class UMcPlayerDataComponent;
struct FMcPlayerData;

namespace McPlayerSave
{
	// Fills the survival part of Out from the component.
	void Capture(const UMcPlayerDataComponent& Data, FMcPlayerData& Out);
	// Puts a saved state into the component. Does nothing to one saved without survival state (the player stays new).
	// Returns whether it did.
	bool Apply(const FMcPlayerData& Saved, UMcPlayerDataComponent& Data);
}
