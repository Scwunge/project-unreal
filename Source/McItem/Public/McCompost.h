// Composters (minecraft.wiki, "Composter"): which items add a level and how often.

#pragma once

#include "CoreMinimal.h"

// The chance in percent (0 to 100) that the item raises a composter's level; 0 if it cannot be composted.
MCITEM_API int32 McCompostChance(FName Item);
