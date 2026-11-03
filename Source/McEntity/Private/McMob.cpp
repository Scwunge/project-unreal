#include "McMob.h"
#include "McVillager.h"
#include "McArrow.h"
#include "McPotion.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McPathfinding.h"
#include "Serialization/Archive.h"

namespace
{
	// Whether no block with collision stands between the two points.
	bool HasLineOfSight(const FMcBlockWorld& World, const FVector& From, const FVector& To)
	{
		const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
		const FVector Way = To - From;
		const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Way.Size() / 0.25));
		for (int32 Step = 1; Step < Steps; ++Step)
		{
			const FVector At = From + Way * (static_cast<double>(Step) / Steps);
			const FIntVector Cell(FMath::FloorToInt32(At.X), FMath::FloorToInt32(At.Y), FMath::FloorToInt32(At.Z));
			if (!World.HasChunk(FIntPoint(Cell.X >> 4, Cell.Z >> 4)) || Shapes.HasCollision(World.GetBlock(Cell)))
			{
				return false;
			}
		}
		return true;
	}

	FIntVector FeetCell(const FVector& Position)
	{
		// A little up, so a body that settled at 0.9999 of a block stands in the block above that.
		return FIntVector(FMath::FloorToInt32(Position.X), FMath::FloorToInt32(Position.Y + 0.05), FMath::FloorToInt32(Position.Z));
	}
}

const TArray<FMcMobType>& FMcMobType::GetAll()
{
	static TArray<FMcMobType> Types;
	if (Types.IsEmpty())
	{
		const auto Add = [](const TCHAR* Name, float Width, float Height, float Health, float Speed, float Attack, float Follow, bool bPanics, bool bBurns) -> FMcMobType&
		{
			FMcMobType Type;
			Type.Name = Name;
			Type.Width = Width;
			Type.Height = Height;
			Type.MaxHealth = Health;
			Type.Speed = Speed;
			Type.AttackDamage = Attack;
			Type.FollowRange = Follow;
			Type.bPanics = bPanics;
			Type.bBurnsInSun = bBurns;
			return Types.Add_GetRef(Type);
		};
		//  name                width height hp   speed  atk follow panics burns
		Add(TEXT("minecraft:pig"), 0.9f, 0.9f, 10.f, 0.25f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:cow"), 0.9f, 1.4f, 10.f, 0.2f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:sheep"), 0.9f, 1.3f, 8.f, 0.23f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:chicken"), 0.4f, 0.7f, 4.f, 0.25f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:zombie"), 0.6f, 1.95f, 20.f, 0.23f, 3.f, 35.f, false, true).bHuntsVillagers = true;
		// A drowned (reference, `attribute get` on a spawned one): the zombie's 20 health, 0.23 speed, 3 bite, 35 follow range, burns in
		// the sun like it; its box is taken as the zombie's (not measured). Swimming (its water movement efficiency reads 0), the thrown
		// trident, and spawning in water are not done.
		Add(TEXT("minecraft:drowned"), 0.6f, 1.95f, 20.f, 0.23f, 3.f, 35.f, false, true);
		Add(TEXT("minecraft:skeleton"), 0.6f, 1.99f, 20.f, 0.25f, 2.f, 16.f, false, true).bShoots = true;
		// The spider's box is 1.4 wide and 0.9 tall. Its bite is 2 (3 on Hard).
		FMcMobType& Spider = Add(TEXT("minecraft:spider"), 1.4f, 0.9f, 16.f, 0.3f, 2.f, 16.f, false, false);
		Spider.bClimbsWalls = true;
		Spider.bNeutralInLight = true;
		// The creeper has no bite: it goes off. Follows within 16 blocks.
		Add(TEXT("minecraft:creeper"), 0.6f, 1.7f, 20.f, 0.25f, 0.f, 16.f, false, false).bSwells = true;
		// 7 damage on Normal; hostile only once hurt. It sees a player from 64 blocks away.
		FMcMobType& Enderman = Add(TEXT("minecraft:enderman"), 0.6f, 2.9f, 40.f, 0.3f, 7.f, 64.f, false, false);
		Enderman.bNeutral = true;
		Enderman.bTeleports = true;
		// The smallest size: 0.52 wide, 1 health. The others are made by SetSize; the attack is by size.
		Add(TEXT("minecraft:slime"), 0.52f, 0.52f, 1.f, 0.3f, 1.f, 16.f, false, false).bHops = true;

		// The overworld animals (minecraft.wiki, the page of each). Horses and llamas have a random health of 15 to 30
		// and a random speed; the middle is used.
		// Wolves are neutral: a wild one bites (4 on Normal) whoever hurt it, for a minute.
		Add(TEXT("minecraft:wolf"), 0.6f, 0.85f, 8.f, 0.3f, 4.f, 16.f, false, false).bNeutral = true;
		Add(TEXT("minecraft:rabbit"), 0.4f, 0.5f, 3.f, 0.3f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:fox"), 0.6f, 0.7f, 10.f, 0.3f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:horse"), 1.3965f, 1.6f, 22.5f, 0.225f, 0.f, 0.f, true, false);
		// Donkeys and mules (wiki): the horse's width, a donkey 1.5 tall; health 15 to 30 and speed 0.175 (the middle of its range).
		Add(TEXT("minecraft:donkey"), 1.3965f, 1.5f, 22.5f, 0.175f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:mule"), 1.3965f, 1.6f, 22.5f, 0.175f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:goat"), 0.9f, 1.3f, 10.f, 0.2f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:llama"), 0.9f, 1.87f, 22.5f, 0.175f, 0.f, 0.f, true, false);
		// The wandering trader's llamas are a llama in all but their blanket.
		Add(TEXT("minecraft:trader_llama"), 0.9f, 1.87f, 22.5f, 0.175f, 0.f, 0.f, true, false);
		// A turtle is slow on land.
		Add(TEXT("minecraft:turtle"), 1.2f, 0.4f, 30.f, 0.1f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:frog"), 0.5f, 0.5f, 10.f, 0.15f, 0.f, 0.f, true, false);
		// A bee stings (2 on Normal) whoever hurt it, and flies.
		FMcMobType& Bee = Add(TEXT("minecraft:bee"), 0.7f, 0.6f, 10.f, 0.3f, 2.f, 16.f, false, false);
		Bee.bNeutral = true;
		Bee.bFlies = true;

		// Villagers walk about at 0.5 of a 0.5 speed attribute and run from what hurts them.
		Add(TEXT("minecraft:villager"), 0.6f, 1.95f, 20.f, 0.25f, 0.f, 0.f, true, false);
		// A villager a zombie got: as a zombie, a bite of 3 and burning in the sun.
		Add(TEXT("minecraft:zombie_villager"), 0.6f, 1.95f, 20.f, 0.23f, 3.f, 35.f, false, true).bHuntsVillagers = true;
		// The wandering trader walks about and runs from what hurts it.
		Add(TEXT("minecraft:wandering_trader"), 0.6f, 1.95f, 20.f, 0.35f, 0.f, 0.f, true, false);
		// The iron golem: 100 health, a heavy swing of 15, and it only turns on a player it was hurt by.
		FMcMobType& Golem = Add(TEXT("minecraft:iron_golem"), 1.4f, 2.7f, 100.f, 0.25f, 15.f, 16.f, false, false);
		Golem.bNeutral = true;
		Golem.bHuntsHostiles = true;
		// The illagers (minecraft.wiki, the page of each). They go for players, villagers and golems.
		FMcMobType& Pillager = Add(TEXT("minecraft:pillager"), 0.6f, 1.95f, 24.f, 0.35f, 4.f, 32.f, false, false);
		Pillager.bShoots = true;
		Pillager.bHuntsVillagers = true;
		Pillager.bIsRaider = true;
		FMcMobType& Vindicator = Add(TEXT("minecraft:vindicator"), 0.6f, 1.95f, 24.f, 0.35f, 13.f, 12.f, false, false);
		Vindicator.bHuntsVillagers = true;
		Vindicator.bIsRaider = true;
		FMcMobType& Evoker = Add(TEXT("minecraft:evoker"), 0.6f, 1.95f, 24.f, 0.5f, 0.f, 12.f, false, false);
		Evoker.bCastsSpells = true;
		Evoker.bHuntsVillagers = true;
		Evoker.bIsRaider = true;
		FMcMobType& Vex = Add(TEXT("minecraft:vex"), 0.4f, 0.8f, 14.f, 0.4f, 5.f, 16.f, false, false);
		Vex.bFlies = true;
		FMcMobType& Ravager = Add(TEXT("minecraft:ravager"), 1.95f, 2.2f, 100.f, 0.3f, 12.f, 32.f, false, false);
		Ravager.bHuntsVillagers = true;
		Ravager.bIsRaider = true;
		// The witch throws potions: the harming one is the only one done, from up to 15 blocks as the arrow goes.
		FMcMobType& Witch = Add(TEXT("minecraft:witch"), 0.6f, 1.95f, 26.f, 0.25f, 3.f, 16.f, false, false);
		Witch.bShoots = true;
		Witch.bThrowsPotions = true;
		Witch.bIsRaider = true;
		FMcMobType& Fangs = Add(TEXT("minecraft:evoker_fangs"), 0.5f, 0.8f, 1.f, 0.f, 0.f, 0.f, false, false);
		Fangs.bIsFangs = true;
	}
	return Types;
}

