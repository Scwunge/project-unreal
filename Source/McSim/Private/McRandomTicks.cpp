// The random tick rules, and the checks that follow a block changing beside a plant or a leaf. What each block does
// is from minecraft.wiki (the pages of the blocks themselves).

#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McCopper.h"
#include "McNature.h"
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
		Nature->TickChunk(Chunk);
	}
}

void FMcSimulation::RandomTickBlock(const FIntVector& Pos)
{
	const FMcBlockLight& Light = FMcBlockLight::Get();
	const FMcStateId State = World.GetBlock(Pos);
	const FString Name = NameOf(State);
	const FMcBlockTags& Tags = FMcBlockTags::Get();

	if (Nature->RandomTick(Pos, State))
	{
		return;
	}
	int32 CopperAge = 0;
	if (McCopper::GetAge(FMcBlockRegistry::Get().GetBlockOf(State).Name, CopperAge))
	{
		WeatherCopper(Pos, State, CopperAge);
		return;
	}
	if (Name.EndsWith(TEXT("redstone_ore")))
	{
		// A lit ore goes out on a random tick (reference run redore: they went out one by one over about 600 ticks at random tick speed 3).
		if (FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("lit")) == TEXT("true"))
		{
			World.SetBlock(Pos, SetProp(State, TEXT("lit"), TEXT("false")));
		}
		return;
	}
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
		bWet |= Nature->IsRainingAt(Pos + Up);
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
	const bool bStem = Name == TEXT("minecraft:melon_stem") || Name == TEXT("minecraft:pumpkin_stem");
	if (Name == TEXT("minecraft:nether_wart") || Name == TEXT("minecraft:cocoa"))
	{
		// Light and the ground don't matter to either. Per random tick: wart 1 in 10, a cocoa pod 1 in 5 (wiki: "Nether Wart", "Cocoa Beans").
		const int32 Age = NumberProperty(State, TEXT("age"));
		if (Age < MaxValue(State, TEXT("age")) && Random.RandHelper(Name == TEXT("minecraft:cocoa") ? 5 : 10) == 0)
		{
			World.SetBlock(Pos, SetProp(State, TEXT("age"), FString::FromInt(Age + 1)));
		}
		return;
	}
	if (Tags.Has(TEXT("minecraft:crops"), State))
	{
		const int32 Age = NumberProperty(State, TEXT("age"));
		const bool bFruiting = bStem && Age == 7;
		if (Age < 0 || (Age >= MaxValue(State, TEXT("age")) && !bFruiting))
		{
			return;
		}
		// Needs light 9 or more where it stands (wiki: "Wheat Seeds", "Melon Seeds").
		if (FMath::Max(Nature->SkyLightAt(Pos), Nature->BlockLightAt(Pos)) < 9)
		{
			return;
		}
		// Growth speed (wiki, "Tutorial:Crop farming"): the farmland under it is 4 if wet and 2 if dry, each of the eight
		// beside that adds 0.75 wet or 0.25 dry; halved if the same plant stands diagonally, or in line on both axes.
		float Speed = 0.0f;
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			for (int32 DZ = -1; DZ <= 1; ++DZ)
			{
				const FMcStateId Soil = World.GetBlock(Pos + FIntVector(DX, -1, DZ));
				if (NameOf(Soil) == TEXT("minecraft:farmland"))
				{
					const bool bWetSoil = NumberProperty(Soil, TEXT("moisture")) > 0;
					Speed += DX == 0 && DZ == 0 ? (bWetSoil ? 4.0f : 2.0f) : (bWetSoil ? 0.75f : 0.25f);
				}
			}
		}
		const auto Same = [&](int32 DX, int32 DZ) { return NameOf(World.GetBlock(Pos + FIntVector(DX, 0, DZ))) == Name; };
		const bool bRowX = Same(-1, 0) || Same(1, 0);
		const bool bRowZ = Same(0, -1) || Same(0, 1);
		if ((bRowX && bRowZ) || Same(-1, -1) || Same(1, -1) || Same(-1, 1) || Same(1, 1))
		{
			Speed /= 2.0f;
		}
		if (Speed <= 0.0f || Random.RandHelper(FMath::FloorToInt(25.0f / Speed) + 1) != 0)
		{
			return;
		}
		if (bFruiting)
		{
			// A stem with its fruit beside it grows no more; otherwise one of the four sides is tried.
			const FString Fruit = Name == TEXT("minecraft:melon_stem") ? TEXT("minecraft:melon") : TEXT("minecraft:pumpkin");
			static const TCHAR* Sides[4] = { TEXT("north"), TEXT("east"), TEXT("south"), TEXT("west") };
			static const FIntVector SideStep[4] = { FIntVector(0, 0, -1), FIntVector(1, 0, 0), FIntVector(0, 0, 1), FIntVector(-1, 0, 0) };
			for (int32 Side = 0; Side < 4; ++Side)
			{
				if (NameOf(World.GetBlock(Pos + SideStep[Side])) == Fruit)
				{
					return;
				}
			}
			const int32 Side = Random.RandHelper(4);
			const FIntVector Target = Pos + SideStep[Side];
			const FMcStateId Ground = World.GetBlock(Target + Down);
			if (FMcBlockRegistry::Get().IsAir(World.GetBlock(Target)) && (NameOf(Ground) == TEXT("minecraft:farmland") || Tags.Has(TEXT("minecraft:dirt"), Ground)))
			{
				World.SetBlock(Target, StateOf(*Fruit));
				const FString Attached = Name == TEXT("minecraft:melon_stem") ? TEXT("minecraft:attached_melon_stem") : TEXT("minecraft:attached_pumpkin_stem");
				World.SetBlock(Pos, SetProp(StateOf(*Attached), TEXT("facing"), Sides[Side]));
			}
			return;
		}
		// Beetroot fails to advance a third of the time that growth comes up.
		if (Name == TEXT("minecraft:beetroots") && Random.RandHelper(3) == 0)
		{
			return;
		}
		World.SetBlock(Pos, SetProp(State, TEXT("age"), FString::FromInt(Age + 1)));
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
		// On cane, or on ground with water (or frosted ice) beside it.
		bOk = BelowName == Name;
		if (!bOk && Tags.Has(TEXT("minecraft:supports_sugar_cane"), Below))
		{
			for (int32 Side = 0; Side < 4 && !bOk; ++Side)
			{
				const FMcStateId Next = World.GetBlock(Pos + Down + Around[Side]);
				bOk = IsWaterAt(World, Pos + Down + Around[Side]) || NameOf(Next) == TEXT("minecraft:frosted_ice");
			}
		}
	}
	else if (Name == TEXT("minecraft:cactus"))
	{
		// Breaks if a solid block or lava is beside it, or water or lava above.
		bOk = BelowName == Name || Tags.Has(TEXT("minecraft:supports_cactus"), Below);
		const FMcBlockLight& Light = FMcBlockLight::Get();
		for (int32 Side = 0; Side < 4 && bOk; ++Side)
		{
			const FMcStateId Next = World.GetBlock(Pos + Around[Side]);
			bOk = !(Light.GetDampening(Next) >= 15 && FMcBlockShapes::Get().HasCollision(Next)) && FMcBlockShapes::Get().GetFluid(Next) != EMcFluid::Lava;
		}
		bOk = bOk && FMcBlockShapes::Get().GetFluid(World.GetBlock(Pos + Up)) == EMcFluid::None;
	}
	else if (Name == TEXT("minecraft:attached_melon_stem") || Name == TEXT("minecraft:attached_pumpkin_stem"))
	{
		// The fruit gone, the stem goes back to full grown and can bear another.
		const FString Facing = FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("facing")).ToString();
		const FIntVector Step = Facing == TEXT("north") ? Around[0] : Facing == TEXT("south") ? Around[1] : Facing == TEXT("west") ? Around[2] : Around[3];
		const FString Fruit = Name == TEXT("minecraft:attached_melon_stem") ? TEXT("minecraft:melon") : TEXT("minecraft:pumpkin");
		if (NameOf(World.GetBlock(Pos + Step)) != Fruit)
		{
			World.SetBlock(Pos, SetProp(StateOf(Name == TEXT("minecraft:attached_melon_stem") ? TEXT("minecraft:melon_stem") : TEXT("minecraft:pumpkin_stem")), TEXT("age"), TEXT("7")));
		}
		return;
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

