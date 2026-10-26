// The random tick rules, and the checks that follow a block changing beside a plant or a leaf. What each block does
// is from minecraft.wiki (the pages of the blocks themselves).

#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McSimulation.h"

namespace
{
	const FIntVector Up(0, 1, 0);
	const FIntVector Down(0, -1, 0);
	const FIntVector Around[6] = { FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0) };

	FMcStateId StateOf(const TCHAR* Block)
	{
		return FMcBlockRegistry::Get().FindDefaultState(Block).Get(0);
	}

	FString NameOf(FMcStateId State)
	{
		return FMcBlockRegistry::Get().GetBlockOf(State).Name.ToString();
	}

	int32 NumberProperty(FMcStateId State, const TCHAR* Property)
	{
		const FName Value = FMcBlockRegistry::Get().GetPropertyValue(State, Property);
		return Value.IsNone() ? -1 : FCString::Atoi(*Value.ToString());
	}

	// The state with one property changed; State itself if the block has no such property.
	FMcStateId SetProp(FMcStateId State, const TCHAR* Property, const FString& Value)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcBlock& Block = Registry.GetBlockOf(State);
		const FMcBlockState& Info = Registry.GetState(State);
		TMap<FName, FName> Properties;
		for (int32 Index = 0; Index < Block.Properties.Num(); ++Index)
		{
			Properties.Add(Block.Properties[Index].Name, Block.Properties[Index].Values[Info.Values[Index]]);
		}
		if (!Properties.Contains(Property))
		{
			return State;
		}
		Properties.Add(Property, FName(*Value));
		return Registry.FindState(Block.Name, Properties).Get(State);
	}

	int32 MaxValue(FMcStateId State, const TCHAR* Property)
	{
		const FMcBlock& Block = FMcBlockRegistry::Get().GetBlockOf(State);
		const int32 Index = Block.FindProperty(Property);
		return Index == INDEX_NONE ? -1 : FCString::Atoi(*Block.Properties[Index].Values.Last().ToString());
	}

	bool IsWaterAt(const FMcBlockWorld& World, const FIntVector& Pos)
	{
		return FMcBlockShapes::Get().GetFluid(World.GetBlock(Pos)) == EMcFluid::Water;
	}

	bool IsSmallPlant(const FString& Name, FMcStateId State)
	{
		const FMcBlockTags& Tags = FMcBlockTags::Get();
		return Tags.Has(TEXT("minecraft:flowers"), State) || Tags.Has(TEXT("minecraft:saplings"), State) || Name == TEXT("minecraft:short_grass") || Name == TEXT("minecraft:fern")
			|| Name == TEXT("minecraft:tall_grass") || Name == TEXT("minecraft:large_fern");
	}
}

