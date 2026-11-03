#include "McRedstone.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McChestLink.h"
#include "McBrewingStand.h"
#include "McChunk.h"
#include "McLightEngine.h"
#include "McFurnace.h"
#include "Serialization/MemoryReader.h"
#include "McRails.h"
#include "McSimulation.h"

namespace
{
	enum EKind : uint8
	{
		None,
		Dust,
		Repeater,
		Comparator,
		Torch,
		WallTorch,
		Lamp,
		Observer,
		Piston,
		StickyPiston,
		PistonHead,
		MovingPiston,
		Lever,
		Button,
		Plate,
		PlateLight,
		PlateHeavy,
		Tripwire,
		TripwireHook,
		Tnt,
		Daylight,
		RedstoneBlock,
		Door,
		Trapdoor,
		Gate,
		Dispenser,
		Hopper,
		Rail,
		PoweredRail,
		ActivatorRail,
		DetectorRail,
		Target,
	};

	enum EDir : int32
	{
		North,
		East,
		South,
		West,
		Up,
		Down,
	};

	// North, east, south, west (each the next clockwise), up, down.
	const FIntVector Vec[6] = { FIntVector(0, 0, -1), FIntVector(1, 0, 0), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0) };
	const TCHAR* const DirName[6] = { TEXT("north"), TEXT("east"), TEXT("south"), TEXT("west"), TEXT("up"), TEXT("down") };

	int32 Opposite(int32 Dir)
	{
		static const int32 Table[6] = { South, West, North, East, Down, Up };
		return Table[Dir];
	}

	const FMcBlockRegistry& Reg()
	{
		return FMcBlockRegistry::Get();
	}

	const TCHAR* Bool(bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	uint8 KindOf(FMcStateId State)
	{
		static TArray<uint8> Kinds;
		if (Kinds.IsEmpty())
		{
			const FMcBlockRegistry& Registry = Reg();
			Kinds.SetNumZeroed(Registry.NumStates());
			for (int32 Index = 0; Index < Registry.NumBlocks(); ++Index)
			{
				const FMcBlock& Block = Registry.GetBlock(Index);
				const FString Name = Block.Name.ToString();
				uint8 Kind = None;
				if (Name == TEXT("minecraft:redstone_wire")) Kind = Dust;
				else if (Name == TEXT("minecraft:repeater")) Kind = Repeater;
				else if (Name == TEXT("minecraft:comparator")) Kind = Comparator;
				else if (Name == TEXT("minecraft:redstone_torch")) Kind = Torch;
				else if (Name == TEXT("minecraft:redstone_wall_torch")) Kind = WallTorch;
				else if (Name == TEXT("minecraft:redstone_lamp")) Kind = Lamp;
				else if (Name == TEXT("minecraft:observer")) Kind = Observer;
				else if (Name == TEXT("minecraft:piston")) Kind = Piston;
				else if (Name == TEXT("minecraft:sticky_piston")) Kind = StickyPiston;
				else if (Name == TEXT("minecraft:piston_head")) Kind = PistonHead;
				else if (Name == TEXT("minecraft:moving_piston")) Kind = MovingPiston;
				else if (Name == TEXT("minecraft:lever")) Kind = Lever;
				else if (Name.EndsWith(TEXT("_button"))) Kind = Button;
				else if (Name == TEXT("minecraft:light_weighted_pressure_plate")) Kind = PlateLight;
				else if (Name == TEXT("minecraft:heavy_weighted_pressure_plate")) Kind = PlateHeavy;
				else if (Name.EndsWith(TEXT("_pressure_plate"))) Kind = Plate;
				else if (Name == TEXT("minecraft:tnt")) Kind = Tnt;
				else if (Name == TEXT("minecraft:tripwire")) Kind = Tripwire;
				else if (Name == TEXT("minecraft:tripwire_hook")) Kind = TripwireHook;
				else if (Name == TEXT("minecraft:daylight_detector")) Kind = Daylight;
				else if (Name == TEXT("minecraft:redstone_block")) Kind = RedstoneBlock;
				else if (Name.EndsWith(TEXT("_door"))) Kind = Door;
				else if (Name.EndsWith(TEXT("_trapdoor"))) Kind = Trapdoor;
				else if (Name.EndsWith(TEXT("_fence_gate"))) Kind = Gate;
				else if (Name == TEXT("minecraft:dispenser") || Name == TEXT("minecraft:dropper")) Kind = Dispenser;
				else if (Name == TEXT("minecraft:hopper")) Kind = Hopper;
				else if (Name == TEXT("minecraft:rail")) Kind = Rail;
				else if (Name == TEXT("minecraft:powered_rail")) Kind = PoweredRail;
				else if (Name == TEXT("minecraft:activator_rail")) Kind = ActivatorRail;
				else if (Name == TEXT("minecraft:detector_rail")) Kind = DetectorRail;
				else if (Name == TEXT("minecraft:target")) Kind = Target;
				for (const FMcStateId State : Block.States)
				{
					Kinds[State] = Kind;
				}
			}
		}
		return Kinds[State];
	}

	bool IsDiode(FMcStateId State)
	{
		const uint8 Kind = KindOf(State);
		return Kind == Repeater || Kind == Comparator;
	}

	// Blocks that never move: the wiki's list of immovable blocks, plus the ones that keep contents.
	bool IsImmovableName(FName Name)
	{
		static const TSet<FName> Names = []
		{
			TSet<FName> Set;
			for (const TCHAR* Block : { TEXT("obsidian"), TEXT("crying_obsidian"), TEXT("bedrock"), TEXT("respawn_anchor"), TEXT("reinforced_deepslate"),
				TEXT("command_block"), TEXT("chain_command_block"), TEXT("repeating_command_block"), TEXT("structure_block"), TEXT("jigsaw"),
				TEXT("barrier"), TEXT("end_portal_frame"), TEXT("end_portal"), TEXT("end_gateway"), TEXT("nether_portal"), TEXT("spawner"),
				TEXT("trial_spawner"), TEXT("vault"), TEXT("enchanting_table"), TEXT("ender_chest"), TEXT("beacon"), TEXT("jukebox"), TEXT("chest"),
				TEXT("trapped_chest"), TEXT("furnace"), TEXT("blast_furnace"), TEXT("smoker"), TEXT("hopper"), TEXT("dropper"), TEXT("dispenser"),
				TEXT("barrel"), TEXT("brewing_stand"), TEXT("lectern"), TEXT("decorated_pot"), TEXT("crafter") })
			{
				Set.Add(FName(*(FString(TEXT("minecraft:")) + Block)));
			}
			return Set;
		}();
		return Names.Contains(Name);
	}

	FMcStateId Air()
	{
		return Reg().FindDefaultState(TEXT("minecraft:air")).Get(0);
	}

	// Stone and polished blackstone buttons stay pressed 20 ticks; wooden ones 30.
	bool IsStoneButton(FName Block)
	{
		return Block == TEXT("minecraft:stone_button") || Block == TEXT("minecraft:polished_blackstone_button");
	}

	// The sound a lever, button, door, trapdoor or gate makes as it goes on or opens (bOn) or off or shut. Bamboo, cherry
	// and the Nether's woods have their own sounds; iron and copper doors theirs.
	FName GetUseSound(uint8 Kind, FName Block, bool bOn)
	{
		const FString Name = Block.ToString();
		FString Wood;
		if (Name.Contains(TEXT("bamboo"))) Wood = TEXT("bamboo_wood");
		else if (Name.Contains(TEXT("cherry"))) Wood = TEXT("cherry_wood");
		else if (Name.Contains(TEXT("crimson")) || Name.Contains(TEXT("warped"))) Wood = TEXT("nether_wood");
		const TCHAR* const OpenClose = bOn ? TEXT(".open") : TEXT(".close");
		switch (Kind)
		{
		case Lever:
			return TEXT("block.lever.click");
		case Button:
			return FName(TEXT("block.") + (IsStoneButton(Block) ? FString(TEXT("stone")) : Wood.IsEmpty() ? FString(TEXT("wooden")) : Wood)
				+ (bOn ? TEXT("_button.click_on") : TEXT("_button.click_off")));
		case Door:
		case Trapdoor:
		{
			const FString Material = Name.Contains(TEXT("iron")) ? TEXT("iron") : Name.Contains(TEXT("copper")) ? TEXT("copper") : Wood.IsEmpty() ? TEXT("wooden") : Wood;
			return FName(TEXT("block.") + Material + (Kind == Door ? TEXT("_door") : TEXT("_trapdoor")) + OpenClose);
		}
		case Gate:
			return FName(TEXT("block.") + (Wood.IsEmpty() ? FString() : Wood + TEXT("_")) + TEXT("fence_gate") + OpenClose);
		default:
			return NAME_None;
		}
	}

	// Doors, trapdoors and gates sound at a slightly random pitch, 0.9 to 1.
	FMcSoundEvent OpeningSound(uint8 Kind, FName Block, const FIntVector& Pos, bool bOpen)
	{
		return FMcSoundEvent{ GetUseSound(Kind, Block, bOpen), FVector(Pos) + FVector(0.5), 1.0f, FMath::FRandRange(0.9f, 1.0f) };
	}
}

