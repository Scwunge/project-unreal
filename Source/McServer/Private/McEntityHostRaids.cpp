// Pillager patrols and raids on a village (minecraft.wiki, "Pillager patrol" and "Raid"): the host's half of them. The
// omen a player carries into a village is turned into a raid by the game mode (it knows the players); everything
// that happens after is here.

#include "McEntityHost.h"
#include "McMob.h"
#include "McVillager.h"

namespace
{
	// Raiders per wave (index 0 unused, waves 1 to 7), the base of each kind before the difficulty's extra ones.
	constexpr int32 PillagersPerWave[8] = { 0, 4, 3, 3, 4, 4, 4, 2 };
	constexpr int32 VindicatorsPerWave[8] = { 0, 0, 2, 0, 1, 4, 2, 5 };
	constexpr int32 EvokersPerWave[8] = { 0, 0, 0, 0, 0, 1, 1, 2 };
	constexpr int32 WitchesPerWave[8] = { 0, 0, 0, 0, 3, 0, 0, 1 };
	constexpr int32 RavagersPerWave[8] = { 0, 0, 0, 1, 0, 1, 0, 2 };

	// How far from a villager still counts as the village.
	constexpr double VillageRadius = 48.0;
	constexpr double RaidVillageRadius = 64.0;

	bool IsRaidVillager(const FMcEntity& Entity)
	{
		return !Entity.bRemoved && Entity.GetType() == FMcVillager::StaticType() && !static_cast<const FMcVillager&>(Entity).IsBaby();
	}
}

int32 FMcEntityHost::GetWaveCount(int32 Difficulty, int32 Level)
{
	const int32 Base = Difficulty <= 1 ? 3 : (Difficulty == 2 ? 5 : 7);
	return Base + (Level >= 2 ? 1 : 0);
}

void FMcEntityHost::RollWave(int32 Difficulty, int32 Wave, FRandomStream& Random, TArray<TPair<FName, int32>>& Out)
{
	// The extra wave is as strong as the last of the ordinary ones.
	const int32 Index = FMath::Clamp(Wave, 1, 7);
	// The pillagers and vindicators come with up to one (up to two on hard) more than the base; on easy only the pillagers do.
	const int32 Extra = Difficulty >= 3 ? 3 : 2;
	int32 Pillagers = PillagersPerWave[Index] + Random.RandHelper(Extra);
	int32 Vindicators = VindicatorsPerWave[Index] + (Difficulty >= 2 ? Random.RandHelper(Extra) : 0);
	int32 Witches = WitchesPerWave[Index];
	// A witch may join the third and fifth waves.
	if (Difficulty >= 2 && (Index == 3 || Index == 5) && Random.RandHelper(2) == 0)
	{
		++Witches;
	}
	// A hard raid sends a witch with every wave from the seventh.
	if (Difficulty >= 3 && Index >= 7)
	{
		Witches = FMath::Max(1, Witches);
	}
	Out.Reset();
	const auto Put = [&Out](const TCHAR* Kind, int32 Count)
	{
		if (Count > 0)
		{
			Out.Emplace(FName(Kind), Count);
		}
	};
	Put(TEXT("minecraft:pillager"), Pillagers);
	Put(TEXT("minecraft:vindicator"), Vindicators);
	Put(TEXT("minecraft:evoker"), EvokersPerWave[Index]);
	Put(TEXT("minecraft:witch"), Witches);
	Put(TEXT("minecraft:ravager"), RavagersPerWave[Index]);
}

bool FMcEntityHost::FindVillage(const FVector& Where, FVector& OutCenter) const
{
	FVector Sum = FVector::ZeroVector;
	int32 Count = 0;
	bool bNear = false;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (!IsRaidVillager(*Entity))
		{
			continue;
		}
		const double Distance = FVector::Dist(Entity->Body.Position, Where);
		bNear |= Distance <= VillageRadius;
		if (Distance <= RaidVillageRadius + VillageRadius)
		{
			Sum += Entity->Body.Position;
			++Count;
		}
	}
	if (!bNear || Count == 0)
	{
		return false;
	}
	OutCenter = Sum / Count;
	return true;
}

int32 FMcEntityHost::StartRaid(const FVector& Center, int32 Level, int32 DelayTicks)
{
	if (Rules.IsPeaceful())
	{
		return 0;
	}
	for (const FRaid& Each : Raids)
	{
		if (Each.Status != FRaid::EStatus::Victory && Each.Status != FRaid::EStatus::Defeat && FVector::Dist(Each.Center, Center) < 2.0 * RaidVillageRadius)
		{
			return 0;
		}
	}
	FRaid Raid;
	Raid.Id = NextRaidId++;
	Raid.Center = Center;
	Raid.Level = FMath::Clamp(Level, 1, 5);
	Raid.TotalWaves = GetWaveCount(Rules.Difficulty, Raid.Level);
	Raid.Timer = FMath::Max(0, DelayTicks) + RaidWarmupTicks;
	Raids.Add(Raid);
	return Raid.Id;
}