const FMcMobType* FMcMobType::Find(FName Name)
{
	return GetAll().FindByPredicate([Name](const FMcMobType& Type) { return Type.Name == Name; });
}

FMcMob::FMcMob(const FMcMobType& InType)
	: Type(InType)
	, Health(InType.MaxHealth)
{
	Body.Width = InType.Width;
	Body.Height = InType.Height;
	Body.StepHeight = 0.6f;
	// What they carry when they spawn: a skeleton its bow, a vex its iron sword.
	if (InType.Name == FName(TEXT("minecraft:skeleton")))
	{
		HeldItem = TEXT("minecraft:bow");
	}
	else if (InType.Name == FName(TEXT("minecraft:vex")))
	{
		HeldItem = TEXT("minecraft:iron_sword");
	}
}

void FMcMob::SetSize(int32 InSize, bool bHeal)
{
	Size = InSize;
	Body.Width = Type.Width * Size;
	Body.Height = Type.Height * Size;
	if (bHeal)
	{
		Health = Type.MaxHealth * Size * Size;
	}
}

float FMcMob::GetAttackDamage() const
{
	if (Type.bHops)
	{
		return Size > 1 ? static_cast<float>(Size) : 0.0f;
	}
	return Type.AttackDamage;
}

bool FMcMob::Serialize(FArchive& Ar)
{
	if (!FMcEntity::Serialize(Ar))
	{
		return false;
	}
	// The rest (the flash after a hit, the goal being walked to) is a few ticks' worth and starts afresh.
	Ar << Health;
	if (Type.Name == FName(TEXT("minecraft:wolf")))
	{
		// Older saves end here: a wolf with nothing after its health is a wild one.
		if (!Ar.IsLoading() || !Ar.AtEnd())
		{
			Ar << bTamed << bSitting << CollarColor << OwnerName;
			CollarColor = FMath::Clamp(CollarColor, 0, 15);
		}
	}
	if (Type.bHops)
	{
		int32 Saved = Size;
		Ar << Saved;
		if (Ar.IsLoading())
		{
			if (Saved != 1 && Saved != 2 && Saved != 4)
			{
				return false;
			}
			SetSize(Saved, false);
		}
	}
	return !Ar.IsError() && !FMath::IsNaN(Health);
}