bool FMcRedstone::Use(const FIntVector& Pos, float Yaw)
{
	const FMcStateId State = World.GetBlock(Pos);
	const uint8 Kind = KindOf(State);
	const FName Block = Reg().GetBlockOf(State).Name;
	const FVector Centre = FVector(Pos) + FVector(0.5);
	switch (Kind)
	{
	case Lever:
	{
		const bool bOn = !Flag(State, TEXT("powered"));
		Set(Pos, { FChange(TEXT("powered"), Bool(bOn)) });
		Sim.PlaySound(FMcSoundEvent{ GetUseSound(Kind, Block, bOn), Centre, 0.3f, bOn ? 0.6f : 0.5f });
		return true;
	}
	case Button:
		// A pressed button takes the click and stays as it is.
		if (!Flag(State, TEXT("powered")))
		{
			Set(Pos, { FChange(TEXT("powered"), TEXT("true")) });
			Sim.ScheduleTick(Pos, IsStoneButton(Block) ? 20 : 30);
			Sim.PlaySound(FMcSoundEvent{ GetUseSound(Kind, Block, true), Centre });
		}
		return true;
	case Door:
	case Trapdoor:
	case Gate:
	{
		if (Block.ToString().Contains(TEXT("iron")))
		{
			return false;
		}
		const bool bOpen = !Flag(State, TEXT("open"));
		if (Kind == Door)
		{
			const bool bLower = Reg().GetPropertyValue(State, TEXT("half")) == TEXT("lower");
			const FIntVector Other = Pos + Vec[bLower ? Up : Down];
			Set(Pos, { FChange(TEXT("open"), Bool(bOpen)) });
			if (IsKind(Other, Door))
			{
				Set(Other, { FChange(TEXT("open"), Bool(bOpen)) });
			}
		}
		else if (Kind == Gate && bOpen)
		{
			// Minecraft's yaw: 0 looks south, 90 west, 180 north, 270 east.
			const int32 Quarter = FMath::RoundToInt32(FMath::Fmod(FMath::Fmod(Yaw, 360.0f) + 360.0f, 360.0f) / 90.0f) & 3;
			const int32 Looking = (Quarter + 2) & 3;
			Set(Pos, { FChange(TEXT("open"), TEXT("true")), FChange(TEXT("facing"), DirName[FacingOf(State) == Opposite(Looking) ? Looking : FacingOf(State)]) });
		}
		else
		{
			Set(Pos, { FChange(TEXT("open"), Bool(bOpen)) });
		}
		Sim.PlaySound(OpeningSound(Kind, Block, Pos, bOpen));
		return true;
	}
	default:
		return false;
	}
}

FMcRedstone::FMcRedstone(FMcBlockWorld& InWorld, FMcSimulation& InSim)
	: World(InWorld)
	, Sim(InSim)
{
	FMcBlockLight::Get().EnsureLoaded();
}

FMcStateId FMcRedstone::With(FMcStateId State, std::initializer_list<FChange> Changes) const
{
	const FMcBlockRegistry& Registry = Reg();
	const FMcBlock& Block = Registry.GetBlockOf(State);
	const FMcBlockState& Data = Registry.GetState(State);
	TMap<FName, FName> Properties;
	for (int32 Index = 0; Index < Block.Properties.Num(); ++Index)
	{
		Properties.Add(Block.Properties[Index].Name, Block.Properties[Index].Values[Data.Values[Index]]);
	}
	for (const FChange& Change : Changes)
	{
		Properties.Add(Change.Name, Change.Value);
	}
	return Registry.FindState(Block.Name, Properties).Get(State);
}

void FMcRedstone::Set(const FIntVector& Pos, std::initializer_list<FChange> Changes)
{
	const FMcStateId State = World.GetBlock(Pos);
	const FMcStateId Next = With(State, Changes);
	if (Next != State)
	{
		World.SetBlock(Pos, Next);
	}
}

int32 FMcRedstone::Number(FMcStateId State, const TCHAR* Property) const
{
	const FName Value = Reg().GetPropertyValue(State, Property);
	return Value.IsNone() ? 0 : FCString::Atoi(*Value.ToString());
}

bool FMcRedstone::Flag(FMcStateId State, const TCHAR* Property) const
{
	return Reg().GetPropertyValue(State, Property) == TEXT("true");
}

int32 FMcRedstone::FacingOf(FMcStateId State) const
{
	const FName Value = Reg().GetPropertyValue(State, TEXT("facing"));
	for (int32 Dir = 0; Dir < 6; ++Dir)
	{
		if (Value == DirName[Dir])
		{
			return Dir;
		}
	}
	return -1;
}

bool FMcRedstone::IsKind(const FIntVector& Pos, int32 Kind) const
{
	return KindOf(World.GetBlock(Pos)) == Kind;
}

bool FMcRedstone::IsConductor(FMcStateId State) const
{
	// A full, opaque block takes power and gives it on; a redstone block gives power but doesn't take it, and neither does an
	// observer: a lamp behind a chain of them lights as the last one fires, not two ticks earlier (reference run rs_world).
	return FMcBlockLight::Get().GetDampening(State) >= 15 && FMcBlockShapes::Get().HasCollision(State) && KindOf(State) != RedstoneBlock && KindOf(State) != Observer;
}

int32 FMcRedstone::GetDustPower(const FIntVector& Pos) const
{
	const FMcStateId State = World.GetBlock(Pos);
	return KindOf(State) == Dust ? Number(State, TEXT("power")) : 0;
}

// ---- Power ----

int32 FMcRedstone::Signal(FMcStateId State, const FIntVector& Pos, int32 Toward) const
{
	switch (KindOf(State))
	{
	case RedstoneBlock:
		return 15;
	case Lever:
	case Button:
	case Plate:
	case DetectorRail:
	case TripwireHook:
		return Flag(State, TEXT("powered")) ? 15 : 0;
	case PlateLight:
	case PlateHeavy:
	case Daylight:
	case Target:
		return Number(State, TEXT("power"));
	case Torch:
		return Flag(State, TEXT("lit")) && Toward != Up ? 15 : 0;
	case WallTorch:
		return Flag(State, TEXT("lit")) && Toward != FacingOf(State) ? 15 : 0;
	case Repeater:
	case Observer:
		return Flag(State, TEXT("powered")) && FacingOf(State) == Toward ? 15 : 0;
	case Comparator:
	{
		if (!Flag(State, TEXT("powered")) || FacingOf(State) != Toward)
		{
			return 0;
		}
		const int32* Output = ComparatorOutputs.Find(Pos);
		return Output ? *Output : 15;
	}
	case Dust:
	{
		if (bIgnoreDust || Toward == Down)
		{
			return 0;
		}
		const int32 Power = Number(State, TEXT("power"));
		if (Power == 0 || Toward == Up)
		{
			return Power;
		}
		return Reg().GetPropertyValue(State, DirName[Opposite(Toward)]) != TEXT("none") ? Power : 0;
	}
	default:
		return 0;
	}
}

int32 FMcRedstone::DirectSignal(FMcStateId State, const FIntVector& Pos, int32 Toward) const
{
	switch (KindOf(State))
	{
	case Lever:
	case Button:
	{
		if (!Flag(State, TEXT("powered")))
		{
			return 0;
		}
		const FName Face = Reg().GetPropertyValue(State, TEXT("face"));
		const int32 Attached = Face == TEXT("floor") ? Up : Face == TEXT("ceiling") ? Down : FacingOf(State);
		return Attached == Toward ? 15 : 0;
	}
	case Plate:
	case PlateLight:
	case PlateHeavy:
	case DetectorRail:
		return Toward == Up ? Signal(State, Pos, Toward) : 0;
	// The hook powers the block it hangs on (the receiver sees it in the direction the hook faces).
	case TripwireHook:
		return Toward == FacingOf(State) ? Signal(State, Pos, Toward) : 0;
	case Torch:
	case WallTorch:
		return Toward == Down ? Signal(State, Pos, Toward) : 0;
	case Repeater:
	case Comparator:
	case Observer:
	case Dust:
		return Signal(State, Pos, Toward);
	default:
		return 0;
	}
}

int32 FMcRedstone::DirectSignalTo(const FIntVector& Pos) const
{
	int32 Best = 0;
	for (int32 Dir = 0; Dir < 6 && Best < 15; ++Dir)
	{
		const FIntVector Source = Pos + Vec[Dir];
		Best = FMath::Max(Best, DirectSignal(World.GetBlock(Source), Source, Dir));
	}
	return Best;
}

int32 FMcRedstone::SignalAt(const FIntVector& Pos, int32 Toward) const
{
	const FMcStateId State = World.GetBlock(Pos);
	const int32 Own = Signal(State, Pos, Toward);
	return IsConductor(State) ? FMath::Max(Own, DirectSignalTo(Pos)) : Own;
}

int32 FMcRedstone::BestSignal(const FIntVector& Pos) const
{
	int32 Best = 0;
	for (int32 Dir = 0; Dir < 6 && Best < 15; ++Dir)
	{
		Best = FMath::Max(Best, SignalAt(Pos + Vec[Dir], Dir));
	}
	return Best;
}

bool FMcRedstone::HasPower(const FIntVector& Pos) const
{
	return BestSignal(Pos) > 0;
}

int32 FMcRedstone::ControlInput(const FIntVector& Pos, int32 Toward, bool bOnlyDiodes) const
{
	const FMcStateId State = World.GetBlock(Pos);
	if (bOnlyDiodes && !IsDiode(State))
	{
		return 0;
	}
	if (KindOf(State) == RedstoneBlock)
	{
		return 15;
	}
	if (KindOf(State) == Dust)
	{
		return Number(State, TEXT("power"));
	}
	return DirectSignal(State, Pos, Toward);
}

