#include "McNature.h"
#include "Dom/JsonValue.h"
#include "McBiomeRegistry.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McCache.h"
#include "McFeature.h"
#include "McGenRegion.h"
#include "McRandom.h"

namespace
{
	const FIntVector Up(0, 1, 0);
	const FIntVector Down(0, -1, 0);
	const FIntVector Sides[4] = { FIntVector(0, 0, -1), FIntVector(1, 0, 0), FIntVector(0, 0, 1), FIntVector(-1, 0, 0) };

	// Where the box a tree may change reaches from the sapling (mega jungle trees are the widest and tallest).
	constexpr int32 TreeRadius = 12;
	constexpr int32 TreeHeight = 45;

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

	// The state of Block with some properties set, its default state if they don't fit.
	FMcStateId StateWith(const TCHAR* Block, std::initializer_list<TPair<FName, FName>> Values)
	{
		TMap<FName, FName> Properties;
		for (const TPair<FName, FName>& Value : Values)
		{
			Properties.Add(Value.Key, Value.Value);
		}
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		return Registry.FindState(Block, Properties).Get(StateOf(Block));
	}

	FMcStateId WithNumber(FMcStateId State, const TCHAR* Property, int32 Value)
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
		Properties.Add(Property, FName(*FString::FromInt(Value)));
		return Registry.FindState(Block.Name, Properties).Get(State);
	}

	int32 MaxValue(FMcStateId State, const TCHAR* Property)
	{
		const FMcBlock& Block = FMcBlockRegistry::Get().GetBlockOf(State);
		const int32 Index = Block.FindProperty(Property);
		return Index == INDEX_NONE ? -1 : FCString::Atoi(*Block.Properties[Index].Values.Last().ToString());
	}

	bool IsWaterBlock(FMcStateId State)
	{
		return NameOf(State) == TEXT("minecraft:water");
	}

	// How far the sun is round its day, 0 at noon and 0.5 at midnight, bent the way the game bends it.
	double CelestialAngle(int64 DayTime)
	{
		const double Raw = FMath::Frac(static_cast<double>(DayTime % 24000) / 24000.0 - 0.25);
		const double Bent = 1.0 - (FMath::Cos(Raw * PI) + 1.0) / 2.0;
		return Raw + (Bent - Raw) / 3.0;
	}
}

FMcNature::FMcNature(FMcBlockWorld& InWorld, FMcSimulation& InSim)
	: World(InWorld)
	, Sim(InSim)
{
	NextRain();
	NextThunder();
}

FMcNature::~FMcNature() = default;

// ---- Weather and the sun ----

void FMcNature::NextRain()
{
	RainTime = bRaining ? Random.RandRange(12000, 24000) : Random.RandRange(12000, 180000);
}

void FMcNature::NextThunder()
{
	ThunderTime = bThundering ? Random.RandRange(3600, 15600) : Random.RandRange(12000, 180000);
}

void FMcNature::Tick()
{
	if (!bWeatherCycle)
	{
		return;
	}
	if (--RainTime <= 0)
	{
		bRaining = !bRaining;
		NextRain();
	}
	if (--ThunderTime <= 0)
	{
		bThundering = !bThundering;
		NextThunder();
	}
}

EMcWeather FMcNature::GetWeather() const
{
	// Thunder with no rain is a clear sky.
	return !bRaining ? EMcWeather::Clear : bThundering ? EMcWeather::Thunder : EMcWeather::Rain;
}

void FMcNature::SetWeather(EMcWeather Weather, int32 Duration)
{
	bRaining = Weather != EMcWeather::Clear;
	bThundering = Weather == EMcWeather::Thunder;
	if (Duration > 0)
	{
		RainTime = Duration;
		ThunderTime = Duration;
	}
	else
	{
		NextRain();
		NextThunder();
	}
}

