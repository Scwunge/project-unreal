#include "McSimulation.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McNature.h"
#include "McRedstone.h"
#include "McSim.h"

namespace
{
	const FIntVector Up(0, 1, 0);
	const FIntVector Down(0, -1, 0);
	const FIntVector Sides[4] = { FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0) };
	const FIntVector Around[6] = { FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0) };

	FMcStateId StateOf(const TCHAR* Block)
	{
		return FMcBlockRegistry::Get().FindDefaultState(Block).Get(0);
	}

	// Whether the state is a water or lava block itself (not one that only holds water, like a waterlogged slab).
	bool IsFluidBlock(FMcStateId State)
	{
		const FName Name = FMcBlockRegistry::Get().GetBlockOf(State).Name;
		return Name == TEXT("minecraft:water") || Name == TEXT("minecraft:lava");
	}

	EMcFluid FluidOf(FMcStateId State)
	{
		return FMcBlockShapes::Get().GetFluid(State);
	}

	int32 LevelOf(FMcStateId State)
	{
		return FMcBlockShapes::Get().GetFluidLevel(State);
	}

	bool IsGravityBlock(FMcStateId State)
	{
		const FString Name = FMcBlockRegistry::Get().GetBlockOf(State).Name.ToString();
		return FMcBlockTags::Get().Has(TEXT("minecraft:sand"), State) || Name == TEXT("minecraft:gravel") || Name == TEXT("minecraft:dragon_egg")
			|| Name.EndsWith(TEXT("concrete_powder")) || Name.EndsWith(TEXT("anvil"));
	}

	FMcStateId FluidState(FName Block, int32 Level)
	{
		TMap<FName, FName> Properties;
		Properties.Add(TEXT("level"), FName(*FString::FromInt(Level)));
		return FMcBlockRegistry::Get().FindState(Block, Properties).Get(0);
	}

	// A block a fluid pushes aside: nothing to collide with (air, plants, torches).
	bool IsDisplaceable(FMcStateId State)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		return Registry.IsAir(State) || (FluidOf(State) == EMcFluid::None && !FMcBlockShapes::Get().HasCollision(State) && !FMcBlockTags::Get().Has(TEXT("minecraft:climbable"), State)
			&& !FMcBlockTags::Get().Has(TEXT("minecraft:portals"), State));
	}
}

FMcSimulation::FMcSimulation(FMcBlockWorld& InWorld)
	: World(InWorld)
{
	FMcBlockLight::Get().EnsureLoaded();
	Redstone = MakeUnique<FMcRedstone>(World, *this);
	Nature = MakeUnique<FMcNature>(World, *this);
	ChangedHandle = World.OnBlockChanged.AddRaw(this, &FMcSimulation::OnBlockChanged);
}

FMcSimulation::~FMcSimulation()
{
	World.OnBlockChanged.Remove(ChangedHandle);
}

void FMcSimulation::ScheduleTick(const FIntVector& Pos, int32 Delay)
{
	if (Pending.Contains(Pos))
	{
		return;
	}
	Pending.Add(Pos);
	Due.FindOrAdd(GameTime + FMath::Max(Delay, 1)).Add(Pos);
}

void FMcSimulation::SetPressurePlate(const FIntVector& Pos, int32 NumEntities)
{
	Redstone->SetPlate(Pos, NumEntities);
}

void FMcSimulation::HitTarget(const FIntVector& Pos, int32 Power, int32 Duration)
{
	Redstone->HitTarget(Pos, Power, Duration);
}

void FMcSimulation::SetDetectorRail(const FIntVector& Pos, bool bOccupied)
{
	Redstone->SetDetector(Pos, bOccupied);
}

void FMcSimulation::SetSunLight(int32 Level)
{
	bSunFixed = true;
	Redstone->SetSunLight(Level);
}

void FMcSimulation::ClearSunLight()
{
	bSunFixed = false;
}

