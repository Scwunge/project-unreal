#include "McVillager.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McCache.h"
#include "McRandom.h"
#include "Serialization/Archive.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

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

	// 2 added the bed.
	constexpr int32 SaveVersion = 5;
	// What a player's standing with a villager runs between.
	constexpr int32 MinReputation = -200;
	constexpr int32 MaxReputation = 100;
	// Ticks between looks at the job site.
	constexpr int32 ProfessionInterval = 20;
}

namespace McVillager
{
	bool IsFeared(FName Kind)
	{
		static const TSet<FName> Feared = { TEXT("minecraft:zombie"), TEXT("minecraft:zombie_villager"), TEXT("minecraft:husk"), TEXT("minecraft:drowned"), TEXT("minecraft:pillager"), TEXT("minecraft:vindicator"), TEXT("minecraft:evoker"), TEXT("minecraft:illusioner"), TEXT("minecraft:vex"), TEXT("minecraft:ravager") };
		return Feared.Contains(Kind);
	}

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

	EActivity GetActivityAt(int32 TimeOfDay, bool bAlarm)
	{
		if (bAlarm)
		{
			return EActivity::Alarm;
		}
		if (TimeOfDay >= RestStart)
		{
			return EActivity::Rest;
		}
		if (TimeOfDay >= MeetStart && TimeOfDay < FreeStart)
		{
			return EActivity::Meet;
		}
		if (TimeOfDay >= WorkStart && TimeOfDay < MeetStart)
		{
			return EActivity::Work;
		}
		return EActivity::Idle;
	}

	bool IsBed(FName Block)
	{
		return McCache::PathOf(Block.ToString()).EndsWith(TEXT("_bed"));
	}

	bool IsBell(FName Block)
	{
		return Block == TEXT("minecraft:bell");
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
	const FName Old = Profession;
	Profession = InProfession.IsNone() ? NoProfession() : InProfession;
	if (Profession != Old)
	{
		Offers.Reset();
	}
	if (!HasProfession())
	{
		bHasJobSite = false;
	}
}

TArray<McTrading::FOffer>& FMcVillager::GetOffers()
{
	if (Offers.IsEmpty() && HasProfession() && !bBaby)
	{
		FMcRandom Rolls(static_cast<uint64>(Id) * 7919u + 17u);
		for (int32 Reached = 1; Reached <= Level; ++Reached)
		{
			McTrading::RollLevel(Profession, Reached, Variant, Rolls, Offers);
		}
	}
	return Offers;
}

void FMcVillager::AddTradeXp(int32 Amount)
{
	Xp += FMath::Max(0, Amount);
	const int32 Reached = McVillager::GetLevelOfXp(Xp);
	if (Reached > Level)
	{
		FMcRandom Rolls(static_cast<uint64>(Id) * 104729u + Reached);
		for (int32 Next = Level + 1; Next <= Reached; ++Next)
		{
			McTrading::RollLevel(Profession, Next, Variant, Rolls, Offers);
		}
		Level = Reached;
	}
}

int32 FMcVillager::GetReputation(const FString& Player) const
{
	const int32* Standing = Reputation.Find(Player);
	return Standing ? *Standing : 0;
}

void FMcVillager::AddReputation(const FString& Player, int32 Amount)
{
	int32& Standing = Reputation.FindOrAdd(Player);
	Standing = FMath::Clamp(Standing + Amount, MinReputation, MaxReputation);
}

void FMcVillager::Restock()
{
	for (McTrading::FOffer& Offer : Offers)
	{
		McTrading::Restock(Offer);
	}
	++RestocksToday;
	LastRestockAge = Age;
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
			Offers.Reset();
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
	SetProfession(McVillager::GetProfessionOfJobSite(Registry.GetBlockOf(World.GetBlock(Found)).Name));
	ClaimJobSite(Found);
}

bool FMcVillager::IsBedClaimedByOther(FMcEntityManager& Manager, const FIntVector& Pos) const
{
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (Entity.Get() != this && !Entity->bRemoved && Entity->GetType() == StaticType())
		{
			const FMcVillager& Other = static_cast<const FMcVillager&>(*Entity);
			// The two halves of a bed are one bed.
			if (Other.bHasBed && Other.Bed.Y == Pos.Y && FMath::Abs(Other.Bed.X - Pos.X) + FMath::Abs(Other.Bed.Z - Pos.Z) <= 1)
			{
				return true;
			}
		}
	}
	return false;
}

