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
class UMaterialInstanceDynamic;

namespace McRenderMode
{
	MCRENDER_API bool IsEnhanced();
	MCRENDER_API void SetEnhanced(bool bEnhanced);
	// Goes up by one on every switch; starts at 1.
	MCRENDER_API uint32 GetGeneration();
	// HD Textures: a second switch on top of Unreal Mode that draws the generated (or resource pack) high-resolution
	// albedo, normal and specular maps. It only takes effect while Unreal Mode is on (IsHd); GetHdSwitch is the
	// setting itself. A switch changes GetGeneration like Unreal Mode does.
	MCRENDER_API bool IsHd();
	// Unreal Mode's quality preset, 0 Low, 1 Medium, 2 High, 3 Ultra (default High): the render scale (TSR fills the rest),
	// Lumen's probe density and reflections, shadow detail, volumetric fog and the post effects. The preset only
	// reaches the engine while Unreal Mode is on; vanilla always draws at full scale. Low and Medium are for cards like
	// the GTX 1070, on software Lumen.
	MCRENDER_API int32 GetQuality();
	MCRENDER_API void SetQuality(int32 Quality);
	// The sky-ambient light of mobs and players in Unreal Mode: their textures are darker than most blocks' (a brown
	// cow), so they get more of it to look as bright as blocks under the same light. 1 for the vanilla look.
	MCRENDER_API float GetEntityAmbient();
	MCRENDER_API bool GetHdSwitch();
	MCRENDER_API void SetHd(bool bHd);
	// The material to draw with in the current mode: in enhanced mode a block material's lit twin (made by
	// tools/make_materials.py with "Lit" on the name), with HD on its HD twin ("Hd"), else Vanilla itself. The twin
	// that exists: HD falls back to Lit, Lit to Vanilla.
	MCRENDER_API UMaterialInterface* Pick(UMaterialInterface* Vanilla);
	// With HD Textures on, gives a material made from a Pick the HD arrays to sample ("BlocksHd", "BlocksHdN" and
	// "BlocksHdS"): the block layers', or those of the picture in this png (a skin, a mob). bWhite: the picture's
	// silhouette in white, for a mob flashing. Does nothing with HD off.
	MCRENDER_API void ApplyHdBlocks(UMaterialInstanceDynamic* Material);
	MCRENDER_API void ApplyHdPicture(UMaterialInstanceDynamic* Material, const FString& PngPath, bool bWhite = false);
}