void FMcSimulation::SetDayTime(int64 DayTime)
{
	DayOffset = DayTime - GameTime;
}

int32 FMcSimulation::GetSunLevel() const
{
	return Nature->GetSunLight();
}

EMcWeather FMcSimulation::GetWeather() const
{
	return Nature->GetWeather();
}

void FMcSimulation::SetWeatherCycle(bool bCycle)
{
	Nature->SetWeatherCycle(bCycle);
}

void FMcSimulation::SetWeather(EMcWeather Weather, int32 Duration)
{
	Nature->SetWeather(Weather, Duration);
}

void FMcSimulation::SetSnowAccumulation(int32 Layers)
{
	Nature->SetSnowAccumulation(Layers);
}

bool FMcSimulation::IsRainingAt(const FIntVector& Pos) const
{
	return Nature->IsRainingAt(Pos);
}

bool FMcSimulation::Bonemeal(const FIntVector& Pos)
{
	return Nature->Bonemeal(Pos);
}

bool FMcSimulation::UseBlock(const FIntVector& Pos, float Yaw)
{
	return Redstone->Use(Pos, Yaw);
}

int32 FMcSimulation::GetDustPower(const FIntVector& Pos) const
{
	return Redstone->GetDustPower(Pos);
}

void FMcSimulation::OnBlockChanged(const FIntVector& Pos, FMcStateId State)
{
	Redstone->OnBlockChanged(Pos, State);
	Notify(Pos);
	for (const FIntVector& Offset : Around)
	{
		Notify(Pos + Offset);
	}
}

void FMcSimulation::Notify(const FIntVector& Pos)
{
	const FMcStateId State = World.GetBlock(Pos);
	switch (FluidOf(State))
	{
	case EMcFluid::Water:
		if (IsFluidBlock(State))
		{
			ScheduleTick(Pos, WaterDelay);
		}
		return;
	case EMcFluid::Lava:
		// Lava that water touches (beside or above) hardens at once when the water arrives, not on its own next tick:
		// a source into obsidian, flowing lava into cobblestone (rs_lava).
		if (IsFluidBlock(State))
		{
			for (const FIntVector& Offset : { Sides[0], Sides[1], Sides[2], Sides[3], Up })
			{
				if (FluidOf(World.GetBlock(Pos + Offset)) == EMcFluid::Water)
				{
					World.SetBlock(Pos, StateOf(LevelOf(State) == 0 ? TEXT("minecraft:obsidian") : TEXT("minecraft:cobblestone")));
					return;
				}
			}
		}
		ScheduleTick(Pos, LavaDelay);
		return;
	default:
		break;
	}
	if (IsGravityBlock(State))
	{
		ScheduleTick(Pos, GravityDelay);
		return;
	}
	const FString Name = FMcBlockRegistry::Get().GetBlockOf(State).Name.ToString();
	const FMcBlockTags& Tags = FMcBlockTags::Get();
	if (IsFireBlock(State))
	{
		ScheduleTick(Pos, 30);
	}
	else if (Tags.Has(TEXT("minecraft:leaves"), State))
	{
		ScheduleTick(Pos, LeafDelay);
	}
	else if (Tags.Has(TEXT("minecraft:flowers"), State) || Tags.Has(TEXT("minecraft:saplings"), State) || Tags.Has(TEXT("minecraft:crops"), State)
		|| Name == TEXT("minecraft:short_grass") || Name == TEXT("minecraft:fern") || Name == TEXT("minecraft:tall_grass") || Name == TEXT("minecraft:large_fern")
		|| Name == TEXT("minecraft:sugar_cane") || Name == TEXT("minecraft:cactus") || Name == TEXT("minecraft:attached_melon_stem") || Name == TEXT("minecraft:attached_pumpkin_stem") || Name == TEXT("minecraft:dead_bush"))
	{
		ScheduleTick(Pos, 1);
	}
}