void FMcMob::UpdateWolfPose()
{
	ArmPose = (bSitting ? 1 : 0) | ((AngerTicks > 0 && !bTamed) ? 2 : 0) | (bTamed ? 4 : 0) | (CollarColor << 8);
}

void FMcMob::SetYoung(int32 Ticks)
{
	YoungTicks = FMath::Max(0, Ticks);
	const float Scale = YoungTicks > 0 ? 0.5f : 1.0f;
	Body.Width = Type.Width * Scale;
	Body.Height = Type.Height * Scale;
}

bool FMcMob::CanBeYoung(FName Kind)
{
	static const TSet<FName> Breeders = {
		TEXT("minecraft:pig"), TEXT("minecraft:cow"), TEXT("minecraft:sheep"), TEXT("minecraft:chicken"), TEXT("minecraft:horse"), TEXT("minecraft:donkey"), TEXT("minecraft:mule"),
		TEXT("minecraft:wolf"), TEXT("minecraft:goat"), TEXT("minecraft:fox"), TEXT("minecraft:llama"), TEXT("minecraft:rabbit"),
		TEXT("minecraft:turtle"), TEXT("minecraft:bee"),
	};
	return Breeders.Contains(Kind);
}

bool FMcMob::IsPrey(const FMcEntity& Other) const
{
	const FName Name = Other.GetType();
	if (Type.bHuntsVillagers && (Name == FName(TEXT("minecraft:villager")) || Name == FName(TEXT("minecraft:iron_golem"))))
	{
		return true;
	}
	if (Type.bHuntsHostiles && Other.IsLiving())
	{
		const FMcMobType* OtherType = FMcMobType::Find(Name);
		return OtherType && OtherType->IsHostile() && !OtherType->bSwells;
	}
	return false;
}

bool FMcMob::Damage(float Amount, const FVector* Source)
{
	// Right after a hit only a stronger one gets through, and only by the difference; here that is simply ignored.
	if (HurtTime > 0 || Health <= 0.0f)
	{
		return false;
	}
	Health -= Amount;
	HurtTime = 10;
	++HurtEvents;
	AngerTicks = 1200;
	if (Type.bPanics)
	{
		PanicTicks = 60;
	}
	if (Type.bTeleports)
	{
		bTeleportPending = true;
	}
	if (Source)
	{
		// Knockback: 0.4 away from the hitter, and a hop.
		FVector Away = Body.Position - *Source;
		Away.Y = 0.0;
		Away = Away.GetSafeNormal();
		Body.Velocity.X = Body.Velocity.X * 0.5 + Away.X * 0.4;
		Body.Velocity.Z = Body.Velocity.Z * 0.5 + Away.Z * 0.4;
		Body.Velocity.Y = FMath::Min(Body.Velocity.Y * 0.5 + 0.4, 0.4);
		Body.bOnGround = false;
	}
	PlayVoice(Health <= 0.0f ? McSound::EMobSound::Death : McSound::EMobSound::Hurt);
	if (Health <= 0.0f)
	{
		bRemoved = true;
		if (OnDeath)
		{
			OnDeath(*this);
		}
		return true;
	}
	return false;
}

void FMcMob::PlayVoice(McSound::EMobSound Kind)
{
	if (!Owner)
	{
		return;
	}
	const FName Name = McSound::GetMobSoundName(Type.Name, Kind, Size);
	if (Name.IsNone())
	{
		return;
	}
	if (Type.bHops)
	{
		PlaySlimeSound(Name);
		return;
	}
	Owner->PlaySound(Name, Body.Position, 1.0f, McSound::GetVoicePitch(VoiceRandom.FRand(), VoiceRandom.FRand()));
}

void FMcMob::PlaySlimeSound(FName Event)
{
	if (Owner)
	{
		const float VoicePitch = McSound::GetVoicePitch(VoiceRandom.FRand(), VoiceRandom.FRand()) * (Size <= 1 ? 1.4f : 0.8f);
		Owner->PlaySound(Event, Body.Position, 0.4f * Size, VoicePitch);
	}
}

void FMcMob::UpdateSteps(FMcEntityManager& Manager)
{
	// What was walked since last tick, counted at 0.6 a block on the ground; a step sounds each time it passes a mark a
	// block further than the last. A jump of more than a few blocks (a teleport) isn't walking.
	const double Moved = FMath::Sqrt(FMath::Square(Body.Position.X - StepLast.X) + FMath::Square(Body.Position.Z - StepLast.Z));
	StepLast = Body.Position;
	if (Type.bHops || !Body.bOnGround || Moved > 3.0)
	{
		return;
	}
	StepDistance += Moved * 0.6;
	if (StepDistance <= NextStep)
	{
		return;
	}
	NextStep = StepDistance + 1.0;
	const FMcBlockWorld& World = Manager.GetWorld();
	const FIntVector Below(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y - 0.2), FMath::FloorToInt32(Body.Position.Z));
	const FMcStateId State = World.GetBlock(Below);
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (Registry.IsValidState(State))
	{
		Manager.PlaySound(McSound::GetMobStep(Type.Name, Registry.GetBlockOf(State).Name, Body.Position));
	}
}

