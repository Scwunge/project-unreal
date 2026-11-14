#include "McCopper.h"
#include "McBlockRegistry.h"

namespace
{
	const TCHAR* const Prefixes[4] = { TEXT(""), TEXT("exposed_"), TEXT("weathered_"), TEXT("oxidized_") };

	// "copper_block" is "copper" in the exposed names (exposed_copper), every other block just takes the prefix.
	bool Split(FName Block, int32& OutAge, FString& OutCore)
	{
		FString Name = Block.ToString();
		if (!Name.RemoveFromStart(TEXT("minecraft:")) || Name.StartsWith(TEXT("waxed_")))
		{
			return false;
		}
		OutAge = 0;
		for (int32 Age = 1; Age < 4; ++Age)
		{
			if (Name.RemoveFromStart(Prefixes[Age]))
			{
				OutAge = Age;
				break;
			}
		}
		if (OutAge == 0 && Name == TEXT("copper_block"))
		{
			Name = TEXT("copper");
		}
		OutCore = Name;
		return true;
	}

	FName Join(int32 Age, const FString& Core)
	{
		const FString Name = Age == 0 && Core == TEXT("copper") ? FString(TEXT("copper_block")) : FString(Prefixes[Age]) + Core;
		return FName(*(FString(TEXT("minecraft:")) + Name));
	}

	bool Exists(FName Block)
	{
		return FMcBlockRegistry::Get().FindDefaultState(Block).IsSet();
	}
}

bool McCopper::GetAge(FName Block, int32& OutAge)
{
	FString Core;
	if (!Split(Block, OutAge, Core))
	{
		return false;
	}
	// A family has all four steps (this keeps copper_ore, raw_copper_block and the like out).
	return Exists(Join(0, Core)) && Exists(Join(1, Core)) && Exists(Join(2, Core)) && Exists(Join(3, Core));
}

FName McCopper::GetNextAge(FName Block)
{
	int32 Age;
	FString Core;
	return GetAge(Block, Age) && Age < 3 && Split(Block, Age, Core) ? Join(Age + 1, Core) : NAME_None;
}

FName McCopper::GetPreviousAge(FName Block)
{
	int32 Age;
	FString Core;
	return GetAge(Block, Age) && Age > 0 && Split(Block, Age, Core) ? Join(Age - 1, Core) : NAME_None;
}

FName McCopper::GetWaxed(FName Block)
{
	int32 Age;
	if (!GetAge(Block, Age))
	{
		return NAME_None;
	}
	FString Name = Block.ToString();
	Name.RemoveFromStart(TEXT("minecraft:"));
	const FName Waxed(*(FString(TEXT("minecraft:waxed_")) + Name));
	return Exists(Waxed) ? Waxed : NAME_None;
}

FName McCopper::GetUnwaxed(FName Block)
{
	FString Name = Block.ToString();
	if (!Name.RemoveFromStart(TEXT("minecraft:waxed_")))
	{
		return NAME_None;
	}
	const FName Plain(*(FString(TEXT("minecraft:")) + Name));
	int32 Age;
	return GetAge(Plain, Age) ? Plain : NAME_None;
}

float McCopper::GetWeatherFactor(int32 Age, int32 Higher, int32 Same)
{
	const float F = static_cast<float>(Higher + 1) / static_cast<float>(Higher + Same + 1);
	return F * F * (Age == 0 ? 0.75f : 1.0f);
}