int32 FMcNature::GetSunLight() const
{
	// 0.505 is a fit: it keeps the wiki's daylight detector table within a tick at its steps and the reference client's inverted detector right at 7 times of day (0.5 is 1 off at tick 13500).
	double Level = 1.0 - (FMath::Cos(CelestialAngle(Sim.GetDayTime()) * 2.0 * PI) * 2.0 + 0.505);
	Level = 1.0 - FMath::Clamp(Level, 0.0, 1.0);
	if (bRaining)
	{
		Level *= 1.0 - 5.0 / 16.0;
		if (bThundering)
		{
			Level *= 1.0 - 5.0 / 16.0;
		}
	}
	return 15 - static_cast<int32>((1.0 - Level) * 11.0);
}

int32 FMcNature::GetDetectorLevel() const
{
	// The sky's light, scaled by how high the sun is: noon reads full, dusk and night nothing.
	double Angle = CelestialAngle(Sim.GetDayTime()) * 2.0 * PI;
	const double Target = Angle < PI ? 0.0 : 2.0 * PI;
	Angle += (Target - Angle) * 0.2;
	return FMath::Clamp(FMath::RoundToInt(GetSunLight() * FMath::Cos(Angle)), 0, 15);
}

// ---- Biomes and light ----

const FMcNature::FBiomeInfo& FMcNature::InfoAt(const FIntVector& Pos) const
{
	static const FBiomeInfo Default;
	const FMcChunk* Chunk = World.FindChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
	if (!Chunk || Pos.Y < Chunk->GetMinY() || Pos.Y > Chunk->GetMaxY())
	{
		return Default;
	}
	const FMcBiomeId Id = Chunk->GetBiome(Pos.X & 15, Pos.Y, Pos.Z & 15);
	if (const FBiomeInfo* Found = Biomes.Find(Id))
	{
		return *Found;
	}
	// A biome's temperature and whether anything falls from its sky are in its data file.
	FBiomeInfo Info;
	const FString Name = FMcBiomeRegistry::Get().GetName(Id).ToString();
	static const TSet<FString> Humid = { TEXT("minecraft:jungle"), TEXT("minecraft:sparse_jungle"), TEXT("minecraft:bamboo_jungle"), TEXT("minecraft:swamp"),
		TEXT("minecraft:mangrove_swamp"), TEXT("minecraft:snowy_slopes"), TEXT("minecraft:frozen_peaks"), TEXT("minecraft:jagged_peaks"), TEXT("minecraft:mushroom_fields") };
	Info.bHumid = Humid.Contains(Name);
	if (const TSharedPtr<FJsonObject> Object = McCache::LoadJsonObject(McCache::Data(TEXT("worldgen/biome/") + McCache::PathOf(Name) + TEXT(".json"))))
	{
		double Temperature = 0.8;
		Object->TryGetNumberField(TEXT("temperature"), Temperature);
		Info.Temperature = static_cast<float>(Temperature);
		Object->TryGetBoolField(TEXT("has_precipitation"), Info.bPrecipitation);
	}
	return Biomes.Add(Id, Info);
}

float FMcNature::GetTemperature(const FIntVector& Pos) const
{
	// It falls by 0.00125 a block above y 80.
	return InfoAt(Pos).Temperature - (Pos.Y > 80 ? (Pos.Y - 80) * 0.00125f : 0.0f);
}

FMcNature::EPrecipitation FMcNature::GetPrecipitation(const FIntVector& Pos) const
{
	if (!InfoAt(Pos).bPrecipitation)
	{
		return EPrecipitation::None;
	}
	return GetTemperature(Pos) < 0.15f ? EPrecipitation::Snow : EPrecipitation::Rain;
}

bool FMcNature::IsExposedToSky(const FIntVector& Pos) const
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	for (int32 Y = Pos.Y + 1; Y <= World.GetDimension().MaxY(); ++Y)
	{
		if (!Registry.IsAir(World.GetBlock(FIntVector(Pos.X, Y, Pos.Z))))
		{
			return false;
		}
	}
	return true;
}