bool FMcMob::TryTeleport(FMcEntityManager& Manager)
{
	FMcBlockWorld& World = Manager.GetWorld();
	const FVector Origin = Body.Position;
	const int32 Clearance = FMath::CeilToInt32(Body.Height);
	for (int32 Attempt = 0; Attempt < 16; ++Attempt)
	{
		const int32 X = FMath::FloorToInt32(Body.Position.X) + Random.RandRange(-32, 32);
		const int32 Z = FMath::FloorToInt32(Body.Position.Z) + Random.RandRange(-32, 32);
		const int32 Base = FMath::FloorToInt32(Body.Position.Y) + Random.RandRange(-16, 16);
		for (int32 Y = Base + 8; Y >= Base - 8; --Y)
		{
			if (McPath::IsStandable(World, FIntVector(X, Y, Z), Clearance))
			{
				Body.Position = FVector(X + 0.5, Y, Z + 0.5);
				Body.Velocity = FVector::ZeroVector;
				Path.Reset();
				Goal.Reset();
				// Heard where it went from and where it came to.
				Manager.PlaySound(FName("entity.enderman.teleport"), Origin);
				Manager.PlaySound(FName("entity.enderman.teleport"), Body.Position);
				return true;
			}
		}
	}
	return false;
}

void FMcMob::Shoot(FMcEntityManager& Manager, const FVector& TargetFeet)
{
	const FVector Eye = Body.Position + FVector(0.0, Type.Height * 0.85, 0.0);
	// At a third of the way up the target, a bit high for the arrow's drop (a fifth of the way across), as the game
	// aims: from the eyes down to the legs, then up by the allowance.
	FVector Aim = TargetFeet + FVector(0.0, 0.6, 0.0) - Eye;
	Aim.Y += FMath::Sqrt(FMath::Square(Aim.X) + FMath::Square(Aim.Z)) * 0.2;
	const auto Gauss = [this]()
	{
		const double U = FMath::Max(static_cast<double>(Random.FRand()), 1e-6);
		return FMath::Sqrt(-2.0 * FMath::Loge(U)) * FMath::Cos(2.0 * PI * Random.FRand());
	};
	const FVector Direction = Aim.GetSafeNormal();
	const double Spread = 0.0075 * ArrowInaccuracy;
	TUniquePtr<FMcArrowEntity> Arrow = MakeUnique<FMcArrowEntity>();
	Arrow->Body.Position = Eye + Direction * 0.5;
	Arrow->Body.Velocity = (Direction + FVector(Gauss(), Gauss(), Gauss()) * Spread) * ArrowSpeed;
	// 3 to 5 on Normal.
	Arrow->Damage = static_cast<float>(Random.RandRange(3, 5));
	Manager.Add(MoveTemp(Arrow));
	Manager.PlaySound(FName("entity.skeleton.shoot"), Eye, 1.0f, 1.0f / (VoiceRandom.FRand() * 0.4f + 0.8f));
}

void FMcMob::ThrowPotion(FMcEntityManager& Manager, const FVector& TargetFeet)
{
	const FVector Eye = Body.Position + FVector(0.0, Type.Height * 0.85, 0.0);
	const FVector Flat(TargetFeet.X - Body.Position.X, 0.0, TargetFeet.Z - Body.Position.Z);
	const double Distance = Flat.Size();
	TUniquePtr<FMcThrownPotion> Potion = MakeUnique<FMcThrownPotion>();
	// Slowness from far (8 blocks and more), weakness from close; otherwise harming, or poison one time in four.
	if (Distance >= 8.0)
	{
		Potion->Effect = FName(TEXT("slowness"));
		Potion->Duration = 20 * 30;
	}
	else if (Distance <= 3.0 && Random.FRand() < 0.25f)
	{
		Potion->Effect = FName(TEXT("weakness"));
		Potion->Duration = 20 * 22;
	}
	else if (Random.FRand() < 0.25f)
	{
		Potion->Effect = FName(TEXT("poison"));
		Potion->Duration = 20 * 22;
	}
	// Thrown up a little at the target's head height, a flat arc the length of the throw.
	FVector Aim = TargetFeet + FVector(0.0, 1.0, 0.0) - Eye;
	Aim.Y += Distance * 0.2;
	Potion->Body.Position = Eye;
	Potion->Body.Velocity = Aim.GetSafeNormal() * 0.75;
	Manager.Add(MoveTemp(Potion));
	Manager.PlaySound(FName("entity.witch.throw"), Eye, 1.0f, 0.8f + Random.FRand() * 0.4f);
}

void FMcMob::TickWitch(FMcEntityManager& Manager, bool bChasing, double TargetDistance)
{
	if (DrinkCooldown > 0)
	{
		--DrinkCooldown;
	}
	if (SwiftTicks > 0)
	{
		--SwiftTicks;
	}
	if (DrinkTicks > 0)
	{
		// Drinking takes a second; the potion is in hand the while.
		ArmPose = 4;
		if (--DrinkTicks == 0)
		{
			Manager.PlaySound(FName("entity.witch.drink"), Body.Position, 1.0f, 0.8f + Random.FRand() * 0.4f);
			if (DrinkEffect == FName(TEXT("healing")))
			{
				Health = FMath::Min(Type.MaxHealth, Health + 4.0f);
			}
			else if (DrinkEffect == FName(TEXT("swiftness")))
			{
				SwiftTicks = 20 * 30;
			}
			DrinkEffect = NAME_None;
			DrinkCooldown = 40;
		}
		return;
	}
	ArmPose = (bChasing && TargetDistance <= ShootRange) ? 1 : 0;
	if (DrinkCooldown > 0)
	{
		return;
	}
	FName Pick;
	if (Body.bInLava)
	{
		Pick = FName(TEXT("fire_resistance"));
	}
	else if (Body.bInWater)
	{
		Pick = FName(TEXT("water_breathing"));
	}
	else if (Health < Type.MaxHealth * 0.5f)
	{
		Pick = FName(TEXT("healing"));
	}
	else if (bChasing && TargetDistance >= 8.0 && SwiftTicks == 0 && Random.FRand() < 0.01f)
	{
		Pick = FName(TEXT("swiftness"));
	}
	if (!Pick.IsNone())
	{
		DrinkEffect = Pick;
		DrinkTicks = 32;
		ArmPose = 4;
	}
}