const FMcEntityHost::FRaid* FMcEntityHost::FindRaid(int32 Id) const
{
	return Raids.FindByPredicate([Id](const FRaid& Each) { return Each.Id == Id; });
}

bool FMcEntityHost::GetRaidBar(const FVector& Player, FString& OutTitle, float& OutFraction) const
{
	const FRaid* Best = nullptr;
	double BestDistance = RaidBarRange;
	for (const FRaid& Each : Raids)
	{
		const double Distance = FVector::Dist(Each.Center, Player);
		if (Distance <= BestDistance)
		{
			BestDistance = Distance;
			Best = &Each;
		}
	}
	if (!Best)
	{
		return false;
	}
	OutTitle = TEXT("Raid");
	switch (Best->Status)
	{
	case FRaid::EStatus::Waiting:
		// The bar fills while the village waits for the first wave.
		OutFraction = FMath::Clamp(1.0f - static_cast<float>(Best->Timer) / RaidWarmupTicks, 0.0f, 1.0f);
		break;
	case FRaid::EStatus::Victory:
		OutTitle += TEXT(" - Victory");
		OutFraction = 0.0f;
		break;
	case FRaid::EStatus::Defeat:
		OutTitle += TEXT(" - Defeat");
		OutFraction = 0.0f;
		break;
	default:
	{
		// What is left of the wave's health.
		float Health = 0.0f;
		for (const int32 Id : Best->Raiders)
		{
			if (const FMcEntity* Found = const_cast<FMcEntityManager&>(Manager).Find(Id))
			{
				if (!Found->bRemoved && Found->IsLiving())
				{
					Health += FMath::Max(0.0f, static_cast<const FMcMob&>(*Found).Health);
				}
			}
		}
		OutFraction = Best->WaveHealth > 0.0f ? FMath::Clamp(Health / Best->WaveHealth, 0.0f, 1.0f) : 0.0f;
		break;
	}
	}
	return true;
}

int32 FMcEntityHost::RingBell(const FVector& Bell)
{
	int32 Count = 0;
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (Entity->bRemoved || !Entity->IsLiving())
		{
			continue;
		}
		FMcMob& Mob = static_cast<FMcMob&>(*Entity);
		if (Mob.Type.bIsRaider && FVector::Dist(Mob.Body.Position, Bell) <= BellGlowRange)
		{
			Mob.GlowTicks = BellGlowTicks;
			++Count;
		}
	}
	return Count;
}

bool FMcEntityHost::SpawnPatrol(const FVector& Player)
{
	for (int32 Try = 0; Try < 20; ++Try)
	{
		const float Angle = Random.FRand() * 2.0f * PI;
		const float Radius = 24.0f + Random.FRand() * 24.0f;
		const int32 X = FMath::FloorToInt32(Player.X + FMath::Cos(Angle) * Radius);
		const int32 Z = FMath::FloorToInt32(Player.Z + FMath::Sin(Angle) * Radius);
		const TOptional<int32> Y = FindStandingY(X, Z, FMath::FloorToInt32(Player.Y), 16);
		if (!Y.IsSet())
		{
			continue;
		}
		const FVector Pos(X + 0.5, Y.GetValue(), Z + 0.5);
		FVector Middle;
		if (FindVillage(Pos, Middle))
		{
			// Not within a couple of chunks of a village.
			return false;
		}
		// A captain, and as many pillagers again as the difficulty is hard, give or take one.
		const int32 Members = 1 + FMath::Max(1, Rules.Difficulty) + Random.RandHelper(2);
		const FVector Heading = Player + FVector(Random.FRandRange(-8.0, 8.0), 0.0, Random.FRandRange(-8.0, 8.0));
		int32 Made = 0;
		for (int32 Index = 0; Index < Members; ++Index)
		{
			FMcMob* Pillager = SpawnMob(TEXT("minecraft:pillager"), Pos + FVector(Random.FRandRange(-2.0, 2.0), 0.0, Random.FRandRange(-2.0, 2.0)));
			if (!Pillager)
			{
				continue;
			}
			Pillager->bCaptain = Index == 0;
			Pillager->Goal = Heading;
			++Made;
		}
		return Made > 0;
	}
	return false;
}