bool FMcVillager::WalkTo(const FVector& Place)
{
	const double Across = FVector::Dist2D(Place, Body.Position);
	if (Across < 2.0 && FMath::Abs(Place.Y - Body.Position.Y) < 3.0)
	{
		bStayPut = true;
		Goal.Reset();
		return true;
	}
	bStayPut = false;
	Goal = Place;
	return false;
}

void FMcVillager::UpdateRoutine(FMcEntityManager& Manager)
{
	const FMcBlockWorld& World = Manager.GetWorld();
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FIntVector Here(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y), FMath::FloorToInt32(Body.Position.Z));
	const McVillager::EActivity Now = McVillager::GetActivityAt(Manager.TimeOfDay, Manager.IsAlarmSounding());
	if (Manager.TimeOfDay < McVillager::WorkStart)
	{
		RestocksToday = 0;
	}
	// At its job site it restocks, up to twice a day, at least 2400 ticks apart.
	if (Now == McVillager::EActivity::Work && bHasJobSite && !Offers.IsEmpty() && RestocksToday < 2 && (RestocksToday == 0 || Age - LastRestockAge >= 2400))
	{
		Restock();
	}
	// From dusk on, doors left open are shut behind the villagers.
	if (!Manager.bDaylight && !bBaby)
	{
		CloseDoorsNearby(Manager);
	}
	if (Now != Activity)
	{
		Activity = Now;
		bHasBell = false;
		bStayPut = false;
		Goal.Reset();
	}
	// A baby plays, and a villager hurt in its sleep gets up.
	if (bBaby || HurtTime > 0)
	{
		bSleeping = false;
		bStayPut = false;
		return;
	}
	const auto CenterOf = [](const FIntVector& Cell) { return FVector(Cell.X + 0.5, Cell.Y, Cell.Z + 0.5); };
	bool bWalking = false;
	switch (Activity)
	{
	case McVillager::EActivity::Work:
		if (bHasJobSite)
		{
			WalkTo(CenterOf(JobSite));
			bWalking = true;
		}
		break;
	case McVillager::EActivity::Meet:
		if (!bHasBell)
		{
			bHasBell = McVillager::FindNearestBlock(World, Here, McVillager::BellRadius, McVillager::SearchHeight, [](FName Block, const FIntVector&) { return McVillager::IsBell(Block); }, Bell);
		}
		if (bHasBell)
		{
			WalkTo(CenterOf(Bell));
			bWalking = true;
		}
		break;
	case McVillager::EActivity::Alarm:
		WalkTo(Manager.GetAlarmPos());
		bWalking = true;
		break;
	case McVillager::EActivity::Rest:
	{
		if (bHasBed)
		{
			const FMcStateId State = World.GetBlock(Bed);
			if (!Registry.IsValidState(State) || !McVillager::IsBed(Registry.GetBlockOf(State).Name))
			{
				ReleaseBed();
			}
		}
		if (!bHasBed)
		{
			FIntVector Found;
			if (McVillager::FindNearestBlock(World, Here, McVillager::SearchRadius, McVillager::SearchHeight, [&](FName Block, const FIntVector& Pos) { return McVillager::IsBed(Block) && !IsBedClaimedByOther(Manager, Pos); }, Found))
			{
				ClaimBed(Found);
			}
		}
		if (bHasBed)
		{
			bSleeping = WalkTo(CenterOf(Bed));
			bWalking = true;
		}
		break;
	}
	default:
		break;
	}
	if (!bWalking)
	{
		bStayPut = false;
	}
	if (Activity != McVillager::EActivity::Rest)
	{
		bSleeping = false;
	}
}

void FMcVillager::CloseDoorsNearby(FMcEntityManager& Manager)
{
	if (!Manager.OnSetBlock)
	{
		return;
	}
	const FMcBlockWorld& World = Manager.GetWorld();
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FIntVector Here(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y), FMath::FloorToInt32(Body.Position.Z));
	const auto Shut = [&](const FIntVector& Cell)
	{
		const FMcStateId State = World.GetBlock(Cell);
		if (!Registry.IsValidState(State))
		{
			return false;
		}
		const FString Name = Registry.GetBlockOf(State).Name.ToString();
		// Iron doors answer only to redstone.
		if (!Name.EndsWith(TEXT("_door")) || Name.Contains(TEXT("iron")) || Registry.GetPropertyValue(State, TEXT("open")) != FName(TEXT("true")))
		{
			return false;
		}
		const TOptional<FMcStateId> Closed = Registry.ParseState(Registry.Describe(State).Replace(TEXT("open=true"), TEXT("open=false")));
		if (!Closed.IsSet())
		{
			return false;
		}
		Manager.OnSetBlock(Cell, Closed.GetValue());
		return true;
	};
	for (int32 X = -3; X <= 3; ++X)
	{
		for (int32 Z = -3; Z <= 3; ++Z)
		{
			for (int32 Y = -1; Y <= 2; ++Y)
			{
				const FIntVector Cell = Here + FIntVector(X, Y, Z);
				if (Shut(Cell))
				{
					// A door is two blocks, each with its own state.
					const FMcStateId State = World.GetBlock(Cell);
					const bool bLower = Registry.GetPropertyValue(State, TEXT("half")) == FName(TEXT("lower"));
					Shut(Cell + FIntVector(0, bLower ? 1 : -1, 0));
				}
			}
		}
	}
}

