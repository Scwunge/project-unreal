// 4096 block states (one 16x16x16 section), stored as small indices into a palette.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

// Our own layout, not vanilla's:
//  - a section of one state stores no data at all (0 bits);
//  - otherwise each entry is a palette index of 4 to 8 bits, packed into 64-bit words without spanning two;
//  - past 256 distinct states the palette is dropped and entries are raw 16-bit state ids.
// The palette only grows. Saving (M9) can compact it.
class MCWORLD_API FMcPalettedContainer
{
public:
	static constexpr int32 NumEntries = 16 * 16 * 16;

	explicit FMcPalettedContainer(FMcStateId Initial = 0);

	FMcStateId Get(int32 Index) const;
	void Set(int32 Index, FMcStateId Value);
	void Fill(FMcStateId Value);

	int32 GetBitsPerEntry() const { return Bits; }
	int32 GetPaletteSize() const { return Palette.Num(); }
	bool IsDirect() const { return Bits == DirectBits; }
	bool IsUniform() const { return Bits == 0; }

	// Every entry, in index order. 8 KiB; used to snapshot a section for a worker thread.
	void CopyTo(TArrayView<FMcStateId> Out) const;

	SIZE_T GetAllocatedSize() const { return Palette.GetAllocatedSize() + Words.GetAllocatedSize(); }

private:
	static constexpr int32 MaxPaletteBits = 8;
	static constexpr int32 DirectBits = 16;

	uint32 ReadRaw(int32 Index) const;
	void WriteRaw(int32 Index, uint32 Raw);
	void Resize(int32 NewBits);

	TArray<FMcStateId> Palette;  // empty when direct
	TArray<uint64> Words;
	int32 Bits = 0;
	int32 EntriesPerWord = 0;
};
