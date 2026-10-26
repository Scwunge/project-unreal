// The structures the game builds in code, with no template in the jar: mineshafts, strongholds, ocean monuments,
// nether fortresses and woodland mansions, and the template built nether fossils and end cities. What each is
// follows minecraft.wiki; the layouts are our own, simpler than the game's but the same in kind (a mineshaft is
// rooms and timbered corridors, a stronghold a staircase, corridors and rooms ending in the portal room ...).
//
// Every builder lays its whole structure out from the random stream it is given (which depends on the seed and
// the start chunk only) and writes just the blocks inside the clip rectangle, so each chunk that a structure
// reaches builds its own part and the parts meet.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FMcGenRegion;
class FMcRandom;

namespace McProcedural
{
	struct FEnv
	{
		FMcGenRegion& Region;
		FIntPoint Start;     // the start chunk
		FIntPoint ClipMin;   // the block columns of the chunk being decorated (x, z), inclusive
		FIntPoint ClipMax;
		int32 MinY;
		int32 MaxY;
		int32 SeaLevel;
		// One above the top block of the ground at a column (trees don't count), the same from every chunk.
		TFunction<int32(int32, int32)> Floor;

		int32 CentreX() const { return Start.X * 16 + 8; }
		int32 CentreZ() const { return Start.Y * 16 + 8; }
	};

	// How far from its start chunk each reaches, in chunks.
	constexpr int32 MineshaftReach = 6;
	constexpr int32 StrongholdReach = 6;
	constexpr int32 MonumentReach = 3;
	constexpr int32 FortressReach = 6;
	constexpr int32 MansionReach = 3;
	constexpr int32 EndCityReach = 3;

	void BuildMineshaft(FEnv& Env, FMcRandom& Random, bool bMesa);
	void BuildStronghold(FEnv& Env, FMcRandom& Random);
	void BuildMonument(FEnv& Env, FMcRandom& Random);
	void BuildFortress(FEnv& Env, FMcRandom& Random);
	void BuildMansion(FEnv& Env, FMcRandom& Random);
	void BuildEndCity(FEnv& Env, FMcRandom& Random);
	void BuildNetherFossil(FEnv& Env, FMcRandom& Random);

	// The chunks the stronghold start in, from a structure set's concentric rings (count strongholds in rings
	// round the origin, the first Spread of them in the innermost, each ring further out than the one before).
	void StrongholdChunks(uint64 Seed, int32 Count, int32 Distance, int32 Spread, TArray<FIntPoint>& Out);
}
