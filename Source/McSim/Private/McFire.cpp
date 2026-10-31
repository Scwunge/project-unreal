// Fire (minecraft.wiki, "Fire"): it ages, burns the flammable blocks beside it and goes out when it has nothing to
// feed on. Odds are the wiki's encouragement and flammability values, grouped by kind of block.

#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McNature.h"
#include "McSimulation.h"

namespace
{
	const FIntVector Around[6] = { FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0) };

	// How a block takes fire and how readily it burns away: the wiki's ignite odds and burn odds ("Fire", flammability
	// table), 0 / 0 if it doesn't burn.
	struct FFlammability { int32 Ignite = 0; int32 Burn = 0; };

	FFlammability Flammability(FMcStateId State)
	{
		const FMcBlockTags& Tags = FMcBlockTags::Get();
		const FString Name = FMcBlockRegistry::Get().GetBlockOf(State).Name.ToString();
		if (Tags.Has(TEXT("minecraft:wool_carpets"), State) || Name == TEXT("minecraft:hay_block"))
		{
			return { 60, 20 };
		}
		if (Tags.Has(TEXT("minecraft:leaves"), State) || Tags.Has(TEXT("minecraft:wool"), State))
		{
			return { 30, 60 };
		}
		if (Tags.Has(TEXT("minecraft:logs_that_burn"), State))
		{
			return { 5, 5 };
		}
		if (Tags.Has(TEXT("minecraft:planks"), State) || Tags.Has(TEXT("minecraft:wooden_slabs"), State) || Tags.Has(TEXT("minecraft:wooden_stairs"), State)
			|| Tags.Has(TEXT("minecraft:wooden_fences"), State) || Tags.Has(TEXT("minecraft:fence_gates"), State))
		{
			return { 5, 20 };
		}
		if (Name == TEXT("minecraft:bookshelf"))
		{
			return { 30, 20 };
		}
		if (Name == TEXT("minecraft:vine") || Name == TEXT("minecraft:tnt"))
		{
			return { 15, 100 };
		}
		if (Tags.Has(TEXT("minecraft:flowers"), State) || Tags.Has(TEXT("minecraft:saplings"), State) || Name == TEXT("minecraft:short_grass") || Name == TEXT("minecraft:fern")
			|| Name == TEXT("minecraft:tall_grass") || Name == TEXT("minecraft:large_fern") || Name == TEXT("minecraft:dead_bush"))
		{
			return { 60, 100 };
		}
		return {};
	}

	int32 BurnOdds(FMcStateId State)
	{
		return Flammability(State).Burn;
	}

	bool IsFire(FMcStateId State)
	{
		return FMcBlockRegistry::Get().GetBlockOf(State).Name == TEXT("minecraft:fire");
	}
}

bool FMcSimulation::IsFireBlock(FMcStateId State)
{
	return IsFire(State);
}

