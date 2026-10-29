#include "McVillager.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McCache.h"
#include "Serialization/Archive.h"

namespace
{
	struct FProfessionSite
	{
		const TCHAR* Profession;
		const TCHAR* Block;
	};

	// The job site of each profession (minecraft.wiki, "Villager#Professions").
	const FProfessionSite ProfessionSites[] = {
		{ TEXT("minecraft:armorer"), TEXT("minecraft:blast_furnace") },
		{ TEXT("minecraft:butcher"), TEXT("minecraft:smoker") },
		{ TEXT("minecraft:cartographer"), TEXT("minecraft:cartography_table") },
		{ TEXT("minecraft:cleric"), TEXT("minecraft:brewing_stand") },
		{ TEXT("minecraft:farmer"), TEXT("minecraft:composter") },
		{ TEXT("minecraft:fisherman"), TEXT("minecraft:barrel") },
		{ TEXT("minecraft:fletcher"), TEXT("minecraft:fletching_table") },
		{ TEXT("minecraft:leatherworker"), TEXT("minecraft:cauldron") },
		{ TEXT("minecraft:librarian"), TEXT("minecraft:lectern") },
		{ TEXT("minecraft:mason"), TEXT("minecraft:stonecutter") },
		{ TEXT("minecraft:shepherd"), TEXT("minecraft:loom") },
		{ TEXT("minecraft:toolsmith"), TEXT("minecraft:smithing_table") },
		{ TEXT("minecraft:weaponsmith"), TEXT("minecraft:grindstone") },
	};

	// Experience at which each level starts.
	const int32 LevelXp[] = { 0, 10, 70, 150, 250 };

	constexpr int32 SaveVersion = 1;
	// Ticks between looks at the job site.
	constexpr int32 ProfessionInterval = 20;
}

namespace McVillager
{
	const TArray<FName>& GetVariants()
	{
		static const TArray<FName> Variants = { TEXT("desert"), TEXT("jungle"), TEXT("plains"), TEXT("savanna"), TEXT("snow"), TEXT("swamp"), TEXT("taiga") };
		return Variants;
	}

	FName GetVariantOfBiome(FName Biome)
	{
		const FString Name = McCache::PathOf(Biome.ToString());
		if (Name.Contains(TEXT("desert")))
		{
			return TEXT("desert");
		}
		if (Name.Contains(TEXT("jungle")) || Name.Contains(TEXT("bamboo")))
		{
			return TEXT("jungle");
		}
		if (Name.Contains(TEXT("savanna")))
		{
			return TEXT("savanna");
		}
		if (Name.Contains(TEXT("snow")) || Name.Contains(TEXT("ice")) || Name.Contains(TEXT("frozen")) || Name == TEXT("grove") || Name == TEXT("snowy_slopes") || Name.Contains(TEXT("peaks")))
		{
			return TEXT("snow");
		}
		if (Name.Contains(TEXT("swamp")))
		{
			return TEXT("swamp");
		}
		if (Name.Contains(TEXT("taiga")))
		{
			return TEXT("taiga");
		}
		return TEXT("plains");
	}

	FName GetProfessionOfJobSite(FName Block)
	{
		const FString Name = Block.ToString();
		// A cauldron with anything in it still is one.
		const FString Plain = Name.EndsWith(TEXT("_cauldron")) ? FString(TEXT("minecraft:cauldron")) : Name;
		for (const FProfessionSite& Site : ProfessionSites)
		{
			if (Plain == Site.Block)
			{
				return FName(Site.Profession);
			}
		}
		return NAME_None;
	}

	FName GetJobSite(FName Profession)
	{
		for (const FProfessionSite& Site : ProfessionSites)
		{
			if (Profession == Site.Profession)
			{
				return FName(Site.Block);
			}
		}
		return NAME_None;
	}

	const TArray<FName>& GetProfessions()
	{
		static TArray<FName> Professions;
		if (Professions.IsEmpty())
		{
			for (const FProfessionSite& Site : ProfessionSites)
			{
				Professions.Add(FName(Site.Profession));
			}
		}
		return Professions;
	}

	int32 GetXpForLevel(int32 Level)
	{
		return LevelXp[FMath::Clamp(Level, 1, MaxLevel) - 1];
	}

	int32 GetLevelOfXp(int32 Xp)
	{
		int32 Level = 1;
		while (Level < MaxLevel && Xp >= LevelXp[Level])
		{
			++Level;
		}
		return Level;
	}

	FString GetLevelName(int32 Level)
	{
		static const TCHAR* Names[] = { TEXT("Novice"), TEXT("Apprentice"), TEXT("Journeyman"), TEXT("Expert"), TEXT("Master") };
		return Names[FMath::Clamp(Level, 1, MaxLevel) - 1];
	}

