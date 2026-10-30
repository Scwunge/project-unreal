#include "McStructureSpawns.h"

FMcStructureSpawns& FMcStructureSpawns::Get()
{
	static FMcStructureSpawns Instance;
	return Instance;
}

void FMcStructureSpawns::AddZone(const FMcStructureZone& Zone)
{
	FScopeLock Guard(&Lock);
	for (const FMcStructureZone& Each : Zones)
	{
		if (Each.Structure == Zone.Structure && Each.Min == Zone.Min && Each.Max == Zone.Max)
		{
			return;
		}
	}
	Zones.Add(Zone);
}

void FMcStructureSpawns::AddMob(const FString& Key, const FMcStructureMob& Mob)
{
	FScopeLock Guard(&Lock);
	bool bAlready = false;
	MobKeys.Add(Key + TEXT("/") + Mob.Kind.ToString(), &bAlready);
	if (!bAlready)
	{
		Mobs.Add(Mob);
	}
}

bool FMcStructureSpawns::FindMonsterZone(const FIntVector& Cell, FMcStructureZone& Out) const
{
	FScopeLock Guard(&Lock);
	for (const FMcStructureZone& Each : Zones)
	{
		if (!Each.Monsters.IsEmpty() && Each.Contains(Cell))
		{
			Out = Each;
			return true;
		}
	}
	return false;
}

void FMcStructureSpawns::GetMonsterZonesNear(double X, double Z, double Radius, TArray<FMcStructureZone>& Out) const
{
	FScopeLock Guard(&Lock);
	for (const FMcStructureZone& Each : Zones)
	{
		if (!Each.Monsters.IsEmpty() && X >= Each.Min.X - Radius && X <= Each.Max.X + Radius && Z >= Each.Min.Z - Radius && Z <= Each.Max.Z + Radius)
		{
			Out.Add(Each);
		}
	}
}

void FMcStructureSpawns::TakeMobs(TFunctionRef<bool(const FMcStructureMob&)> Take)
{
	TArray<FMcStructureMob> Waiting;
	{
		FScopeLock Guard(&Lock);
		Waiting = Mobs;
	}
	TArray<FMcStructureMob> Left;
	for (const FMcStructureMob& Mob : Waiting)
	{
		if (!Take(Mob))
		{
			Left.Add(Mob);
		}
	}
	FScopeLock Guard(&Lock);
	// Mobs added while Take ran are kept after the ones left.
	TArray<FMcStructureMob> Added;
	for (int32 Index = Waiting.Num(); Index < Mobs.Num(); ++Index)
	{
		Added.Add(Mobs[Index]);
	}
	Mobs = Left;
	Mobs.Append(Added);
}

void FMcStructureSpawns::Clear()
{
	FScopeLock Guard(&Lock);
	Zones.Reset();
	Mobs.Reset();
	MobKeys.Reset();
}
