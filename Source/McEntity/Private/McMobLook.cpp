#include "McMobLook.h"

namespace McMobLook
{
	namespace
	{
		// The orders of the lists in McEntityModel.cpp.
		enum EWolf { Pale, Ashen, Black, Chestnut, Rusty, Snowy, Spotted, Striped, Woods };
		enum ERabbit { Brown, White, BlackRabbit, Gold, Salt, Splotched, Toast };

		bool IsSnowy(const FString& Biome)
		{
			return Biome.Contains(TEXT("snowy")) || Biome.Contains(TEXT("ice")) || Biome.Contains(TEXT("frozen")) || Biome.Contains(TEXT("grove"));
		}
	}

	int32 GetCount(FName Kind)
	{
		static const TMap<FName, int32> Counts = {
			{ TEXT("minecraft:horse"), 35 }, { TEXT("minecraft:wolf"), 9 }, { TEXT("minecraft:llama"), 4 },
			{ TEXT("minecraft:fox"), 2 }, { TEXT("minecraft:rabbit"), 7 },
		};
		const int32* Found = Counts.Find(Kind);
		return Found ? *Found : 0;
	}

	int32 Pick(FName Kind, FName Biome, FRandomStream& Random)
	{
		const int32 Count = GetCount(Kind);
		if (Count == 0)
		{
			return 0;
		}
		const FString Name = Biome.ToString();
		if (Kind == TEXT("minecraft:wolf") && !Biome.IsNone())
		{
			static const TArray<TPair<const TCHAR*, int32>> Breeds = {
				{ TEXT("old_growth_pine_taiga"), Black }, { TEXT("old_growth_spruce_taiga"), Chestnut }, { TEXT("snowy_taiga"), Ashen },
				{ TEXT("taiga"), Pale }, { TEXT("sparse_jungle"), Rusty }, { TEXT("jungle"), Rusty }, { TEXT("grove"), Snowy },
				{ TEXT("savanna_plateau"), Spotted }, { TEXT("wooded_badlands"), Striped }, { TEXT("forest"), Woods },
			};
			for (const TPair<const TCHAR*, int32>& Breed : Breeds)
			{
				if (Name.EndsWith(Breed.Key))
				{
					return Breed.Value;
				}
			}
		}
		else if (Kind == TEXT("minecraft:fox") && !Biome.IsNone())
		{
			return Name.Contains(TEXT("snowy")) ? 1 : 0;
		}
		else if (Kind == TEXT("minecraft:rabbit") && !Biome.IsNone())
		{
			if (Name.EndsWith(TEXT("desert")))
			{
				return Gold;
			}
			if (IsSnowy(Name))
			{
				return White;
			}
			// Elsewhere half are brown, with salt and pepper, black and black and white among the rest.
			const int32 Roll = Random.RandRange(0, 99);
			return Roll < 50 ? Brown : Roll < 80 ? Salt : Roll < 90 ? BlackRabbit : Splotched;
		}
		return Random.RandRange(0, Count - 1);
	}
}
