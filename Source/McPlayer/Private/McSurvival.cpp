#include "McSurvival.h"
#include "McEffects.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McGameRules.h"
#include "McEntityManager.h"
#include "McXpOrb.h"
#include "McItemRegistry.h"
#include "Serialization/Archive.h"

bool FMcPlayerStats::Serialize(FArchive& Ar)
{
	int32 Version = 2;
	Ar << Version;
	if (Ar.IsLoading() && Version != 1 && Version != 2)
	{
		return false;
	}
	Ar << Health << MaxHealth << Absorption << Food << Saturation << Air << MaxAir << XpLevel << XpProgress << Armor << HurtTime << bHardcore;
	Ar << InvulnTicks << LastDamage << OnFireTicks << Exhaustion << FoodTimer;
	if (Version >= 2)
	{
		int32 Count = Effects.Num();
		Ar << Count;
		if (Ar.IsLoading())
		{
			if (Count < 0 || Count > 256)
			{
				return false;
			}
			Effects.SetNum(Count);
		}
		for (FMcStatusEffect& Effect : Effects)
		{
			Ar << Effect.Id << Effect.Amplifier << Effect.Duration;
		}
	}
	return !Ar.IsError();
}

namespace McSurvival
{
	namespace
	{
		FName BlockNameAt(const FMcBlockWorld& World, const FMcBlockRegistry& Registry, double X, double Y, double Z, FMcStateId& OutState)
		{
			OutState = World.GetBlock(FIntVector(FMath::FloorToInt32(X), FMath::FloorToInt32(Y), FMath::FloorToInt32(Z)));
			return Registry.IsValidState(OutState) ? Registry.GetBlockOf(OutState).Name : NAME_None;
		}

		bool HoldsWater(const FMcBlockRegistry& Registry, FName Name, FMcStateId State)
		{
			static const FName Water(TEXT("minecraft:water")), Bubbles(TEXT("minecraft:bubble_column")), Kelp(TEXT("minecraft:kelp")),
				KelpPlant(TEXT("minecraft:kelp_plant")), Seagrass(TEXT("minecraft:seagrass")), TallSeagrass(TEXT("minecraft:tall_seagrass"));
			return Name == Water || Name == Bubbles || Name == Kelp || Name == KelpPlant || Name == Seagrass || Name == TallSeagrass
				|| Registry.GetPropertyValue(State, TEXT("waterlogged")) == TEXT("true");
		}
	}

	FEnvironment SampleEnvironment(const FMcBlockWorld& World, const FVector& Feet, float EyeHeight)
	{
		static const FName Lava(TEXT("minecraft:lava")), Fire(TEXT("minecraft:fire")), SoulFire(TEXT("minecraft:soul_fire")),
			Ladder(TEXT("minecraft:ladder")), Vine(TEXT("minecraft:vine")), Scaffolding(TEXT("minecraft:scaffolding")),
			TwistingVines(TEXT("minecraft:twisting_vines")), WeepingVines(TEXT("minecraft:weeping_vines")), CaveVines(TEXT("minecraft:cave_vines")),
			Cobweb(TEXT("minecraft:cobweb"));
		FEnvironment Environment;
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		if (!Registry.IsLoaded())
		{
			return Environment;
		}
		FMcStateId State = 0;
		const FName AtFeet = BlockNameAt(World, Registry, Feet.X, Feet.Y + 0.1, Feet.Z, State);
		Environment.bFeetInWater = HoldsWater(Registry, AtFeet, State);
		Environment.bInLava = AtFeet == Lava;
		Environment.bInFire = AtFeet == Fire || AtFeet == SoulFire;
		Environment.bClimbing = AtFeet == Ladder || AtFeet == Vine || AtFeet == Scaffolding || AtFeet == TwistingVines || AtFeet == WeepingVines || AtFeet == CaveVines;
		Environment.bInCobweb = AtFeet == Cobweb;
		const FName AtBody = BlockNameAt(World, Registry, Feet.X, Feet.Y + 0.9, Feet.Z, State);
		Environment.bInLava |= AtBody == Lava;
		Environment.bInFire |= AtBody == Fire || AtBody == SoulFire;
		const FName AtEyes = BlockNameAt(World, Registry, Feet.X, Feet.Y + EyeHeight, Feet.Z, State);
		Environment.bHeadInWater = HoldsWater(Registry, AtEyes, State);
		return Environment;
	}

	void GetArmorValues(const FMcContainer& Worn, float& OutArmor, float& OutToughness)
	{
		OutArmor = 0.0f;
		OutToughness = 0.0f;
		for (int32 Slot = 0; Slot < Worn.Num(); ++Slot)
		{
			FMcArmorData Data;
			if (!Worn.Get(Slot).IsEmpty() && FMcItemRegistry::Get().GetArmor(Worn.Get(Slot).Item, Data))
			{
				OutArmor += static_cast<float>(Data.Armor);
				OutToughness += static_cast<float>(Data.Toughness);
			}
		}
	}

