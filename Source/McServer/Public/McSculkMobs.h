// What joins the simulation's sculk and creaking hearts to the warden and the creaking: wardens hear the game events, a shrieker at
// warning level 4 calls one up, and an awake creaking heart puts a creaking beside it. Kept apart so neither side knows the other.

#pragma once

#include "CoreMinimal.h"

class FMcSimulation;
class FMcEntityHost;
class FMcBlockWorld;

namespace McSculkMobs
{
	// Hooks the simulation's listeners and callbacks to the host's mobs. Call once, after both exist.
	MCSERVER_API void Connect(FMcSimulation& Simulation, FMcEntityHost& Host, FMcBlockWorld& World);
}