int32 FMcRedstone::ContainerLevel(const FIntVector& Pos) const
{
	// Blocks that give a reading from their state (minecraft.wiki, "Redstone Comparator").
	const FMcStateId State = World.GetBlock(Pos);
	if (Reg().IsValidState(State))
	{
		const FString Name = Reg().GetBlockOf(State).Name.ToString();
		if (Name == TEXT("minecraft:cake"))
		{
			return (7 - Number(State, TEXT("bites"))) * 2;
		}
		if (Name == TEXT("minecraft:composter"))
		{
			return Number(State, TEXT("level"));
		}
		if (Name == TEXT("minecraft:water_cauldron") || Name == TEXT("minecraft:powder_snow_cauldron"))
		{
			return Number(State, TEXT("level"));
		}
		if (Name == TEXT("minecraft:lava_cauldron"))
		{
			return 3;
		}
		if (Name == TEXT("minecraft:end_portal_frame"))
		{
			return Flag(State, TEXT("eye")) ? 15 : 0;
		}
		if (Name == TEXT("minecraft:respawn_anchor"))
		{
			const int32 Charges = Number(State, TEXT("charges"));
			return Charges == 0 ? 0 : Charges >= 4 ? 15 : Charges * 4;
		}
		if (Name == TEXT("minecraft:beehive") || Name == TEXT("minecraft:bee_nest"))
		{
			return Number(State, TEXT("honey_level")) * 3;
		}
	}
	// The slots of a container, and of the other half of a double chest.
	TArray<FIntVector, TInlineAllocator<2>> Halves;
	Halves.Add(Pos);
	if (const TOptional<FIntVector> Partner = McChestLink::FindPartner(World, Pos))
	{
		Halves.Add(Partner.GetValue());
	}
	float Fill = 0.0f;
	int32 NonEmpty = 0;
	int32 NumSlots = 0;
	for (const FIntVector& Half : Halves)
	{
		const FMcChunk* Chunk = World.FindChunk(FIntPoint(Half.X >> 4, Half.Z >> 4));
		const FMcBlockEntity* Entity = Chunk ? Chunk->FindBlockEntity(Half.X & 15, Half.Y, Half.Z & 15) : nullptr;
		// A furnace or brewing stand keeps its slots in its saved bytes (as of the last sweep).
		TArray<FMcItemStack> Slots;
		if (Entity && Entity->Kind == EMcBlockEntityKind::Container)
		{
			Slots = Entity->Slots;
		}
		else if (Entity && !Entity->Data.IsEmpty() && (Entity->Kind == EMcBlockEntityKind::Furnace || Entity->Kind == EMcBlockEntityKind::BrewingStand))
		{
			const TSharedPtr<FMcMachine> Machine = Entity->Kind == EMcBlockEntityKind::BrewingStand ? TSharedPtr<FMcMachine>(MakeShared<FMcBrewingStand>()) : TSharedPtr<FMcMachine>(MakeShared<FMcFurnace>());
			FMemoryReaderView Reader(Entity->Data);
			if (Machine->Serialize(Reader))
			{
				Slots = Machine->GetItems()->GetAll();
			}
		}
		if (Slots.IsEmpty())
		{
			if (Half == Pos)
			{
				// A furnace or brewing stand nobody has used reads empty.
				const FString Name = Reg().GetBlockOf(World.GetBlock(Pos)).Name.ToString();
				return Name == TEXT("minecraft:furnace") || Name == TEXT("minecraft:blast_furnace") || Name == TEXT("minecraft:smoker") || Name == TEXT("minecraft:brewing_stand") ? 0 : -1;
			}
			// A half nobody has put anything in yet is as many empty slots.
			NumSlots += 27;
			continue;
		}
		NumSlots += Slots.Num();
		for (const FMcItemStack& Stack : Slots)
		{
			if (!Stack.IsEmpty())
			{
				Fill += static_cast<float>(Stack.Count) / FMath::Max(Stack.GetMaxStackSize(), 1);
				++NonEmpty;
			}
		}
	}
	Fill /= NumSlots;
	return FMath::FloorToInt(Fill * 14.0f) + (NonEmpty > 0 ? 1 : 0);
}

// ---- Block changes ----

void FMcRedstone::OnBlockChanged(const FIntVector& Pos, FMcStateId State)
{
	if (bQuietChange)
	{
		return;
	}
	// A disarmed string that goes (shears) is not felt by the hooks (reference client, rs_trip3).
	const bool bDisarmedGone = DisarmedStrings.Remove(Pos) > 0 && KindOf(State) != Tripwire;
	if (KindOf(State) == Tripwire && Flag(State, TEXT("disarmed")))
	{
		DisarmedStrings.Add(Pos);
	}
	TGuardValue<bool> GuardShorn(bShornBreak, bDisarmedGone);
	// A hook put in place connects the string beside it but does not look along its line yet (reference run rs_tripwire:
	// the hooks stay unattached until the first string is placed or something touches the string).
	// A string that becomes another state of itself does not look along its line (reference run rs_tripwire: a disarmed one
	// put over a string leaves the hooks and the other strings as they were).
	const bool bWasString = StringCells.Contains(Pos);
	if (KindOf(State) == Tripwire) StringCells.Add(Pos); else StringCells.Remove(Pos);
	TGuardValue<FIntVector> GuardChanged(ChangedPos, Pos);
	TGuardValue<FIntVector> GuardRemoved(RemovedString, bWasString && KindOf(State) != Tripwire ? Pos : FIntVector(INT32_MIN));
	TGuardValue<bool> GuardSame(bStringReplaced, bWasString && KindOf(State) == Tripwire);
	TGuardValue<FIntVector> GuardHook(HookPlaced, KindOf(State) == TripwireHook ? Pos : FIntVector(INT32_MIN));
	// Kelp with kelp growing above it is the stem block, kelp_plant (reference run rs_world: the lower kelp changes the
	// moment one is put above it).
	static const FName KelpName(TEXT("minecraft:kelp")), KelpPlantName(TEXT("minecraft:kelp_plant"));
	const auto IsKelpName = [&](FMcStateId Block) { const FName Name = Reg().GetBlockOf(Block).Name; return Name == KelpName || Name == KelpPlantName; };
	if (Reg().IsValidState(State) && IsKelpName(State))
	{
		const FMcStateId Plant = Reg().FindState(KelpPlantName, TMap<FName, FName>()).Get(0);
		const FIntVector Above = Pos + Vec[Up], Below = Pos + Vec[Down];
		const FMcStateId AboveState = World.GetBlock(Above), BelowState = World.GetBlock(Below);
		if (Plant != State && Reg().IsValidState(AboveState) && IsKelpName(AboveState))
		{
			World.SetBlock(Pos, Plant);
			return;
		}
		if (Reg().IsValidState(BelowState) && Reg().GetBlockOf(BelowState).Name == KelpName)
		{
			World.SetBlock(Below, Plant);
		}
	}
	Notify(Pos);
	for (int32 Dir = 0; Dir < 6; ++Dir)
	{
		Notify(Pos + Vec[Dir]);
	}
	// One step further: through a conductor (a block a signal passes through), and from anything redstone does
	// itself, so a piston still hears dust that reaches the block above it.
	// (A redstone block or a removed block sends no update past its own neighbours: reference run rs_quasi.)
	const bool bFull = KindOf(State) != None && KindOf(State) != RedstoneBlock;
	for (int32 Dir = 0; Dir < 6; ++Dir)
	{
		const FIntVector Next = Pos + Vec[Dir];
		// (Nor through a conductor: a removed block with a dropper beside it does not reach the dropper's other neighbours,
		// so a dropper under a quasi-powered dropper keeps triggered=true, reference run lg_logi.)
		if (bFull || (KindOf(State) != None && KindOf(State) != RedstoneBlock && IsConductor(World.GetBlock(Next))))
		{
			for (int32 Other = 0; Other < 6; ++Other)
			{
				if (Other != Opposite(Dir))
				{
					Notify(Next + Vec[Other]);
				}
			}
		}
	}
	// A new observer gives one pulse of its own (reference run rs_observer): its changes of its own do not.
	if (!bObserverSelf && KindOf(State) == Observer && !Flag(State, TEXT("powered")))
	{
		Sim.ScheduleTick(Pos, 2);
	}
	// An observer sees the block it faces change.
	for (int32 Dir = 0; Dir < 6; ++Dir)
	{
		const FIntVector Next = Pos + Vec[Dir];
		const FMcStateId Beside = World.GetBlock(Next);
		if (KindOf(Beside) == Observer && FacingOf(Beside) == Opposite(Dir) && !Flag(Beside, TEXT("powered")))
		{
			Sim.ScheduleTick(Next, 2);
		}
	}
	if (!bFlushing && !DirtyDust.IsEmpty())
	{
		bFlushing = true;
		FlushDust();
		bFlushing = false;
	}
}

bool FMcRedstone::IsSupported(const FIntVector& Pos, FMcStateId State) const
{
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	switch (KindOf(State))
	{
	case Dust:
	case Repeater:
	case Comparator:
	case Plate:
	case PlateLight:
	case PlateHeavy:
	case Torch:
	case Rail:
	case PoweredRail:
	case ActivatorRail:
	case DetectorRail:
		return Shapes.HasCollision(World.GetBlock(Pos + Vec[Down]));
	case WallTorch:
		return Shapes.HasCollision(World.GetBlock(Pos + Vec[Opposite(FacingOf(State))]));
	case TripwireHook:
		// A hook just put in place is not looked at (setblock does not check; reference run rs_trip2, "nowall"), only when a neighbour changes.
		return Pos == HookPlaced || Shapes.HasCollision(World.GetBlock(Pos + Vec[Opposite(FacingOf(State))]));
	case Lever:
	case Button:
	{
		const FName Face = Reg().GetPropertyValue(State, TEXT("face"));
		const int32 Attached = Face == TEXT("floor") ? Down : Face == TEXT("ceiling") ? Up : Opposite(FacingOf(State));
		return Shapes.HasCollision(World.GetBlock(Pos + Vec[Attached]));
	}
	default:
		return true;
	}
}

void FMcRedstone::NotifyContainerChanged(const FIntVector& Pos)
{
	// The comparators beside it, and beyond a solid block the comparator that reads through it (minecraft.wiki, "Redstone
	// Comparator"). Only comparators are told: a dropper beside a changed container keeps its triggered flag (reference run lg_logi).
	for (int32 Dir = 0; Dir < 6; ++Dir)
	{
		if (Dir == Up || Dir == Down)
		{
			continue;
		}
		const FIntVector Next = Pos + Vec[Dir];
		if (KindOf(World.GetBlock(Next)) == Comparator)
		{
			Notify(Next);
		}
		else if (IsConductor(World.GetBlock(Next)) && KindOf(World.GetBlock(Next + Vec[Dir])) == Comparator)
		{
			Notify(Next + Vec[Dir]);
		}
	}
}