int32 McVillager::GetFoodValue(FName Item)
{
	if (Item == FName(TEXT("minecraft:bread")))
	{
		return 4;
	}
	if (Item == FName(TEXT("minecraft:carrot")) || Item == FName(TEXT("minecraft:potato")) || Item == FName(TEXT("minecraft:beetroot")))
	{
		return 1;
	}
	return 0;
}

void FMcVillager::PickUpFood(FMcEntityManager& Manager)
{
	if (bBaby || FoodPoints >= McVillager::MaxFood)
	{
		return;
	}
	for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
	{
		if (Other->bRemoved || Other->GetType() != FMcItemEntity::StaticType() || FVector::DistSquared(Other->Body.Position, Body.Position) > 2.25)
		{
			continue;
		}
		FMcItemEntity& Item = static_cast<FMcItemEntity&>(*Other);
		const int32 Value = McVillager::GetFoodValue(Item.Stack.Item);
		if (Value <= 0 || Item.PickupDelay > 0 || Item.Stack.Count <= 0)
		{
			continue;
		}
		// As much of the stack as it has room for.
		const int32 Eaten = FMath::Min(Item.Stack.Count, (McVillager::MaxFood - FoodPoints + Value - 1) / Value);
		FoodPoints = FMath::Min(McVillager::MaxFood, FoodPoints + Eaten * Value);
		Item.Stack.Count -= Eaten;
		if (Item.Stack.Count <= 0)
		{
			Item.bRemoved = true;
		}
		if (FoodPoints >= McVillager::MaxFood)
		{
			return;
		}
	}
}

void FMcVillager::TryBreed(FMcEntityManager& Manager)
{
	if (!IsWilling() || !Manager.OnVillagerBred)
	{
		return;
	}
	// A mate in range that is willing too.
	FMcVillager* Mate = nullptr;
	for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
	{
		if (Other.Get() != this && !Other->bRemoved && Other->GetType() == StaticType()
			&& FVector::DistSquared(Other->Body.Position, Body.Position) < FMath::Square(McVillager::BreedRange)
			&& static_cast<FMcVillager&>(*Other).IsWilling())
		{
			Mate = static_cast<FMcVillager*>(Other.Get());
			break;
		}
	}
	if (!Mate)
	{
		return;
	}
	// And a bed nobody sleeps in, for the child.
	const FMcBlockWorld& World = Manager.GetWorld();
	const FIntVector Here(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y), FMath::FloorToInt32(Body.Position.Z));
	FIntVector Free;
	if (!McVillager::FindNearestBlock(World, Here, McVillager::SearchRadius, McVillager::SearchHeight, [&](FName Block, const FIntVector& Pos) { return McVillager::IsBed(Block) && !IsBedClaimedByOther(Manager, Pos) && !Mate->IsBedClaimedByOther(Manager, Pos) && !(bHasBed && Bed == Pos) && !(Mate->bHasBed && Mate->Bed == Pos); }, Free))
	{
		return;
	}
	FoodPoints -= McVillager::BreedFood;
	Mate->FoodPoints -= McVillager::BreedFood;
	BreedCooldown = McVillager::BreedCooldownTicks;
	Mate->BreedCooldown = McVillager::BreedCooldownTicks;
	Manager.OnVillagerBred((Body.Position + Mate->Body.Position) * 0.5, *this);
}

