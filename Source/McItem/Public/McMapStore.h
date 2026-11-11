// The maps of a world: filled maps are items with a "minecraft:map_id" component, and the data behind the id lives here
// (the 128 by 128 colours, the scale and centre, whether it is locked). Copies of a map share one id, so a cloned map keeps
// showing what the original learns, as in the game.

#pragma once

#include "CoreMinimal.h"
#include "McMapData.h"
#include "McItemStack.h"

namespace McMapStore
{
	// The id a filled map item points at, or INDEX_NONE.
	MCITEM_API int32 GetId(const FMcItemStack& Stack);
	MCITEM_API void SetId(FMcItemStack& Stack, int32 Id);

	MCITEM_API FMcMapData* Find(int32 Id);
	// A new blank map at a scale, centred by the cell that holds (X, Z) (run mapexp1 to 4); returns its id.
	MCITEM_API int32 Create(int32 Scale, int32 X, int32 Z, FName Dimension);
	// The id of the map made by zooming Id out one step (a map of paper around it): the same centre, one scale up, blank.
	// Asking twice for one source gives the same id. INDEX_NONE if the map is gone or already at the largest scale (4).
	MCITEM_API int32 Zoom(int32 Id);
	// True if the map item can be zoomed out (it exists, is unlocked and is below scale 4).
	MCITEM_API bool CanZoom(const FMcItemStack& Stack);
	MCITEM_API void Lock(int32 Id);
	// A new locked map with the picture, scale, centre and markers of Id (the cartography table's glass pane); INDEX_NONE if gone.
	MCITEM_API int32 LockedCopy(int32 Id);

	MCITEM_API void Reset();
	MCITEM_API int32 Num();

	// The whole store as JSON (colours as base64), for a world's save.
	MCITEM_API FString ToJson();
	MCITEM_API bool FromJson(const FString& Json);
}