void FMcRedstone::Notify(const FIntVector& Pos)
{
	const FMcStateId State = World.GetBlock(Pos);
	const uint8 Kind = KindOf(State);
	if (Kind == None)
	{
		return;
	}
	// (A hook only looks at what it hangs on when that is the block that changed.)
	// (And only when it was taken away: a string put where the hook hangs does not drop it, reference run rs_trip2, "same".)
	const bool bHookOk = Kind == TripwireHook && (ChangedPos != Pos + Vec[Opposite(FacingOf(State))] || !Reg().IsAir(World.GetBlock(ChangedPos)));
	if (!bHookOk && !IsSupported(Pos, State))
	{
		if (Kind == Dust)
		{
			DirtyDust.Add(Pos);
		}
		else
		{
			Sim.ScheduleTick(Pos, 1);
		}
		return;
	}
	switch (Kind)
	{
	case Dust:
		DirtyDust.Add(Pos);
		break;
	case Tripwire:
		ShapeTripwire(Pos);
		if (HookPlaced.X == INT32_MIN && !bStringReplaced)
		{
			UpdateTripwires(Pos);
		}
		break;
	case TripwireHook:
		if (HookPlaced.X == INT32_MIN && !bStringReplaced)
		{
			UpdateTripwires(Pos);
		}
		break;
	// Power on TNT lights it (the server makes it primed TNT and takes the block away).
	case Tnt:
		if (HasPower(Pos))
		{
			Sim.LightTnt(Pos);
		}
		break;
	case Repeater:
	{
		const bool bLocked = IsRepeaterLocked(Pos, State);
		if (bLocked != Flag(State, TEXT("locked")))
		{
			Set(Pos, { FChange(TEXT("locked"), Bool(bLocked)) });
			break;
		}
		if (!bLocked && Flag(State, TEXT("powered")) != (RepeaterInput(Pos, State) > 0))
		{
			Sim.ScheduleTick(Pos, Number(State, TEXT("delay")) * 2);
		}
		break;
	}
	case Comparator:
		if (ComparatorOutput(Pos, State) != ComparatorOutputs.FindRef(Pos) || (ComparatorOutput(Pos, State) > 0) != Flag(State, TEXT("powered")))
		{
			Sim.ScheduleTick(Pos, 2);
		}
		break;
	case Torch:
	case WallTorch:
		if (Flag(State, TEXT("lit")) == IsTorchPowered(Pos, State))
		{
			Sim.ScheduleTick(Pos, 2);
		}
		break;
	case Lamp:
		if (Flag(State, TEXT("lit")) && !HasPower(Pos))
		{
			Sim.ScheduleTick(Pos, 4);
		}
		else if (!Flag(State, TEXT("lit")) && HasPower(Pos))
		{
			// An unlit lamp that is powered lights at once (reference run rs_repeater); only going out waits 4 ticks.
			Set(Pos, { FChange(TEXT("lit"), TEXT("true")) });
		}
		break;
	case Piston:
	case StickyPiston:
		if (bInTick)
		{
			PistonEvents.AddUnique(Pos);
			break;
		}
		Sim.ScheduleTick(Pos, 1);
		break;
	case PistonHead:
	case Door:
	case Trapdoor:
	case Gate:
	case Daylight:
	case PoweredRail:
	case ActivatorRail:
		Sim.ScheduleTick(Pos, 1);
		break;
	case Dispenser:
	{
		// Powered (quasi-connectivity too, as for pistons), it fires 4 ticks later; it fires again only after the
		// power has gone and come back.
		const bool bPowered = ShouldExtend(Pos, -1);
		if (bPowered && !Flag(State, TEXT("triggered")))
		{
			// The flag is set without a neighbour update: after a pulse through quasi-connectivity, a dropper under a
			// dropper keeps triggered=true when the blocks go (reference run lg_logi).
			bQuietChange = true;
			World.SetBlock(Pos, With(State, { FChange(TEXT("triggered"), TEXT("true")) }));
			bQuietChange = false;
			Sim.ScheduleTick(Pos, 4);
		}
		else if (!bPowered && Flag(State, TEXT("triggered")))
		{
			bQuietChange = true;
			World.SetBlock(Pos, With(State, { FChange(TEXT("triggered"), TEXT("false")) }));
			bQuietChange = false;
		}
		break;
	}
	case Hopper:
	{
		// A powered hopper is locked.
		const bool bEnabled = !HasPower(Pos);
		if (Flag(State, TEXT("enabled")) != bEnabled)
		{
			World.SetBlock(Pos, With(State, { FChange(TEXT("enabled"), bEnabled ? TEXT("true") : TEXT("false")) }));
		}
		break;
	}
	case Button:
		if (Flag(State, TEXT("powered")))
		{
			Sim.ScheduleTick(Pos, Reg().GetBlockOf(State).Name.ToString().Contains(TEXT("stone")) ? 20 : 30);
		}
		break;
	default:
		break;
	}
}

bool FMcRedstone::RunTick(const FIntVector& Pos, FMcStateId State)
{
	const uint8 Kind = KindOf(State);
	if (Kind == None)
	{
		return false;
	}
	if (!IsSupported(Pos, State))
	{
		World.SetBlock(Pos, Air());
		return true;
	}
	switch (Kind)
	{
	case Repeater:
		TickRepeater(Pos, State);
		break;
	case Comparator:
		TickComparator(Pos, State);
		break;
	case Torch:
	case WallTorch:
		TickTorch(Pos, State);
		break;
	case Lamp:
		TickLamp(Pos, State);
		break;
	case Observer:
		TickObserver(Pos, State);
		break;
	case PoweredRail:
	case ActivatorRail:
	{
		const bool bPowered = IsRailPowered(Pos, State);
		if (bPowered != Flag(State, TEXT("powered")))
		{
			Set(Pos, { FChange(TEXT("powered"), Bool(bPowered)) });
		}
		break;
	}
	case DetectorRail:
		TickDetector(Pos, State);
		break;
	case Piston:
	case StickyPiston:
		TickPiston(Pos, State);
		break;
	case PistonHead:
		TickPistonHead(Pos, State);
		break;
	case MovingPiston:
		TickMovingPiston(Pos, State);
		break;
	case Dispenser:
		if (Sim.OnDispense)
		{
			Sim.OnDispense(Pos);
		}
		break;
	case Door:
	case Trapdoor:
	case Gate:
		TickOpening(Pos, State);
		break;
	case Button:
		TickButton(Pos, State);
		break;
	case Plate:
	case PlateLight:
	case PlateHeavy:
		TickPlate(Pos, State);
		break;
	case Daylight:
		TickDaylight(Pos, State);
		break;
	case Target:
		TickTarget(Pos, State);
		break;
	case Tripwire:
		TickTripwire(Pos, State);
		break;
	case TripwireHook:
		UpdateHookLine(Pos);
		break;
	default:
		break;
	}
	return true;
}

// ---- Dust ----

bool FMcRedstone::ConnectsTo(FMcStateId State, int32 Toward) const
{
	switch (KindOf(State))
	{
	case Dust:
	case RedstoneBlock:
	case Lever:
	case Button:
	case Plate:
	case PlateLight:
	case PlateHeavy:
	case Torch:
	case WallTorch:
	case Comparator:
	case Daylight:
	case Target:
	case TripwireHook:
		return true;
	case Repeater:
		return FacingOf(State) == Toward || FacingOf(State) == Opposite(Toward);
	case Observer:
		return FacingOf(State) == Toward;
	default:
		return false;
	}
}

void FMcRedstone::DustReads(const FIntVector& Pos, TArray<FIntVector, TInlineAllocator<8>>& Out) const
{
	const bool bUpConductor = IsConductor(World.GetBlock(Pos + Vec[Up]));
	for (int32 Dir = 0; Dir < 4; ++Dir)
	{
		const FIntVector Next = Pos + Vec[Dir];
		const FMcStateId State = World.GetBlock(Next);
		if (KindOf(State) == Dust)
		{
			Out.Add(Next);
		}
		if (IsConductor(State) && !bUpConductor)
		{
			if (IsKind(Next + Vec[Up], Dust))
			{
				Out.Add(Next + Vec[Up]);
			}
		}
		else if (!IsConductor(State) && IsKind(Next + Vec[Down], Dust))
		{
			Out.Add(Next + Vec[Down]);
		}
	}
}

// The dust at Pos with this power and the connections its surroundings give it.
FMcStateId FMcRedstone::DustShape(const FIntVector& Pos, FMcStateId State, int32 Power) const
{
	enum { NoLink, Side, Climb };
	const bool bWasDot = Reg().GetPropertyValue(State, TEXT("north")) == TEXT("none") && Reg().GetPropertyValue(State, TEXT("east")) == TEXT("none")
		&& Reg().GetPropertyValue(State, TEXT("south")) == TEXT("none") && Reg().GetPropertyValue(State, TEXT("west")) == TEXT("none");
	const bool bUpConductor = IsConductor(World.GetBlock(Pos + Vec[Up]));
	int32 Link[4] = { NoLink, NoLink, NoLink, NoLink };
	for (int32 Dir = 0; Dir < 4; ++Dir)
	{
		const FIntVector Next = Pos + Vec[Dir];
		const FMcStateId Beside = World.GetBlock(Next);
		if (!bUpConductor && IsConductor(Beside) && IsKind(Next + Vec[Up], Dust))
		{
			Link[Dir] = Climb;
		}
		else if (ConnectsTo(Beside, Dir) || (!IsConductor(Beside) && IsKind(Next + Vec[Down], Dust)))
		{
			Link[Dir] = Side;
		}
	}
	const bool bIsDot = !Link[0] && !Link[1] && !Link[2] && !Link[3];
	if (!(bWasDot && bIsDot))
	{
		// A line of dust is drawn straight through: with nothing north or south both east and west are linked, and so on.
		const bool bNoNorthSouth = !Link[North] && !Link[South];
		const bool bNoEastWest = !Link[East] && !Link[West];
		if (bNoNorthSouth)
		{
			Link[West] = Link[West] ? Link[West] : Side;
			Link[East] = Link[East] ? Link[East] : Side;
		}
		if (bNoEastWest)
		{
			Link[North] = Link[North] ? Link[North] : Side;
			Link[South] = Link[South] ? Link[South] : Side;
		}
	}
	static const TCHAR* const Names[3] = { TEXT("none"), TEXT("side"), TEXT("up") };
	return With(State, { FChange(TEXT("power"), Power), FChange(TEXT("north"), Names[Link[North]]), FChange(TEXT("east"), Names[Link[East]]),
		FChange(TEXT("south"), Names[Link[South]]), FChange(TEXT("west"), Names[Link[West]]) });
}

