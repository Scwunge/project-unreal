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
	// The fullness bar under a bundle holding something (vanilla ref run bundle1: widths 1, 4, 7, 10, 12 and 13 of 13 for
	// 1, 16, 32, 48, 63 and 64 of 64; blue 0x7087FF, red 0xFF5454 when full).
	MCUI_API FDurabilityBar GetBundleBar(double Fullness);

	// The bundle tooltip (ref runs bundletip, bundletip2): all sizes in GUI pixels. The content is 96 wide; from the top
	// the title (9), 3 apart a grid of 24 pixel cells or, for an empty bundle, the wrapped description (9 a line), 4 apart
	// the 13 high fullness bar. The picture round it is the tooltip sprites, 12 bigger on every side.
	struct FBundleGrid
	{
		int32 Columns = 0;
		int32 Rows = 0;
		// Cells left out (not drawn) at the start of the first row, so that the items fill the last cells.
		int32 Blanks = 0;
		// Stacks not shown, written "+N" in the last cell; 0 when all are shown.
		int32 Hidden = 0;
	};
	// Measured (bundletip2 and 3: 1 to 24 items): always 4 columns, ceil(n/4) rows up to 3, the items in the last cells in
	// list order. Over 12 the last cell says +N, N = 4 * ceil((n - 12) / 4) + 1, and the stacks before those are shown.
	MCUI_API FBundleGrid GetBundleGrid(int32 NumStacks);
	constexpr int32 BundleTipWidth = 96;
	constexpr int32 BundleTipCell = 24;
	constexpr int32 BundleTipBarHeight = 13;
	constexpr int32 BundleTipTitleHeight = 9;
	constexpr int32 BundleTipTitleGap = 3;
	constexpr int32 BundleTipBarGap = 4;
	constexpr int32 BundleTipLineHeight = 9;
	// The height of the content for a grid of rows (or, with none, a description of DescriptionLines lines).
	MCUI_API int32 GetBundleTipHeight(int32 Rows, int32 DescriptionLines);

	// Whether a slot writes a count, and which. Stacks of one say nothing.
	MCUI_API bool ShowsCount(int32 Count);
}
