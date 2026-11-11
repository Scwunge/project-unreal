#pragma once

#include "CoreMinimal.h"

// A plain "Loading" screen shown by the engine's movie player (its own thread and window drawing) while the game thread is
// stuck in a long synchronous load, so a 30 second resource pack load looks like loading and not a hang. Nested uses share
// the outermost one. Does nothing without Slate (commandlets, servers, tests) or if a loading screen is already up.
class FMcLoadScreen
{
public:
	FMcLoadScreen(const FText& Message, bool bShow);
	~FMcLoadScreen();

private:
	bool bOwns = false;
};