	float ReduceByArmor(float Damage, float ArmorPoints, float Toughness)
	{
		const float Protection = FMath::Min(20.0f, FMath::Max(ArmorPoints / 5.0f, ArmorPoints - Damage / (2.0f + Toughness / 4.0f)));
		return Damage * (1.0f - Protection / 25.0f);
	}

	bool BypassesArmor(EDamage Kind)
	{
		switch (Kind)
		{
		case EDamage::Generic:
		case EDamage::Fall:
		case EDamage::Drown:
		case EDamage::OnFire:
		case EDamage::Starve:
		case EDamage::Void:
		case EDamage::Magic:
		case EDamage::Wither:
			return true;
		default:
			return false;
		}
	}

	float GetHurtExhaustion(EDamage Kind)
	{
		switch (Kind)
		{
		case EDamage::InFire:
		case EDamage::Lava:
		case EDamage::Mob:
			return 0.1f;
		default:
			return 0.0f;
		}
	}

	bool IsDamageEnabled(EDamage Kind, const FMcGameRules& Rules)
	{
		switch (Kind)
		{
		case EDamage::Fall: return Rules.bFallDamage;
		case EDamage::Drown: return Rules.bDrowningDamage;
		case EDamage::InFire:
		case EDamage::OnFire:
		case EDamage::Lava: return Rules.bFireDamage;
		default: return true;
		}
	}

	float Hurt(FMcPlayerStats& Stats, float Amount, EDamage Kind, FMcContainer* Worn, const FMcGameRules* Rules)
	{
		if (Amount <= 0.0f || Stats.Health <= 0.0f || (Rules && !IsDamageEnabled(Kind, *Rules)))
		{
			return 0.0f;
		}
		const bool bFire = Kind == EDamage::InFire || Kind == EDamage::OnFire || Kind == EDamage::Lava;
		if (bFire && McEffects::IsFireImmune(Stats))
		{
			return 0.0f;
		}
		float Dealt = Amount;
		if (Kind != EDamage::Void && Stats.InvulnTicks > 10)
		{
			// Still immune from the last hit: only the part that is more than it got through.
			if (Amount <= Stats.LastDamage)
			{
				return 0.0f;
			}
			Dealt = Amount - Stats.LastDamage;
			Stats.LastDamage = Amount;
		}
		else
		{
			Stats.LastDamage = Amount;
			Stats.InvulnTicks = 20;
		}
		Stats.HurtTime = 10;
		Stats.LastHurtKind = static_cast<uint8>(Kind);

		float Taken = Dealt;
		if (!BypassesArmor(Kind))
		{
			float ArmorPoints = static_cast<float>(Stats.Armor);
			float Toughness = 0.0f;
			if (Worn)
			{
				GetArmorValues(*Worn, ArmorPoints, Toughness);
				// Every piece takes some wear from the blow, and one may break. Elytra aren't armour: only flying wears them.
				const int32 Wear = FMath::Max(1, FMath::FloorToInt32(Dealt / 4.0f));
				static const FName Elytra(TEXT("minecraft:elytra"));
				for (int32 Slot = 0; Slot < Worn->Num(); ++Slot)
				{
					FMcItemStack& Piece = Worn->GetMutable(Slot);
					if (!Piece.IsEmpty() && Piece.Item != Elytra && Piece.HurtAndBreak(Wear))
					{
						Piece.Clear();
					}
				}
				float NewArmor = 0.0f, NewToughness = 0.0f;
				GetArmorValues(*Worn, NewArmor, NewToughness);
				Stats.Armor = FMath::RoundToInt32(NewArmor);
			}
			Taken = ReduceByArmor(Dealt, ArmorPoints, Toughness);
		}
		if (Kind != EDamage::Void && Kind != EDamage::Starve)
		{
			Taken *= 1.0f - McEffects::GetResistance(Stats);
		}
		const float Absorbed = FMath::Min(Stats.Absorption, Taken);
		Stats.Absorption -= Absorbed;
		Taken -= Absorbed;
		Stats.Health = FMath::Max(0.0f, Stats.Health - Taken);
		AddExhaustion(Stats, GetHurtExhaustion(Kind));
		return Taken;
	}

	void Heal(FMcPlayerStats& Stats, float Amount)
	{
		if (Amount > 0.0f && Stats.Health > 0.0f)
		{
			Stats.Health = FMath::Min(Stats.MaxHealth, Stats.Health + Amount);
		}
	}