void FMcEntityHost::SpawnWave(FRaid& Raid)
{
	++Raid.Wave;
	TArray<TPair<FName, int32>> Kinds;
	FRandomStream Wave(static_cast<int32>(Seed) ^ (Raid.Id * 7919) ^ (Raid.Wave * 104729));
	RollWave(Rules.Difficulty, Raid.Wave, Wave, Kinds);
	Raid.Raiders.Reset();
	Raid.WaveHealth = 0.0f;
	bool bCaptainMade = false;
	for (const TPair<FName, int32>& Kind : Kinds)
	{
		for (int32 Index = 0; Index < Kind.Value; ++Index)
		{
			FVector Pos = Raid.Center + FVector(Random.FRandRange(-6.0, 6.0), 0.0, Random.FRandRange(-6.0, 6.0));
			// Out at the edge of the village, on whatever ground there is, or at its middle if there is none.
			for (int32 Try = 0; Try < 12; ++Try)
			{
				const float Angle = Random.FRand() * 2.0f * PI;
				const float Radius = 28.0f + Random.FRand() * 12.0f;
				const int32 X = FMath::FloorToInt32(Raid.Center.X + FMath::Cos(Angle) * Radius);
				const int32 Z = FMath::FloorToInt32(Raid.Center.Z + FMath::Sin(Angle) * Radius);
				const TOptional<int32> Y = FindStandingY(X, Z, FMath::FloorToInt32(Raid.Center.Y), 24);
				if (Y.IsSet())
				{
					Pos = FVector(X + 0.5, Y.GetValue(), Z + 0.5);
					break;
				}
			}
			FMcMob* Raider = SpawnMob(Kind.Key, Pos);
			if (!Raider)
			{
				continue;
			}
			Raider->RaidId = Raid.Id;
			Raider->Goal = Raid.Center;
			// One of each wave leads it and carries the banner.
			if (!bCaptainMade)
			{
				Raider->bCaptain = true;
				bCaptainMade = true;
			}
			Raid.Raiders.Add(Raider->Id);
			Raid.WaveHealth += Raider->Type.MaxHealth;
		}
	}
	Manager.PlaySound(FName("event.raid.horn"), Raid.Center, 64.0f, 1.0f);
}

void FMcEntityHost::TickRaids()
{
	if (Raids.IsEmpty())
	{
		return;
	}
	const bool bPeaceful = Rules.IsPeaceful();
	for (int32 Index = Raids.Num() - 1; Index >= 0; --Index)
	{
		FRaid& Raid = Raids[Index];
		++Raid.Age;
		if (bPeaceful)
		{
			Raids.RemoveAt(Index);
			continue;
		}
		if (Raid.Status == FRaid::EStatus::Victory || Raid.Status == FRaid::EStatus::Defeat)
		{
			if (--Raid.Timer <= 0)
			{
				Raids.RemoveAt(Index);
			}
			continue;
		}
		const auto Finish = [&Raid](FRaid::EStatus Status)
		{
			Raid.Status = Status;
			Raid.Timer = RaidCelebrationTicks;
		};
		// A village with nobody in it is not one any more, and a raid that drags on is over.
		bool bVillagers = false;
		for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
		{
			if (IsRaidVillager(*Entity) && FVector::Dist(Entity->Body.Position, Raid.Center) <= RaidVillageRadius)
			{
				bVillagers = true;
				break;
			}
		}
		if (!bVillagers || Raid.Age > RaidMaxTicks)
		{
			Finish(FRaid::EStatus::Defeat);
			for (const int32 Id : Raid.Raiders)
			{
				if (FMcEntity* Found = Manager.Find(Id))
				{
					static_cast<FMcMob*>(Found)->RaidId = 0;
				}
			}
			continue;
		}
		if (Raid.Status == FRaid::EStatus::Waiting)
		{
			if (--Raid.Timer <= 0)
			{
				Raid.Status = FRaid::EStatus::Active;
				SpawnWave(Raid);
			}
			continue;
		}
		// The raiders of the wave still alive; the others are dead (or gone).
		Raid.Raiders.RemoveAll([this](int32 Id)
		{
			const FMcEntity* Found = Manager.Find(Id);
			return !Found || Found->bRemoved || static_cast<const FMcMob&>(*Found).Health <= 0.0f;
		});
		if (Raid.Raiders.IsEmpty())
		{
			if (Raid.Timer > 0)
			{
				if (--Raid.Timer == 0 && Raid.Wave < Raid.TotalWaves)
				{
					SpawnWave(Raid);
				}
				else if (Raid.Timer == 0)
				{
					Finish(FRaid::EStatus::Victory);
					if (OnRaidWon)
					{
						OnRaidWon(Raid.Center, Raid.Level);
					}
				}
			}
			else
			{
				Raid.Timer = RaidWaveDelayTicks;
			}
			continue;
		}
		Raid.Timer = 0;
		// Raiders that have no one to fight make for the village.
		if (Ticks % 40 == 0)
		{
			for (const int32 Id : Raid.Raiders)
			{
				FMcMob* Raider = static_cast<FMcMob*>(Manager.Find(Id));
				if (Raider && (!Raider->Goal.IsSet() || FVector::DistSquared(Raider->Body.Position, Raid.Center) > 144.0))
				{
					Raider->Goal = Raid.Center;
				}
			}
		}
	}
}
