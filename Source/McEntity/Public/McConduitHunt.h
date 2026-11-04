// A conduit with its whole frame hunts (minecraft.wiki, "Conduit": one hostile mob at a time, in water or rain, until
// it dies or leaves). Fitted to vanilla's mob health logged beside a hunting conduit (mc-reference conduit_hunt.txt):
//  - each of the conduit's checks (every 40 ticks) with a target does it 4 damage, the check that picks it included;
//  - with none it picks one at random among the wet hostile mobs whose box reaches the conduit's block grown by 8 on
//    every side (one 8 blocks off was picked, one 9 off never);
//  - it keeps it while it lives, is wet and stands within 8 blocks of the conduit's middle; otherwise that check
//    drops it and does nothing (the one picked at 8.1 blocks was dropped every next check, one at 7.2 kept until it
//    died). Dry hostile mobs (a husk on the lid) and passive ones (a cod) were never hurt.

#pragma once

#include "CoreMinimal.h"

class FMcEntity;
class FMcEntityManager;

namespace McConduitHunt
{
	constexpr float Damage = 4.0f;
	constexpr double Reach = 8.0;

	// One check of the conduit at Pos with the target it had (an entity id, 0 for none); returns the target it has
	// after. IsWet says whether a mob is in water or out in the rain.
	MCENTITY_API int32 Pulse(FMcEntityManager& Manager, const FIntVector& Pos, int32 Target, TFunctionRef<bool(const FMcEntity&)> IsWet);
}
