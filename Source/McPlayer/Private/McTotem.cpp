#include "McTotem.h"
#include "McEffects.h"
#include "McItemStack.h"
#include "McPlayerData.h"
#include "McSurvival.h"

bool McTotem::CanSave(uint8 DamageKind)
{
	return DamageKind != static_cast<uint8>(McSurvival::EDamage::Void);
}

bool McTotem::TryUse(FMcPlayerStats& Stats, FMcItemStack& MainHand, FMcItemStack& OffHand)
{
	static const FName Totem(TEXT("minecraft:totem_of_undying"));
	FMcItemStack* Held = MainHand.Item == Totem && MainHand.Count > 0 ? &MainHand : OffHand.Item == Totem && OffHand.Count > 0 ? &OffHand : nullptr;
	if (!Held)
	{
		return false;
	}
	if (--Held->Count <= 0)
	{
		Held->Clear();
	}
	Stats.Health = 1.0f;
	McEffects::Remove(Stats, NAME_None);
	McEffects::Add(Stats, TEXT("regeneration"), 1, 45 * 20);
	McEffects::Add(Stats, TEXT("fire_resistance"), 0, 40 * 20);
	McEffects::Add(Stats, TEXT("absorption"), 1, 5 * 20);
	return true;
}

McTotem::FFlight McTotem::GetFlight(float Age)
{
	FFlight Flight;
	const float T = Age / AnimationTicks;
	if (T < 0.0f || T >= 1.0f)
	{
		return Flight;
	}
	Flight.bVisible = true;
	// Measured from vanilla (mc-reference scripts/totem_ticks.txt, a shot every 2 ticks): it shows a tenth of the
	// screen high in the middle, half turned (edge on) and a third high 2 ticks later, facing and just over half the
	// screen high by 4, and then hangs there, a little below the middle, until it goes.
	const float Grow = FMath::Min(Age / GrowTicks, 1.0f);
	Flight.Centre = FVector2D(0.5, 0.56);
	Flight.Size = FMath::Lerp(0.11f, 0.54f, Grow);
	Flight.Turn = PI * Grow;
	return Flight;
}