	int32 GetFallDamage(float Distance)
	{
		return FMath::Max(0, FMath::CeilToInt32(Distance - 3.0f));
	}

	int32 FFallTracker::Update(float PreviousY, float NewY, bool bOnGround, bool bBreaksFall)
	{
		if (bBreaksFall)
		{
			Distance = 0.0f;
			return 0;
		}
		const float Rise = NewY - PreviousY;
		Distance = FMath::Max(0.0f, Distance - Rise);
		if (bOnGround)
		{
			const int32 Damage = GetFallDamage(Distance);
			Distance = 0.0f;
			return Damage;
		}
		return 0;
	}

	bool Tick(FMcPlayerStats& Stats, const FEnvironment& Environment, EDifficulty Difficulty, FMcContainer* Worn, const FMcGameRules* Rules)
	{
		if (Stats.Health <= 0.0f)
		{
			return true;
		}
		Stats.HurtTime = FMath::Max(0, Stats.HurtTime - 1);
		Stats.InvulnTicks = FMath::Max(0, Stats.InvulnTicks - 1);
		McEffects::Tick(Stats);
		if (Stats.Health <= 0.0f)
		{
			return true;
		}
		if (McEffects::IsFireImmune(Stats))
		{
			Stats.OnFireTicks = 0;
		}

		// Breath: 1 a tick under water, and each time it runs 20 ticks past empty, 2 damage; 4 a tick back in the air.
		// Water Breathing and Conduit Power fill it under water too (minecraft.wiki, "Conduit Power").
		if (Environment.bHeadInWater && !McEffects::Find(Stats, TEXT("water_breathing")) && !McEffects::Find(Stats, TEXT("conduit_power")))
		{
			if (--Stats.Air <= -20)
			{
				Stats.Air = 0;
				Hurt(Stats, 2.0f, EDamage::Drown, Worn, Rules);
			}
		}
		else
		{
			Stats.Air = FMath::Min(Stats.MaxAir, Stats.Air + 4);
		}

		// Burning: lava sets it for 15 seconds, fire for 8, water puts it out; 1 damage a second.
		if (Environment.bFeetInWater)
		{
			Stats.OnFireTicks = 0;
		}
		if (Environment.bInLava)
		{
			Stats.OnFireTicks = FMath::Max(Stats.OnFireTicks, 300);
			Hurt(Stats, 4.0f, EDamage::Lava, Worn, Rules);
		}
		if (Environment.bInFire && !Environment.bFeetInWater)
		{
			Stats.OnFireTicks = FMath::Max(Stats.OnFireTicks, 160);
			Hurt(Stats, 1.0f, EDamage::InFire, Worn, Rules);
		}
		if (Stats.OnFireTicks > 0)
		{
			if (--Stats.OnFireTicks % 20 == 0)
			{
				Hurt(Stats, 1.0f, EDamage::OnFire, Worn, Rules);
			}
		}

		TickNeeds(Stats, Difficulty, Worn, Rules);
		return Stats.Health <= 0.0f;
	}

	void AddExhaustion(FMcPlayerStats& Stats, float Amount)
	{
		Stats.Exhaustion = FMath::Min(40.0f, Stats.Exhaustion + Amount);
	}

	void TickNeeds(FMcPlayerStats& Stats, EDifficulty Difficulty, FMcContainer* Worn, const FMcGameRules* Rules)
	{
		const bool bMayHeal = !Rules || Rules->bNaturalRegeneration;
		if (Difficulty == EDifficulty::Peaceful)
		{
			// Nothing is spent, and health comes back by itself, a point a second.
			Stats.Exhaustion = 0.0f;
			if (bMayHeal && Stats.Health < Stats.MaxHealth && ++Stats.FoodTimer >= 20)
			{
				Heal(Stats, 1.0f);
				Stats.FoodTimer = 0;
			}
			return;
		}
		if (Stats.Exhaustion >= 4.0f)
		{
			Stats.Exhaustion -= 4.0f;
			if (Stats.Saturation > 0.0f)
			{
				Stats.Saturation = FMath::Max(0.0f, Stats.Saturation - 1.0f);
			}
			else
			{
				Stats.Food = FMath::Max(0, Stats.Food - 1);
			}
		}

		const bool bHurt = bMayHeal && Stats.Health < Stats.MaxHealth;
		if (Stats.Food >= 20 && Stats.Saturation > 0.0f && bHurt)
		{
			if (++Stats.FoodTimer >= 10)
			{
				const float Spent = FMath::Min(Stats.Saturation, 6.0f);
				Heal(Stats, Spent / 6.0f);
				AddExhaustion(Stats, Spent);
				Stats.FoodTimer = 0;
			}
		}
		else if (Stats.Food >= 18 && bHurt)
		{
			if (++Stats.FoodTimer >= 80)
			{
				Heal(Stats, 1.0f);
				AddExhaustion(Stats, 6.0f);
				Stats.FoodTimer = 0;
			}
		}
		else if (Stats.Food <= 0)
		{
			if (++Stats.FoodTimer >= 80)
			{
				// Easy stops at 10 health, Normal at half a heart; Hard goes on to the end.
				const bool bMay = Difficulty == EDifficulty::Hard || (Difficulty == EDifficulty::Normal && Stats.Health > 1.0f) || Stats.Health > 10.0f;
				if (bMay)
				{
					Hurt(Stats, 1.0f, EDamage::Starve, Worn);
				}
				Stats.FoodTimer = 0;
			}
		}
		else
		{
			Stats.FoodTimer = 0;
		}
	}

