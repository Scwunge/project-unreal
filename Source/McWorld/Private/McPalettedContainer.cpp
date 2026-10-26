#include "McPalettedContainer.h"

FMcPalettedContainer::FMcPalettedContainer(FMcStateId Initial)
{
	Palette.Add(Initial);
}

FMcStateId FMcPalettedContainer::Get(int32 Index) const
{
	checkSlow(Index >= 0 && Index < NumEntries);
	if (Bits == 0)
	{
		return Palette[0];
	}
	const uint32 Raw = ReadRaw(Index);
	return IsDirect() ? static_cast<FMcStateId>(Raw) : Palette[Raw];
}

void FMcPalettedContainer::Set(int32 Index, FMcStateId Value)
{
	checkSlow(Index >= 0 && Index < NumEntries);
	if (IsDirect())
	{
		WriteRaw(Index, Value);
		return;
	}
	if (Bits == 0 && Palette[0] == Value)
	{
		return;
	}

	int32 PaletteIndex = Palette.IndexOfByKey(Value);
	if (PaletteIndex == INDEX_NONE)
	{
		PaletteIndex = Palette.Add(Value);
		if (Palette.Num() > (1 << Bits))
		{
			const int32 NeededBits = FMath::Max(4, static_cast<int32>(FMath::CeilLogTwo(static_cast<uint32>(Palette.Num()))));
			if (NeededBits > MaxPaletteBits)
			{
				Resize(DirectBits);
				WriteRaw(Index, Value);
				return;
			}
			Resize(NeededBits);
		}
	}
	WriteRaw(Index, static_cast<uint32>(PaletteIndex));
}

void FMcPalettedContainer::Fill(FMcStateId Value)
{
	Palette.Reset();
	Palette.Add(Value);
	Words.Empty();
	Bits = 0;
	EntriesPerWord = 0;
}

void FMcPalettedContainer::CopyTo(TArrayView<FMcStateId> Out) const
{
	check(Out.Num() == NumEntries);
	if (Bits == 0)
	{
		for (FMcStateId& Entry : Out)
		{
			Entry = Palette[0];
		}
		return;
	}
	for (int32 Index = 0; Index < NumEntries; ++Index)
	{
		Out[Index] = Get(Index);
	}
}

uint32 FMcPalettedContainer::ReadRaw(int32 Index) const
{
	const int32 Shift = (Index % EntriesPerWord) * Bits;
	return static_cast<uint32>((Words[Index / EntriesPerWord] >> Shift) & ((uint64(1) << Bits) - 1));
}

void FMcPalettedContainer::WriteRaw(int32 Index, uint32 Raw)
{
	const int32 Shift = (Index % EntriesPerWord) * Bits;
	const uint64 Mask = ((uint64(1) << Bits) - 1) << Shift;
	uint64& Word = Words[Index / EntriesPerWord];
	Word = (Word & ~Mask) | ((uint64(Raw) << Shift) & Mask);
}

void FMcPalettedContainer::Resize(int32 NewBits)
{
	// Palette indices don't change when the palette grows, so read the old raw entries before repacking.
	TArray<uint32> OldRaw;
	OldRaw.SetNumZeroed(NumEntries);
	if (Bits > 0)
	{
		for (int32 Index = 0; Index < NumEntries; ++Index)
		{
			OldRaw[Index] = ReadRaw(Index);
		}
	}

	Bits = NewBits;
	EntriesPerWord = 64 / Bits;
	Words.Reset();
	Words.SetNumZeroed((NumEntries + EntriesPerWord - 1) / EntriesPerWord);

	if (IsDirect())
	{
		for (int32 Index = 0; Index < NumEntries; ++Index)
		{
			WriteRaw(Index, Palette[OldRaw[Index]]);
		}
		Palette.Empty();
	}
	else
	{
		for (int32 Index = 0; Index < NumEntries; ++Index)
		{
			WriteRaw(Index, OldRaw[Index]);
		}
	}
}