void FMcVillager::Play(FMcEntityManager& Manager)
{
	if (!bBaby || Age % 100 != Id % 100 || PanicTicks > 0)
	{
		return;
	}
	// The nearest other villager within 12 blocks, a baby by preference.
	FMcVillager* Friend = nullptr;
	double Best = FMath::Square(12.0);
	for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
	{
		if (Other.Get() == this || Other->bRemoved || Other->GetType() != StaticType())
		{
			continue;
		}
		FMcVillager& Candidate = static_cast<FMcVillager&>(*Other);
		const double Distance = FVector::DistSquared(Candidate.Body.Position, Body.Position) * (Candidate.IsBaby() ? 0.25 : 1.0);
		if (Distance > 4.0 && Distance < Best)
		{
			Best = Distance;
			Friend = &Candidate;
		}
	}
	if (Friend)
	{
		Goal = Friend->Body.Position;
	}
}

void FMcVillager::WantGolem(FMcEntityManager& Manager, int32 MinVillagers)
{
	if (!Manager.OnGolemWanted || bBaby)
	{
		return;
	}
	int32 Villagers = 0;
	for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
	{
		if (Other->bRemoved)
		{
			continue;
		}
		const double Distance = FVector::DistSquared(Other->Body.Position, Body.Position);
		if (Other->GetType() == StaticType() && Distance < FMath::Square(McVillager::GolemVillageRadius))
		{
			++Villagers;
		}
		else if (Other->GetType() == FName(TEXT("minecraft:iron_golem")) && Distance < FMath::Square(McVillager::GolemGuardRadius))
		{
			// The village is guarded already.
			return;
		}
	}
	if (Villagers >= MinVillagers)
	{
		Manager.OnGolemWanted(Body.Position);
	}
}