bool FMcNature::IsHumidAt(const FIntVector& Pos) const
{
	return InfoAt(Pos).bHumid;
}

bool FMcNature::IsRainingAt(const FIntVector& Pos) const
{
	return bRaining && GetPrecipitation(Pos) != EPrecipitation::None && IsExposedToSky(Pos);
}

int32 FMcNature::BlockLightAt(const FIntVector& Pos) const
{
	// Light of 15 falls by one a block, so nothing further than 15 blocks matters; the rules here only compare with
	// 9 to 12, which sources within 6 blocks decide.
	const FMcBlockLight& Light = FMcBlockLight::Get();
	int32 Best = 0;
	for (int32 DX = -6; DX <= 6; ++DX)
	{
		for (int32 DY = -6; DY <= 6; ++DY)
		{
			for (int32 DZ = -6; DZ <= 6; ++DZ)
			{
				const int32 Distance = FMath::Abs(DX) + FMath::Abs(DY) + FMath::Abs(DZ);
				if (Distance > 6)
				{
					continue;
				}
				const int32 Level = Light.GetEmission(World.GetBlock(Pos + FIntVector(DX, DY, DZ))) - Distance;
				Best = FMath::Max(Best, Level);
			}
		}
	}
	return Best;
}

int32 FMcNature::SkyLightAt(const FIntVector& Pos) const
{
	const FMcBlockLight& Light = FMcBlockLight::Get();
	int32 Level = World.GetDimension().bHasSkylight ? GetSunLight() : 0;
	for (int32 Y = Pos.Y + 1; Y <= World.GetDimension().MaxY() && Level > 0; ++Y)
	{
		Level -= Light.GetDampening(World.GetBlock(FIntVector(Pos.X, Y, Pos.Z)));
	}
	return FMath::Max(Level, 0);
}

// ---- Random ticks ----

bool FMcNature::RandomTick(const FIntVector& Pos, FMcStateId State)
{
	const FString Name = NameOf(State);
	if (Name == TEXT("minecraft:ice"))
	{
		// Light bright enough thaws it, into water that can flow again.
		if (BlockLightAt(Pos) > 11)
		{
			World.SetBlock(Pos, StateOf(TEXT("minecraft:water")));
		}
		return true;
	}
	if (Name == TEXT("minecraft:snow"))
	{
		if (BlockLightAt(Pos) > 11)
		{
			World.SetBlock(Pos, StateOf(TEXT("minecraft:air")));
		}
		return true;
	}
	if (Name == TEXT("minecraft:cauldron") || Name == TEXT("minecraft:water_cauldron") || Name == TEXT("minecraft:powder_snow_cauldron"))
	{
		if (!bRaining || !IsExposedToSky(Pos))
		{
			return true;
		}
		const EPrecipitation Falling = GetPrecipitation(Pos);
		if (Falling == EPrecipitation::Rain && Name != TEXT("minecraft:powder_snow_cauldron") && Random.FRand() < 0.05f)
		{
			const int32 Level = Name == TEXT("minecraft:cauldron") ? 0 : NumberProperty(State, TEXT("level"));
			if (Level < 3)
			{
				World.SetBlock(Pos, StateWith(TEXT("minecraft:water_cauldron"), { { TEXT("level"), *FString::FromInt(Level + 1) } }));
			}
		}
		else if (Falling == EPrecipitation::Snow && Name != TEXT("minecraft:water_cauldron") && Random.FRand() < 0.1f)
		{
			const int32 Level = Name == TEXT("minecraft:cauldron") ? 0 : NumberProperty(State, TEXT("level"));
			if (Level < 3)
			{
				World.SetBlock(Pos, StateWith(TEXT("minecraft:powder_snow_cauldron"), { { TEXT("level"), *FString::FromInt(Level + 1) } }));
			}
		}
		return true;
	}
	if (FMcBlockTags::Get().Has(TEXT("minecraft:saplings"), State))
	{
		const FIntVector Above = Pos + Up;
		if (FMath::Max(SkyLightAt(Above), BlockLightAt(Above)) >= 9 && Random.RandHelper(7) == 0)
		{
			GrowSapling(Pos, State);
		}
		return true;
	}
	return false;
}

