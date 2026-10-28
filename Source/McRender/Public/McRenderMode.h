// The two looks (M10). Vanilla is Minecraft's own: unlit blocks carrying the game's light curve, no tonemapper, fog in
// the fog colour. Enhanced lets Unreal light the world: a sun and moon with real shadows (virtual shadow maps), Lumen's
// global illumination and reflections, volumetric fog, bloom and the filmic tonemapper. The world, its light levels and
// everything that plays are the same in both; only the drawing changes.
//
// Every component that draws picks its materials through Pick and redoes them when GetGeneration changes, so the mode
// can be switched at any time (the pause menu's Graphics button, F7, or -McEnhanced at start).

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

namespace McRenderMode
{
	MCRENDER_API bool IsEnhanced();
	MCRENDER_API void SetEnhanced(bool bEnhanced);
	// Goes up by one on every switch; starts at 1.
	MCRENDER_API uint32 GetGeneration();
	// The material to draw with in the current mode: in enhanced mode a block material's lit twin (made by
	// tools/make_materials.py with "Lit" on the name), else Vanilla itself. Vanilla if it has no twin.
	MCRENDER_API UMaterialInterface* Pick(UMaterialInterface* Vanilla);
}
