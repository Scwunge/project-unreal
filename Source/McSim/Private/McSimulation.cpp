#include "McSimulation.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McConduit.h"
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

	// A bubble column can fill a water source or replace another column block.
	bool CanHoldColumn(FMcStateId State)
	{
		const FName Name = FMcBlockRegistry::Get().GetBlockOf(State).Name;
		return Name == TEXT("minecraft:bubble_column") || (Name == TEXT("minecraft:water") && FMcBlockShapes::Get().GetFluidLevel(State) == 0);
	}

	// What water above this block turns into: a column pushing up over soul sand, down over magma, the same
	// way over another column; plain water over anything else.
	FMcStateId ColumnStateAbove(const FMcBlockWorld& World, const FIntVector& Below)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcStateId State = World.GetBlock(Below);
		const FName Name = Registry.GetBlockOf(State).Name;
		FName Drag;
		if (Name == TEXT("minecraft:soul_sand"))
		{
			Drag = TEXT("false");
		}
		else if (Name == TEXT("minecraft:magma_block"))
		{
			Drag = TEXT("true");
		}
		else if (Name == TEXT("minecraft:bubble_column"))
		{
			Drag = Registry.GetPropertyValue(State, TEXT("drag"));
		}
		else
		{
			return StateOf(TEXT("minecraft:water"));
		}
		TMap<FName, FName> Properties;
		Properties.Add(TEXT("drag"), Drag);
		return Registry.FindState(TEXT("minecraft:bubble_column"), Properties).Get(0);
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

	// What flowing water and lava replace (reference run displace, 50 blocks beside a source): torches, redstone dust, levers and
	// buttons, carpets, snow layers, pots, plants, fire, cobweb, repeaters, comparators, tripwire, and the end rod. They leave
	// every block that can be waterlogged (signs, slabs, stairs, bars, panes, fences, trapdoors, chests, lanterns, candles,
	// scaffolding, lights, barriers), and also rails, pressure plates, doors, hoppers, cauldrons and pots (those have a shape).
	bool IsDisplaceable(FMcStateId State)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		if (Registry.IsAir(State))
		{
			return true;
		}
		if (FluidOf(State) != EMcFluid::None || Registry.GetPropertyValue(State, TEXT("waterlogged")) != NAME_None)
		{
			return false;
		}
		// These have a small shape but were still replaced (measured; not generalised to similar blocks).
		const FString Name = Registry.GetBlockOf(State).Name.ToString();
		if (Name == TEXT("minecraft:end_rod") || Name == TEXT("minecraft:repeater") || Name == TEXT("minecraft:comparator") || Name == TEXT("minecraft:flower_pot")
			|| Name.EndsWith(TEXT("_carpet")))
		{
			return true;
		}
		const FMcBlockTags& Tags = FMcBlockTags::Get();
		return !FMcBlockShapes::Get().HasCollision(State) && !Tags.Has(TEXT("minecraft:climbable"), State) && !Tags.Has(TEXT("minecraft:portals"), State)
			&& !Tags.Has(TEXT("minecraft:rails"), State) && !Tags.Has(TEXT("minecraft:pressure_plates"), State);
	}
}

FMcSimulation::FMcSimulation(FMcBlockWorld& InWorld)
	: World(InWorld)
{
	FMcBlockLight::Get().EnsureLoaded();
	Redstone = MakeUnique<FMcRedstone>(World, *this);
	Nature = MakeUnique<FMcNature>(World, *this);
	ChangedHandle = World.OnBlockChanged.AddRaw(this, &FMcSimulation::OnBlockChanged);
	AddedHandle = World.OnChunkAdded.AddRaw(this, &FMcSimulation::OnChunkAdded);
}

