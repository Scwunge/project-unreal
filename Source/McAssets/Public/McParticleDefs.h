// The game's particle definitions (assets/minecraft/particles/<name>.json): the pictures each kind of particle uses, in
// frame order. A particle's picture is textures/particle/<name>.png; the block texture array holds all of them
// ("minecraft:particle/<name>") so the particle component draws them with the same material as everything else.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

namespace McParticleDefs
{
	// The pictures of the particle "minecraft:heart" (or just "heart") as texture ids ("minecraft:particle/heart"), in frame order.
	// Empty for a name the game has no definition for.
	MCASSETS_API const TArray<FName>& GetTextures(FName Particle);
	// Every picture any particle uses (no repeats).
	MCASSETS_API const TArray<FName>& GetAllTextures();
	// Every defined particle, as "minecraft:name".
	MCASSETS_API const TArray<FName>& GetNames();
}