void FMcSimulation::TickFire(const FIntVector& Pos, FMcStateId State)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId Air = Registry.FindDefaultState(TEXT("minecraft:air")).Get(0);
	const FMcStateId Fire = Registry.FindDefaultState(TEXT("minecraft:fire")).Get(0);
	const FMcStateId Below = World.GetBlock(Pos + FIntVector(0, -1, 0));
	const bool bEternal = FMcBlockTags::Get().Has(TEXT("minecraft:infiniburn_overworld"), Below);

	bool bFuel = false;
	for (const FIntVector& Offset : Around)
	{
		bFuel |= BurnOdds(World.GetBlock(Pos + Offset)) > 0;
	}
	const bool bSolidBelow = FMcBlockShapes::Get().HasCollision(Below);
	if (!bFuel && !bSolidBelow)
	{
		World.SetBlock(Pos, Air);
		return;
	}
	const FName AgeValue = Registry.GetPropertyValue(State, TEXT("age"));
	int32 Age = AgeValue.IsNone() ? 0 : FCString::Atoi(*AgeValue.ToString());
	// Rain on it or beside it puts it out, netherrack or not (the wiki's 20% plus 3% per age).
	{
		bool bRain = Nature->IsRainingAt(Pos);
		for (int32 Side = 0; Side < 4 && !bRain; ++Side)
		{
			bRain = Nature->IsRainingAt(Pos + Around[Side]);
		}
		if (bRain && Random.FRand() < 0.2f + Age * 0.03f)
		{
			World.SetBlock(Pos, Air);
			return;
		}
	}
	// It ages by one a third of the time (the wiki); with nothing to feed on it goes out past age 3.
	if (Age < 15 && Random.RandHelper(3) == 0)
	{
		++Age;
	}
	if (!bEternal)
	{
		if (!bFuel && (!bSolidBelow || Age > 3))
		{
			World.SetBlock(Pos, Air);
			return;
		}
		if (Age == 15 && BurnOdds(Below) == 0 && Random.RandHelper(4) == 0)
		{
			World.SetBlock(Pos, Air);
			return;
		}
	}
	// Each block beside it burns away with chance odds in 300 (the odds fitted to the reference client's field:
	// planks, wool and leaves burning down over 600 ticks). Fire takes its place half-ways to likely, else it is gone.
	for (const FIntVector& Offset : Around)
	{
		const FIntVector At = Pos + Offset;
		const int32 Odds = BurnOdds(World.GetBlock(At));
		if (Odds > 0 && Random.RandHelper(300) < Odds)
		{
			const bool bFireInstead = Random.RandHelper(Age + 10) < 5 && !Nature->IsRainingAt(At);
			if (bFireInstead)
			{
				TMap<FName, FName> Properties;
				Properties.Add(TEXT("age"), FName(*FString::FromInt(FMath::Min(15, Age + Random.RandHelper(5) / 4))));
				World.SetBlock(At, Registry.FindState(TEXT("minecraft:fire"), Properties).Get(Fire));
			}
			else
			{
				World.SetBlock(At, Air);
			}
		}
	}
	// It spreads to air beside, below and up to four above that has fuel next to it (the wiki's ignition degree:
	// (highest ignite odds + 7 per difficulty + 40) / (age + 30), out of 100 within one above, plus 100 per block higher).
	constexpr int32 Difficulty = 2;
	for (int32 Dx = -1; Dx <= 1; ++Dx) for (int32 Dz = -1; Dz <= 1; ++Dz) for (int32 Dy = -1; Dy <= 4; ++Dy)
	{
		if (Dx == 0 && Dy == 0 && Dz == 0)
		{
			continue;
		}
		const FIntVector At = Pos + FIntVector(Dx, Dy, Dz);
		if (!Registry.IsAir(World.GetBlock(At)))
		{
			continue;
		}
		int32 Ignite = 0;
		for (const FIntVector& Next : Around)
		{
			Ignite = FMath::Max(Ignite, Flammability(World.GetBlock(At + Next)).Ignite);
		}
		if (Ignite <= 0)
		{
			continue;
		}
		bool bRain = Nature->IsRainingAt(At);
		for (int32 Side = 0; Side < 4 && !bRain; ++Side)
		{
			bRain = Nature->IsRainingAt(At + Around[Side]);
		}
		if (bRain)
		{
			continue;
		}
		const int32 Degree = (Ignite + 7 * Difficulty + 40) / (Age + 30);
		const int32 Base = Dy > 1 ? 100 * (Dy - 1) + 100 : 100;
		if (Degree > 0 && Random.RandHelper(Base) <= Degree)
		{
			TMap<FName, FName> Properties;
			Properties.Add(TEXT("age"), FName(*FString::FromInt(FMath::Min(15, Age + Random.RandHelper(5) / 4))));
			World.SetBlock(At, Registry.FindState(TEXT("minecraft:fire"), Properties).Get(Fire));
		}
	}
	if (Age != (AgeValue.IsNone() ? 0 : FCString::Atoi(*AgeValue.ToString())))
	{
		TMap<FName, FName> Properties;
		Properties.Add(TEXT("age"), FName(*FString::FromInt(Age)));
		World.SetBlock(Pos, Registry.FindState(TEXT("minecraft:fire"), Properties).Get(State));
	}
	ScheduleTick(Pos, 30 + Random.RandHelper(10));
}