void FMcRedstone::FlushDust()
{
	while (!DirtyDust.IsEmpty())
	{
		const FIntVector Seed = *DirtyDust.CreateIterator();
		DirtyDust.Remove(Seed);
		if (!IsKind(Seed, Dust))
		{
			continue;
		}
		if (!IsSupported(Seed, World.GetBlock(Seed)))
		{
			World.SetBlock(Seed, Air());
			continue;
		}
		// Every dust that reads from or is read by this one, and so on: the whole connected network.
		TArray<FIntVector> Net;
		TSet<FIntVector> InNet;
		TMap<FIntVector, TArray<FIntVector, TInlineAllocator<8>>> Reads;
		TArray<FIntVector> Stack;
		Stack.Add(Seed);
		InNet.Add(Seed);
		while (!Stack.IsEmpty())
		{
			const FIntVector At = Stack.Pop();
			Net.Add(At);
			TArray<FIntVector, TInlineAllocator<8>>& ReadsHere = Reads.Add(At);
			DustReads(At, ReadsHere);
			for (const FIntVector& Other : ReadsHere)
			{
				if (!InNet.Contains(Other))
				{
					InNet.Add(Other);
					Stack.Add(Other);
				}
			}
			for (int32 Dir = 0; Dir < 4; ++Dir)
			{
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					const FIntVector Other = At + Vec[Dir] + FIntVector(0, DY, 0);
					if (!InNet.Contains(Other) && IsKind(Other, Dust))
					{
						TArray<FIntVector, TInlineAllocator<8>> OtherReads;
						DustReads(Other, OtherReads);
						if (OtherReads.Contains(At))
						{
							InNet.Add(Other);
							Stack.Add(Other);
						}
					}
				}
			}
		}
		// Power: what the other blocks give each dust, then spread along the network losing 1 a block.
		TMap<FIntVector, int32> Power;
		bIgnoreDust = true;
		for (const FIntVector& At : Net)
		{
			Power.Add(At, BestSignal(At));
		}
		bIgnoreDust = false;
		TMap<FIntVector, TArray<FIntVector, TInlineAllocator<8>>> Readers;
		for (const FIntVector& At : Net)
		{
			for (const FIntVector& Source : Reads[At])
			{
				Readers.FindOrAdd(Source).Add(At);
			}
		}
		for (int32 Level = 15; Level > 1; --Level)
		{
			for (const FIntVector& At : Net)
			{
				if (Power[At] == Level)
				{
					if (const auto* Following = Readers.Find(At))
					{
						for (const FIntVector& Next : *Following)
						{
							int32& NextPower = Power[Next];
							NextPower = FMath::Max(NextPower, Level - 1);
						}
					}
				}
			}
		}
		for (const FIntVector& At : Net)
		{
			const FMcStateId State = World.GetBlock(At);
			const FMcStateId Next = DustShape(At, State, Power[At]);
			if (Next != State)
			{
				World.SetBlock(At, Next);
			}
		}
		// What the writes disturbed was this network, which is now settled.
		for (const FIntVector& At : Net)
		{
			DirtyDust.Remove(At);
		}
	}
}

// ---- Components ----

int32 FMcRedstone::RepeaterInput(const FIntVector& Pos, FMcStateId State) const
{
	const int32 Facing = FacingOf(State);
	const FIntVector Source = Pos + Vec[Facing];
	const int32 Level = SignalAt(Source, Facing);
	if (Level >= 15)
	{
		return Level;
	}
	const FMcStateId Beside = World.GetBlock(Source);
	return KindOf(Beside) == Dust ? FMath::Max(Level, Number(Beside, TEXT("power"))) : Level;
}

bool FMcRedstone::IsRepeaterLocked(const FIntVector& Pos, FMcStateId State) const
{
	const int32 Facing = FacingOf(State);
	for (const int32 Side : { (Facing + 1) % 4, (Facing + 3) % 4 })
	{
		if (ControlInput(Pos + Vec[Side], Side, true) > 0)
		{
			return true;
		}
	}
	return false;
}

void FMcRedstone::TickRepeater(const FIntVector& Pos, FMcStateId State)
{
	const bool bLocked = IsRepeaterLocked(Pos, State);
	if (bLocked != Flag(State, TEXT("locked")))
	{
		Set(Pos, { FChange(TEXT("locked"), Bool(bLocked)) });
		State = World.GetBlock(Pos);
	}
	if (bLocked)
	{
		return;
	}
	const bool bPowered = Flag(State, TEXT("powered"));
	const bool bShould = RepeaterInput(Pos, State) > 0;
	if (bPowered && !bShould)
	{
		Set(Pos, { FChange(TEXT("powered"), TEXT("false")) });
	}
	else if (!bPowered)
	{
		Set(Pos, { FChange(TEXT("powered"), TEXT("true")) });
		if (!bShould)
		{
			Sim.ScheduleTick(Pos, Number(State, TEXT("delay")) * 2);
		}
	}
}

int32 FMcRedstone::ComparatorOutput(const FIntVector& Pos, FMcStateId State) const
{
	const int32 Facing = FacingOf(State);
	const FIntVector Source = Pos + Vec[Facing];
	int32 Rear = SignalAt(Source, Facing);
	const FMcStateId Behind = World.GetBlock(Source);
	const int32 Container = ContainerLevel(Source);
	if (Container >= 0)
	{
		Rear = Container;
	}
	else if (Rear < 15 && IsConductor(Behind))
	{
		const int32 Further = ContainerLevel(Source + Vec[Facing]);
		if (Further >= 0)
		{
			Rear = Further;
		}
	}
	int32 Side = 0;
	for (const int32 Dir : { (Facing + 1) % 4, (Facing + 3) % 4 })
	{
		Side = FMath::Max(Side, ControlInput(Pos + Vec[Dir], Dir, false));
	}
	if (Reg().GetPropertyValue(State, TEXT("mode")) == TEXT("subtract"))
	{
		return FMath::Max(Rear - Side, 0);
	}
	return Rear >= Side ? Rear : 0;
}

void FMcRedstone::TickComparator(const FIntVector& Pos, FMcStateId State)
{
	const int32 Output = ComparatorOutput(Pos, State);
	const int32 Before = ComparatorOutputs.FindRef(Pos);
	if (Output > 0)
	{
		ComparatorOutputs.Add(Pos, Output);
	}
	else
	{
		ComparatorOutputs.Remove(Pos);
	}
	if ((Output > 0) != Flag(State, TEXT("powered")))
	{
		Set(Pos, { FChange(TEXT("powered"), Bool(Output > 0)) });
	}
	else if (Output != Before)
	{
		// The strength changed without the block changing: tell what it feeds.
		const FIntVector Front = Pos + Vec[Opposite(FacingOf(State))];
		OnBlockChanged(Front, World.GetBlock(Front));
	}
}

bool FMcRedstone::IsTorchPowered(const FIntVector& Pos, FMcStateId State) const
{
	const int32 Toward = KindOf(State) == Torch ? static_cast<int32>(Down) : Opposite(FacingOf(State));
	return SignalAt(Pos + Vec[Toward], Toward) > 0;
}

bool FMcRedstone::IsToggledTooOften(const FIntVector& Pos, bool bAdd)
{
	TArray<int64>& Toggles = TorchToggles.FindOrAdd(Pos);
	if (bAdd)
	{
		Toggles.Add(Sim.GetGameTime());
	}
	return Toggles.Num() >= 8;
}

void FMcRedstone::TickTorch(const FIntVector& Pos, FMcStateId State)
{
	const bool bPowered = IsTorchPowered(Pos, State);
	if (TArray<int64>* Toggles = TorchToggles.Find(Pos))
	{
		Toggles->RemoveAll([&](int64 Time) { return Sim.GetGameTime() - Time > 60; });
	}
	if (Flag(State, TEXT("lit")))
	{
		if (bPowered)
		{
			Set(Pos, { FChange(TEXT("lit"), TEXT("false")) });
			if (IsToggledTooOften(Pos, true))
			{
				// Burned out: it stays dark for a while.
				Sim.ScheduleTick(Pos, 160);
			}
		}
	}
	else if (!bPowered && !IsToggledTooOften(Pos, false))
	{
		Set(Pos, { FChange(TEXT("lit"), TEXT("true")) });
	}
}

void FMcRedstone::TickLamp(const FIntVector& Pos, FMcStateId State)
{
	const bool bPowered = HasPower(Pos);
	if (Flag(State, TEXT("lit")) && !bPowered)
	{
		Set(Pos, { FChange(TEXT("lit"), TEXT("false")) });
	}
	else if (!Flag(State, TEXT("lit")) && bPowered)
	{
		Set(Pos, { FChange(TEXT("lit"), TEXT("true")) });
	}
}

