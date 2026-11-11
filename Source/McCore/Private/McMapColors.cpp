#include "McMapColors.h"
#include "McBlockRegistry.h"
#include "Misc/ScopeLock.h"

namespace
{
#include "McMapColorData.inc"

	TArray<uint8> ByState;
	FCriticalSection Lock;

	void Build()
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		ByState.SetNumZeroed(Registry.NumStates());
		for (const FBlockColor& Entry : BlockColors)
		{
			if (const FMcBlock* Block = Registry.FindBlock(FName(Entry.Block)))
			{
				for (const FMcStateId State : Block->States)
				{
					ByState[State] = static_cast<uint8>(Entry.Color);
				}
			}
		}
		for (const FStateColor& Entry : StateColors)
		{
			const FMcBlock* Block = Registry.FindBlock(FName(Entry.Block));
			if (!Block)
			{
				continue;
			}
			TArray<FString> Pairs;
			FString(Entry.Properties).ParseIntoArray(Pairs, TEXT(","));
			for (const FMcStateId State : Block->States)
			{
				bool bMatch = true;
				for (const FString& Pair : Pairs)
				{
					FString Name, Value;
					Pair.Split(TEXT("="), &Name, &Value);
					bMatch = bMatch && Registry.GetPropertyValue(State, FName(*Name)) == FName(*Value);
				}
				if (bMatch)
				{
					ByState[State] = static_cast<uint8>(Entry.Color);
				}
			}
		}
	}
}

namespace McMapColors
{
	uint8 ColorOf(FMcStateId State)
	{
		FScopeLock Guard(&Lock);
		if (ByState.IsEmpty())
		{
			Build();
		}
		return ByState.IsValidIndex(State) ? ByState[State] : 0;
	}

	uint32 Argb(uint8 Pixel)
	{
		const int32 Color = Pixel >> 2;
		if (Color == 0)
		{
			return 0;
		}
		const uint32 Rgb = PaletteRgb[Color];
		const int32 Modifier = BrightnessModifier[Pixel & 3];
		const uint32 R = ((Rgb >> 16) & 255) * Modifier / 255;
		const uint32 G = ((Rgb >> 8) & 255) * Modifier / 255;
		const uint32 B = (Rgb & 255) * Modifier / 255;
		return 0xFF000000u | (R << 16) | (G << 8) | B;
	}
}
