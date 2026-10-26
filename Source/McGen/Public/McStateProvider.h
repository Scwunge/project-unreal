// Block state providers (minecraft.wiki, "Block state provider"): what block a feature places, given as a
// plain state, a reference to worldgen/block_state_provider, or one of: weighted, randomized_int, random_block, rotated,
// noise, dual_noise, noise_threshold, rule_based.
//
// The noise providers sample their own noise (seeded by the provider's "seed", like the game, so flower
// patches don't depend on the world seed). How dual_noise picks among its states is our own.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

class FJsonValue;
class FMcGenRegion;
class FMcRandom;

class MCGEN_API FMcStateProvider
{
public:
	virtual ~FMcStateProvider() = default;
	// Unset when nothing should be placed (a rule_based provider with no fallback).
	virtual TOptional<FMcStateId> Get(FMcGenRegion& Region, FMcRandom& Random, const FIntVector& Pos) const = 0;

	// Null (and an error logged) if the JSON isn't a provider we know.
	static TSharedPtr<const FMcStateProvider> Parse(const TSharedPtr<FJsonValue>& Json);
	// A single state.
	static TSharedPtr<const FMcStateProvider> Simple(FMcStateId State);
};

// The state with one property changed; State itself if the block has no such property or value.
MCGEN_API FMcStateId McWithProperty(FMcStateId State, FName Property, FName Value);
