#include "McNoteBlock.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McSimulation.h"
#include "McSound.h"

namespace
{
	struct FInstrumentBlock
	{
		const TCHAR* Name;
		const TCHAR* Instrument;
	};
	// Every block measured to give something other than the harp (name without "minecraft:").
#include "McNoteTable.inc"

	const TMap<FName, FName>& GetTable()
	{
		static const TMap<FName, FName> Table = []
		{
			TMap<FName, FName> Map;
			for (const FInstrumentBlock& Entry : Blocks)
			{
				Map.Add(FName(Entry.Name), FName(Entry.Instrument));
			}
			return Map;
		}();
		return Table;
	}

	FString Short(FName Block)
	{
		FString Name = Block.ToString();
		Name.RemoveFromStart(TEXT("minecraft:"));
		return Name;
	}
}

FName McNoteBlock::GetInstrumentBelow(FName Block)
{
	const TMap<FName, FName>& Table = GetTable();
	FString Name = Short(Block);
	if (const FName* Found = Table.Find(FName(*Name)))
	{
		return *Found;
	}
	// Made of another block: its stairs, buttons and plates sound like it.
	for (const TCHAR* Suffix : { TEXT("_stairs"), TEXT("_button"), TEXT("_pressure_plate") })
	{
		if (Name.EndsWith(Suffix))
		{
			const FString Base = Name.LeftChop(FCString::Strlen(Suffix));
			for (const FString& Candidate : { Base, Base + TEXT("_slab"), Base + TEXT("s"), Base + TEXT("_planks") })
			{
				if (const FName* Found = Table.Find(FName(*Candidate)))
				{
					return *Found;
				}
			}
		}
	}
	return FName(TEXT("harp"));
}

FName McNoteBlock::GetInstrumentAbove(FName Block)
{
	const FString Name = Short(Block);
	if (Name == TEXT("zombie_head") || Name == TEXT("zombie_wall_head"))
	{
		return FName(TEXT("zombie"));
	}
	if (Name == TEXT("skeleton_skull") || Name == TEXT("skeleton_wall_skull"))
	{
		return FName(TEXT("skeleton"));
	}
	if (Name == TEXT("creeper_head") || Name == TEXT("creeper_wall_head"))
	{
		return FName(TEXT("creeper"));
	}
	if (Name == TEXT("dragon_head") || Name == TEXT("dragon_wall_head"))
	{
		return FName(TEXT("dragon"));
	}
	if (Name == TEXT("wither_skeleton_skull") || Name == TEXT("wither_skeleton_wall_skull"))
	{
		return FName(TEXT("wither_skeleton"));
	}
	if (Name == TEXT("piglin_head") || Name == TEXT("piglin_wall_head"))
	{
		return FName(TEXT("piglin"));
	}
	if (Name == TEXT("player_head") || Name == TEXT("player_wall_head"))
	{
		return FName(TEXT("custom_head"));
	}
	return NAME_None;
}

float McNoteBlock::GetPitch(int32 Note)
{
	return FMath::Pow(2.0f, (FMath::Clamp(Note, 0, NumNotes - 1) - 12) / 12.0f);
}

bool McNoteBlock::IsHeadInstrument(FName Instrument)
{
	static const TSet<FName> Heads = { FName(TEXT("zombie")), FName(TEXT("skeleton")), FName(TEXT("creeper")), FName(TEXT("dragon")),
		FName(TEXT("wither_skeleton")), FName(TEXT("piglin")), FName(TEXT("custom_head")) };
	return Heads.Contains(Instrument);
}

FMcSoundEvent McNoteBlock::GetSound(FName Instrument, int32 Note, const FVector& Position)
{
	FMcSoundEvent Event;
	Event.Position = Position;
	Event.Volume = 3.0f;
	if (Instrument == TEXT("custom_head"))
	{
		return Event;
	}
	if (IsHeadInstrument(Instrument))
	{
		const FString Voice = Instrument == TEXT("dragon") ? FString(TEXT("ender_dragon")) : Instrument.ToString();
		Event.Name = FName(*(TEXT("block.note_block.imitate.") + Voice));
		Event.Pitch = Instrument == TEXT("creeper") ? 0.5f : 1.0f;
		if (Instrument == TEXT("piglin"))
		{
			Event.Volume = 1.98f;
		}
		return Event;
	}
	Event.Name = FName(*(TEXT("block.note_block.") + Instrument.ToString()));
	Event.Pitch = GetPitch(Note);
	return Event;
}

