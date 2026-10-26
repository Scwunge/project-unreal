// McCore: Registries: blocks, block states, items, tags, recipes, loot tables.

#pragma once

#include "CoreMinimal.h"

MCCORE_API DECLARE_LOG_CATEGORY_EXTERN(LogMcCore, Log, All);

// A block state id, as numbered in the data generator's blocks.json (26.3 has 35,723, so 16 bits is enough).
using FMcStateId = uint16;
