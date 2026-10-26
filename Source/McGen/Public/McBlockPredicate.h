// The tests features use to decide where they may go (minecraft.wiki, "Block predicate", "Rule test"):
//
// - Block predicates look at the world around a position: matching_blocks, matching_block_tag,
//   matching_fluids, solid, replaceable, would_survive, has_sturdy_face, inside_world_bounds, height_range,
//   volume_match, all_of, any_of, not, true. Most take an offset from the position.
// - Rule tests look at a single state (ore targets): tag_match, block_match, blockstate_match,
//   random_block_match, random_blockstate_match, height_match, all_of, any_of, not, always_true.
//
// would_survive asks whether a block could stay where it is. The game decides that per block in code; we
// answer from the jar's support tags (supports_vegetation, supports_sugar_cane, ...) and minecraft.wiki's
// placement rules, see McSurvival in the .cpp.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FJsonValue;
class FMcGenRegion;
class FMcRandom;

class MCGEN_API FMcBlockPredicate
{
public:
	virtual ~FMcBlockPredicate() = default;
	virtual bool Test(FMcGenRegion& Region, const FIntVector& Pos) const = 0;

	// Null (and an error logged) if the JSON isn't a predicate we know. SeaLevel resolves relative_to_sea_level.
	static TSharedPtr<const FMcBlockPredicate> Parse(const TSharedPtr<FJsonValue>& Json, int32 SeaLevel = 63);
	// Whether State could stay at Pos (would_survive).
	static bool CanSurvive(FMcGenRegion& Region, const FIntVector& Pos, FMcStateId State);
};

class MCGEN_API FMcRuleTest
{
public:
	virtual ~FMcRuleTest() = default;
	// Pos is where the state is, for height_match.
	virtual bool Test(FMcStateId State, const FIntVector& Pos, FMcRandom& Random) const = 0;

	static TSharedPtr<const FMcRuleTest> Parse(const TSharedPtr<FJsonValue>& Json);
};

// A block state written as "minecraft:stone" or { "id" (or "Name"): ..., "properties" (or "Properties"): {...} }.
MCGEN_API TOptional<FMcStateId> McParseBlockState(const TSharedPtr<FJsonValue>& Json);
// An [x, y, z] offset; zero if absent.
MCGEN_API FIntVector McParseOffset(const TSharedPtr<FJsonValue>& Json);
// Whether the face of State pointing along Direction (a unit vector) is a full square.
MCGEN_API bool McHasSturdyFace(FMcStateId State, const FIntVector& Direction);
// Whether a block is solid: it has collision and isn't #replaceable (the solid predicate).
MCGEN_API bool McIsSolid(FMcStateId State);