FMcSimulation::~FMcSimulation()
{
	World.OnBlockChanged.Remove(ChangedHandle);
	World.OnChunkAdded.Remove(AddedHandle);
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

void FMcSimulation::LightningStruck(const FIntVector& Pos)
{
	Redstone->LightningStruck(Pos);
}

void FMcSimulation::PlayerPlaced(const FIntVector& Pos)
{
	Redstone->PlayerPlaced(Pos);
}

void FMcSimulation::PlayerBroke(const FIntVector& Pos, FMcStateId State)
{
	Redstone->PlayerBroke(Pos, State);
}

void FMcSimulation::Vibrate(const FIntVector& Pos, int32 Frequency)
{
	FMcGameEvent Event;
	Event.Name = TEXT("vibration");
	Event.Frequency = Frequency;
	Event.Pos = Pos;
	Redstone->Vibrate(Event);
}

void FMcSimulation::EmitGameEvent(FName Name, const FIntVector& Pos, const FMcGameEventSource& Source)
{
	const int32 Frequency = McGameEvents::FrequencyOf(Name);
	if (Frequency <= 0)
	{
		return;
	}
	FMcGameEvent Event;
	Event.Name = Name;
	Event.Frequency = Frequency;
	Event.Pos = Pos;
	Event.Source = Source;
	Redstone->Vibrate(Event);
	for (const TPair<int32, TFunction<void(const FMcGameEvent&)>>& Listener : GameListeners)
	{
		Listener.Value(Event);
	}
}

int32 FMcSimulation::AddGameEventListener(TFunction<void(const FMcGameEvent&)> Listener)
{
	GameListeners.Add(NextListener, MoveTemp(Listener));
	return NextListener++;
}

void FMcSimulation::RemoveGameEventListener(int32 Handle)
{
	GameListeners.Remove(Handle);
}

bool FMcSimulation::IsVibrationOccluded(const FIntVector& From, const FIntVector& To) const
{
	return Redstone->IsOccluded(From, To);
}

void FMcSimulation::ShriekerTouched(const FIntVector& Pos, int32 PlayerId)
{
	Redstone->ShriekerTouched(Pos, PlayerId);
}

void FMcSimulation::BookshelfUsed(const FIntVector& Pos, int32 Slot)
{
	Redstone->BookshelfUsed(Pos, Slot);
}

void FMcSimulation::CreakingHit(const FIntVector& Heart)
{
	Redstone->CreakingHit(Heart);
}

int32 FMcSimulation::GetWarningLevel(int32 PlayerId) const
{
	const FWarning* Warning = Warnings.Find(PlayerId);
	if (!Warning)
	{
		return 0;
	}
	return FMath::Max(0, Warning->Level - static_cast<int32>((GameTime - Warning->Raised) / 12000));
}

bool FMcSimulation::CanShriekFor(int32 PlayerId, bool bCanSummon) const
{
	const FWarning* Warning = Warnings.Find(PlayerId);
	return !bCanSummon || !Warning || GameTime >= Warning->NextAllowed;
}

// A shrieker set off for a player: any shrieker shrieks; a naturally generated one also raises the player's warning level, and
// the player cannot set one off again for 10 seconds, the shriek included (minecraft.wiki, "Sculk Shrieker").
void FMcSimulation::ShriekerActivated(const FIntVector& Pos, int32 PlayerId, bool bCanSummon)
{
	if (!bCanSummon)
	{
		return;
	}
	FWarning& Warning = Warnings.FindOrAdd(PlayerId);
	Warning.Level = GetWarningLevel(PlayerId);
	Warning.Level = FMath::Min(4, Warning.Level + 1);
	Warning.Raised = GameTime;
	Warning.NextAllowed = GameTime + 200;
	ShriekedFor.Add(Pos, PlayerId);
	if (OnShriek)
	{
		OnShriek(Pos, PlayerId, Warning.Level);
	}
}

void FMcSimulation::ShriekerFinished(const FIntVector& Pos, bool bCanSummon)
{
	int32 PlayerId = 0;
	if (!ShriekedFor.RemoveAndCopyValue(Pos, PlayerId) || !bCanSummon)
	{
		return;
	}
	if (GetWarningLevel(PlayerId) >= 4 && OnWardenSummon)
	{
		OnWardenSummon(Pos, PlayerId);
	}
}

void FMcSimulation::NotifyContainerChanged(const FIntVector& Pos)
{
	Redstone->NotifyContainerChanged(Pos);
}

void FMcSimulation::SetChestViewers(const FIntVector& Pos, int32 NumViewers)
{
	Redstone->SetChestViewers(Pos, NumViewers);
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

void FMcSimulation::SetSeaLevel(int32 Level)
{
	Nature->SetSeaLevel(Level);
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
	// The ore lights up and the click goes on (a block in hand is placed against it).
	LightRedstoneOre(Pos);
	return Redstone->Use(Pos, Yaw);
}

void FMcSimulation::LightRedstoneOre(const FIntVector& Pos)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId State = World.GetBlock(Pos);
	if (!Registry.IsValidState(State))
	{
		return;
	}
	const FString Name = Registry.GetBlockOf(State).Name.ToString();
	if (Name.EndsWith(TEXT("redstone_ore")) && Registry.GetPropertyValue(State, TEXT("lit")) != TEXT("true"))
	{
		World.SetBlock(Pos, Registry.ParseState(*(Name + TEXT("[lit=true]"))).Get(State));
	}
}

int32 FMcSimulation::GetDustPower(const FIntVector& Pos) const
{
	return Redstone->GetDustPower(Pos);
}

void FMcSimulation::OnChunkAdded(FIntPoint ChunkPos)
{
	const FMcChunk* Chunk = World.FindChunk(ChunkPos);
	if (!Chunk)
	{
		return;
	}
	TArray<FMcStateId> States;
	for (int32 Section = 0; Section < Chunk->NumSections(); ++Section)
	{
		const FMcChunkSection& Blocks = Chunk->GetSection(Section);
		if (Blocks.IsEmpty() || !Blocks.Blocks.MayContain([](FMcStateId State) { return McConduit::IsConduit(State); }))
		{
			continue;
		}
		States.SetNumUninitialized(FMcPalettedContainer::NumEntries);
		Blocks.Blocks.CopyTo(States);
		for (int32 Index = 0; Index < States.Num(); ++Index)
		{
			if (McConduit::IsConduit(States[Index]))
			{
				ScheduleTick(FIntVector(ChunkPos.X * 16 + (Index & 15), Chunk->SectionMinY(Section) + (Index >> 8), ChunkPos.Y * 16 + ((Index >> 4) & 15)), 1);
			}
		}
	}
}

void FMcSimulation::OnBlockChanged(const FIntVector& Pos, FMcStateId State)
{
	Redstone->OnBlockChanged(Pos, State);
	RefreshNoteBlock(Pos);
	RefreshNoteBlock(Pos + FIntVector(0, 1, 0));
	RefreshNoteBlock(Pos - FIntVector(0, 1, 0));
	// A conduit placed in a ready frame wakes at once, its round of checks counted from then (vanilla's was awake 5
	// ticks after it was set, conduit_rain).
	if (McConduit::IsConduit(State))
	{
		ScheduleTick(Pos, 1);
	}
	Notify(Pos);
	for (const FIntVector& Offset : Around)
	{
		Notify(Pos + Offset);
	}
}

void FMcSimulation::AbsorbWater(const FIntVector& Sponge)
{
	// Reference run rs_sponge: a sponge that has water against it takes the water within 6 steps (through water only), at most 65 blocks,
	// at once, and becomes a wet sponge. Not fitted: which blocks go when more than 65 are in reach.
	static const FIntVector Order[6] = { FIntVector(0, -1, 0), FIntVector(0, 1, 0), FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0) };
	bool bTouching = false;
	for (const FIntVector& Offset : Order)
	{
		bTouching = bTouching || FluidOf(World.GetBlock(Sponge + Offset)) == EMcFluid::Water;
	}
	if (!bTouching)
	{
		return;
	}
	// Wet first, so the changes below do not set it off again.
	World.SetBlock(Sponge, StateOf(TEXT("minecraft:wet_sponge")));
	TSet<FIntVector> Seen;
	Seen.Add(Sponge);
	TArray<TPair<FIntVector, int32>> Queue;
	Queue.Add(TPair<FIntVector, int32>(Sponge, 0));
	int32 Removed = 0;
	for (int32 Head = 0; Head < Queue.Num() && Removed < 65; ++Head)
	{
		const TPair<FIntVector, int32> Here = Queue[Head];
		for (const FIntVector& Offset : Order)
		{
			const FIntVector Next = Here.Key + Offset;
			if (Seen.Contains(Next) || Removed >= 65)
			{
				continue;
			}
			Seen.Add(Next);
			const FMcStateId NextState = World.GetBlock(Next);
			if (FluidOf(NextState) != EMcFluid::Water)
			{
				continue;
			}
			if (IsFluidBlock(NextState))
			{
				World.SetBlock(Next, StateOf(TEXT("minecraft:air")));
			}
			else
			{
				// Water held by a block (waterlogged): the block stays and dries.
				const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
				const FMcBlock& Block = Registry.GetBlockOf(NextState);
				TMap<FName, FName> Props;
				const FMcBlockState& Info = Registry.GetState(NextState);
				for (int32 Index = 0; Index < Block.Properties.Num(); ++Index)
				{
					Props.Add(Block.Properties[Index].Name, Block.Properties[Index].Values[Info.Values[Index]]);
				}
				Props.Add(TEXT("waterlogged"), TEXT("false"));
				World.SetBlock(Next, Registry.FindState(Block.Name, Props).Get(NextState));
			}
			++Removed;
			if (Here.Value + 1 < 6)
			{
				Queue.Add(TPair<FIntVector, int32>(Next, Here.Value + 1));
			}
		}
	}
}