void FMcVillager::Tick(FMcEntityManager& Manager)
{
	if (bBaby && --GrowthTicks <= 0)
	{
		SetBaby(false);
	}
	if (BreedCooldown > 0)
	{
		--BreedCooldown;
	}
	if (Age % 10 == Id % 10)
	{
		PickUpFood(Manager);
		TryBreed(Manager);
	}
	Play(Manager);
	// A villager runs from the zombies and illagers within 8 blocks, away from the nearest.
	if (Age % 10 == Id % 10 && PanicTicks <= 0)
	{
		const FVector* Fear = nullptr;
		double Nearest = 8.0 * 8.0;
		for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
		{
			if (Other.Get() != this && !Other->bRemoved && McVillager::IsFeared(Other->GetType()))
			{
				const double Distance = FVector::DistSquared(Other->Body.Position, Body.Position);
				if (Distance < Nearest)
				{
					Nearest = Distance;
					Fear = &Other->Body.Position;
				}
			}
		}
		if (Fear)
		{
			FVector Away = Body.Position - *Fear;
			Away.Y = 0.0;
			Away = Away.GetSafeNormal();
			if (Away.IsNearlyZero())
			{
				Away = FVector(1.0, 0.0, 0.0);
			}
			Goal = Body.Position + Away * 12.0;
			PanicTicks = 60;
			WantGolem(Manager, 2);
		}
	}
	// A village of three or more calls for a golem every so often (gossip: the villagers that have seen each other lately).
	if (Age % McVillager::GolemCheckTicks == Id % McVillager::GolemCheckTicks && (Age / McVillager::GolemCheckTicks + Id) % 4 == 0)
	{
		WantGolem(Manager, 3);
	}
	// Spread over the villagers by id, so they don't all look in the same tick.
	if (PanicTicks <= 0 && Age % ProfessionInterval == Id % ProfessionInterval)
	{
		UpdateProfession(Manager);
		UpdateRoutine(Manager);
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
	if (Ar.IsLoading() && (Version < 1 || Version > SaveVersion))
	{
		return false;
	}
	FString VariantText = Variant.ToString();
	FString ProfessionText = Profession.ToString();
	bool bSavedBaby = bBaby;
	Ar << VariantText << ProfessionText << Level << Xp << bSavedBaby << GrowthTicks << bHasJobSite << JobSite.X << JobSite.Y << JobSite.Z;
	if (Version >= 2)
	{
		Ar << bHasBed << Bed.X << Bed.Y << Bed.Z;
	}
	if (Version >= 3)
	{
		int32 NumOffers = Offers.Num();
		Ar << NumOffers << RestocksToday;
		if (Ar.IsLoading())
		{
			if (NumOffers < 0 || NumOffers > 64)
			{
				return false;
			}
			Offers.SetNum(NumOffers);
		}
		for (McTrading::FOffer& Offer : Offers)
		{
			if (!Offer.Serialize(Ar))
			{
				return false;
			}
		}
	}
	if (Version >= 4)
	{
		int32 NumKnown = Reputation.Num();
		Ar << NumKnown;
		if (Ar.IsLoading())
		{
			if (NumKnown < 0 || NumKnown > 256)
			{
				return false;
			}
			Reputation.Reset();
			for (int32 Index = 0; Index < NumKnown; ++Index)
			{
				FString Player;
				int32 Standing = 0;
				Ar << Player << Standing;
				Reputation.Add(Player, FMath::Clamp(Standing, MinReputation, MaxReputation));
			}
		}
		else
		{
			for (TPair<FString, int32>& Known : Reputation)
			{
				Ar << Known.Key << Known.Value;
			}
		}
	}
	if (Version >= 5)
	{
		Ar << FoodPoints << BreedCooldown;
		if (Ar.IsLoading())
		{
			FoodPoints = FMath::Clamp(FoodPoints, 0, McVillager::MaxFood);
			BreedCooldown = FMath::Max(0, BreedCooldown);
		}
	}
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

const FName& FMcZombieVillager::StaticType()
{
	static const FName Name(TEXT("minecraft:zombie_villager"));
	return Name;
}

FMcZombieVillager::FMcZombieVillager(const FMcMobType& InType)
	: FMcMob(InType)
	, Variant(TEXT("plains"))
	, Profession(FMcVillager::NoProfession())
{
}

void FMcZombieVillager::Become(FMcVillager& Villager)
{
	Variant = Villager.GetVariant();
	Profession = Villager.GetProfession();
	Level = Villager.GetLevel();
	VillagerData.Reset();
	FMemoryWriter Writer(VillagerData);
	Villager.Serialize(Writer);
}

bool FMcZombieVillager::StartCure(const FString& InCurer, int32 Ticks)
{
	if (!bWeakened || bCuring)
	{
		return false;
	}
	bCuring = true;
	CureTicks = FMath::Max(1, Ticks);
	Curer = InCurer;
	return true;
}

void FMcZombieVillager::Tick(FMcEntityManager& Manager)
{
	AdvanceCure();
	FMcMob::Tick(Manager);
}

bool FMcZombieVillager::Serialize(FArchive& Ar)
{
	if (!FMcMob::Serialize(Ar))
	{
		return false;
	}
	FString VariantText = Variant.ToString();
	FString ProfessionText = Profession.ToString();
	int32 NumBytes = VillagerData.Num();
	Ar << VariantText << ProfessionText << Level << bWeakened << bCuring << CureTicks << Curer << NumBytes;
	if (Ar.IsLoading())
	{
		if (NumBytes < 0 || NumBytes > 1 << 20 || Level < 1 || Level > McVillager::MaxLevel || VariantText.IsEmpty() || ProfessionText.IsEmpty() || Ar.IsError())
		{
			return false;
		}
		Variant = FName(*VariantText);
		Profession = FName(*ProfessionText);
		VillagerData.SetNumUninitialized(NumBytes);
	}
	if (NumBytes > 0)
	{
		Ar.Serialize(VillagerData.GetData(), NumBytes);
	}
	return !Ar.IsError();
}

const FName& FMcWanderingTrader::StaticType()
{
	static const FName Name(TEXT("minecraft:wandering_trader"));
	return Name;
}

FMcWanderingTrader::FMcWanderingTrader(const FMcMobType& InType)
	: FMcMob(InType)
{
}

TArray<McTrading::FOffer>& FMcWanderingTrader::GetOffers()
{
	if (Offers.IsEmpty())
	{
		FMcRandom Rolls(static_cast<uint64>(Id) * 7919u + 29u);
		for (const TCHAR* Set : { TEXT("wandering_trader/buying"), TEXT("wandering_trader/common"), TEXT("wandering_trader/uncommon") })
		{
			McTrading::RollSet(Set, FName(), Rolls, Offers);
		}
	}
	return Offers;
}

void FMcWanderingTrader::Tick(FMcEntityManager& Manager)
{
	if (--StayTicks <= 0)
	{
		bRemoved = true;
		return;
	}
	FMcMob::Tick(Manager);
}

bool FMcWanderingTrader::Serialize(FArchive& Ar)
{
	if (!FMcMob::Serialize(Ar))
	{
		return false;
	}
	int32 NumOffers = Offers.Num();
	Ar << StayTicks << NumOffers;
	if (Ar.IsLoading())
	{
		if (NumOffers < 0 || NumOffers > 64 || StayTicks < 0)
		{
			return false;
		}
		Offers.SetNum(NumOffers);
	}
	for (McTrading::FOffer& Offer : Offers)
	{
		if (!Offer.Serialize(Ar))
		{
			return false;
		}
	}
	return !Ar.IsError();
}
