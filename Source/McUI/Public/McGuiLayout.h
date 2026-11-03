// Where everything on the HUD goes, in GUI pixels (minecraft.wiki, "Health", "Hunger", "Armor", "Experience",
// "Breathing"): plain maths on the screen size and the player's numbers, so it can be tested without drawing.
// One GUI pixel is Scale screen pixels.

#pragma once

#include "CoreMinimal.h"
#include "McPlayerData.h"

namespace McGui
{
	// How a bar icon is drawn.
	enum class EIcon : uint8 { Empty, Half, Full };

	struct FBarIcon
	{
		FIntPoint Pos;
		EIcon Fill = EIcon::Empty;
		// Whether the container behind a heart flashes (the hurt blink).
		bool bBlink = false;
	};

	struct FHudLayout
	{
		FIntPoint Hotbar;      // top left of the 182 x 22 bar
		FIntPoint Crosshair;   // top left of the 15 x 15 sprite
		TArray<FIntPoint> HotbarSlots;   // top left of the 16 x 16 item in each of the nine slots
		TArray<FBarIcon> Hearts;         // the containers, left to right then row by row from the bottom
		TArray<FBarIcon> Absorption;     // golden hearts, after the red ones
		TArray<FBarIcon> Armor;
		TArray<FBarIcon> Food;           // right to left from the right edge of the bar
		TArray<FBarIcon> Air;            // only while breath is short of full: Full, Half is the one bursting
		FIntPoint XpBar;                 // top left of the 182 x 5 bar
		int32 XpFill = 0;                // pixels of the bar that are filled, 0 to 182
		FIntPoint LevelText;             // centre of the level number; only when the level is above 0
		bool bShowLevel = false;
		bool bShowArmor = false;
		bool bShowAir = false;
	};

	// The largest whole GUI scale the screen allows (at least 320 x 240 GUI pixels), or Setting if that is smaller.
	// Setting 0 is automatic: the largest.
	MCUI_API int32 GetGuiScale(int32 Width, int32 Height, int32 Setting = 0);

	// How strongly the vignette picture (textures/misc/vignette.png, grey 0 in the middle to 0.82 in a corner) darkens the
	// screen, for a brightness of 0 (dark) to 1 (noon): the picture's grey times this is the share of the view that is lost.
	// Fitted to the reference client's HUD-on and HUD-off pairs: 0.356 at noon, 0.289 at night.
	// How dark the screen's edges end up for the light at the player's eyes (0 to 15, between levels it is interpolated):
	// 1 in the dark, 0 at full light. The strength the vignette eases towards.
	MCUI_API float GetVignetteTarget(float EyeLight);
	// One step of the ease, Ticks game ticks long (the strength moves about 0.9 percent of the way to its target each tick).
	MCUI_API float EaseVignette(float Current, float Target, float Ticks);

	// The layout for a screen of Width x Height GUI pixels. Shake is a number that changes every few ticks: hearts
	// jiggle by a pixel when health is low, and which ones depends on it.
	MCUI_API FHudLayout ComputeHud(int32 Width, int32 Height, const FMcPlayerStats& Stats, int32 Shake = 0);

	// The icons of a row of ten for a value in halves: Full where there are two points, Half for one.
	MCUI_API EIcon IconFor(int32 Index, float PointsInHalves);

	// Whether the heart containers show the blink for HurtTime ticks left.
	MCUI_API bool IsHurtBlink(int32 HurtTime);

	// The durability bar under an item: 13 pixels at full, and the colour (0xRRGGBB) shading from green to red.
	// Width is 0 and the bar not shown when the item isn't damaged.
	struct FDurabilityBar
	{
		bool bShow = false;
		int32 Width = 0;
		uint32 Color = 0;
	};
	MCUI_API FDurabilityBar GetDurabilityBar(int32 Damage, int32 MaxDamage);

	// Whether a slot writes a count, and which. Stacks of one say nothing.
	MCUI_API bool ShowsCount(int32 Count);
}
