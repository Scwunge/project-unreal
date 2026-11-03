#include "McEffects.h"
#include "McPlayerData.h"
#include "McSurvival.h"

namespace
{
	// Ticks between each effect of an effect that works in pulses, at level I; each level halves it.
	bool IsDue(int32 Duration, int32 Interval, int32 Amplifier)
	{
		const int32 Gap = Interval >> FMath::Min(Amplifier, 30);
		return Gap <= 0 || Duration % Gap == 0;
	}
}

const TArray<FName>& McEffects::GetAll()
{
	static const TArray<FName> All = {
		TEXT("absorption"), TEXT("bad_omen"), TEXT("blindness"), TEXT("breath_of_the_nautilus"), TEXT("conduit_power"), TEXT("darkness"),
		TEXT("dolphins_grace"), TEXT("fire_resistance"), TEXT("glowing"), TEXT("haste"), TEXT("health_boost"), TEXT("hero_of_the_village"),
		TEXT("hunger"), TEXT("infested"), TEXT("instant_damage"), TEXT("instant_health"), TEXT("invisibility"), TEXT("jump_boost"),
		TEXT("levitation"), TEXT("luck"), TEXT("mining_fatigue"), TEXT("nausea"), TEXT("night_vision"), TEXT("oozing"), TEXT("poison"),
		TEXT("raid_omen"), TEXT("regeneration"), TEXT("resistance"), TEXT("saturation"), TEXT("slow_falling"), TEXT("slowness"), TEXT("speed"),
		TEXT("strength"), TEXT("trial_omen"), TEXT("unluck"), TEXT("water_breathing"), TEXT("weakness"), TEXT("weaving"), TEXT("wind_charged"),
		TEXT("wither"),
	};
	return All;
}

bool McEffects::Parse(const FString& Text, FName& OutId)
{
	FString Name = Text;
	Name.RemoveFromStart(TEXT("minecraft:"));
	const FName Id(*Name);
	if (!GetAll().Contains(Id))
	{
		return false;
	}
	OutId = Id;
	return true;
}

bool McEffects::IsInstant(FName Id)
{
	return Id == TEXT("instant_health") || Id == TEXT("instant_damage");
}

bool McEffects::IsHarmful(FName Id)
{
	static const TSet<FName> Harmful = {
		TEXT("bad_omen"), TEXT("blindness"), TEXT("darkness"), TEXT("hunger"), TEXT("infested"), TEXT("instant_damage"), TEXT("levitation"),
		TEXT("mining_fatigue"), TEXT("nausea"), TEXT("oozing"), TEXT("poison"), TEXT("raid_omen"), TEXT("slowness"), TEXT("trial_omen"),
		TEXT("unluck"), TEXT("weakness"), TEXT("weaving"), TEXT("wind_charged"), TEXT("wither"),
	};
	return Harmful.Contains(Id);
}

const FMcStatusEffect* McEffects::Find(const FMcPlayerStats& Stats, FName Id)
{
	return Stats.Effects.FindByPredicate([Id](const FMcStatusEffect& Effect) { return Effect.Id == Id; });
}

bool McEffects::Add(FMcPlayerStats& Stats, FName Id, int32 Amplifier, int32 Duration)
{
	if (Stats.Health <= 0.0f || !GetAll().Contains(Id))
	{
		return false;
	}
	Amplifier = FMath::Clamp(Amplifier, 0, 255);
	if (IsInstant(Id))
	{
		// Healing 4 a level (4 << amplifier), damage 6 a level.
		if (Id == TEXT("instant_health"))
		{
			McSurvival::Heal(Stats, static_cast<float>(4 << FMath::Min(Amplifier, 20)));
		}
		else
		{
			Stats.InvulnTicks = 0;
			McSurvival::Hurt(Stats, static_cast<float>(6 << FMath::Min(Amplifier, 20)), McSurvival::EDamage::Magic);
		}
		return true;
	}
	Duration = FMath::Max(Duration, 1);
	if (FMcStatusEffect* Have = Stats.Effects.FindByPredicate([Id](const FMcStatusEffect& Effect) { return Effect.Id == Id; }))
	{
		if (Amplifier < Have->Amplifier || (Amplifier == Have->Amplifier && Duration <= Have->Duration))
		{
			return false;
		}
		Have->Amplifier = Amplifier;
		Have->Duration = Duration;
	}
	else
	{
		Stats.Effects.Add({ Id, Amplifier, Duration });
	}
	if (Id == TEXT("absorption"))
	{
		// Four hearts' worth a level, on top of what is already there.
		Stats.Absorption = FMath::Max(Stats.Absorption, 4.0f * (Amplifier + 1));
	}
	return true;
}

int32 McEffects::Remove(FMcPlayerStats& Stats, FName Id)
{
	int32 Removed = 0;
	for (int32 Index = Stats.Effects.Num() - 1; Index >= 0; --Index)
	{
		if (Id.IsNone() || Stats.Effects[Index].Id == Id)
		{
			if (Stats.Effects[Index].Id == TEXT("absorption"))
			{
				Stats.Absorption = 0.0f;
			}
			Stats.Effects.RemoveAt(Index);
			++Removed;
		}
	}
	return Removed;
}

