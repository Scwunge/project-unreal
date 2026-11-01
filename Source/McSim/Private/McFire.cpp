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

	// How readily a block burns away (the wiki's flammability), 0 if it doesn't burn.
	int32 BurnOdds(FMcStateId State)
	{
		const FMcBlockTags& Tags = FMcBlockTags::Get();
		const FString Name = FMcBlockRegistry::Get().GetBlockOf(State).Name.ToString();
		if (Tags.Has(TEXT("minecraft:leaves"), State) || Tags.Has(TEXT("minecraft:wool"), State) || Tags.Has(TEXT("minecraft:wool_carpets"), State))
		{
			return 60;
		}
		if (Tags.Has(TEXT("minecraft:logs_that_burn"), State) || Tags.Has(TEXT("minecraft:planks"), State) || Tags.Has(TEXT("minecraft:wooden_slabs"), State)
			|| Tags.Has(TEXT("minecraft:wooden_stairs"), State) || Tags.Has(TEXT("minecraft:wooden_fences"), State) || Name == TEXT("minecraft:bookshelf"))
		{
			return 20;
		}
		if (Tags.Has(TEXT("minecraft:flowers"), State) || Tags.Has(TEXT("minecraft:saplings"), State) || Name == TEXT("minecraft:short_grass") || Name == TEXT("minecraft:fern")
			|| Name == TEXT("minecraft:tall_grass") || Name == TEXT("minecraft:large_fern") || Name == TEXT("minecraft:dead_bush") || Name == TEXT("minecraft:vine")
			|| Name == TEXT("minecraft:hay_block") || Name == TEXT("minecraft:tnt"))
		{
			return 100;
		}
		return 0;
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
	const int32 Age = AgeValue.IsNone() ? 0 : FCString::Atoi(*AgeValue.ToString());
	// Rain on it or beside it puts it out, unless it burns on something that never burns out.
	if (!bEternal)
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
	if (!bEternal && (!bFuel && Age > 3 || (Age == 15 && Random.RandHelper(4) == 0)))
	{
		World.SetBlock(Pos, Air);
		return;
	}
	for (const FIntVector& Offset : Around)
	{
		const FIntVector At = Pos + Offset;
		const int32 Odds = BurnOdds(World.GetBlock(At));
		if (Odds > 0 && Random.RandHelper(100) < Odds)
		{
			// The block burns up; fire takes its place if something can still hold it, otherwise it is just gone. TNT is lit first.
			if (Registry.GetBlockOf(World.GetBlock(At)).Name == TEXT("minecraft:tnt"))
			{
				LightTnt(At);
			}
			World.SetBlock(At, Registry.IsAir(World.GetBlock(At + FIntVector(0, 1, 0))) ? Fire : Air);
		}
	}
	// Flames lick across to open air beside fuel.
	for (const FIntVector& Offset : Around)
	{
		const FIntVector At = Pos + Offset;
		if (!Registry.IsAir(World.GetBlock(At)) || Random.RandHelper(100) >= 25)
		{
			continue;
		}
		for (const FIntVector& Next : Around)
		{
			if (BurnOdds(World.GetBlock(At + Next)) > 0)
			{
				World.SetBlock(At, Fire);
				break;
			}
		}
	}
	if (Age < 15)
	{
		TMap<FName, FName> Properties;
		Properties.Add(TEXT("age"), FName(*FString::FromInt(Age + 1)));
		World.SetBlock(Pos, Registry.FindState(TEXT("minecraft:fire"), Properties).Get(State));
	}
	ScheduleTick(Pos, 30 + Random.RandHelper(10));
}
