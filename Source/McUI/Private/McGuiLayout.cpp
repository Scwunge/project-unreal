#include "McGuiLayout.h"

namespace
{
	constexpr int32 HotbarWidth = 182;
	constexpr int32 HotbarHeight = 22;
	constexpr int32 CrosshairSize = 15;
	constexpr int32 SlotSpacing = 20;
	constexpr int32 IconSpacing = 8;
	constexpr int32 IconSize = 9;
	constexpr int32 IconsPerRow = 10;

	// Which of the hearts jiggle when health is low: a cheap hash of the icon and the shake counter.
	int32 Jiggle(int32 Shake, int32 Index)
	{
		uint32 Value = static_cast<uint32>(Shake) * 2654435761u + static_cast<uint32>(Index) * 40503u;
		Value ^= Value >> 13;
		return static_cast<int32>(Value & 1u);
	}
}

float McGui::GetVignetteTarget(float EyeLight)
{
	// Measured on the reference client (a hall lit from far away, a light block in the eyes' cell, each level held
	// 500 ticks so the ease had settled; the strength is the fitted opacity of vignette.png): levels 0, 1, 2, 4, 5, 7, 8,
	// 10, 11, 13, 14 and 15 as seen, the others (3, 6, 9, 12) filled in between.
	static const float Targets[16] = { 1.0f, 0.977f, 0.941f, 0.920f, 0.900f, 0.881f, 0.829f, 0.777f, 0.748f, 0.700f, 0.621f, 0.572f, 0.475f, 0.378f, 0.192f, 0.0f };
	const float Clamped = FMath::Clamp(EyeLight, 0.0f, 15.0f);
	const int32 Low = FMath::FloorToInt32(Clamped);
	const int32 High = FMath::Min(Low + 1, 15);
	return FMath::Lerp(Targets[Low], Targets[High], Clamped - Low);
}

float McGui::EaseVignette(float Current, float Target, float Ticks)
{
	// About 115 ticks to come 63 percent of the way (fitted to a bright to dim change: 0.29 after 20 ticks, 0.50 after 100,
	// 0.72 after 400, towards 0.73).
	constexpr float PerTick = 0.0087f;
	return Target + (Current - Target) * FMath::Pow(1.0f - PerTick, FMath::Max(Ticks, 0.0f));
}

int32 McGui::GetGuiScale(int32 Width, int32 Height, int32 Setting)
{
	int32 Largest = 1;
	while (Width / (Largest + 1) >= 320 && Height / (Largest + 1) >= 240)
	{
		++Largest;
	}
	return Setting <= 0 ? Largest : FMath::Min(Setting, Largest);
}

McGui::EIcon McGui::IconFor(int32 Index, float PointsInHalves)
{
	const int32 Points = FMath::FloorToInt(PointsInHalves);
	return Points >= Index * 2 + 2 ? EIcon::Full : Points == Index * 2 + 1 ? EIcon::Half : EIcon::Empty;
}

bool McGui::IsHurtBlink(int32 HurtTime)
{
	return HurtTime > 0 && (HurtTime / 3) % 2 == 1;
}

McGui::FDurabilityBar McGui::GetDurabilityBar(int32 Damage, int32 MaxDamage)
{
	FDurabilityBar Bar;
	if (MaxDamage <= 0 || Damage <= 0)
	{
		return Bar;
	}
	Bar.bShow = true;
	const float Left = FMath::Clamp(static_cast<float>(MaxDamage - Damage) / MaxDamage, 0.0f, 1.0f);
	Bar.Width = FMath::RoundToInt(13.0f - Damage * 13.0f / MaxDamage);
	Bar.Width = FMath::Clamp(Bar.Width, 0, 13);
	// Green at full, through yellow, to red when nearly gone: hue a third of the way round the wheel at full.
	const FLinearColor Color = FLinearColor::MakeFromHSV8(static_cast<uint8>(Left / 3.0f * 255.0f), 255, 255);
	const FColor Out = Color.ToFColor(false);
	Bar.Color = (static_cast<uint32>(Out.R) << 16) | (static_cast<uint32>(Out.G) << 8) | Out.B;
	return Bar;
}

McGui::FBundleGrid McGui::GetBundleGrid(int32 NumStacks)
{
	FBundleGrid Grid;
	if (NumStacks <= 0)
	{
		return Grid;
	}
	Grid.Columns = 4;
	Grid.Rows = FMath::Min(3, (NumStacks + Grid.Columns - 1) / Grid.Columns);
	if (NumStacks > 12)
	{
		Grid.Hidden = 4 * ((NumStacks - 12 + 3) / 4) + 1;
		// The cells before the +N one are filled from the end with what is shown.
		Grid.Blanks = 11 - (NumStacks - Grid.Hidden);
		return Grid;
	}
	Grid.Blanks = Grid.Rows * Grid.Columns - NumStacks;
	return Grid;
}

int32 McGui::GetBundleTipHeight(int32 Rows, int32 DescriptionLines)
{
	const int32 Body = Rows > 0 ? Rows * BundleTipCell : DescriptionLines * BundleTipLineHeight;
	return BundleTipTitleHeight + BundleTipTitleGap + Body + BundleTipBarGap + BundleTipBarHeight;
}

McGui::FDurabilityBar McGui::GetBundleBar(double Fullness)
{
	FDurabilityBar Bar;
	if (Fullness <= 0.0)
	{
		return Bar;
	}
	Bar.bShow = true;
	Bar.Width = FMath::Clamp(FMath::FloorToInt32(1.0 + 12.0 * Fullness + 1.0e-9), 1, 13);
	Bar.Color = Fullness >= 1.0 - 1.0e-9 ? 0xFF5454 : 0x7087FF;
	return Bar;
}

