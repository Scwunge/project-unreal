#include "McCombat.h"
#include "Dom/JsonObject.h"
#include "McItemRegistry.h"

namespace McCombat
{
	FAttack GetAttack(const FMcItemStack& Held)
	{
		FAttack Result;
		if (Held.IsEmpty())
		{
			return Result;
		}
		const TSharedPtr<FJsonValue> Modifiers = FMcItemRegistry::Get().GetDefault(Held.Item, TEXT("minecraft:attribute_modifiers"));
		if (!Modifiers.IsValid() || Modifiers->Type != EJson::Array)
		{
			return Result;
		}
		for (const TSharedPtr<FJsonValue>& Each : Modifiers->AsArray())
		{
			const TSharedPtr<FJsonObject> Modifier = Each.IsValid() ? Each->AsObject() : nullptr;
			FString Type, Slot;
			double Amount = 0.0;
			if (!Modifier.IsValid() || !Modifier->TryGetStringField(TEXT("type"), Type) || !Modifier->TryGetNumberField(TEXT("amount"), Amount))
			{
				continue;
			}
			if (Modifier->TryGetStringField(TEXT("slot"), Slot) && Slot != TEXT("mainhand") && Slot != TEXT("any") && Slot != TEXT("hand"))
			{
				continue;
			}
			if (Type == TEXT("minecraft:attack_damage"))
			{
				Result.Damage += static_cast<float>(Amount);
			}
			else if (Type == TEXT("minecraft:attack_speed"))
			{
				Result.Speed += static_cast<float>(Amount);
			}
		}
		Result.Speed = FMath::Max(Result.Speed, 0.1f);
		return Result;
	}

	float GetStrength(int32 TicksSinceAttack, float Speed)
	{
		return FMath::Clamp((static_cast<float>(TicksSinceAttack) + 0.5f) / (20.0f / FMath::Max(Speed, 0.1f)), 0.0f, 1.0f);
	}

	float GetBowPower(int32 DrawTicks)
	{
		const float Seconds = static_cast<float>(DrawTicks) / 20.0f;
		return FMath::Min(1.0f, (Seconds * Seconds + Seconds * 2.0f) / 3.0f);
	}

	float GetBowFovScale(float DrawSeconds)
	{
		const float T = FMath::Clamp(DrawSeconds, 0.0f, 1.0f);
		return 1.0f - 0.15f * T * T;
	}

	namespace
	{
		double Gaussian(FRandomStream& Random)
		{
			// Box-Muller from two uniforms.
			const double U1 = FMath::Max(static_cast<double>(Random.FRand()), 1.0e-9);
			return FMath::Sqrt(-2.0 * FMath::Loge(U1)) * FMath::Cos(2.0 * PI * static_cast<double>(Random.FRand()));
		}
	}

	FVector GetBowAim(const FVector& Look, FRandomStream& Random)
	{
		return Look + FVector(Gaussian(Random), Gaussian(Random), Gaussian(Random)) * 0.0085;
	}

	double GetBowLaunchDrop()
	{
		return 0.1;
	}

	float GetHurtTilt(float HurtTicks)
	{
		const float T = FMath::Clamp(HurtTicks, 0.0f, 8.0f);
		return 41.1f * FMath::Pow(T / 10.0f, 4.08f);
	}

	float GetHurtSide(float McYawDegrees, double OffsetX, double OffsetZ, bool bHasSource)
	{
		if (!bHasSource)
		{
			return 1.0f;
		}
		const double Length = FMath::Sqrt(OffsetX * OffsetX + OffsetZ * OffsetZ);
		if (Length < 1.0e-6)
		{
			return 0.0f;
		}
		const double Yaw = FMath::DegreesToRadians(static_cast<double>(McYawDegrees));
		// Facing is (-sin, cos) in (x, z); the left of it is (cos, sin).
		return static_cast<float>((OffsetX * FMath::Cos(Yaw) + OffsetZ * FMath::Sin(Yaw)) / Length);
	}

	float GetBowShootPitch(float U, float Power)
	{
		return 0.62f + 0.21f * U + 0.5f * Power;
	}

	float GetDamage(float BaseDamage, float Strength, bool bCritical)
	{
		float Damage = BaseDamage * (0.2f + Strength * Strength * 0.8f);
		if (bCritical && Strength > 0.9f)
		{
			Damage *= 1.5f;
		}
		return Damage;
	}

	int32 GetWear(const FMcItemStack& Held)
	{
		if (Held.IsEmpty() || FMcItemRegistry::Get().GetMaxDamage(Held.Item) <= 0)
		{
			return 0;
		}
		const FString Name = Held.Item.ToString();
		if (Name.EndsWith(TEXT("_sword")) || Name.EndsWith(TEXT("_spear")) || Name.EndsWith(TEXT("_mace")) || Name == TEXT("minecraft:trident"))
		{
			return 1;
		}
		return Name.EndsWith(TEXT("_axe")) || Name.EndsWith(TEXT("_pickaxe")) || Name.EndsWith(TEXT("_shovel")) || Name.EndsWith(TEXT("_hoe")) ? 2 : 0;
	}

	FVector GetKnockback(const FVector& Victim, const FVector& Attacker, const FVector& OldVelocity, float Strength)
	{
		FVector Away(Victim.X - Attacker.X, 0.0, Victim.Z - Attacker.Z);
		if (Away.SizeSquared() < 1e-6)
		{
			Away = FVector(1.0, 0.0, 0.0);
		}
		Away.Normalize();
		// The old motion is halved, the push taken off it, and the player is lifted a little: at most 0.4.
		FVector Result(OldVelocity.X * 0.5 + Away.X * Strength, FMath::Min(0.4, OldVelocity.Y * 0.5 + Strength), OldVelocity.Z * 0.5 + Away.Z * Strength);
		return Result;
	}
}