FVector FMcMob::NextWaypoint(FMcEntityManager& Manager, const FVector& Dest)
{
	const FVector Flat(Dest.X - Body.Position.X, 0.0, Dest.Z - Body.Position.Z);
	// Spiders go straight at the wall; near the goal there is nothing to find.
	if (Type.bClimbsWalls || (Flat.SizeSquared() < 1.5 * 1.5 && FMath::Abs(Dest.Y - Body.Position.Y) < 1.5))
	{
		Path.Reset();
		return Dest;
	}

	const FIntVector DestCell = FeetCell(Dest);
	const bool bGoalMoved = Path.IsEmpty() ? true : FMath::Max3(FMath::Abs(DestCell.X - PathGoal.X), FMath::Abs(DestCell.Y - PathGoal.Y), FMath::Abs(DestCell.Z - PathGoal.Z)) >= 2;
	const bool bFinished = PathIndex >= Path.Num();
	// Stuck for a second, and again every second after that.
	const bool bStuck = StuckTicks > 20 && StuckTicks % 20 == 1;
	if (RepathCooldown <= 0 && (bGoalMoved || bFinished || bStuck))
	{
		RepathCooldown = 10 + Random.RandHelper(10);
		FMcPathOptions Options;
		Options.Clearance = FMath::Max(1, FMath::CeilToInt32(Body.Height));
		Options.MaxNodes = 500;
		McPath::Find(Manager.GetWorld(), FeetCell(Body.Position), DestCell, Options, Path);
		PathIndex = 0;
		PathGoal = DestCell;
	}

	// Past the blocks already reached.
	while (PathIndex < Path.Num())
	{
		const FIntVector& Block = Path[PathIndex];
		const double DX = Block.X + 0.5 - Body.Position.X;
		const double DZ = Block.Z + 0.5 - Body.Position.Z;
		if (DX * DX + DZ * DZ < 0.4 * 0.4 && FMath::Abs(Block.Y - Body.Position.Y) < 1.2)
		{
			++PathIndex;
		}
		else
		{
			return FVector(Block.X + 0.5, Block.Y, Block.Z + 0.5);
		}
	}
	return Dest;
}