void FMcSimulation::WeatherCopper(const FIntVector& Pos, FMcStateId State, int32 Age)
{
	if (Age >= 3 || Random.FRand() >= McCopper::WeatherChance)
	{
		return;
	}
	// A block with a less weathered one within 4 steps waits for it; the more of the same age, the slower.
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	int32 Higher = 0, Same = 0;
	for (int32 DX = -4; DX <= 4; ++DX)
	{
		for (int32 DY = -4 + FMath::Abs(DX); DY <= 4 - FMath::Abs(DX); ++DY)
		{
			const int32 Rest = 4 - FMath::Abs(DX) - FMath::Abs(DY);
			for (int32 DZ = -Rest; DZ <= Rest; ++DZ)
			{
				if (DX == 0 && DY == 0 && DZ == 0)
				{
					continue;
				}
				const FMcStateId Other = World.GetBlock(Pos + FIntVector(DX, DY, DZ));
				int32 OtherAge;
				if (Registry.IsValidState(Other) && McCopper::GetAge(Registry.GetBlockOf(Other).Name, OtherAge))
				{
					if (OtherAge < Age)
					{
						return;
					}
					(OtherAge > Age ? Higher : Same)++;
				}
			}
		}
	}
	if (Random.FRand() >= McCopper::GetWeatherFactor(Age, Higher, Same))
	{
		return;
	}
	const FName Old = Registry.GetBlockOf(State).Name;
	const FName Next = McCopper::GetNextAge(Old);
	const TOptional<FMcStateId> Changed = Registry.ParseState(*(Next.ToString() + Registry.Describe(State).RightChop(Old.ToString().Len())));
	if (!Next.IsNone() && Changed.IsSet())
	{
		World.SetBlock(Pos, Changed.GetValue());
	}
}