bool McGui::ShowsCount(int32 Count)
{
	return Count != 1;
}

McGui::FHudLayout McGui::ComputeHud(int32 Width, int32 Height, const FMcPlayerStats& Stats, int32 Shake)
{
	FHudLayout Layout;
	Layout.Hotbar = FIntPoint((Width - HotbarWidth) / 2, Height - HotbarHeight);
	Layout.Crosshair = FIntPoint((Width - CrosshairSize) / 2, (Height - CrosshairSize) / 2);
	for (int32 Slot = 0; Slot < 9; ++Slot)
	{
		Layout.HotbarSlots.Add(FIntPoint(Layout.Hotbar.X + 3 + Slot * SlotSpacing, Layout.Hotbar.Y + 3));
	}

	const int32 Left = Width / 2 - 91;
	const int32 Right = Width / 2 + 91;
	const int32 BaseY = Height - 39;

	// More than ten hearts (extra max health, absorption) go in rows up the screen, packed closer as there are more.
	const int32 NumHearts = FMath::Max(FMath::CeilToInt(Stats.MaxHealth / 2.0f), 0);
	const int32 NumAbsorption = FMath::Max(FMath::CeilToInt(Stats.Absorption / 2.0f), 0);
	const int32 Rows = FMath::Max(FMath::DivideAndRoundUp(NumHearts + NumAbsorption, IconsPerRow), 1);
	const int32 RowHeight = FMath::Max(10 - (Rows - 2), 3);
	const bool bLow = Stats.Health <= 4.0f;
	const bool bBlink = IsHurtBlink(Stats.HurtTime);
	const auto HeartPos = [&](int32 Index)
	{
		return FIntPoint(Left + (Index % IconsPerRow) * IconSpacing, BaseY - (Index / IconsPerRow) * RowHeight + (bLow ? Jiggle(Shake, Index) : 0));
	};
	for (int32 Index = 0; Index < NumHearts; ++Index)
	{
		Layout.Hearts.Add({ HeartPos(Index), IconFor(Index, Stats.Health), bBlink });
	}
	for (int32 Index = 0; Index < NumAbsorption; ++Index)
	{
		Layout.Absorption.Add({ HeartPos(NumHearts + Index), IconFor(Index, Stats.Absorption), bBlink });
	}

	// Armour sits above the top row of hearts.
	Layout.bShowArmor = Stats.Armor > 0;
	if (Layout.bShowArmor)
	{
		const int32 ArmorY = BaseY - (Rows - 1) * RowHeight - 10;
		for (int32 Index = 0; Index < IconsPerRow; ++Index)
		{
			Layout.Armor.Add({ FIntPoint(Left + Index * IconSpacing, ArmorY), IconFor(Index, static_cast<float>(Stats.Armor)), false });
		}
	}

	// Riding a living mount, its hearts take the food row's place, ten to a row and rows going up.
	const int32 MountHearts = Stats.MountMaxHealth > 0.0f ? FMath::CeilToInt(Stats.MountMaxHealth / 2.0f) : 0;
	const int32 MountRows = FMath::DivideAndRoundUp(MountHearts, IconsPerRow);
	for (int32 Index = 0; Index < MountHearts; ++Index)
	{
		Layout.Mount.Add({ FIntPoint(Right - (Index % IconsPerRow) * IconSpacing - IconSize, BaseY - (Index / IconsPerRow) * 10), IconFor(Index, Stats.MountHealth), false });
	}
	for (int32 Index = 0; Index < (MountHearts > 0 ? 0 : IconsPerRow); ++Index)
	{
		Layout.Food.Add({ FIntPoint(Right - Index * IconSpacing - IconSize, BaseY), IconFor(Index, static_cast<float>(Stats.Food)), false });
	}

	// Breath: a bubble per tenth, and the one that has just gone bursts.
	Layout.bShowAir = Stats.Air < Stats.MaxAir && Stats.MaxAir > 0;
	if (Layout.bShowAir)
	{
		const int32 Air = FMath::Max(Stats.Air, 0);
		const int32 Whole = FMath::Clamp(FMath::DivideAndRoundUp((Air - 2) * IconsPerRow, Stats.MaxAir), 0, IconsPerRow);
		const int32 Shown = FMath::Clamp(FMath::DivideAndRoundUp(Air * IconsPerRow, Stats.MaxAir), 0, IconsPerRow);
		for (int32 Index = 0; Index < Shown; ++Index)
		{
			Layout.Air.Add({ FIntPoint(Right - Index * IconSpacing - IconSize, BaseY - 10 - FMath::Max(MountRows - 1, 0) * 10), Index < Whole ? EIcon::Full : EIcon::Half, false });
		}
	}

	Layout.XpBar = FIntPoint(Width / 2 - 91, Height - 32 + 3);
	Layout.XpFill = FMath::Clamp(static_cast<int32>(Stats.XpProgress * 183.0f), 0, HotbarWidth);
	// Vanilla draws the jump bar only while a charge is building (vehicle ride shot: idle, the experience bar and level stay).
	Layout.bShowJump = Stats.MountJump > 0.0f;
	Layout.JumpFill = FMath::Clamp(static_cast<int32>(Stats.MountJump * 183.0f), 0, HotbarWidth);
	Layout.bShowLevel = Stats.XpLevel > 0 && !Layout.bShowJump;
	Layout.LevelText = FIntPoint(Width / 2, Height - 31 - 4);
	return Layout;
}