void FMcRedstone::TickObserver(const FIntVector& Pos, FMcStateId State)
{
	TGuardValue<bool> Own(bObserverSelf, true);
	if (Flag(State, TEXT("powered")))
	{
		Set(Pos, { FChange(TEXT("powered"), TEXT("false")) });
	}
	else
	{
		Set(Pos, { FChange(TEXT("powered"), TEXT("true")) });
		Sim.ScheduleTick(Pos, 2);
	}
}

void FMcRedstone::TickOpening(const FIntVector& Pos, FMcStateId State)
{
	if (KindOf(State) == Door)
	{
		const bool bLower = Reg().GetPropertyValue(State, TEXT("half")) == TEXT("lower");
		const FIntVector Other = Pos + Vec[bLower ? Up : Down];
		const bool bPowered = HasPower(Pos) || HasPower(Other);
		if (bPowered != Flag(State, TEXT("open")) || bPowered != Flag(State, TEXT("powered")))
		{
			if (bPowered != Flag(State, TEXT("open")))
			{
				Sim.PlaySound(OpeningSound(Door, Reg().GetBlockOf(State).Name, Pos, bPowered));
			}
			Set(Pos, { FChange(TEXT("open"), Bool(bPowered)), FChange(TEXT("powered"), Bool(bPowered)) });
			if (IsKind(Other, Door))
			{
				Set(Other, { FChange(TEXT("open"), Bool(bPowered)), FChange(TEXT("powered"), Bool(bPowered)) });
			}
		}
		return;
	}
	// Trapdoors and gates follow the power only when it changes, so a player's hand still opens them.
	const bool bPowered = HasPower(Pos);
	if (bPowered != Flag(State, TEXT("powered")))
	{
		if (bPowered != Flag(State, TEXT("open")))
		{
			Sim.PlaySound(OpeningSound(KindOf(State), Reg().GetBlockOf(State).Name, Pos, bPowered));
		}
		Set(Pos, { FChange(TEXT("open"), Bool(bPowered)), FChange(TEXT("powered"), Bool(bPowered)) });
	}
}

void FMcRedstone::TickButton(const FIntVector& Pos, FMcStateId State)
{
	if (Flag(State, TEXT("powered")))
	{
		Set(Pos, { FChange(TEXT("powered"), TEXT("false")) });
		Sim.PlaySound(FMcSoundEvent{ GetUseSound(Button, Reg().GetBlockOf(State).Name, false), FVector(Pos) + FVector(0.5) });
	}
}

bool FMcRedstone::IsRailPowered(const FIntVector& Pos, FMcStateId State) const
{
	if (HasPower(Pos))
	{
		return true;
	}
	McRails::FTrack Track;
	if (!McRails::GetTrack(State, Track))
	{
		return false;
	}
	// Power reaches along the track from a powered rail of the same kind up to eight rails away.
	const FName Block = Reg().GetBlockOf(State).Name;
	for (int32 End = 0; End < 2; ++End)
	{
		FIntVector Here = Pos;
		FIntVector Dir = End ? Track.EndB : Track.EndA;
		bool bRise = End ? Track.bRiseB : Track.bRiseA;
		for (int32 Step = 0; Step < 8; ++Step)
		{
			FIntVector Next;
			if (!McRails::FindNext(World, Here, Dir, bRise, Next) || Reg().GetBlockOf(World.GetBlock(Next)).Name != Block)
			{
				break;
			}
			if (HasPower(Next))
			{
				return true;
			}
			McRails::FTrack NextTrack;
			McRails::GetTrack(World.GetBlock(Next), NextTrack);
			const FIntVector Back(-Dir.X, 0, -Dir.Z);
			const bool bOnA = NextTrack.EndA == Back;
			Dir = bOnA ? NextTrack.EndB : NextTrack.EndA;
			bRise = bOnA ? NextTrack.bRiseB : NextTrack.bRiseA;
			Here = Next;
		}
	}
	return false;
}

void FMcRedstone::SetDetector(const FIntVector& Pos, bool bOccupied)
{
	if (KindOf(World.GetBlock(Pos)) != DetectorRail)
	{
		return;
	}
	if (bOccupied)
	{
		Detectors.Add(Pos);
		if (!Flag(World.GetBlock(Pos), TEXT("powered")))
		{
			Sim.ScheduleTick(Pos, 1);
		}
	}
	else if (Detectors.Remove(Pos) > 0)
	{
		// Powered for a second after the cart has gone.
		Sim.ScheduleTick(Pos, 20);
	}
}

void FMcRedstone::TickDetector(const FIntVector& Pos, FMcStateId State)
{
	const bool bOccupied = Detectors.Contains(Pos);
	if (bOccupied != Flag(State, TEXT("powered")))
	{
		Set(Pos, { FChange(TEXT("powered"), Bool(bOccupied)) });
	}
	if (bOccupied)
	{
		// Held while it is there: check again in a second.
		Sim.ScheduleTick(Pos, 20);
	}
}

void FMcRedstone::SetPlate(const FIntVector& Pos, int32 NumEntities)
{
	const FMcStateId State = World.GetBlock(Pos);
	const uint8 Kind = KindOf(State);
	if (Kind == Tripwire)
	{
		Plates.Add(Pos, NumEntities);
		if (NumEntities > 0 && !Flag(State, TEXT("powered")))
		{
			Set(Pos, { FChange(TEXT("powered"), TEXT("true")) });
			Sim.ScheduleTick(Pos, TripwireDelay);
			// Something on a disarmed string lets go of the hooks and the strings until the next look (reference run
			// rs_tripwire: all unattached when the item lands, attached again 10 ticks on).
			TGuardValue<bool> Guard(bDetachLine, Flag(State, TEXT("disarmed")));
			UpdateTripwires(Pos);
		}
		return;
	}
	if (Kind != Plate && Kind != PlateLight && Kind != PlateHeavy)
	{
		return;
	}
	Plates.Add(Pos, NumEntities);
	// Pressing is at once; letting go takes a moment (wooden and stone plates 20 ticks, weighted ones 10).
	Sim.ScheduleTick(Pos, NumEntities > 0 ? 1 : (Kind == Plate ? 20 : 10));
}

void FMcRedstone::TickPlate(const FIntVector& Pos, FMcStateId State)
{
	const int32 Count = Plates.FindRef(Pos);
	const uint8 Kind = KindOf(State);
	if (Kind == Plate)
	{
		if ((Count > 0) != Flag(State, TEXT("powered")))
		{
			Set(Pos, { FChange(TEXT("powered"), Bool(Count > 0)) });
		}
	}
	else
	{
		const int32 Power = Kind == PlateLight ? FMath::Min(Count, 15) : FMath::Min((Count + 9) / 10, 15);
		if (Power != Number(State, TEXT("power")))
		{
			Set(Pos, { FChange(TEXT("power"), Power) });
		}
	}
}

// ---- Tripwire (minecraft.wiki, "Tripwire", "Tripwire Hook") ----
// Two hooks facing each other 1 to 40 blocks apart with string between them. An entity touching the string powers it
// (not once it is disarmed); the hooks are powered while any of the string is. The string looks again 10 ticks after
// it was triggered and every 10 while something stays in it. Held to the reference client's rs_tripwire log: an entity
// powers the string and both hooks the tick it enters, they let go at the first 10-tick look that finds it empty, a
// disarmed string powers itself but not the hooks, and a broken string powers the hooks. Shears and hook placement are
// not done.

void FMcRedstone::UpdateTripwires(const FIntVector& Pos)
{
	// The hooks whose line passes through Pos (or that is Pos itself).
	TArray<FIntVector> Hooks;
	if (KindOf(World.GetBlock(Pos)) == TripwireHook)
	{
		Hooks.Add(Pos);
	}
	else
	{
		for (const int32 Dir : { North, East, South, West })
		{
			FIntVector Cell = Pos;
			for (int32 Step = 0; Step <= TripwireMaxLength + 1; ++Step)
			{
				Cell += Vec[Dir];
				const FMcStateId State = World.GetBlock(Cell);
				const uint8 Kind = KindOf(State);
				if (Kind == TripwireHook)
				{
					if (FacingOf(State) == Opposite(Dir))
					{
						Hooks.Add(Cell);
					}
					break;
				}
				if (Kind != Tripwire)
				{
					break;
				}
			}
		}
	}
	for (const FIntVector& Hook : Hooks)
	{
		UpdateHookLine(Hook);
	}
}

