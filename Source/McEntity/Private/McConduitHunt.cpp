#include "McConduitHunt.h"
#include "McEntityManager.h"
#include "McMob.h"

namespace
{
	FMcMob* AsHuntable(FMcEntity* Entity, TFunctionRef<bool(const FMcEntity&)> IsWet)
	{
		FMcMob* Mob = Entity && !Entity->bRemoved && Entity->IsLiving() ? static_cast<FMcMob*>(Entity) : nullptr;
		return Mob && Mob->Health > 0.0f && Mob->Type.IsHostile() && IsWet(*Mob) ? Mob : nullptr;
	}
}

int32 McConduitHunt::Pulse(FMcEntityManager& Manager, const FIntVector& Pos, int32 Target, TFunctionRef<bool(const FMcEntity&)> IsWet)
{
	const FVector Middle = FVector(Pos) + FVector(0.5);
	if (Target != 0)
	{
		FMcMob* Mob = AsHuntable(Manager.Find(Target), IsWet);
		if (!Mob || FVector::Dist(Mob->Body.Position, Middle) >= Reach)
		{
			return 0;
		}
		Mob->Damage(Damage, nullptr);
		return Target;
	}
	TArray<FMcEntity*> Near;
	Manager.FindInBox(FBox(FVector(Pos) - FVector(Reach), FVector(Pos) + FVector(Reach + 1.0)), Near);
	TArray<FMcMob*> Hunted;
	for (FMcEntity* Entity : Near)
	{
		if (FMcMob* Mob = AsHuntable(Entity, IsWet))
		{
			Hunted.Add(Mob);
		}
	}
	if (Hunted.IsEmpty())
	{
		return 0;
	}
	FMcMob* Mob = Hunted[Manager.GetRandom().RandRange(0, Hunted.Num() - 1)];
	Mob->Damage(Damage, nullptr);
	return Mob->Id;
}