void FMcNature::TickChunk(FIntPoint Chunk)
{
	if (!World.HasChunk(Chunk) || Random.RandHelper(16) != 0)
	{
		return;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	FIntVector Top(Chunk.X * 16 + Random.RandHelper(16), World.GetDimension().MaxY(), Chunk.Y * 16 + Random.RandHelper(16));
	while (Top.Y > World.GetDimension().MinY && Registry.IsAir(World.GetBlock(Top)))
	{
		--Top.Y;
	}
	FreezeColumn(Top);
}

// What cold does to the top of a column: freezes the water there, or lays snow on it while it snows.
void FMcNature::FreezeColumn(const FIntVector& Top)
{
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	const FMcStateId State = World.GetBlock(Top);
	const FString Name = NameOf(State);
	const FIntVector Above = Top + Up;
	if (GetTemperature(Top) >= 0.15f)
	{
		return;
	}
	if (Name == TEXT("minecraft:water") && NumberProperty(State, TEXT("level")) == 0)
	{
		// Only a source at the edge of a pool freezes, and not beside a light.
		bool bEdge = false;
		for (const FIntVector& Side : Sides)
		{
			bEdge |= !IsWaterBlock(World.GetBlock(Top + Side));
		}
		if (bEdge && BlockLightAt(Top) < 10)
		{
			World.SetBlock(Top, StateOf(TEXT("minecraft:ice")));
		}
		return;
	}
	// Snow piles up on the ground while it snows here.
	if (!bRaining || GetPrecipitation(Above) != EPrecipitation::Snow || BlockLightAt(Above) >= 10)
	{
		return;
	}
	if (Name == TEXT("minecraft:snow"))
	{
		const int32 Layers = NumberProperty(State, TEXT("layers"));
		if (Layers < FMath::Min(8, SnowAccumulation))
		{
			World.SetBlock(Top, WithNumber(State, TEXT("layers"), Layers + 1));
		}
		return;
	}
	const bool bBare = Name != TEXT("minecraft:ice") && Name != TEXT("minecraft:packed_ice") && Name != TEXT("minecraft:blue_ice") && Name != TEXT("minecraft:barrier");
	if (bBare && Shapes.HasCollision(State) && Shapes.GetFluid(State) == EMcFluid::None && SnowAccumulation > 0)
	{
		World.SetBlock(Above, WithNumber(StateOf(TEXT("minecraft:snow")), TEXT("layers"), 1));
	}
}

// ---- Saplings and bone meal ----

bool FMcNature::GrowSapling(const FIntVector& Pos, FMcStateId State)
{
	if (NumberProperty(State, TEXT("stage")) == 0)
	{
		World.SetBlock(Pos, WithNumber(State, TEXT("stage"), 1));
		return true;
	}
	return GrowTree(Pos, State);
}

bool FMcNature::GrowTree(const FIntVector& Pos, FMcStateId State)
{
	const FString Name = NameOf(State);
	// Four saplings of a kind in a square grow one big tree.
	FIntVector Origin = Pos;
	bool bBig = false;
	for (int32 DX = -1; DX <= 0 && !bBig; ++DX)
	{
		for (int32 DZ = -1; DZ <= 0 && !bBig; ++DZ)
		{
			bBig = NameOf(World.GetBlock(Pos + FIntVector(DX, 0, DZ))) == Name && NameOf(World.GetBlock(Pos + FIntVector(DX + 1, 0, DZ))) == Name
				&& NameOf(World.GetBlock(Pos + FIntVector(DX, 0, DZ + 1))) == Name && NameOf(World.GetBlock(Pos + FIntVector(DX + 1, 0, DZ + 1))) == Name;
			if (bBig)
			{
				Origin = Pos + FIntVector(DX, 0, DZ);
			}
		}
	}

	const TCHAR* Feature = nullptr;
	if (Name == TEXT("minecraft:oak_sapling"))
	{
		Feature = Random.RandHelper(10) == 0 ? TEXT("minecraft:fancy_oak") : TEXT("minecraft:oak");
	}
	else if (Name == TEXT("minecraft:birch_sapling"))
	{
		Feature = TEXT("minecraft:birch");
	}
	else if (Name == TEXT("minecraft:spruce_sapling"))
	{
		Feature = bBig ? (Random.RandHelper(2) == 0 ? TEXT("minecraft:mega_spruce") : TEXT("minecraft:mega_pine")) : TEXT("minecraft:spruce");
	}
	else if (Name == TEXT("minecraft:jungle_sapling"))
	{
		Feature = bBig ? TEXT("minecraft:mega_jungle_tree") : TEXT("minecraft:jungle_tree_no_vine");
	}
	else if (Name == TEXT("minecraft:acacia_sapling"))
	{
		Feature = TEXT("minecraft:acacia");
	}
	else if (Name == TEXT("minecraft:cherry_sapling"))
	{
		Feature = TEXT("minecraft:cherry");
	}
	else if (Name == TEXT("minecraft:dark_oak_sapling") && bBig)
	{
		Feature = TEXT("minecraft:dark_oak");
	}
	else if (Name == TEXT("minecraft:pale_oak_sapling") && bBig)
	{
		Feature = TEXT("minecraft:pale_oak");
	}
	if (!Feature)
	{
		return false;
	}

	if (!Features)
	{
		Features = MakeUnique<FMcFeatureLibrary>(63, World.GetDimension().MinY, World.GetDimension().Height);
	}
	const TSharedPtr<const FMcFeature> Tree = Features->ParseFeature(MakeShared<FJsonValueString>(Feature));
	if (!Tree.IsValid())
	{
		return false;
	}

	// The tree is placed on the chunks themselves, then its changes are told to the world as ordinary block changes.
	const FIntPoint Centre(Pos.X >> 4, Pos.Z >> 4);
	TArray<FMcChunk*, TInlineAllocator<9>> Chunks;
	for (int32 DZ = -1; DZ <= 1; ++DZ)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			Chunks.Add(World.FindChunk(Centre + FIntPoint(DX, DZ)));
		}
	}
	if (!Chunks[4])
	{
		return false;
	}
	FMcGenRegion Region(Centre, Chunks);

	const FIntVector Min(Pos.X - TreeRadius, FMath::Max(Pos.Y - 1, Region.GetMinY()), Pos.Z - TreeRadius);
	const FIntVector Max(Pos.X + TreeRadius, FMath::Min(Pos.Y + TreeHeight, Region.GetMaxY()), Pos.Z + TreeRadius);
	const int32 SizeX = Max.X - Min.X + 1;
	const int32 SizeY = Max.Y - Min.Y + 1;
	const int32 SizeZ = Max.Z - Min.Z + 1;
	TArray<FMcStateId> Before;
	Before.Reserve(SizeX * SizeY * SizeZ);
	for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
	{
		for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
		{
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				Before.Add(Region.GetBlock(FIntVector(X, Y, Z)));
			}
		}
	}

	const FMcStateId Air = StateOf(TEXT("minecraft:air"));
	if (bBig)
	{
		for (int32 DX = 0; DX < 2; ++DX)
		{
			for (int32 DZ = 0; DZ < 2; ++DZ)
			{
				Region.SetBlock(Origin + FIntVector(DX, 0, DZ), Air);
			}
		}
	}
	else
	{
		Region.SetBlock(Pos, Air);
	}
	FMcRandom Rng((static_cast<uint64>(Random.GetUnsignedInt()) << 32) | Random.GetUnsignedInt());
	FMcFeatureContext Context{ Region, Rng, *Features, 63, nullptr };
	const bool bPlaced = Tree->Place(Context, Origin);

	TArray<TPair<FIntVector, FMcStateId>> Changes;
	int32 Index = 0;
	for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
	{
		for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
		{
			for (int32 X = Min.X; X <= Max.X; ++X, ++Index)
			{
				const FIntVector At(X, Y, Z);
				const FMcStateId After = Region.GetBlock(At);
				if (After != Before[Index])
				{
					Changes.Add(TPair<FIntVector, FMcStateId>(At, After));
					Region.SetBlock(At, Before[Index]);
				}
			}
		}
	}
	if (!bPlaced)
	{
		return false;
	}
	for (const TPair<FIntVector, FMcStateId>& Change : Changes)
	{
		World.SetBlock(Change.Key, Change.Value);
	}
	return true;
}