	bool CanEat(const FMcPlayerStats& Stats, const FMcFoodData& Food)
	{
		return Food.bCanAlwaysEat || Stats.Food < 20;
	}

	void Eat(FMcPlayerStats& Stats, const FMcFoodData& Food)
	{
		Stats.Food = FMath::Min(20, Stats.Food + Food.Nutrition);
		Stats.Saturation = FMath::Min(static_cast<float>(Stats.Food), Stats.Saturation + Food.Saturation);
	}

	int32 GetXpToNextLevel(int32 Level)
	{
		Level = FMath::Max(0, Level);
		return Level <= 15 ? 2 * Level + 7 : Level <= 30 ? 5 * Level - 38 : 9 * Level - 158;
	}

	int32 GetTotalXpForLevel(int32 Level)
	{
		int32 Total = 0;
		for (int32 Index = 0; Index < Level; ++Index)
		{
			Total += GetXpToNextLevel(Index);
		}
		return Total;
	}

	int32 GetTotalXp(const FMcPlayerStats& Stats)
	{
		return GetTotalXpForLevel(Stats.XpLevel) + FMath::RoundToInt32(Stats.XpProgress * GetXpToNextLevel(Stats.XpLevel));
	}

	void AddXp(FMcPlayerStats& Stats, int32 Points)
	{
		int32 Total = FMath::Max(0, GetTotalXp(Stats) + Points);
		int32 Level = 0;
		while (Level < 100000 && Total >= GetXpToNextLevel(Level))
		{
			Total -= GetXpToNextLevel(Level);
			++Level;
		}
		Stats.XpLevel = Level;
		Stats.XpProgress = static_cast<float>(Total) / GetXpToNextLevel(Level);
	}

	int32 GetDeathXp(const FMcPlayerStats& Stats)
	{
		return FMath::Min(Stats.XpLevel * 7, 100);
	}

	FMcItemStack GiveItem(FMcContainer& Inventory, const FMcItemStack& Stack)
	{
		return Stack.IsEmpty() ? FMcItemStack() : Inventory.AddItem(Stack);
	}

	int32 CollectItems(FMcEntityManager& Entities, const FBox& PlayerBox, FMcContainer& Inventory)
	{
		int32 Taken = 0;
		TArray<FMcEntity*> Near;
		Entities.FindInBox(PlayerBox.ExpandBy(FVector(1.0, 0.5, 1.0)), Near);
		for (FMcEntity* Entity : Near)
		{
			if (Entity->GetType() != FMcItemEntity::StaticType())
			{
				continue;
			}
			FMcItemEntity* Item = static_cast<FMcItemEntity*>(Entity);
			if (Item->PickupDelay > 0 || Item->bRemoved || Item->Stack.IsEmpty())
			{
				continue;
			}
			const int32 Before = Item->Stack.Count;
			const FMcItemStack Left = Inventory.AddItem(Item->Stack);
			Taken += Before - Left.Count;
			if (Left.IsEmpty())
			{
				Item->bRemoved = true;
			}
			else
			{
				Item->Stack = Left;
			}
		}
		return Taken;
	}

	int32 CollectXp(FMcEntityManager& Entities, const FBox& PlayerBox, FMcPlayerStats& Stats)
	{
		int32 Points = 0;
		TArray<FMcEntity*> Near;
		Entities.FindInBox(PlayerBox.ExpandBy(FVector(0.5, 0.25, 0.5)), Near);
		for (FMcEntity* Entity : Near)
		{
			if (Entity->GetType() == FMcXpOrb::StaticType() && !Entity->bRemoved)
			{
				Points += static_cast<FMcXpOrb*>(Entity)->Value;
				Entity->bRemoved = true;
			}
		}
		if (Points > 0)
		{
			AddXp(Stats, Points);
		}
		return Points;
	}
}