	bool FindNearestBlock(const FMcBlockWorld& World, const FIntVector& Origin, int32 Radius, int32 Height, TFunctionRef<bool(FName, const FIntVector&)> Is, FIntVector& Out)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		int64 Best = MAX_int64;
		for (int32 X = Origin.X - Radius; X <= Origin.X + Radius; ++X)
		{
			for (int32 Z = Origin.Z - Radius; Z <= Origin.Z + Radius; ++Z)
			{
				if (!World.HasChunk(FIntPoint(X >> 4, Z >> 4)))
				{
					continue;
				}
				for (int32 Y = Origin.Y - Height; Y <= Origin.Y + Height; ++Y)
				{
					const FMcStateId State = World.GetBlock(FIntVector(X, Y, Z));
					if (!Registry.IsValidState(State) || Registry.IsAir(State))
					{
						continue;
					}
					const int64 Distance = static_cast<int64>(X - Origin.X) * (X - Origin.X) + static_cast<int64>(Y - Origin.Y) * (Y - Origin.Y) + static_cast<int64>(Z - Origin.Z) * (Z - Origin.Z);
					if (Distance < Best && Is(Registry.GetBlockOf(State).Name, FIntVector(X, Y, Z)))
					{
						Best = Distance;
						Out = FIntVector(X, Y, Z);
					}
				}
			}
		}
		return Best != MAX_int64;
	}
}

const FName& FMcVillager::StaticType()
{
	static const FName Name(TEXT("minecraft:villager"));
	return Name;
}

FMcVillager::FMcVillager(const FMcMobType& InType)
	: FMcMob(InType)
	, Variant(TEXT("plains"))
	, Profession(NoProfession())
{
}

void FMcVillager::SetBaby(bool bInBaby)
{
	bBaby = bInBaby;
	const float Scale = bBaby ? 0.5f : 1.0f;
	Body.Width = Type.Width * Scale;
	Body.Height = Type.Height * Scale;
	GrowthTicks = bBaby ? (GrowthTicks > 0 ? GrowthTicks : McVillager::BabyTicks) : 0;
}

void FMcVillager::SetProfession(FName InProfession)
{
	Profession = InProfession.IsNone() ? NoProfession() : InProfession;
	if (!HasProfession())
	{
		bHasJobSite = false;
	}
}

bool FMcVillager::IsClaimedByOther(FMcEntityManager& Manager, const FIntVector& Pos) const
{
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (Entity.Get() != this && !Entity->bRemoved && Entity->GetType() == StaticType())
		{
			const FMcVillager& Other = static_cast<const FMcVillager&>(*Entity);
			if (Other.bHasJobSite && Other.JobSite == Pos)
			{
				return true;
			}
		}
	}
	return false;
}

void FMcVillager::UpdateProfession(FMcEntityManager& Manager)
{
	const FMcBlockWorld& World = Manager.GetWorld();
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FIntVector Here(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y), FMath::FloorToInt32(Body.Position.Z));
	if (bHasJobSite)
	{
		// Still there? A villager that has traded keeps its profession (and level) without its job site, and looks for one.
		const FMcStateId State = World.GetBlock(JobSite);
		if (Registry.IsValidState(State) && McVillager::GetProfessionOfJobSite(Registry.GetBlockOf(State).Name) == Profession)
		{
			return;
		}
		bHasJobSite = false;
		if (Xp == 0 && Level == 1)
		{
			Profession = NoProfession();
		}
		return;
	}
	if (bBaby || Profession == NitwitProfession())
	{
		return;
	}
	// A jobless villager takes the nearest job site nobody has claimed; one that kept its profession only takes one of its own.
	const bool bHasKept = HasProfession();
	const FName Wanted = Profession;
	FIntVector Found;
	if (!McVillager::FindNearestBlock(World, Here, McVillager::SearchRadius, McVillager::SearchHeight, [&](FName Block, const FIntVector& Pos)
		{
			const FName Of = McVillager::GetProfessionOfJobSite(Block);
			return !Of.IsNone() && (!bHasKept || Of == Wanted) && !IsClaimedByOther(Manager, Pos);
		}, Found))
	{
		return;
	}
	Profession = McVillager::GetProfessionOfJobSite(Registry.GetBlockOf(World.GetBlock(Found)).Name);
	ClaimJobSite(Found);
}

void FMcVillager::Tick(FMcEntityManager& Manager)
{
	if (bBaby && --GrowthTicks <= 0)
	{
		SetBaby(false);
	}
	// Spread over the villagers by id, so they don't all look in the same tick.
	if (Age % ProfessionInterval == Id % ProfessionInterval)
	{
		UpdateProfession(Manager);
	}
	FMcMob::Tick(Manager);
}

bool FMcVillager::Serialize(FArchive& Ar)
{
	if (!FMcMob::Serialize(Ar))
	{
		return false;
	}
	int32 Version = SaveVersion;
	Ar << Version;
	if (Ar.IsLoading() && Version != SaveVersion)
	{
		return false;
	}
	FString VariantText = Variant.ToString();
	FString ProfessionText = Profession.ToString();
	bool bSavedBaby = bBaby;
	Ar << VariantText << ProfessionText << Level << Xp << bSavedBaby << GrowthTicks << bHasJobSite << JobSite.X << JobSite.Y << JobSite.Z;
	if (Ar.IsLoading())
	{
		if (Level < 1 || Level > McVillager::MaxLevel || Xp < 0 || VariantText.IsEmpty() || ProfessionText.IsEmpty() || Ar.IsError())
		{
			return false;
		}
		Variant = FName(*VariantText);
		Profession = FName(*ProfessionText);
		const int32 Saved = GrowthTicks;
		SetBaby(bSavedBaby);
		GrowthTicks = bSavedBaby ? FMath::Max(1, Saved) : 0;
	}
	return !Ar.IsError();
}