void FMcNature::SpreadGrass(const FIntVector& Start)
{
	// As the game does it: 128 tries, each wandering further from Start than the last, and a plant lands where it
	// stops on grass with room above.
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	for (int32 Try = 0; Try < 128; ++Try)
	{
		FIntVector At = Start;
		bool bOk = true;
		for (int32 Step = 0; Step < Try / 16 && bOk; ++Step)
		{
			At += FIntVector(Random.RandRange(-1, 1), Random.RandRange(-1, 1) * Random.RandHelper(3) / 2, Random.RandRange(-1, 1));
			bOk = NameOf(World.GetBlock(At + Down)) == TEXT("minecraft:grass_block") && !FMcBlockShapes::Get().HasCollision(World.GetBlock(At));
		}
		if (bOk && Registry.IsAir(World.GetBlock(At)) && NameOf(World.GetBlock(At + Down)) == TEXT("minecraft:grass_block"))
		{
			const TCHAR* Plant = Random.RandHelper(8) == 0 ? (Random.RandHelper(2) == 0 ? TEXT("minecraft:dandelion") : TEXT("minecraft:poppy")) : TEXT("minecraft:short_grass");
			World.SetBlock(At, StateOf(Plant));
		}
	}
}

bool FMcNature::Bonemeal(const FIntVector& Pos)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcBlockTags& Tags = FMcBlockTags::Get();
	const FMcStateId State = World.GetBlock(Pos);
	const FString Name = NameOf(State);
	if (Tags.Has(TEXT("minecraft:saplings"), State))
	{
		if (Random.FRand() < 0.45f)
		{
			GrowSapling(Pos, State);
		}
		return true;
	}
	if (Tags.Has(TEXT("minecraft:crops"), State))
	{
		const int32 Age = NumberProperty(State, TEXT("age"));
		const int32 Max = MaxValue(State, TEXT("age"));
		if (Age < 0 || Age >= Max)
		{
			return false;
		}
		// Beetroot grows a stage at a time; the others two to five.
		const int32 Gain = Name == TEXT("minecraft:beetroots") ? 1 : Random.RandRange(2, 5);
		World.SetBlock(Pos, WithNumber(State, TEXT("age"), FMath::Min(Age + Gain, Max)));
		return true;
	}
	if (Name == TEXT("minecraft:grass_block"))
	{
		if (!Registry.IsAir(World.GetBlock(Pos + Up)))
		{
			return false;
		}
		SpreadGrass(Pos + Up);
		return true;
	}
	if ((Name == TEXT("minecraft:short_grass") || Name == TEXT("minecraft:fern")) && Registry.IsAir(World.GetBlock(Pos + Up)))
	{
		const TCHAR* Tall = Name == TEXT("minecraft:fern") ? TEXT("minecraft:large_fern") : TEXT("minecraft:tall_grass");
		World.SetBlock(Pos, StateWith(Tall, { { TEXT("half"), TEXT("lower") } }));
		World.SetBlock(Pos + Up, StateWith(Tall, { { TEXT("half"), TEXT("upper") } }));
		return true;
	}
	return false;
}