void FMcMob::Tick(FMcEntityManager& Manager)
{
	FMcBlockWorld& World = Manager.GetWorld();
	if (Age == 0)
	{
		// Every mob would otherwise run the same stream (a default one is seeded 0) and the herd would wander in step.
		// Ids count up one at a time, so spread them over the seed space.
		Random.Initialize(static_cast<int32>(static_cast<uint32>(Id) * 2654435761u));
		VoiceRandom.Initialize(static_cast<int32>(static_cast<uint32>(Id) * 40503u + 12345u));
		LastPosition = Body.Position;
		StepLast = Body.Position;
	}
	++Age;
	GlowTicks = FMath::Max(0, GlowTicks - 1);
	if (Type.Name == FName(TEXT("minecraft:wolf")))
	{
		UpdateWolfPose();
	}
	if (bFrozen)
	{
		return;
	}
	if (Type.bIsFangs)
	{
		// They come up after the delay, bite whatever is within reach once, and are gone a second later.
		if (--SpellDelay == 0)
		{
			Manager.PlaySound(FName("entity.evoker_fangs.attack"), Body.Position, 1.0f, 1.0f);
			for (const FVector& Player : Manager.PlayerPositions)
			{
				if (FVector::DistSquared(FVector(Player.X, 0.0, Player.Z), FVector(Body.Position.X, 0.0, Body.Position.Z)) < 0.8 * 0.8 && FMath::Abs(Player.Y - Body.Position.Y) < 1.5)
				{
					Manager.HurtPlayer(Player, 6.0f);
				}
			}
			for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
			{
				if (!Other->bRemoved && Other->IsLiving() && Other.Get() != this && FVector::DistSquared(Other->Body.Position, Body.Position) < 0.8 * 0.8
					&& FMath::Abs(Other->Body.Position.Y - Body.Position.Y) < 1.5)
				{
					FMcMob& Victim = static_cast<FMcMob&>(*Other);
					if (!Victim.Type.bCastsSpells && Victim.Type.Name != FName(TEXT("minecraft:vex")) && !Victim.Type.bIsFangs)
					{
						Victim.Damage(6.0f, &Body.Position);
					}
				}
			}
		}
		if (SpellDelay < -20)
		{
			bRemoved = true;
		}
		return;
	}
	// A vex lives 30 to 120 seconds and then wastes away.
	if (LifeTicks > 0 && --LifeTicks == 0)
	{
		LifeTicks = -1;
	}
	if (LifeTicks < 0 && Age % 20 == 0 && Damage(1.0f, nullptr))
	{
		return;
	}
	if (YoungTicks > 0 && --YoungTicks == 0)
	{
		SetYoung(0);
	}
	if (HurtTime > 0)
	{
		--HurtTime;
	}
	if (AttackCooldown > 0)
	{
		--AttackCooldown;
	}
	if (AngerTicks > 0)
	{
		--AngerTicks;
	}
	if (ShootCooldown > 0)
	{
		--ShootCooldown;
	}
	if (RepathCooldown > 0)
	{
		--RepathCooldown;
	}
	if (!World.HasChunk(FIntPoint(FMath::FloorToInt32(Body.Position.X) >> 4, FMath::FloorToInt32(Body.Position.Z) >> 4)))
	{
		return;
	}
	if (Body.Position.Y < World.GetDimension().MinY - 64)
	{
		bRemoved = true;
		return;
	}
	if (McSound::TickAmbient(AmbientTime, VoiceRandom.RandHelper(1000)))
	{
		PlayVoice(McSound::EMobSound::Ambient);
	}
	UpdateSteps(Manager);

	// Undead burn in daylight when the sky is open over them: a point of damage a second.
	if (Type.bBurnsInSun && Manager.bDaylight && Age % 20 == 0
		&& Manager.IsSkyExposed(FIntVector(FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Y + Type.Height), FMath::FloorToInt32(Body.Position.Z))))
	{
		HurtTime = 0;
		Damage(1.0f, nullptr);
		if (bRemoved)
		{
			return;
		}
	}

	if (bTeleportPending)
	{
		bTeleportPending = false;
		TryTeleport(Manager);
	}
	// Spiders go by the light round them, looked at once a second.
	if (Type.bNeutralInLight && (Age % 20 == 1))
	{
		bBrightHere = Manager.EstimateLightLevel(FeetCell(Body.Position)) > 11;
	}

	// A golem sometimes holds a poppy out to a child villager near it, for 20 seconds, standing still.
	if (Type.bHuntsHostiles)
	{
		if (OfferTicks > 0)
		{
			--OfferTicks;
		}
		else if (Age % 100 == Id % 100 && Random.RandHelper(3) == 0)
		{
			for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
			{
				if (!Other->bRemoved && Other->GetType() == FName(TEXT("minecraft:villager")) && FVector::DistSquared(Other->Body.Position, Body.Position) < 36.0
					&& static_cast<const FMcVillager&>(*Other).IsBaby())
				{
					OfferTicks = 400;
					Yaw = FMath::RadiansToDegrees(FMath::Atan2(-(Other->Body.Position.X - Body.Position.X), Other->Body.Position.Z - Body.Position.Z));
					break;
				}
			}
		}
	}
	// A sitting wolf stays where it is, whatever happens.
	if (bSitting)
	{
		PanicTicks = 0;
		Goal.Reset();
	}
	// Goals, in priority order: flee, chase and bite (or shoot, or swell), wander.
	FVector Target = FVector::ZeroVector;
	bool bHasTarget = false;
	bool bChasing = false;
	double PreyDistSq = 0.0;
	// A villager being hunted, rather than a player.
	FMcMob* Prey = nullptr;
	if (PanicTicks > 0)
	{
		--PanicTicks;
		if (!Goal.IsSet())
		{
			const double Angle = Random.FRandRange(0.0, 2.0 * PI);
			Goal = Body.Position + FVector(FMath::Cos(Angle) * 8.0, 0.0, FMath::Sin(Angle) * 8.0);
		}
		Target = Goal.GetValue();
		bHasTarget = true;
	}
	else if (Type.FollowRange > 0.0f && !bTamed && (!Type.bNeutral || AngerTicks > 0) && (!Type.bNeutralInLight || AngerTicks > 0 || !bBrightHere))
	{
		double Best = Type.FollowRange * Type.FollowRange;
		for (const FVector& Player : Manager.PlayerPositions)
		{
			const double Distance = FVector::DistSquared(Player, Body.Position);
			if (Distance < Best)
			{
				Best = Distance;
				Target = Player;
				Prey = nullptr;
				PreyDistSq = Distance;
				bHasTarget = true;
				bChasing = true;
			}
		}
	}
	if ((Type.bHuntsVillagers || Type.bHuntsHostiles) && Type.FollowRange > 0.0f)
	{
		// The nearest villager within reach of its nose, if nearer than any player.
		double Nearest = bChasing ? PreyDistSq : FMath::Square(Type.FollowRange);
		for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
		{
			if (Other->bRemoved || Other.Get() == this || !IsPrey(*Other))
			{
				continue;
			}
			const double Distance = FVector::DistSquared(Other->Body.Position, Body.Position);
			if (Distance < Nearest)
			{
				Nearest = Distance;
				Prey = static_cast<FMcMob*>(Other.Get());
				Target = Other->Body.Position;
				PreyDistSq = Distance;
				bHasTarget = true;
				bChasing = true;
			}
		}
	}
	if (!bHasTarget && bTamed && !bSitting)
	{
		// A tamed wolf comes after its owner from more than 10 blocks off, up to where it stops 3 short.
		const FVector* Nearest = nullptr;
		double Best = 0.0;
		for (const FVector& Player : Manager.PlayerPositions)
		{
			const double Distance = FVector::DistSquared(Player, Body.Position);
			if (!Nearest || Distance < Best)
			{
				Nearest = &Player;
				Best = Distance;
			}
		}
		if (Nearest && (Best > 100.0 || (Goal.IsSet() && Best > 9.0)))
		{
			Goal = *Nearest;
		}
		else if (Nearest && Best <= 9.0)
		{
			Goal.Reset();
		}
	}
	if (!bHasTarget && LeashHolderId != 0)
	{
		const FMcEntity* Holder = Manager.Find(LeashHolderId);
		if (!Holder || Holder->bRemoved)
		{
			LeashHolderId = 0;
		}
		else if (FVector::DistSquared(Holder->Body.Position, Body.Position) > 25.0)
		{
			// Pulled along once it is more than 5 blocks off.
			Goal = Holder->Body.Position;
		}
	}
	if (!bHasTarget)
	{
		if (!Goal.IsSet() && !bStayPut && !bSitting && OfferTicks == 0 && Random.RandHelper(120) == 0)
		{
			Goal = Body.Position + FVector(Random.FRandRange(-10.0, 10.0), Type.bFlies ? Random.FRandRange(-2.0, 3.0) : 0.0, Random.FRandRange(-10.0, 10.0));
		}
		if (Goal.IsSet())
		{
			Target = Goal.GetValue();
			bHasTarget = true;
		}
	}

	if (Type.bSwells)
	{
		// Starts burning within 3 blocks, goes out past 7 (or when there is nobody to chase); stands still while it burns.
		const bool bWasSwelling = bSwelling;
		if (bSwelling)
		{
			bSwelling = bChasing && PreyDistSq <= 7.0 * 7.0;
		}
		else
		{
			bSwelling = bChasing && PreyDistSq < 3.0 * 3.0;
		}
		SwellTicks = bSwelling ? SwellTicks + 1 : FMath::Max(0, SwellTicks - 1);
		if (bSwelling && !bWasSwelling)
		{
			// The hiss when the fuse is lit.
			Manager.PlaySound(FName("entity.creeper.primed"), Body.Position, 1.0f, 0.5f);
		}
		if (SwellTicks >= FuseTicks)
		{
			// A creeper that goes off drops nothing.
			bRemoved = true;
			// A hair above the floor: from exactly the floor's level every ray goes into it at its first step and the
			// crater would be one block; from just above, the shallow ones skim the ground and spread it.
			Manager.Explode(Body.Position + FVector(0.0, 0.0625, 0.0), ExplosionPower);
			return;
		}
		if (bSwelling)
		{
			bHasTarget = false;
		}
	}
	if (Type.bCastsSpells && bChasing)
	{
		const double Distance = FMath::Sqrt(PreyDistSq);
		Yaw = FMath::RadiansToDegrees(FMath::Atan2(-(Target.X - Body.Position.X), Target.Z - Body.Position.Z));
		if (SpellCooldown > 0)
		{
			--SpellCooldown;
		}
		else if (Distance < 16.0 && Manager.OnSpawnMob)
		{
			SpellCooldown = 100;
			int32 Vexes = 0;
			for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
			{
				Vexes += (!Other->bRemoved && Other->GetType() == FName(TEXT("minecraft:vex")) && FVector::DistSquared(Other->Body.Position, Body.Position) < 256.0) ? 1 : 0;
			}
			if (Vexes == 0 && Random.RandHelper(3) == 0)
			{
				// Three vexes round it, each with a life of 30 to 120 seconds.
				for (int32 Index = 0; Index < 3; ++Index)
				{
					if (FMcMob* Vex = Manager.OnSpawnMob(TEXT("minecraft:vex"), Body.Position + FVector(Random.FRandRange(-1.0, 1.0), 0.5, Random.FRandRange(-1.0, 1.0))))
					{
						Vex->LifeTicks = Random.RandRange(600, 2400);
					}
				}
				Manager.PlaySound(FName("entity.evoker.prepare_summon"), Body.Position, 1.0f, 1.0f);
			}
			else
			{
				// A line of fangs toward the target, one every 1.25 blocks and a little later each.
				FVector Dir(Target.X - Body.Position.X, 0.0, Target.Z - Body.Position.Z);
				const double Reach = FMath::Min(Dir.Size(), 16.0);
				Dir = Dir.GetSafeNormal();
				for (int32 Index = 1; Index * 1.25 <= Reach + 1.25; ++Index)
				{
					if (FMcMob* Fang = Manager.OnSpawnMob(TEXT("minecraft:evoker_fangs"), FVector(Body.Position.X + Dir.X * Index * 1.25, Target.Y, Body.Position.Z + Dir.Z * Index * 1.25)))
					{
						Fang->SpellDelay = 4 + Index * 2;
					}
				}
				Manager.PlaySound(FName("entity.evoker.prepare_attack"), Body.Position, 1.0f, 1.0f);
			}
		}
		if (Distance < 8.0)
		{
			bHasTarget = false;
		}
	}
	if (Type.bThrowsPotions)
	{
		TickWitch(Manager, bChasing, bChasing ? FMath::Sqrt(PreyDistSq) : 0.0);
	}
	else if (Type.bIsRaider)
	{
		// Arms: up for a crossbow or an axe once it has a target, drawn back while the crossbow reloads, raised to cast.
		if (Type.bCastsSpells)
		{
			ArmPose = (bChasing && SpellCooldown > 60) ? 3 : 0;
		}
		else if (Type.bShoots)
		{
			ArmPose = !bChasing ? 0 : (ShootCooldown > 0 && ShootCooldown <= 25) ? 2 : 1;
		}
		else
		{
			ArmPose = bChasing ? 1 : 0;
		}
	}
	else if (Type.Name == FName(TEXT("minecraft:vex")))
	{
		// A vex with prey charges (the model's pose 1: sword arm thrown up, body upright).
		ArmPose = bChasing ? 1 : 0;
	}
	else if (Type.Name == FName(TEXT("minecraft:skeleton")))
	{
		// A skeleton with a target raises its bow arms (the model's pose 1).
		ArmPose = bChasing ? 1 : 0;
	}
	if (Type.bShoots && bChasing && DrinkTicks == 0)
	{
		// Faces its prey, shoots from up to 15 blocks with nothing in the way, and doesn't close in past 10.
		const double Distance = FMath::Sqrt(PreyDistSq);
		Yaw = FMath::RadiansToDegrees(FMath::Atan2(-(Target.X - Body.Position.X), Target.Z - Body.Position.Z));
		if (Distance <= ShootRange && ShootCooldown == 0
			&& HasLineOfSight(World, Body.Position + FVector(0.0, Type.Height * 0.85, 0.0), Target + FVector(0.0, 1.2, 0.0)))
		{
			ShootCooldown = ShootIntervalTicks;
			if (Type.bThrowsPotions)
			{
				ThrowPotion(Manager, Target);
			}
			else
			{
				Shoot(Manager, Target);
			}
		}
		if (Distance < ShootStopRange)
		{
			bHasTarget = false;
		}
	}
	const float BiteDamage = GetAttackDamage();
	if (bChasing && !Type.bSwells && !Type.bShoots && AttackCooldown == 0 && BiteDamage > 0.0f
		&& PreyDistSq < FMath::Square(FMath::Max(2.0, 1.0 + Body.Width)))
	{
		AttackCooldown = 20;
		if (Prey)
		{
			if (Prey->Damage(BiteDamage, &Body.Position) && Manager.OnVillagerSlain && Prey->GetType() == FName(TEXT("minecraft:villager")) && (Type.Name == FName(TEXT("minecraft:zombie")) || Type.Name == FName(TEXT("minecraft:zombie_villager"))))
			{
				Manager.OnVillagerSlain(*Prey);
			}
		}
		else if (OnAttackPlayer)
		{
			OnAttackPlayer(Target, BiteDamage);
		}
	}

	if (Type.bFlies)
	{
		// No gravity and no paths: straight at the target, at the speed attribute, until it is reached.
		FVector Velocity = FVector::ZeroVector;
		if (bHasTarget)
		{
			const FVector To = Target - Body.Position;
			if (To.SizeSquared() < 0.4 * 0.4)
			{
				Goal.Reset();
			}
			else
			{
				Velocity = To.GetSafeNormal() * (Type.Speed * BlocksPerSecondPerSpeed / 20.0) * (SwiftTicks > 0 ? 1.3 : 1.0);
				Yaw = FMath::RadiansToDegrees(FMath::Atan2(-To.X, To.Z));
			}
		}
		Body.Velocity = Velocity;
		McPhysics::Move(World, Body, Velocity, false);
		if (bHasTarget && Body.bHorizontalCollision)
		{
			Goal.Reset();
		}
		LastPosition = Body.Position;
		return;
	}

	FMcMoveInput Input;
	Input.Speed = static_cast<float>(GetInputSpeed(Type));
	bool bWalking = false;
	if (bHasTarget)
	{
		FVector ToTarget = Target - Body.Position;
		ToTarget.Y = 0.0;
		if (ToTarget.SizeSquared() < 0.25 * 0.25)
		{
			Goal.Reset();
			Path.Reset();
		}
		else
		{
			const FVector Way = NextWaypoint(Manager, Target);
			FVector Toward = Way - Body.Position;
			Toward.Y = 0.0;
			if (Toward.SizeSquared() > 1e-6)
			{
				// Minecraft yaw: 0 faces +z, 90 faces -x.
				Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Toward.X, Toward.Z));
				bWalking = true;
				if (Type.bHops)
				{
					// A slime sits, then hops: up one block and a couple along.
					if (Body.bOnGround)
					{
						if (HopDelay > 0)
						{
							--HopDelay;
						}
						else
						{
							HopDelay = bChasing ? Random.RandRange(3, 10) : Random.RandRange(10, 30);
							PlaySlimeSound(Size <= 1 ? FName("entity.slime.jump_small") : FName("entity.slime.jump"));
							const FVector Heading = Toward.GetSafeNormal();
							Body.Velocity.X = Heading.X * 0.2;
							Body.Velocity.Z = Heading.Z * 0.2;
							Body.Velocity.Y = 0.42;
							Body.bOnGround = false;
						}
					}
				}
				else
				{
					Input.Forward = 1.0f;
					Input.Yaw = Yaw;
					// Up a ledge: when it's in the way, or the next block of the path is above and close.
					Input.bJump = Body.bOnGround && (Body.bHorizontalCollision || (Way.Y > Body.Position.Y + 0.5 && Toward.SizeSquared() < 1.3 * 1.3));
				}
			}
		}
	}
	else if (Type.bHops && Body.bOnGround && HopDelay > 0)
	{
		--HopDelay;
	}
	// Wading mobs paddle upwards rather than sink.
	Input.bJump |= Body.bInWater || Body.bInLava;
	McPhysics::TickPlayer(World, Body, Input);
	if (Type.bClimbsWalls && bWalking && Body.bHorizontalCollision)
	{
		// Walking into a wall, a spider goes up it.
		Body.Velocity.Y = FMath::Max(Body.Velocity.Y, 0.2);
	}

	// A mob that was trying to walk and hasn't moved for a second finds a new path; after four it gives up the goal.
	if (bWalking && !Type.bHops && FMath::Square(Body.Position.X - LastPosition.X) + FMath::Square(Body.Position.Z - LastPosition.Z) < 0.02 * 0.02)
	{
		if (++StuckTicks > 80)
		{
			StuckTicks = 0;
			Goal.Reset();
			Path.Reset();
		}
	}
	else
	{
		StuckTicks = 0;
	}
	LastPosition = Body.Position;
}
