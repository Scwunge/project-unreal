// Pillager patrols and raids on a village (minecraft.wiki, "Pillager patrol" and "Raid"): the host's half of them. The
// omen a player carries into a village is turned into a raid by the game mode (it knows the players); everything
// that happens after is here.

#include "McEntityHost.h"
#include "McMob.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McVillager.h"

namespace
{
	// Raiders per wave, Java Edition (minecraft.wiki "Raid", wave composition tables read 2026-11-04; ranges are the wiki's, a count is drawn
	// uniformly inside each, which simplifies the game's own bonus rolls). Each row: pillager, vindicator, evoker, witch, ravager,
	// ravager with a pillager rider, ravager with a vindicator rider, ravager with an evoker rider; each as min, max.
	struct FWaveRow
	{
		int32 Min[8];
		int32 Max[8];
	};
	// Kinds in column order.
	const TCHAR* const WaveKinds[8] = { TEXT("minecraft:pillager"), TEXT("minecraft:vindicator"), TEXT("minecraft:evoker"), TEXT("minecraft:witch"),
		TEXT("minecraft:ravager"), TEXT("minecraft:ravager"), TEXT("minecraft:ravager"), TEXT("minecraft:ravager") };
	//                                    pil   vin   evk   wit   rav   r+p   r+v   r+e
	constexpr FWaveRow EasyWaves[4] = {
		{ { 4, 0, 0, 0, 0, 0, 0, 0 }, { 5, 0, 0, 0, 0, 0, 0, 0 } },
		{ { 3, 2, 0, 0, 0, 0, 0, 0 }, { 4, 3, 0, 0, 0, 0, 0, 0 } },
		{ { 3, 0, 0, 0, 1, 0, 0, 0 }, { 4, 1, 0, 0, 1, 0, 0, 0 } },
		{ { 3, 0, 0, 0, 1, 0, 0, 0 }, { 4, 1, 0, 0, 1, 0, 0, 0 } } };
	constexpr FWaveRow NormalWaves[6] = {
		{ { 4, 0, 0, 0, 0, 0, 0, 0 }, { 5, 1, 0, 0, 0, 0, 0, 0 } },
		{ { 3, 2, 0, 0, 0, 0, 0, 0 }, { 4, 3, 0, 0, 0, 0, 0, 0 } },
		{ { 3, 0, 0, 0, 1, 0, 0, 0 }, { 4, 1, 0, 1, 1, 0, 0, 0 } },
		{ { 4, 1, 0, 3, 0, 0, 0, 0 }, { 5, 2, 0, 3, 0, 0, 0, 0 } },
		{ { 4, 4, 1, 0, 0, 1, 0, 0 }, { 5, 5, 1, 1, 0, 1, 0, 0 } },
		{ { 4, 4, 1, 0, 0, 1, 0, 0 }, { 5, 5, 1, 1, 1, 1, 0, 0 } } };
	constexpr FWaveRow HardWaves[8] = {
		{ { 4, 0, 0, 0, 0, 0, 0, 0 }, { 6, 2, 0, 0, 0, 0, 0, 0 } },
		{ { 3, 2, 0, 0, 0, 0, 0, 0 }, { 5, 4, 0, 0, 0, 0, 0, 0 } },
		{ { 3, 0, 0, 0, 1, 0, 0, 0 }, { 5, 2, 0, 1, 1, 0, 0, 0 } },
		{ { 4, 1, 0, 3, 0, 0, 0, 0 }, { 6, 3, 0, 3, 0, 0, 0, 0 } },
		{ { 4, 4, 1, 0, 0, 1, 0, 0 }, { 6, 6, 1, 1, 0, 1, 0, 0 } },
		{ { 4, 2, 1, 0, 0, 0, 0, 0 }, { 6, 4, 1, 1, 0, 0, 0, 0 } },
		{ { 2, 5, 2, 1, 0, 0, 1, 1 }, { 4, 7, 2, 2, 0, 0, 1, 1 } },
		{ { 2, 5, 2, 1, 0, 0, 1, 1 }, { 4, 7, 2, 2, 0, 1, 1, 1 } } };

	// How far from a villager still counts as the village.
	constexpr double VillageRadius = 48.0;
	constexpr double RaidVillageRadius = 64.0;

	bool IsRaidVillager(const FMcEntity& Entity)
	{
		return !Entity.bRemoved && Entity.GetType() == FMcVillager::StaticType() && !static_cast<const FMcVillager&>(Entity).IsBaby();
	}
}

