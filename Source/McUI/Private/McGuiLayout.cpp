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

	for (int32 Index = 0; Index < IconsPerRow; ++Index)
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
			Layout.Air.Add({ FIntPoint(Right - Index * IconSpacing - IconSize, BaseY - 10), Index < Whole ? EIcon::Full : EIcon::Half, false });
		}
	}

	Layout.XpBar = FIntPoint(Width / 2 - 91, Height - 32 + 3);
	Layout.XpFill = FMath::Clamp(static_cast<int32>(Stats.XpProgress * 183.0f), 0, HotbarWidth);
	Layout.bShowLevel = Stats.XpLevel > 0;
	Layout.LevelText = FIntPoint(Width / 2, Height - 31 - 4);
	return Layout;
}