void FMcRedstone::UpdateHookLine(const FIntVector& Hook)
{
	const FMcStateId HookState = World.GetBlock(Hook);
	if (KindOf(HookState) != TripwireHook)
	{
		return;
	}
	const int32 Dir = FacingOf(HookState);
	bool bAttached = false;
	bool bPowered = false;
	bool bBroken = false;
	int32 RemovedStep = 0;
	FIntVector Other = Hook;
	for (int32 Step = 1; Step <= TripwireMaxLength + 1; ++Step)
	{
		const FIntVector Cell = Hook + Vec[Dir] * Step;
		const FMcStateId State = World.GetBlock(Cell);
		const uint8 Kind = KindOf(State);
		if (Cell == RemovedString && (!bShornBreak || Flag(HookState, TEXT("attached"))))
		{
			// The string that was just taken away still counts, powered, in the line it was in (reference runs rs_tripwire,
			// rs_trip3): the hooks join up and power on at once.
			bPowered = true;
			bBroken = true;
			RemovedStep = Step;
			continue;
		}
		if (Kind == TripwireHook)
		{
			bAttached = FacingOf(State) == Opposite(Dir) && Step >= 2;
			Other = Cell;
			break;
		}
		if (Kind != Tripwire)
		{
			break;
		}
		bPowered |= Flag(State, TEXT("powered")) && !Flag(State, TEXT("disarmed"));
	}
	// Taking away the string next to a hook (either end) does not join or power them (reference run rs_trip2).
	if (bBroken && (RemovedStep == 1 || (bAttached && Other - Vec[Dir] == RemovedString)))
	{
		bAttached = false;
		bPowered = false;
		bBroken = false;
	}
	bPowered &= bAttached;
	if (bDetachLine)
	{
		bAttached = bPowered = false;
	}
	const auto Apply = [&](const FIntVector& Pos)
	{
		const FMcStateId State = World.GetBlock(Pos);
		if (Flag(State, TEXT("attached")) != bAttached || Flag(State, TEXT("powered")) != bPowered)
		{
			Set(Pos, { FChange(TEXT("attached"), Bool(bAttached)), FChange(TEXT("powered"), Bool(bPowered)) });
			return true;
		}
		return false;
	};
	// Only a hook joining or leaving its partner marks the strings; a power change alone does not (rs_tripwire).
	const bool bWasAttached = Flag(HookState, TEXT("attached"));
	bool bChanged = bWasAttached != bAttached || (bAttached && !Flag(World.GetBlock(Other), TEXT("attached")));
	Apply(Hook);
	if (bAttached)
	{
		Apply(Other);
		HookPartner.Add(Hook, Other);
		HookPartner.Add(Other, Hook);
	}
	else if (const FIntVector* Partner = HookPartner.Find(Hook))
	{
		// The one it was joined to lets go with it.
		const FIntVector Gone = *Partner;
		HookPartner.Remove(Hook);
		if (bWasAttached)
		{
			UpdateHookLine(Gone);
		}
	}
	// They let go again a while later.
	if (bBroken && bPowered)
	{
		Sim.ScheduleTick(Hook, TripwireDelay);
	}
	// The strings between carry the flag too, when the hooks changed (reference run rs_tripwire: a look that finds
	// nothing new leaves the strings as they are).
	for (int32 Step = 1; bChanged && Step <= TripwireMaxLength; ++Step)
	{
		const FIntVector Cell = Hook + Vec[Dir] * Step;
		if (Cell == RemovedString && (!bShornBreak || Flag(HookState, TEXT("attached"))))
		{
			continue;
		}
		const FMcStateId State = World.GetBlock(Cell);
		if (KindOf(State) != Tripwire)
		{
			break;
		}
		if (Flag(State, TEXT("attached")) != bAttached)
		{
			Set(Cell, { FChange(TEXT("attached"), Bool(bAttached)) });
		}
	}
}

// A string joins the string or the hook that faces it on each side (reference run rs_tripwire: the string next to a hook is
// connected to it from the moment it is there).
void FMcRedstone::ShapeTripwire(const FIntVector& Pos)
{
	static const TCHAR* Names[4] = { TEXT("north"), TEXT("east"), TEXT("south"), TEXT("west") };
	const int32 Dirs[4] = { North, East, South, West };
	const FMcStateId State = World.GetBlock(Pos);
	bool bJoin[4];
	bool bChanged = false;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FMcStateId Beside = World.GetBlock(Pos + Vec[Dirs[Index]]);
		const uint8 Kind = KindOf(Beside);
		bJoin[Index] = Kind == Tripwire || (Kind == TripwireHook && FacingOf(Beside) == Opposite(Dirs[Index]));
		bChanged |= Flag(State, Names[Index]) != bJoin[Index];
	}
	if (bChanged)
	{
		Set(Pos, { FChange(Names[0], Bool(bJoin[0])), FChange(Names[1], Bool(bJoin[1])), FChange(Names[2], Bool(bJoin[2])), FChange(Names[3], Bool(bJoin[3])) });
	}
}

void FMcRedstone::TickTripwire(const FIntVector& Pos, FMcStateId State)
{
	if (Plates.FindRef(Pos) > 0)
	{
		Sim.ScheduleTick(Pos, TripwireDelay);
		UpdateTripwires(Pos);
		return;
	}
	if (Flag(State, TEXT("powered")))
	{
		Set(Pos, { FChange(TEXT("powered"), TEXT("false")) });
	}
	UpdateTripwires(Pos);
}

// A projectile hit a target block: it gives Power for Duration ticks (minecraft.wiki, "Target": 20 for arrows and
// tridents, 8 for the other projectiles; a new hit starts the time again). The strength by where it hit is not
// measured yet (no reference verb shoots), so the caller's figure stands.
void FMcRedstone::HitTarget(const FIntVector& Pos, int32 Power, int32 Duration)
{
	const FMcStateId State = World.GetBlock(Pos);
	if (KindOf(State) != Target)
	{
		return;
	}
	TargetUntil.Add(Pos, Sim.GetGameTime() + Duration);
	if (Power != Number(State, TEXT("power")))
	{
		Set(Pos, { FChange(TEXT("power"), Power) });
	}
	Sim.ScheduleTick(Pos, Duration);
}

void FMcRedstone::TickTarget(const FIntVector& Pos, FMcStateId State)
{
	const int64* Until = TargetUntil.Find(Pos);
	const int64 Left = Until ? *Until - Sim.GetGameTime() : 0;
	if (Left > 0)
	{
		Sim.ScheduleTick(Pos, static_cast<int32>(Left));
		return;
	}
	TargetUntil.Remove(Pos);
	if (Number(State, TEXT("power")) != 0)
	{
		Set(Pos, { FChange(TEXT("power"), 0) });
	}
}

int32 FMcRedstone::SkyLightAt(const FIntVector& Pos) const
{
	// Open above (full sky light passes straight down through everything over it): 15, without the light engine.
	const FMcBlockLight& Light = FMcBlockLight::Get();
	if (!Light.IsLoaded())
	{
		return 15;
	}
	bool bOpen = true;
	for (int32 Y = Pos.Y + 1; Y <= World.GetDimension().MaxY() && bOpen; ++Y)
	{
		bOpen = Light.PropagatesSkylightDown(World.GetBlock(FIntVector(Pos.X, Y, Pos.Z)));
	}
	if (bOpen)
	{
		return 15;
	}
	static TSharedPtr<const FMcLightPalette> Palette;
	if (!Palette.IsValid())
	{
		Palette = FMcLightPalette::Build();
	}
	const FIntPoint Chunk(Pos.X >> 4, Pos.Z >> 4);
	FMcLightRegion Region = FMcLightRegion::Capture(World, Chunk);
	Region.Compute(*Palette);
	return Region.GetSkyLight(Pos.X - Chunk.X * 16 + FMcLightRegion::Border, Pos.Y, Pos.Z - Chunk.Y * 16 + FMcLightRegion::Border);
}

void FMcRedstone::TickDaylight(const FIntVector& Pos, FMcStateId State)
{
	// The light that reaches the detector's own cell, less what the sun leaves off the sky (reference run rs_detcover: a
	// detector under a block reads the light of its cell, 13 under the middle of a 3x3 roof at noon, and the inverted one
	// 15 minus that; the plain one scales it by the sun's height as the open sky's is).
	const int32 Cell = FMath::Max(0, SkyLightAt(Pos) - (15 - SkyLevel));
	const int32 Power = Flag(State, TEXT("inverted")) ? 15 - Cell : FMath::Clamp(FMath::RoundToInt(Cell * SunScale), 0, 15);
	if (Power != Number(State, TEXT("power")))
	{
		Set(Pos, { FChange(TEXT("power"), Power) });
	}
	// It updates whenever the game time divides by 20 (minecraft.wiki, "Daylight Detector").
	Sim.ScheduleTick(Pos, 20 - static_cast<int32>(Sim.GetGameTime() % 20));
}

// ---- Pistons ----

bool FMcRedstone::ShouldExtend(const FIntVector& Pos, int32 Facing) const
{
	for (int32 Dir = 0; Dir < 6; ++Dir)
	{
		if (Dir != Facing && SignalAt(Pos + Vec[Dir], Dir) > 0)
		{
			return true;
		}
	}
	// Quasi-connectivity: a piston is also powered by whatever would power the block above it.
	const FIntVector Above = Pos + Vec[Up];
	for (int32 Dir = 0; Dir < 6; ++Dir)
	{
		if (Dir != Down && SignalAt(Above + Vec[Dir], Dir) > 0)
		{
			return true;
		}
	}
	return false;
}

bool FMcRedstone::IsBreakable(FMcStateId State) const
{
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	if (Reg().IsAir(State))
	{
		return false;
	}
	const FString Name = Reg().GetBlockOf(State).Name.ToString();
	if (Shapes.GetFluid(State) != EMcFluid::None && (Name == TEXT("minecraft:water") || Name == TEXT("minecraft:lava")))
	{
		return true;
	}
	if (Name.EndsWith(TEXT("_door")) || Name == TEXT("minecraft:cactus") || Name == TEXT("minecraft:pumpkin") || Name == TEXT("minecraft:melon")
		|| Name == TEXT("minecraft:carved_pumpkin") || Name == TEXT("minecraft:jack_o_lantern") || Name.EndsWith(TEXT("cake")))
	{
		return true;
	}
	// Plants, torches, dust, levers: anything with no body to push, except rails.
	return !Shapes.HasCollision(State) && !Name.Contains(TEXT("rail")) && KindOf(State) != PistonHead && KindOf(State) != MovingPiston;
}

bool FMcRedstone::IsPushable(const FIntVector& Pos, FMcStateId State) const
{
	const uint8 Kind = KindOf(State);
	if (Kind == PistonHead || Kind == MovingPiston || IsImmovableName(Reg().GetBlockOf(State).Name))
	{
		return false;
	}
	if ((Kind == Piston || Kind == StickyPiston) && Flag(State, TEXT("extended")))
	{
		return false;
	}
	const FMcChunk* Chunk = World.FindChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4));
	return !(Chunk && Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15));
}