void FMcSimulation::RandomTicks()
{
	if (RandomTickSpeed <= 0)
	{
		return;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	for (const FIntPoint& Chunk : TickingChunks)
	{
		const FMcChunk* Loaded = World.FindChunk(Chunk);
		if (!Loaded)
		{
			continue;
		}
		for (int32 Section = 0; Section < Loaded->NumSections(); ++Section)
		{
			for (int32 Pick = 0; Pick < RandomTickSpeed; ++Pick)
			{
				const int32 Packed = Random.RandHelper(4096);
				const FIntVector Pos(Chunk.X * 16 + (Packed & 15), Loaded->SectionMinY(Section) + ((Packed >> 4) & 15), Chunk.Y * 16 + ((Packed >> 8) & 15));
				if (!Registry.IsAir(World.GetBlock(Pos)))
				{
					RandomTickBlock(Pos);
				}
			}
		}
	}
}

void FMcSimulation::RandomTickBlock(const FIntVector& Pos)
{
	const FMcBlockLight& Light = FMcBlockLight::Get();
	const FMcStateId State = World.GetBlock(Pos);
	const FString Name = NameOf(State);
	const FMcBlockTags& Tags = FMcBlockTags::Get();

	if (Name == TEXT("minecraft:grass_block") || Name == TEXT("minecraft:mycelium"))
	{
		// Covered grass dies; grass in the open spreads to dirt near it that has room above.
		if (Light.GetDampening(World.GetBlock(Pos + Up)) >= 15)
		{
			World.SetBlock(Pos, StateOf(TEXT("minecraft:dirt")));
			return;
		}
		const FIntVector Target = Pos + FIntVector(Random.RandRange(-1, 1), Random.RandRange(-3, 1), Random.RandRange(-1, 1));
		const FMcStateId Above = World.GetBlock(Target + Up);
		if (NameOf(World.GetBlock(Target)) == TEXT("minecraft:dirt") && Light.GetDampening(Above) < 2 && FMcBlockShapes::Get().GetFluid(Above) == EMcFluid::None)
		{
			World.SetBlock(Target, SetProp(State, TEXT("snowy"), TEXT("false")));
		}
		return;
	}
	if (Tags.Has(TEXT("minecraft:leaves"), State))
	{
		if (NumberProperty(State, TEXT("distance")) == 7 && FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("persistent")) != TEXT("true"))
		{
			World.SetBlock(Pos, StateOf(TEXT("minecraft:air")));
		}
		return;
	}
	if (Name == TEXT("minecraft:farmland"))
	{
		bool bWet = false;
		for (int32 DX = -4; DX <= 4 && !bWet; ++DX)
		{
			for (int32 DZ = -4; DZ <= 4 && !bWet; ++DZ)
			{
				bWet = IsWaterAt(World, Pos + FIntVector(DX, 0, DZ)) || IsWaterAt(World, Pos + FIntVector(DX, 1, DZ));
			}
		}
		const int32 Moisture = NumberProperty(State, TEXT("moisture"));
		if (bWet)
		{
			if (Moisture < 7)
			{
				World.SetBlock(Pos, SetProp(State, TEXT("moisture"), TEXT("7")));
			}
		}
		else if (Moisture > 0)
		{
			World.SetBlock(Pos, SetProp(State, TEXT("moisture"), FString::FromInt(Moisture - 1)));
		}
		else if (!Tags.Has(TEXT("minecraft:crops"), World.GetBlock(Pos + Up)))
		{
			World.SetBlock(Pos, StateOf(TEXT("minecraft:dirt")));
		}
		return;
	}
	if (Tags.Has(TEXT("minecraft:crops"), State))
	{
		const int32 Age = NumberProperty(State, TEXT("age"));
		if (Age >= 0 && Age < MaxValue(State, TEXT("age")))
		{
			// One growth in about (25 / points) + 1 random ticks; wet farmland is worth twice dry.
			const FMcStateId Below = World.GetBlock(Pos + Down);
			const int32 Points = NameOf(Below) == TEXT("minecraft:farmland") && NumberProperty(Below, TEXT("moisture")) > 0 ? 2 : 1;
			if (Random.RandHelper(25 / Points + 1) == 0)
			{
				World.SetBlock(Pos, SetProp(State, TEXT("age"), FString::FromInt(Age + 1)));
			}
		}
		return;
	}
	if (Name == TEXT("minecraft:sugar_cane") || Name == TEXT("minecraft:cactus"))
	{
		if (!FMcBlockRegistry::Get().IsAir(World.GetBlock(Pos + Up)))
		{
			return;
		}
		int32 Height = 1;
		while (NameOf(World.GetBlock(Pos - FIntVector(0, Height, 0))) == Name)
		{
			++Height;
		}
		if (Height >= 3)
		{
			return;
		}
		const int32 Age = NumberProperty(State, TEXT("age"));
		if (Age == 15)
		{
			World.SetBlock(Pos + Up, StateOf(*Name));
			World.SetBlock(Pos, SetProp(State, TEXT("age"), TEXT("0")));
		}
		else if (Age >= 0)
		{
			World.SetBlock(Pos, SetProp(State, TEXT("age"), FString::FromInt(Age + 1)));
		}
	}
}

void FMcSimulation::TickLeaves(const FIntVector& Pos, FMcStateId State)
{
	// Distance to the nearest log: 1 beside a log, one more than the nearest leaf beside it; 7 is as far as it goes.
	const FMcBlockTags& Tags = FMcBlockTags::Get();
	int32 Distance = 7;
	for (const FIntVector& Offset : Around)
	{
		const FMcStateId Neighbour = World.GetBlock(Pos + Offset);
		if (Tags.Has(TEXT("minecraft:logs"), Neighbour))
		{
			Distance = 1;
			break;
		}
		if (Tags.Has(TEXT("minecraft:leaves"), Neighbour))
		{
			const int32 Other = NumberProperty(Neighbour, TEXT("distance"));
			if (Other > 0)
			{
				Distance = FMath::Min(Distance, Other + 1);
			}
		}
	}
	const FMcStateId Updated = SetProp(State, TEXT("distance"), FString::FromInt(Distance));
	if (Updated != State)
	{
		World.SetBlock(Pos, Updated);
	}
}

void FMcSimulation::TickSupport(const FIntVector& Pos, FMcStateId State)
{
	const FString Name = NameOf(State);
	const FMcBlockTags& Tags = FMcBlockTags::Get();
	const FMcStateId Below = World.GetBlock(Pos + Down);
	const FString BelowName = NameOf(Below);
	bool bOk = true;
	if (Tags.Has(TEXT("minecraft:crops"), State))
	{
		bOk = BelowName == TEXT("minecraft:farmland");
	}
	else if (Name == TEXT("minecraft:sugar_cane"))
	{
		bOk = BelowName == Name || Tags.Has(TEXT("minecraft:supports_sugar_cane"), Below);
	}
	else if (Name == TEXT("minecraft:cactus"))
	{
		bOk = BelowName == Name || Tags.Has(TEXT("minecraft:supports_cactus"), Below);
	}
	else if (Name == TEXT("minecraft:dead_bush"))
	{
		bOk = Tags.Has(TEXT("minecraft:supports_dry_vegetation"), Below);
	}
	else if (IsSmallPlant(Name, State))
	{
		// A top half stays while the bottom half is the same plant.
		bOk = FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("half")) == TEXT("upper") ? BelowName == Name : Tags.Has(TEXT("minecraft:supports_vegetation"), Below);
	}
	else
	{
		return;
	}
	if (!bOk)
	{
		World.SetBlock(Pos, StateOf(TEXT("minecraft:air")));
	}
}