bool FMcEntityHost::CountVillagePoi(const FVector& Center) const
{
	// Beds, bells and the job blocks, within 48 blocks of the raid's middle and 16 up or down (the game counts points of interest within 64).
	static TSet<FName> Names;
	static TMap<FName, bool> Seen;
	if (Names.IsEmpty())
	{
		for (const TCHAR* Name : { TEXT("minecraft:bell"), TEXT("minecraft:composter"), TEXT("minecraft:barrel"), TEXT("minecraft:blast_furnace"), TEXT("minecraft:brewing_stand"),
			TEXT("minecraft:cartography_table"), TEXT("minecraft:cauldron"), TEXT("minecraft:fletching_table"), TEXT("minecraft:grindstone"), TEXT("minecraft:lectern"),
			TEXT("minecraft:loom"), TEXT("minecraft:smithing_table"), TEXT("minecraft:smoker"), TEXT("minecraft:stonecutter") })
		{
			Names.Add(Name);
		}
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcBlockWorld& Blocks = Manager.GetWorld();
	const FIntVector Middle(FMath::FloorToInt32(Center.X), FMath::FloorToInt32(Center.Y), FMath::FloorToInt32(Center.Z));
	for (int32 DY = -16; DY <= 16; ++DY)
	{
		for (int32 DX = -48; DX <= 48; ++DX)
		{
			for (int32 DZ = -48; DZ <= 48; ++DZ)
			{
				const FName Name = Registry.GetBlockOf(Blocks.GetBlock(Middle + FIntVector(DX, DY, DZ))).Name;
				bool* Known = Seen.Find(Name);
				if (!Known)
				{
					Known = &Seen.Add(Name, Names.Contains(Name) || Name.ToString().EndsWith(TEXT("_bed")));
				}
				if (*Known)
				{
					return true;
				}
			}
		}
	}
	return false;
}

int32 FMcEntityHost::GetWaveCount(int32 Difficulty, int32 Level)
{
	const int32 Base = Difficulty <= 1 ? 3 : (Difficulty == 2 ? 5 : 7);
	return Base + (Level >= 2 ? 1 : 0);
}

void FMcEntityHost::RollWave(int32 Difficulty, int32 Wave, FRandomStream& Random, TArray<TPair<FName, int32>>& Out)
{
	// Easy has 3 waves and an extra one, normal 5, hard 7; the extra wave is the table's last row. Waves past the table repeat it.
	const FWaveRow* Rows = Difficulty <= 1 ? EasyWaves : (Difficulty == 2 ? NormalWaves : HardWaves);
	const int32 RowCount = Difficulty <= 1 ? 4 : (Difficulty == 2 ? 6 : 8);
	const FWaveRow& Row = Rows[FMath::Clamp(Wave, 1, RowCount) - 1];
	Out.Reset();
	// Riders (the last three columns) ride a ravager in the game; here the ravager and its rider both appear (riding is not built).
	// The ravager itself is the column's count; the rider is its own mob.
	static const TCHAR* const RiderKinds[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, TEXT("minecraft:pillager"), TEXT("minecraft:vindicator"), TEXT("minecraft:evoker") };
	for (int32 Column = 0; Column < 8; ++Column)
	{
		const int32 Count = Random.RandRange(Row.Min[Column], Row.Max[Column]);
		if (Count > 0)
		{
			Out.Emplace(FName(WaveKinds[Column]), Count);
			if (RiderKinds[Column])
			{
				Out.Emplace(FName(RiderKinds[Column]), Count);
			}
		}
	}
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
	// UNFITTED PLACEHOLDER (provenance gap): the wiki says Hard adds a chance of one more pillager or vindicator a wave but gives no chance, and a vanilla run could not
	// show it (raid1-4.txt: no raid started in the reference world, so no wave was ever seen). The 1 in 2 and the 1 in 2 split between the two kinds are made up.
	if (Rules.Difficulty >= 3 && Wave.FRand() < 0.5f)
	{
		Kinds.Add(TPair<FName, int32>(Wave.FRand() < 0.5f ? FName(TEXT("minecraft:pillager")) : FName(TEXT("minecraft:vindicator")), 1));
	}
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
				Raid.CaptainId = Raider->Id;
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
		if (Ticks % 20 == 0)
		{
			// Raiders that strayed more than 112 blocks are removed.
			for (const int32 Id : Raid.Raiders)
			{
				FMcEntity* Found = Manager.Find(Id);
				if (Found && FVector::Dist(Found->Body.Position, Raid.Center) > RaiderLeashRange)
				{
					Found->bRemoved = true;
				}
			}
			Raid.Raiders.RemoveAll([this](int32 Id) { const FMcEntity* Found = Manager.Find(Id); return !Found || Found->bRemoved; });
		}
		// When the captain falls, the nearest raider takes up the ominous banner (wiki, Raid; the pick-up itself is not built, the banner drops with the dead one).
		if (Raid.CaptainId != 0 && !Raid.Raiders.Contains(Raid.CaptainId))
		{
			const FMcEntity* Old = Manager.Find(Raid.CaptainId);
			const FVector From = Old ? Old->Body.Position : Raid.Center;
			Raid.CaptainId = 0;
			FMcMob* Best = nullptr;
			double BestDistance = TNumericLimits<double>::Max();
			for (const int32 Id : Raid.Raiders)
			{
				FMcMob* Candidate = static_cast<FMcMob*>(Manager.Find(Id));
				const double Distance = Candidate ? FVector::DistSquared(Candidate->Body.Position, From) : BestDistance;
				if (Candidate && Distance < BestDistance)
				{
					BestDistance = Distance;
					Best = Candidate;
				}
			}
			if (Best)
			{
				Best->bCaptain = true;
				Raid.CaptainId = Best->Id;
			}
		}
		// A village with no bed, bell or job block left (that once had one) is no village (wiki, Raid), whatever is alive in it.
		if (Raid.Age % 200 == 0)
		{
			if (CountVillagePoi(Raid.Center))
			{
				Raid.bSawPoi = true;
			}
			else if (Raid.bSawPoi)
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
		}
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