void FMcSimulation::Tick()
{
	++GameTime;
	Nature->Tick();
	if (!bSunFixed)
	{
		Redstone->SetDetector(Nature->GetDetectorLevel(), Nature->GetSunLight());
	}
	// Ticks that run now may schedule more for later; those never come due in this same tick (delay 1 or more).
	// Earliest due first, and within one tick in the order they were asked for.
	TArray<FIntVector> Now;
	TArray<int64> DueTimes;
	for (const TPair<int64, TArray<FIntVector>>& Entry : Due)
	{
		if (Entry.Key <= GameTime)
		{
			DueTimes.Add(Entry.Key);
		}
	}
	DueTimes.Sort();
	for (const int64 Time : DueTimes)
	{
		Now.Append(Due[Time]);
		Due.Remove(Time);
	}
	// A tick stays pending until its own turn, so an update arriving earlier in this same game tick cannot queue it twice.
	for (const FIntVector& Pos : Now)
	{
		Pending.Remove(Pos);
		RunTick(Pos);
	}
	StepFalling();
	RandomTicks();
}

void FMcSimulation::RunTick(const FIntVector& Pos)
{
	if (!World.HasChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)))
	{
		return;
	}
	const FMcStateId State = World.GetBlock(Pos);
	if (Redstone->RunTick(Pos, State))
	{
		return;
	}
	if (FluidOf(State) != EMcFluid::None)
	{
		TickFluid(Pos, State);
	}
	else if (IsGravityBlock(State))
	{
		TickGravity(Pos, State);
	}
	else if (IsFireBlock(State))
	{
		TickFire(Pos, State);
	}
	else if (FMcBlockTags::Get().Has(TEXT("minecraft:leaves"), State))
	{
		TickLeaves(Pos, State);
	}
	else
	{
		TickSupport(Pos, State);
	}
}

// A block that can't stand on what is under it turns into a falling block (minecraft.wiki, "Falling Block": gravity 0.04 a
// tick, 2% drag). It takes its first step in the tick it turns, after the scheduled ticks; measured in the reference
// client (rs_falling): 4 blocks down lands on the 15th step, 8 on the 21st.
void FMcSimulation::TickGravity(const FIntVector& Pos, FMcStateId State)
{
	const int32 MinY = World.GetDimension().MinY;
	const FIntVector Below = Pos + FIntVector(0, -1, 0);
	const FMcStateId Under = World.GetBlock(Below);
	if (Below.Y < MinY || !(IsDisplaceable(Under) || FluidOf(Under) != EMcFluid::None))
	{
		return;
	}
	World.SetBlock(Pos, StateOf(TEXT("minecraft:air")));
	FFallingBlock Block;
	Block.Id = NextFallingId++;
	Block.Column = Pos;
	Block.Y = Pos.Y;
	Block.State = State;
	Falling.Add(Block);
}

void FMcSimulation::StepFalling()
{
	const int32 MinY = World.GetDimension().MinY;
	for (int32 Index = 0; Index < Falling.Num();)
	{
		FFallingBlock& Block = Falling[Index];
		const auto CanFallInto = [&](int32 Y)
		{
			const FMcStateId Cell = World.GetBlock(FIntVector(Block.Column.X, Y, Block.Column.Z));
			return Y >= MinY && (IsDisplaceable(Cell) || FluidOf(Cell) != EMcFluid::None);
		};
		// The lowest cell it can get to from the one it is in.
		int32 Rest = FMath::FloorToInt(Block.Y + 1e-6);
		while (CanFallInto(Rest - 1))
		{
			--Rest;
		}
		Block.Velocity -= 0.04;
		Block.Y += Block.Velocity;
		Block.Velocity *= 0.98;
		if (Block.Y > Rest)
		{
			++Index;
			continue;
		}
		const FIntVector At(Block.Column.X, Rest, Block.Column.Z);
		const FMcStateId Cell = World.GetBlock(At);
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcStateId State = Block.State;
		Falling.RemoveAt(Index);
		if (Registry.IsAir(Cell) || FluidOf(Cell) != EMcFluid::None || FMcBlockTags::Get().Has(TEXT("minecraft:replaceable"), Cell))
		{
			World.SetBlock(At, State);
		}
		else if (OnFallingBlockDropped)
		{
			OnFallingBlockDropped(At, State);
		}
	}
}