void FMcSimulation::Notify(const FIntVector& Pos)
{
	const FMcStateId State = World.GetBlock(Pos);
	if (FMcBlockRegistry::Get().GetBlockOf(State).Name == TEXT("minecraft:sponge"))
	{
		AbsorbWater(Pos);
		return;
	}
	// A conduit keeps its own 40 tick round of checks going from when it is placed (it holds water, so before fluids).
	if (McConduit::IsConduit(State))
	{
		ScheduleTick(Pos, ConduitPeriod);
		return;
	}
	{
		// Soul sand and magma check for a column 20 ticks after they or the water over them changed; a column
		// block rechecks what is under it after 5 (fitted to the bubble2 vanilla run).
		const FName ColumnName = FMcBlockRegistry::Get().GetBlockOf(State).Name;
		if (ColumnName == TEXT("minecraft:soul_sand") || ColumnName == TEXT("minecraft:magma_block"))
		{
			ScheduleTick(Pos, BubbleBaseDelay);
			return;
		}
		if (ColumnName == TEXT("minecraft:bubble_column"))
		{
			ScheduleTick(Pos, BubbleColumnDelay);
			return;
		}
	}
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
		Redstone->SetDetector(Nature->GetDetectorLevel(), Nature->GetSunLight(), Nature->GetDetectorScale());
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
	Redstone->BeginTick();
	for (const FIntVector& Pos : Now)
	{
		Pending.Remove(Pos);
		RunTick(Pos);
	}
	Redstone->EndTick();
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
	const FName Block = FMcBlockRegistry::Get().GetBlockOf(State).Name;
	if (Block == TEXT("minecraft:soul_sand") || Block == TEXT("minecraft:magma_block"))
	{
		UpdateBubbleColumn(Pos + Up);
	}
	else if (Block == TEXT("minecraft:bubble_column"))
	{
		UpdateBubbleColumn(Pos);
	}
	else if (McConduit::IsConduit(State))
	{
		TickConduit(Pos);
	}
	else if (FluidOf(State) != EMcFluid::None)
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

void FMcSimulation::TickConduit(const FIntVector& Pos)
{
	ScheduleTick(Pos, ConduitPeriod);
	if (OnConduitPulse && McConduit::IsActive(World, Pos))
	{
		const int32 Frame = McConduit::CountFrame(World, Pos);
		OnConduitPulse(Pos, McConduit::GetRange(Frame), Frame == McConduit::GetFramePlaces().Num());
	}
}

void FMcSimulation::UpdateBubbleColumn(const FIntVector& Pos)
{
	const FMcStateId State = World.GetBlock(Pos);
	if (!CanHoldColumn(State))
	{
		return;
	}
	const FMcStateId Column = ColumnStateAbove(World, Pos + Down);
	if (Column == State && FMcBlockRegistry::Get().GetBlockOf(State).Name == TEXT("minecraft:water"))
	{
		return;
	}
	// The whole stretch of water sources (or columns) above turns the same way.
	for (FIntVector At = Pos; CanHoldColumn(World.GetBlock(At)); At += Up)
	{
		World.SetBlock(At, Column);
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
		const double PreviousY = Block.Y;
		Block.Y += Block.Velocity;
		Block.Velocity *= 0.98;
		// What it moved counts, stopped at the cell it comes to rest in.
		Block.FallDistance += static_cast<float>(PreviousY - FMath::Max(Block.Y, static_cast<double>(Rest)));
		if (Block.Y > Rest)
		{
			++Index;
			continue;
		}
		const FIntVector At(Block.Column.X, Rest, Block.Column.Z);
		const FMcStateId Cell = World.GetBlock(At);
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcStateId State = Block.State;
		const double Fallen = Block.FallDistance;
		Falling.RemoveAt(Index);
		if (OnFallingBlockLanded)
		{
			OnFallingBlockLanded(At, State, Fallen);
		}
		if (Registry.IsAir(Cell) || FluidOf(Cell) != EMcFluid::None || FMcBlockTags::Get().Has(TEXT("minecraft:replaceable"), Cell))
		{
			// An anvil that falls more than a block may wear as it lands: chipped, then damaged, then gone, each with the
			// same chance 0.05 + 0.05 a block fallen. Fitted to the reference client (anvil_wear, anvil_wear2: 100 anvils
			// each, spaced apart): plain anvils chipped 0, 15, 15, 22, 36, 58, 74, 100, 100 of 100 after 1, 2, 3, 5, 6,
			// 10, 15, 20, 25 blocks (rule: 0, 15, 20, 30, 35, 55, 80, 100, 100); chipped became damaged 20, 50, 100 after
			// 3, 10, 20 (rule 20, 55, 100); damaged ones were gone 18, 51, 100 (same). No wear at a fall of 1.
			FMcStateId Landed = State;
			const FString Name = Registry.GetBlockOf(State).Name.ToString();
			if (Name.EndsWith(TEXT("anvil")) && Fallen > 1.0 && Random.FRand() < 0.05 + 0.05 * Fallen)
			{
				const FString Facing = Registry.GetPropertyValue(State, TEXT("facing")).ToString();
				const bool bPlain = Name == TEXT("minecraft:anvil");
				const bool bChipped = Name == TEXT("minecraft:chipped_anvil");
				Landed = bPlain || bChipped ? Registry.ParseState(FString::Printf(TEXT("minecraft:%s_anvil[facing=%s]"), bPlain ? TEXT("chipped") : TEXT("damaged"), *Facing)).Get(State) : StateOf(TEXT("minecraft:air"));
			}
			World.SetBlock(At, Landed);
			// Sounds measured in the reference client (log sounds, anvilsnd.txt): block.anvil.land at volume 0.15, pitch 0.91 to 0.97.
			if (Name.EndsWith(TEXT("anvil")) && !Registry.IsAir(Landed))
			{
				PlaySound({ FName("block.anvil.land"), FVector(At) + FVector(0.5), 0.15f, 0.91f + 0.06f * Random.FRand() });
			}
		}
		else
		{
			// An anvil that comes down on a slab or a torch breaks: block.anvil.destroy at volume 0.5, pitch 0.93 to 0.96 (same runs).
			if (Registry.GetBlockOf(State).Name.ToString().EndsWith(TEXT("anvil")))
			{
				PlaySound({ FName("block.anvil.destroy"), FVector(At) + FVector(0.5), 0.5f, 0.93f + 0.03f * Random.FRand() });
			}
			if (OnFallingBlockDropped)
			{
				OnFallingBlockDropped(At, State);
			}
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