// The blocks a push from Start toward Toward moves (at most 12) and the ones it breaks. False if it can't happen.
bool FMcRedstone::ResolvePush(const FIntVector& PistonPos, const FIntVector& Start, int32 Toward, TArray<FIntVector>& Move, TArray<FIntVector>& Destroy) const
{
	Move.Reset();
	Destroy.Reset();
	TSet<FIntVector> InSet;
	const FIntVector Step = Vec[Toward];
	const FMcBlockRegistry& Registry = Reg();
	const auto IsSlime = [&](FMcStateId State) { return Registry.GetBlockOf(State).Name == TEXT("minecraft:slime_block"); };
	const auto IsHoney = [&](FMcStateId State) { return Registry.GetBlockOf(State).Name == TEXT("minecraft:honey_block"); };

	// Follows a line of blocks from From in the push direction. A main line fails on a block that can't move; a
	// branch (a block a slime or honey block holds on to) just isn't taken along.
	TFunction<bool(const FIntVector&, bool)> Line = [&](const FIntVector& From, bool bMain) -> bool
	{
		FIntVector At = From;
		for (;;)
		{
			if (InSet.Contains(At))
			{
				return true;
			}
			const FMcStateId State = World.GetBlock(At);
			if (Registry.IsAir(State))
			{
				return true;
			}
			if (IsBreakable(State))
			{
				if (bMain || At != From)
				{
					Destroy.AddUnique(At);
				}
				return true;
			}
			if (!IsPushable(At, State))
			{
				return !bMain && At == From;
			}
			const FIntVector Dest = At + Step;
			if (!World.HasChunk(FIntPoint(Dest.X >> 4, Dest.Z >> 4)) || Dest.Y < World.GetDimension().MinY || Dest.Y > World.GetDimension().MaxY())
			{
				return false;
			}
			Move.Add(At);
			InSet.Add(At);
			if (Move.Num() > 12)
			{
				return false;
			}
			if (IsSlime(State) || IsHoney(State))
			{
				for (int32 Dir = 0; Dir < 6; ++Dir)
				{
					const FIntVector Beside = At + Vec[Dir];
					if (Beside == PistonPos || InSet.Contains(Beside))
					{
						continue;
					}
					const FMcStateId Other = World.GetBlock(Beside);
					if ((IsSlime(State) && IsHoney(Other)) || (IsHoney(State) && IsSlime(Other)))
					{
						continue;
					}
					if (!Line(Beside, false))
					{
						return false;
					}
				}
			}
			At = Dest;
		}
	};
	return Line(Start, true);
}

// Every carried block shows as a moving piston facing the way the piston faces, type normal, for the 2 ticks it
// takes to land (reference run rs_piston).
void FMcRedstone::MoveBlocks(const TArray<FIntVector>& Move, const TArray<FIntVector>& Destroy, int32 Toward, int32 PistonFacing)
{
	const FMcBlockRegistry& Registry = Reg();
	for (const FIntVector& At : Destroy)
	{
		World.SetBlock(At, Air());
	}
	TArray<FMcStateId> Carried;
	for (const FIntVector& At : Move)
	{
		Carried.Add(World.GetBlock(At));
	}
	for (const FIntVector& At : Move)
	{
		World.SetBlock(At, Air());
	}
	TMap<FName, FName> Properties;
	Properties.Add(TEXT("facing"), DirName[PistonFacing]);
	Properties.Add(TEXT("type"), TEXT("normal"));
	const FMcStateId Moving = Registry.FindState(TEXT("minecraft:moving_piston"), Properties).Get(0);
	for (int32 Index = 0; Index < Move.Num(); ++Index)
	{
		const FIntVector Dest = Move[Index] + Vec[Toward];
		MovingBlocks.Add(Dest, TPair<FMcStateId, int64>(Carried[Index], Sim.GetGameTime() + 2));
		World.SetBlock(Dest, Moving);
		Sim.ScheduleTick(Dest, 2);
	}
}

bool FMcRedstone::Extend(const FIntVector& Pos, FMcStateId State, int32 Facing, bool bSticky)
{
	TArray<FIntVector> Move, Destroy;
	if (!ResolvePush(Pos, Pos + Vec[Facing], Facing, Move, Destroy))
	{
		return false;
	}
	MoveBlocks(Move, Destroy, Facing, Facing);
	// The head is a moving piston of the piston's own type for 2 ticks, then the head itself.
	TMap<FName, FName> Properties;
	Properties.Add(TEXT("facing"), DirName[Facing]);
	Properties.Add(TEXT("type"), bSticky ? TEXT("sticky") : TEXT("normal"));
	Properties.Add(TEXT("short"), TEXT("false"));
	const FMcStateId Head = Reg().FindState(TEXT("minecraft:piston_head"), Properties).Get(0);
	Properties.Remove(TEXT("short"));
	const FIntVector HeadPos = Pos + Vec[Facing];
	MovingBlocks.Add(HeadPos, TPair<FMcStateId, int64>(Head, Sim.GetGameTime() + 2));
	World.SetBlock(HeadPos, Reg().FindState(TEXT("minecraft:moving_piston"), Properties).Get(0));
	Sim.ScheduleTick(HeadPos, 2);
	Set(Pos, { FChange(TEXT("extended"), TEXT("true")) });
	ExtendedAt.Add(Pos, Sim.GetGameTime());
	return true;
}

void FMcRedstone::Retract(const FIntVector& Pos, FMcStateId State, int32 Facing, bool bSticky)
{
	// A sticky piston let go within 3 ticks of starting to extend (a pulse of under 3 ticks) lets go of what it pushed:
	// it retracts empty-handed and the block stays.
	const int64* Started = ExtendedAt.Find(Pos);
	const bool bShortPulse = Started && Sim.GetGameTime() - *Started < 3;
	ExtendedAt.Remove(Pos);
	// The base shows as a moving piston of its type for 2 ticks, then is the piston again.
	{
		TMap<FName, FName> Properties;
		Properties.Add(TEXT("facing"), DirName[Facing]);
		Properties.Add(TEXT("type"), bSticky ? TEXT("sticky") : TEXT("normal"));
		MovingBlocks.Add(Pos, TPair<FMcStateId, int64>(With(State, { FChange(TEXT("extended"), TEXT("false")) }), Sim.GetGameTime() + 2));
		World.SetBlock(Pos, Reg().FindState(TEXT("minecraft:moving_piston"), Properties).Get(0));
		Sim.ScheduleTick(Pos, 2);
	}
	const FIntVector HeadPos = Pos + Vec[Facing];
	if (IsKind(HeadPos, PistonHead))
	{
		World.SetBlock(HeadPos, Air());
	}
	// Let go while the extension is still under way (a 1-tick pulse): the head never lands, it is gone the same tick, and a
	// sticky piston's pushed block lands at once instead of a tick later (reference run rs_world, rows of 1-tick pulses).
	else if (const TPair<FMcStateId, int64>* Head = MovingBlocks.Find(HeadPos); Head && KindOf(Head->Key) == PistonHead)
	{
		MovingBlocks.Remove(HeadPos);
		World.SetBlock(HeadPos, Air());
		const FIntVector Pushed = HeadPos + Vec[Facing];
		if (bSticky)
		{
			if (const TPair<FMcStateId, int64>* Carried = MovingBlocks.Find(Pushed))
			{
				const FMcStateId Landed = Carried->Key;
				MovingBlocks.Remove(Pushed);
				World.SetBlock(Pushed, Landed);
			}
		}
	}
	if (bSticky && !bShortPulse)
	{
		TArray<FIntVector> Move, Destroy;
		const FIntVector Start = Pos + Vec[Facing] * 2;
		if (ResolvePush(Pos, Start, Opposite(Facing), Move, Destroy))
		{
			// What can't be moved or broken stays where it is.
			Destroy.Reset();
			MoveBlocks(Move, Destroy, Opposite(Facing), Facing);
		}
	}
}

void FMcRedstone::EndTick()
{
	bInTick = false;
	for (int32 Index = 0; Index < PistonEvents.Num(); ++Index)
	{
		const FIntVector Pos = PistonEvents[Index];
		const FMcStateId State = World.GetBlock(Pos);
		if (KindOf(State) == Piston || KindOf(State) == StickyPiston)
		{
			TickPiston(Pos, State);
		}
	}
	PistonEvents.Reset();
}

void FMcRedstone::TickPiston(const FIntVector& Pos, FMcStateId State)
{
	const int32 Facing = FacingOf(State);
	const bool bSticky = KindOf(State) == StickyPiston;
	const bool bExtended = Flag(State, TEXT("extended"));
	const bool bShould = ShouldExtend(Pos, Facing);
	if (bShould && !bExtended)
	{
		Extend(Pos, State, Facing, bSticky);
	}
	else if (!bShould && bExtended)
	{
		Retract(Pos, State, Facing, bSticky);
	}
}

void FMcRedstone::TickPistonHead(const FIntVector& Pos, FMcStateId State)
{
	const int32 Facing = FacingOf(State);
	const FMcStateId Base = World.GetBlock(Pos + Vec[Opposite(Facing)]);
	const uint8 Kind = KindOf(Base);
	if (!((Kind == Piston || Kind == StickyPiston) && FacingOf(Base) == Facing && Flag(Base, TEXT("extended"))))
	{
		World.SetBlock(Pos, Air());
	}
}

void FMcRedstone::TickMovingPiston(const FIntVector& Pos, FMcStateId State)
{
	FMcStateId Landed = Air();
	if (const TPair<FMcStateId, int64>* Carried = MovingBlocks.Find(Pos))
	{
		// Another tick may be waiting here from before; the block lands when its 2 ticks are up.
		if (Carried->Value > Sim.GetGameTime())
		{
			Sim.ScheduleTick(Pos, static_cast<int32>(Carried->Value - Sim.GetGameTime()));
			return;
		}
		Landed = Carried->Key;
		MovingBlocks.Remove(Pos);
	}
	World.SetBlock(Pos, Landed);
}
