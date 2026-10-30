#include "McStructureSpawns.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

FMcStructureSpawns& FMcStructureSpawns::Get()
{
	static FMcStructureSpawns Instance;
	return Instance;
}

void FMcStructureSpawns::SetRecording(bool bOn)
{
	FScopeLock Guard(&Lock);
	bRecording = bOn;
	Zones.Reset();
	Mobs.Reset();
	MobKeys.Reset();
}

void FMcStructureSpawns::AddZone(const FMcStructureZone& Zone)
{
	FScopeLock Guard(&Lock);
	if (!bRecording)
	{
		return;
	}
	for (const FMcStructureZone& Each : Zones)
	{
		if (Each.Structure == Zone.Structure && Each.Min == Zone.Min && Each.Max == Zone.Max)
		{
			return;
		}
	}
	Zones.Add(Zone);
}

void FMcStructureSpawns::AddMob(const FString& Key, const FMcStructureMob& Mob)
{
	FScopeLock Guard(&Lock);
	if (!bRecording)
	{
		return;
	}
	bool bAlready = false;
	MobKeys.Add(Key + TEXT("/") + Mob.Kind.ToString(), &bAlready);
	if (!bAlready)
	{
		Mobs.Add(Mob);
	}
}

bool FMcStructureSpawns::FindMonsterZone(const FIntVector& Cell, FMcStructureZone& Out) const
{
	FScopeLock Guard(&Lock);
	for (const FMcStructureZone& Each : Zones)
	{
		if (!Each.Monsters.IsEmpty() && Each.Contains(Cell))
		{
			Out = Each;
			return true;
		}
	}
	return false;
}

namespace
{
	void WriteEntries(FArchive& Writer, const TArray<FMcSpawnEntry>& Entries)
	{
		int32 Num = Entries.Num();
		Writer << Num;
		for (const FMcSpawnEntry& Entry : Entries)
		{
			FString Kind = Entry.Kind.ToString();
			int32 Weight = Entry.Weight;
			int32 MinCount = Entry.MinCount;
			int32 MaxCount = Entry.MaxCount;
			Writer << Kind << Weight << MinCount << MaxCount;
		}
	}

	void ReadEntries(FArchive& Reader, TArray<FMcSpawnEntry>& Entries)
	{
		int32 Num = 0;
		Reader << Num;
		for (int32 Index = 0; Index < Num && !Reader.IsError() && Num < 64; ++Index)
		{
			FString Kind;
			FMcSpawnEntry Entry;
			Reader << Kind << Entry.Weight << Entry.MinCount << Entry.MaxCount;
			Entry.Kind = FName(*Kind);
			Entries.Add(Entry);
		}
	}
}

TArray<uint8> FMcStructureSpawns::EncodeChunk(FIntPoint Chunk) const
{
	TArray<uint8> Bytes;
	FScopeLock Guard(&Lock);
	TArray<const FMcStructureZone*> Reaching;
	for (const FMcStructureZone& Each : Zones)
	{
		if (Each.Max.X >= Chunk.X * 16 && Each.Min.X <= Chunk.X * 16 + 15 && Each.Max.Z >= Chunk.Y * 16 && Each.Min.Z <= Chunk.Y * 16 + 15)
		{
			Reaching.Add(&Each);
		}
	}
	if (Reaching.IsEmpty())
	{
		return Bytes;
	}
	FMemoryWriter Writer(Bytes);
	int32 Num = Reaching.Num();
	Writer << Num;
	for (const FMcStructureZone* Each : Reaching)
	{
		FString Structure = Each->Structure.ToString();
		FIntVector Min = Each->Min;
		FIntVector Max = Each->Max;
		Writer << Structure << Min << Max;
		WriteEntries(Writer, Each->Monsters);
		WriteEntries(Writer, Each->Creatures);
	}
	return Bytes;
}

void FMcStructureSpawns::DecodeChunk(TConstArrayView<uint8> Bytes)
{
	if (Bytes.IsEmpty())
	{
		return;
	}
	const TArray<uint8> Copy(Bytes.GetData(), Bytes.Num());
	FMemoryReader Reader(Copy);
	int32 Num = 0;
	Reader << Num;
	for (int32 Index = 0; Index < Num && !Reader.IsError() && Num < 64; ++Index)
	{
		FMcStructureZone Zone;
		FString Structure;
		Reader << Structure << Zone.Min << Zone.Max;
		Zone.Structure = FName(*Structure);
		ReadEntries(Reader, Zone.Monsters);
		ReadEntries(Reader, Zone.Creatures);
		if (!Reader.IsError())
		{
			AddZone(Zone);
		}
	}
}

void FMcStructureSpawns::TakeMobs(TFunctionRef<bool(const FMcStructureMob&)> Take)
{
	TArray<FMcStructureMob> Waiting;
	{
		FScopeLock Guard(&Lock);
		Waiting = Mobs;
	}
	TArray<FMcStructureMob> Left;
	for (const FMcStructureMob& Mob : Waiting)
	{
		if (!Take(Mob))
		{
			Left.Add(Mob);
		}
	}
	FScopeLock Guard(&Lock);
	// Mobs added while Take ran are kept after the ones left.
	TArray<FMcStructureMob> Added;
	for (int32 Index = Waiting.Num(); Index < Mobs.Num(); ++Index)
	{
		Added.Add(Mobs[Index]);
	}
	Mobs = Left;
	Mobs.Append(Added);
}

void FMcStructureSpawns::Clear()
{
	FScopeLock Guard(&Lock);
	Zones.Reset();
	Mobs.Reset();
	MobKeys.Reset();
}