FColor McNoteBlock::GetNoteColour(int32 Note)
{
	static const uint32 Table[NumNotes] = { 0x59E800, 0x82CE00, 0xACAC00, 0xCE8400, 0xE85900, 0xF92E00, 0xFF0606, 0xF9002E, 0xE80059, 0xCE0082,
		0xAC00AC, 0x8200CE, 0x5900E8, 0x2E00F9, 0x0606FF, 0x002EF9, 0x0059E8, 0x0082CE, 0x00ACAC, 0x00CE82, 0x00E859, 0x00F92E, 0x06FF06,
		0x2EF900, 0x59E800 };
	const uint32 Value = Table[FMath::Clamp(Note, 0, NumNotes - 1)];
	return FColor((Value >> 16) & 0xFF, (Value >> 8) & 0xFF, Value & 0xFF, 255);
}

void FMcSimulation::RefreshNoteBlock(const FIntVector& Pos)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId State = World.GetBlock(Pos);
	const FMcBlock& Block = Registry.GetBlockOf(State);
	if (Block.Name != TEXT("minecraft:note_block"))
	{
		return;
	}
	// A head on top wins over what is below.
	FName Instrument = McNoteBlock::GetInstrumentAbove(Registry.GetBlockOf(World.GetBlock(Pos + FIntVector(0, 1, 0))).Name);
	if (Instrument.IsNone())
	{
		Instrument = McNoteBlock::GetInstrumentBelow(Registry.GetBlockOf(World.GetBlock(Pos - FIntVector(0, 1, 0))).Name);
	}
	if (Registry.GetPropertyValue(State, TEXT("instrument")) == Instrument)
	{
		return;
	}
	TMap<FName, FName> Properties;
	Properties.Add(TEXT("instrument"), Instrument);
	Properties.Add(TEXT("note"), Registry.GetPropertyValue(State, TEXT("note")));
	Properties.Add(TEXT("powered"), Registry.GetPropertyValue(State, TEXT("powered")));
	const TOptional<FMcStateId> Next = Registry.FindState(Block.Name, Properties);
	if (Next.IsSet())
	{
		World.SetBlock(Pos, Next.GetValue());
	}
}

bool FMcSimulation::PlayNote(const FIntVector& Pos)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId State = World.GetBlock(Pos);
	if (Registry.GetBlockOf(State).Name != TEXT("minecraft:note_block"))
	{
		return false;
	}
	const FMcStateId Above = World.GetBlock(Pos + FIntVector(0, 1, 0));
	if (!Registry.IsAir(Above) && McNoteBlock::GetInstrumentAbove(Registry.GetBlockOf(Above).Name).IsNone())
	{
		return false;
	}
	const int32 Note = FCString::Atoi(*Registry.GetPropertyValue(State, TEXT("note")).ToString());
	const FMcSoundEvent Event = McNoteBlock::GetSound(Registry.GetPropertyValue(State, TEXT("instrument")), Note, FVector(Pos) + FVector(0.5));
	PlaySound(Event);
	if (OnNoteParticle && !McNoteBlock::IsHeadInstrument(Registry.GetPropertyValue(State, TEXT("instrument"))))
	{
		OnNoteParticle(FVector(Pos) + FVector(0.5, 1.2, 0.5), Note);
	}
	return true;
}

bool FMcSimulation::TuneNote(const FIntVector& Pos)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId State = World.GetBlock(Pos);
	const FMcBlock& Block = Registry.GetBlockOf(State);
	if (Block.Name != TEXT("minecraft:note_block"))
	{
		return false;
	}
	const int32 Note = (FCString::Atoi(*Registry.GetPropertyValue(State, TEXT("note")).ToString()) + 1) % McNoteBlock::NumNotes;
	TMap<FName, FName> Properties;
	Properties.Add(TEXT("instrument"), Registry.GetPropertyValue(State, TEXT("instrument")));
	Properties.Add(TEXT("note"), FName(*FString::FromInt(Note)));
	Properties.Add(TEXT("powered"), Registry.GetPropertyValue(State, TEXT("powered")));
	const TOptional<FMcStateId> Next = Registry.FindState(Block.Name, Properties);
	if (Next.IsSet())
	{
		World.SetBlock(Pos, Next.GetValue());
	}
	PlayNote(Pos);
	return true;
}