void McEffects::Tick(FMcPlayerStats& Stats)
{
	if (Stats.Health <= 0.0f)
	{
		return;
	}
	// Health boost raises the most health there can be, four a level, while it lasts.
	const FMcStatusEffect* Boost = Find(Stats, TEXT("health_boost"));
	const float Wanted = 20.0f + (Boost ? 4.0f * (Boost->Amplifier + 1) : 0.0f);
	if (Stats.MaxHealth != Wanted)
	{
		Stats.MaxHealth = Wanted;
		Stats.Health = FMath::Min(Stats.Health, Stats.MaxHealth);
	}
	for (int32 Index = 0; Index < Stats.Effects.Num(); ++Index)
	{
		const FMcStatusEffect Effect = Stats.Effects[Index];
		if (Effect.Id == TEXT("regeneration") && IsDue(Effect.Duration, 50, Effect.Amplifier))
		{
			McSurvival::Heal(Stats, 1.0f);
		}
		else if (Effect.Id == TEXT("poison") && IsDue(Effect.Duration, 25, Effect.Amplifier) && Stats.Health > 1.0f)
		{
			// Poison never kills: it stops at half a heart.
			Stats.InvulnTicks = 0;
			McSurvival::Hurt(Stats, FMath::Min(1.0f, Stats.Health - 1.0f), McSurvival::EDamage::Magic);
		}
		else if (Effect.Id == TEXT("wither") && IsDue(Effect.Duration, 40, Effect.Amplifier))
		{
			Stats.InvulnTicks = 0;
			McSurvival::Hurt(Stats, 1.0f, McSurvival::EDamage::Wither);
		}
		else if (Effect.Id == TEXT("hunger"))
		{
			McSurvival::AddExhaustion(Stats, 0.005f * (Effect.Amplifier + 1));
		}
		else if (Effect.Id == TEXT("saturation"))
		{
			Stats.Food = FMath::Min(20, Stats.Food + Effect.Amplifier + 1);
			Stats.Saturation = FMath::Min(static_cast<float>(Stats.Food), Stats.Saturation + Effect.Amplifier + 1);
		}
		if (Stats.Health <= 0.0f)
		{
			return;
		}
	}
	for (int32 Index = Stats.Effects.Num() - 1; Index >= 0; --Index)
	{
		if (--Stats.Effects[Index].Duration <= 0)
		{
			if (Stats.Effects[Index].Id == TEXT("absorption"))
			{
				Stats.Absorption = 0.0f;
			}
			Stats.Effects.RemoveAt(Index);
		}
	}
}

float McEffects::GetSpeedFactor(const FMcPlayerStats& Stats)
{
	float Factor = 1.0f;
	if (const FMcStatusEffect* Speed = Find(Stats, TEXT("speed")))
	{
		Factor *= 1.0f + 0.2f * (Speed->Amplifier + 1);
	}
	if (const FMcStatusEffect* Slow = Find(Stats, TEXT("slowness")))
	{
		Factor *= FMath::Max(0.0f, 1.0f - 0.15f * (Slow->Amplifier + 1));
	}
	return Factor;
}

float McEffects::GetJumpBonus(const FMcPlayerStats& Stats)
{
	const FMcStatusEffect* Boost = Find(Stats, TEXT("jump_boost"));
	return Boost ? 0.1f * (Boost->Amplifier + 1) : 0.0f;
}

float McEffects::GetDigFactor(const FMcPlayerStats& Stats)
{
	float Factor = 1.0f;
	// Conduit Power digs as Haste of its level does, the two not adding up: the stronger counts (minecraft.wiki).
	const FMcStatusEffect* Haste = Find(Stats, TEXT("haste"));
	const FMcStatusEffect* Conduit = Find(Stats, TEXT("conduit_power"));
	const int32 Level = FMath::Max(Haste ? Haste->Amplifier + 1 : 0, Conduit ? Conduit->Amplifier + 1 : 0);
	if (Level > 0)
	{
		Factor *= 1.0f + 0.2f * Level;
	}
	if (const FMcStatusEffect* Fatigue = Find(Stats, TEXT("mining_fatigue")))
	{
		Factor *= FMath::Pow(0.3f, static_cast<float>(FMath::Min(Fatigue->Amplifier + 1, 4)));
	}
	return Factor;
}

float McEffects::GetAttackBonus(const FMcPlayerStats& Stats)
{
	float Bonus = 0.0f;
	if (const FMcStatusEffect* Strength = Find(Stats, TEXT("strength")))
	{
		Bonus += 3.0f * (Strength->Amplifier + 1);
	}
	if (const FMcStatusEffect* Weakness = Find(Stats, TEXT("weakness")))
	{
		Bonus -= 4.0f * (Weakness->Amplifier + 1);
	}
	return Bonus;
}

float McEffects::GetResistance(const FMcPlayerStats& Stats)
{
	const FMcStatusEffect* Resistance = Find(Stats, TEXT("resistance"));
	return Resistance ? FMath::Min(1.0f, 0.2f * (Resistance->Amplifier + 1)) : 0.0f;
}

bool McEffects::IsFireImmune(const FMcPlayerStats& Stats)
{
	return Find(Stats, TEXT("fire_resistance")) != nullptr;
}

FString McEffects::GetLevelText(int32 Amplifier)
{
	static const TCHAR* const Numerals[] = { TEXT(""), TEXT("II"), TEXT("III"), TEXT("IV"), TEXT("V"), TEXT("VI"), TEXT("VII"), TEXT("VIII"), TEXT("IX"), TEXT("X") };
	return Amplifier >= 0 && Amplifier < UE_ARRAY_COUNT(Numerals) ? FString(Numerals[Amplifier]) : FString::FromInt(Amplifier + 1);
}