void FMcSimulation::TickFluid(const FIntVector& Pos, FMcStateId State)
{
	if (!IsFluidBlock(State))
	{
		return;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const EMcFluid Kind = FluidOf(State);
	const bool bWater = Kind == EMcFluid::Water;
	const FMcStateId Air = StateOf(TEXT("minecraft:air"));
	const int32 Drop = bWater ? 1 : 2;
	const int32 SlopeReach = bWater ? 4 : 2;
	int32 Level = LevelOf(State);
	const FName Block = Registry.GetBlockOf(State).Name;

	// Lava beside water hardens: a source into obsidian, flowing lava into cobblestone (water above or beside it).
	if (!bWater)
	{
		for (const FIntVector& Offset : { Sides[0], Sides[1], Sides[2], Sides[3], Up })
		{
			if (FluidOf(World.GetBlock(Pos + Offset)) == EMcFluid::Water)
			{
				World.SetBlock(Pos, StateOf(Level == 0 ? TEXT("minecraft:obsidian") : TEXT("minecraft:cobblestone")));
				return;
			}
		}
	}

	const auto SetFluid = [&](const FIntVector& At, int32 NewLevel)
	{
		World.SetBlock(At, FluidState(Block, NewLevel));
	};

	// A flowing block settles on what its neighbours give it, or dries up.
	if (Level != 0)
	{
		int32 Best = MAX_int32;
		int32 Sources = 0;
		for (const FIntVector& Offset : Sides)
		{
			const FMcStateId Neighbour = World.GetBlock(Pos + Offset);
			if (FluidOf(Neighbour) != Kind)
			{
				continue;
			}
			const int32 NeighbourLevel = LevelOf(Neighbour);
			Sources += NeighbourLevel == 0 ? 1 : 0;
			Best = FMath::Min(Best, (NeighbourLevel >= 8 ? 0 : NeighbourLevel) + Drop);
		}
		int32 NewLevel = Best;
		const bool bFalls = FluidOf(World.GetBlock(Pos + Up)) == Kind;
		if (bFalls)
		{
			NewLevel = 8;
		}
		else if (bWater && Sources >= 2)
		{
			const FMcStateId Below = World.GetBlock(Pos + Down);
			if (FluidOf(Below) == Kind && LevelOf(Below) == 0)
			{
				NewLevel = 0;
			}
			else if (!IsDisplaceable(Below) && FluidOf(Below) == EMcFluid::None)
			{
				NewLevel = 0;
			}
		}
		if (NewLevel > 7 && !bFalls)
		{
			World.SetBlock(Pos, Air);
			return;
		}
		if (NewLevel != Level)
		{
			// It changes and spreads on from its new level in the same tick (rs_water: water that a fall reaches sends its own
			// sideways water on that tick, not 5 later).
			SetFluid(Pos, NewLevel);
			Level = NewLevel;
		}
	}

	// Down first.
	const FIntVector BelowPos = Pos + Down;
	if (BelowPos.Y >= World.GetDimension().MinY)
	{
		const FMcStateId Below = World.GetBlock(BelowPos);
		const bool bOpen = IsDisplaceable(Below) || (FluidOf(Below) == Kind && IsFluidBlock(Below) && LevelOf(Below) != 8 && LevelOf(Below) != 0);
		if (FluidOf(Below) != EMcFluid::None && FluidOf(Below) != Kind && IsFluidBlock(Below))
		{
			// Water falling on lava, or lava falling on water.
			if (bWater)
			{
				World.SetBlock(BelowPos, StateOf(LevelOf(Below) == 0 ? TEXT("minecraft:obsidian") : TEXT("minecraft:cobblestone")));
			}
			else
			{
				World.SetBlock(BelowPos, StateOf(TEXT("minecraft:stone")));
			}
			return;
		}
		if (bOpen)
		{
			// A source over a drop falls first and spreads sideways on its next turn (reference run rs_water: the sideways
			// water comes 5 ticks after the fall starts, not with it).
			SetFluid(BelowPos, 8);
			return;
		}
	}

	// Then sideways, towards the nearest drop when there is one.
	const int32 Effective = Level >= 8 ? 0 : Level;
	const int32 NewLevel = Effective + Drop;
	if (NewLevel > 7)
	{
		return;
	}
	const auto CanEnter = [&](const FIntVector& At)
	{
		const FMcStateId Here = World.GetBlock(At);
		if (IsDisplaceable(Here))
		{
			return true;
		}
		if (FluidOf(Here) == Kind && IsFluidBlock(Here))
		{
			return LevelOf(Here) != 0 && LevelOf(Here) < 8 && LevelOf(Here) > NewLevel;
		}
		return FluidOf(Here) != EMcFluid::None && FluidOf(Here) != Kind && IsFluidBlock(Here);
	};
	const auto HasDropBelow = [&](const FIntVector& At)
	{
		const FMcStateId Under = World.GetBlock(At + Down);
		return IsDisplaceable(Under) || (FluidOf(Under) == Kind && IsFluidBlock(Under) && LevelOf(Under) != 0);
	};
	// Steps to the nearest drop going on from At, by flowing paths no longer than Reach blocks; MAX_int32 if none.
	TFunction<int32(const FIntVector&, int32, int32)> Slope = [&](const FIntVector& At, int32 Steps, int32 From) -> int32
	{
		int32 Best = MAX_int32;
		for (int32 Side = 0; Side < 4; ++Side)
		{
			if (From >= 0 && (Side ^ 1) == From)
			{
				continue;
			}
			const FIntVector Next = At + Sides[Side];
			if (!CanEnter(Next) && !(FluidOf(World.GetBlock(Next)) == Kind && IsFluidBlock(World.GetBlock(Next))))
			{
				continue;
			}
			if (HasDropBelow(Next))
			{
				return Steps;
			}
			if (Steps < SlopeReach)
			{
				Best = FMath::Min(Best, Slope(Next, Steps + 1, Side));
			}
		}
		return Best;
	};
	const bool bBelowOpen = BelowPos.Y >= World.GetDimension().MinY && HasDropBelow(Pos);
	if (Level != 0 && bBelowOpen)
	{
		return;
	}
	int32 Distances[4];
	int32 Shortest = MAX_int32;
	for (int32 Side = 0; Side < 4; ++Side)
	{
		const FIntVector Next = Pos + Sides[Side];
		if (!CanEnter(Next))
		{
			Distances[Side] = MAX_int32;
			continue;
		}
		Distances[Side] = HasDropBelow(Next) ? 0 : (SlopeReach > 1 ? Slope(Next, 1, Side) : MAX_int32);
		Shortest = FMath::Min(Shortest, Distances[Side]);
	}
	for (int32 Side = 0; Side < 4; ++Side)
	{
		const FIntVector Next = Pos + Sides[Side];
		if (!CanEnter(Next) || (Shortest != MAX_int32 && Distances[Side] != Shortest))
		{
			continue;
		}
		const FMcStateId Here = World.GetBlock(Next);
		if (FluidOf(Here) != EMcFluid::None && FluidOf(Here) != Kind && IsFluidBlock(Here))
		{
			// Water into lava (or lava into water) turns the block that was in the way to stone.
			if (bWater)
			{
				World.SetBlock(Next, StateOf(LevelOf(Here) == 0 ? TEXT("minecraft:obsidian") : TEXT("minecraft:cobblestone")));
			}
			continue;
		}
		SetFluid(Next, NewLevel);
	}
}
