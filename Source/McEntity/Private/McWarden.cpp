// The warden: see McWarden.h. All numbers are from minecraft.wiki, "Warden", and unverified against the reference client.

#include "McWarden.h"

namespace
{
	int32 AngerClamp(int32 Value)
	{
		return FMath::Clamp(Value, 0, McWarden::MaxAnger);
	}
}

float McWarden::GetMeleeDamage(int32 Difficulty)
{
	return Difficulty <= 1 ? 16.0f : (Difficulty >= 3 ? 45.0f : 30.0f);
}

float McWarden::GetSonicDamage(int32 Difficulty)
{
	return Difficulty <= 1 ? 6.0f : (Difficulty >= 3 ? 15.0f : 10.0f);
}

// ---- The anger table ----

FMcWardenBrain::FSuspect& FMcWardenBrain::Get(int32 Id, const FVector& Pos)
{
	CalmTicks = 0;
	for (FSuspect& Suspect : Suspects)
	{
		if (Suspect.Id == Id)
		{
			Suspect.LastSeen = Pos;
			return Suspect;
		}
	}
	FSuspect& Fresh = Suspects.AddDefaulted_GetRef();
	Fresh.Id = Id;
	Fresh.LastSeen = Pos;
	return Fresh;
}

void FMcWardenBrain::Tick()
{
	if (ListenCooldown > 0)
	{
		--ListenCooldown;
	}
	const bool bDecay = ++DecayCounter >= McWarden::DecayTicks;
	if (bDecay)
	{
		DecayCounter = 0;
	}
	for (int32 Index = Suspects.Num() - 1; Index >= 0; --Index)
	{
		FSuspect& Suspect = Suspects[Index];
		++Suspect.NoticedFor;
		Suspect.ContactCooldown = FMath::Max(0, Suspect.ContactCooldown - 1);
		Suspect.ProjectileWindow = FMath::Max(0, Suspect.ProjectileWindow - 1);
		if (bDecay)
		{
			Suspect.Anger = FMath::Max(0, Suspect.Anger - 1);
		}
		if (Suspect.Anger == 0 && Suspect.ProjectileWindow == 0)
		{
			Suspects.RemoveAtSwap(Index);
		}
	}
	if (Suspects.IsEmpty())
	{
		++CalmTicks;
	}
	else
	{
		CalmTicks = 0;
	}
}

bool FMcWardenBrain::Hear(int32 Id, const FVector& Pos)
{
	if (ListenCooldown > 0)
	{
		return false;
	}
	ListenCooldown = McWarden::ListenCooldownTicks;
	FSuspect& Suspect = Get(Id, Pos);
	Suspect.Anger = AngerClamp(Suspect.Anger + McWarden::VibrationAnger);
	return true;
}

bool FMcWardenBrain::HearProjectile(int32 Id, const FVector& Pos, double Distance)
{
	if (ListenCooldown > 0)
	{
		return false;
	}
	ListenCooldown = McWarden::ListenCooldownTicks;
	FSuspect& Suspect = Get(Id, Pos);
	const bool bSecond = Suspect.ProjectileWindow > 0;
	Suspect.ProjectileWindow = McWarden::ProjectileWindowTicks;
	if (Distance <= McWarden::ProjectileFarRange)
	{
		Suspect.Anger = AngerClamp(Suspect.Anger + (bSecond ? McWarden::VibrationAnger : McWarden::ProjectileAnger));
	}
	return true;
}

void FMcWardenBrain::Touch(int32 Id, const FVector& Pos)
{
	FSuspect& Suspect = Get(Id, Pos);
	if (Suspect.ContactCooldown == 0)
	{
		Suspect.ContactCooldown = McWarden::ContactCooldownTicks;
		Suspect.Anger = AngerClamp(Suspect.Anger + McWarden::ContactAnger);
	}
}

void FMcWardenBrain::Attacked(int32 Id, const FVector& Pos)
{
	FSuspect& Suspect = Get(Id, Pos);
	Suspect.Anger = AngerClamp(Suspect.Anger + McWarden::AttackAnger);
}

void FMcWardenBrain::AddAnger(int32 Id, const FVector& Pos, int32 Amount)
{
	FSuspect& Suspect = Get(Id, Pos);
	Suspect.Anger = AngerClamp(Suspect.Anger + Amount);
}

void FMcWardenBrain::Clear(int32 Id)
{
	Suspects.RemoveAll([Id](const FSuspect& Suspect) { return Suspect.Id == Id; });
}

const FMcWardenBrain::FSuspect* FMcWardenBrain::GetTarget() const
{
	// Players before anything else it is angry at, then the angriest.
	const FSuspect* Best = nullptr;
	for (const FSuspect& Suspect : Suspects)
	{
		if (Suspect.Anger <= 0)
		{
			continue;
		}
		if (!Best || (Suspect.Id > 0 && Best->Id <= 0) || ((Suspect.Id > 0) == (Best->Id > 0) && Suspect.Anger > Best->Anger))
		{
			Best = &Suspect;
		}
	}
	return Best;
}

int32 FMcWardenBrain::GetAngerAt(int32 Id) const
{
	for (const FSuspect& Suspect : Suspects)
	{
		if (Suspect.Id == Id)
		{
			return Suspect.Anger;
		}
	}
	return 0;
}

// ---- The mob ----

const FName& FMcWarden::StaticType()
{
	static const FName Name(TEXT("minecraft:warden"));
	return Name;
}

FMcWarden::FMcWarden(const FMcMobType& InType)
	: FMcMob(InType)
{
	bNoKnockback = true;
	SniffWait = McWarden::SniffWaitMin;
}

void FMcWarden::EnterState(EState Next, int32 Ticks)
{
	State = Next;
	StateTicks = Ticks;
}

void FMcWarden::StartEmerging()
{
	EnterState(EState::Emerging, McWarden::EmergeTicks);
	Body.Height = 1.0f;
	if (Owner)
	{
		Owner->PlaySound(FName("entity.warden.emerge"), Body.Position, 5.0f, 1.0f);
	}
}

FVector FMcWarden::GetTargetPlace(const FMcWardenBrain::FSuspect& Target, FMcEntityManager& Manager) const
{
	FVector Feet;
	if (Target.Id > 0 && FindPlayer && FindPlayer(Target.Id, Feet))
	{
		return Feet;
	}
	if (Target.Id < 0)
	{
		if (const FMcEntity* Mob = Manager.Find(-Target.Id))
		{
			if (!Mob->bRemoved)
			{
				return Mob->Body.Position;
			}
		}
	}
	return Target.LastSeen;
}

void FMcWarden::HearVibration(int32 SuspectId, const FVector& Pos, bool bProjectile, const FVector& FromPos)
{
	if (State == EState::Emerging || State == EState::Digging || Health <= 0.0f)
	{
		return;
	}
	const bool bAngry = IsAngryNow();
	const bool bHeard = bProjectile ? Brain.HearProjectile(SuspectId, Pos, FVector::Dist(FromPos, Body.Position)) : Brain.Hear(SuspectId, Pos);
	if (bHeard && Owner)
	{
		Owner->PlaySound(FName("entity.warden.tendril_clicks"), Body.Position, 5.0f, 1.0f);
		Owner->PlaySound(FName(bAngry ? "entity.warden.listening_angry" : "entity.warden.listening"), Body.Position, 10.0f, 1.0f);
	}
}

void FMcWarden::OnHurt(float Amount, const FVector* Source)
{
	if (!Source || !Owner || !IdOfPlayerAt)
	{
		return;
	}
	// Whoever hit it stood next to it: the nearest player to the hitter's place (the hit is given with the attacker's position).
	const bool bBefore = IsAngryNow();
	double Best = 36.0;
	int32 Hitter = 0;
	FVector Place = *Source;
	for (const FVector& Player : Owner->PlayerPositions)
	{
		const double Distance = FVector::DistSquared(Player, *Source);
		if (Distance < Best)
		{
			Best = Distance;
			Hitter = IdOfPlayerAt(Player);
			Place = Player;
		}
	}
	if (Hitter != 0)
	{
		Brain.Attacked(Hitter, Place);
		if (!bBefore && IsAngryNow() && FVector::Dist(Place, Body.Position) <= McWarden::RoarSkipRange)
		{
			bWasAngry = true;
		}
	}
}

bool FMcWarden::IsAngryNow() const
{
	return Brain.IsAngry();
}

void FMcWarden::Tick(FMcEntityManager& Manager)
{
	Brain.Tick();
	++SinceAttack;
	SonicCooldown = FMath::Max(0, SonicCooldown - 1);
	MeleeWait = FMath::Max(0, MeleeWait - 1);

	// The darkness it gives, and its heartbeat (30 beats a minute at rest, 120 when angry).
	if (++DarknessTimer >= McWarden::DarknessEveryTicks)
	{
		DarknessTimer = 0;
		if (Manager.OnPlayerEffect && State != EState::Digging)
		{
			for (const FVector& Player : Manager.PlayerPositions)
			{
				if (FVector::DistSquared(Player, Body.Position) <= FMath::Square(McWarden::DarknessRadius))
				{
					Manager.OnPlayerEffect(Player, TEXT("darkness"), 0, McWarden::DarknessTicks);
				}
			}
		}
	}
	if (++HeartTimer >= (IsAngryNow() ? 10 : 40))
	{
		HeartTimer = 0;
		Manager.PlaySound(FName("entity.warden.heartbeat"), Body.Position, 5.0f, 1.0f);
	}

	// Whoever left, died or went to Creative is forgotten.
	TArray<int32> Gone;
	for (const FMcWardenBrain::FSuspect& Suspect : Brain.GetSuspects())
	{
		FVector Feet;
		if (Suspect.Id > 0 && FindPlayer && !FindPlayer(Suspect.Id, Feet))
		{
			Gone.Add(Suspect.Id);
		}
		else if (Suspect.Id < 0)
		{
			const FMcEntity* Mob = Manager.Find(-Suspect.Id);
			if (!Mob || Mob->bRemoved)
			{
				Gone.Add(Suspect.Id);
			}
		}
	}
	for (const int32 GoneId : Gone)
	{
		Brain.Clear(GoneId);
	}

	Goal.Reset();
	bStayPut = true;
	const FMcWardenBrain::FSuspect* Target = Brain.GetTarget();
	switch (State)
	{
	case EState::Emerging:
		if (--StateTicks <= 0)
		{
			Body.Height = Type.Height;
			EnterState(EState::Idle, 0);
		}
		break;

	case EState::Digging:
		if (--StateTicks <= 0)
		{
			bRemoved = true;
			if (OnDug)
			{
				OnDug(*this);
			}
		}
		break;

	case EState::Roaring:
		if (--StateTicks <= 0)
		{
			EnterState(EState::Idle, 0);
		}
		break;

	case EState::Sniffing:
		if (--StateTicks <= 0)
		{
			// The nearest living thing within 6 across and 20 up and down is angered.
			double Best = FMath::Square(McWarden::SniffRadius);
			int32 NearestId = 0;
			FVector NearestPlace = FVector::ZeroVector;
			for (const FVector& Player : Manager.PlayerPositions)
			{
				const FVector Away = Player - Body.Position;
				const double Across = FVector2D(Away.X, Away.Z).SizeSquared();
				const int32 PlayerId = IdOfPlayerAt ? IdOfPlayerAt(Player) : 0;
				if (PlayerId != 0 && Across < Best && FMath::Abs(Away.Y) <= McWarden::SniffHeight)
				{
					Best = Across;
					NearestId = PlayerId;
					NearestPlace = Player;
				}
			}
			for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
			{
				if (Other->bRemoved || Other.Get() == this || !Other->IsLiving() || Other->GetType() == StaticType())
				{
					continue;
				}
				const FVector Away = Other->Body.Position - Body.Position;
				const double Across = FVector2D(Away.X, Away.Z).SizeSquared();
				if (Across < Best && FMath::Abs(Away.Y) <= McWarden::SniffHeight)
				{
					Best = Across;
					NearestId = -Other->Id;
					NearestPlace = Other->Body.Position;
				}
			}
			if (NearestId != 0)
			{
				Brain.AddAnger(NearestId, NearestPlace, McWarden::SniffAnger);
			}
			SniffWait = McWarden::SniffWaitMin + static_cast<int32>(FMath::FRand() * (McWarden::SniffWaitMax - McWarden::SniffWaitMin));
			EnterState(EState::Idle, 0);
		}
		break;

	case EState::Charging:
		// 3 seconds from the start: the boom lands when 1.7 s of charge are done, then it recovers.
		--StateTicks;
		if (StateTicks == McWarden::SonicTotalTicks - McWarden::SonicChargeTicks)
		{
			Manager.PlaySound(FName("entity.warden.sonic_boom"), Body.Position, 3.0f, 1.0f);
			SinceAttack = 0;
			SonicCooldown = McWarden::SonicTotalTicks;
			if (Target)
			{
				const FVector Place = GetTargetPlace(*Target, Manager);
				if (Target->Id > 0 && OnAttackPlayer)
				{
					OnAttackPlayer(Place, McWarden::GetSonicDamage(Manager.Difficulty));
				}
				else if (Target->Id < 0)
				{
					if (FMcEntity* Mob = Manager.Find(-Target->Id))
					{
						if (Mob->IsLiving())
						{
							static_cast<FMcMob*>(Mob)->Damage(McWarden::GetSonicDamage(Manager.Difficulty), &Body.Position);
						}
					}
				}
			}
		}
		if (StateTicks <= 0)
		{
			EnterState(EState::Idle, 0);
		}
		break;

	case EState::Idle:
	default:
		TickIdle(Manager, Target);
		break;
	}
	FMcMob::Tick(Manager);
}

void FMcWarden::TickIdle(FMcEntityManager& Manager, const FMcWardenBrain::FSuspect* Target)
{
	if (!Target)
	{
		bWasAngry = false;
		if (Brain.GetCalmTicks() >= McWarden::CalmDespawnTicks)
		{
			Manager.PlaySound(FName("entity.warden.dig"), Body.Position, 5.0f, 1.0f);
			EnterState(EState::Digging, 60);
			return;
		}
		if (--SniffWait <= 0)
		{
			Manager.PlaySound(FName("entity.warden.sniff"), Body.Position, 3.75f, 1.0f);
			EnterState(EState::Sniffing, McWarden::SniffTicks);
		}
		return;
	}
	const FVector Place = GetTargetPlace(*Target, Manager);
	if (Target->Anger < McWarden::HostileAnger)
	{
		// Suspicious: goes to where it last noticed something, without roaring.
		bWasAngry = false;
		bStayPut = false;
		Goal = Place;
		return;
	}
	if (!bWasAngry)
	{
		// Turning hostile: a roar of 4.2 s and 20 more anger.
		bWasAngry = true;
		Brain.AddAnger(Target->Id, Place, McWarden::RoarAnger);
		Manager.PlaySound(FName("entity.warden.roar"), Body.Position, 3.0f, 1.0f);
		EnterState(EState::Roaring, McWarden::RoarTicks);
		return;
	}
	bStayPut = false;
	Goal = Place;
	const FVector Away = Place - Body.Position;
	const double Across = FVector2D(Away.X, Away.Z).Size();
	const double Up = FMath::Abs(Away.Y);
	// Melee within reach: 36 ticks between swings.
	if (Across < 0.45 + 1.4 && Up < 2.0)
	{
		if (MeleeWait == 0)
		{
			MeleeWait = McWarden::MeleeCooldownTicks;
			SinceAttack = 0;
			Manager.PlaySound(FName("entity.warden.attack_impact"), Body.Position, 10.0f, 1.0f);
			const float Damage = McWarden::GetMeleeDamage(Manager.Difficulty);
			if (Target->Id > 0)
			{
				if (OnAttackPlayer)
				{
					OnAttackPlayer(Place, Damage);
				}
			}
			else if (FMcEntity* Mob = Manager.Find(-Target->Id))
			{
				if (Mob->IsLiving())
				{
					static_cast<FMcMob*>(Mob)->Damage(Damage, &Body.Position);
				}
			}
		}
		return;
	}
	// The sonic boom: 10 s after noticing the target and 5 s after its last attack, within 14 across and 20 up and down.
	if (Target->NoticedFor >= McWarden::SonicNoticeTicks && SinceAttack >= McWarden::SonicAttackTicks && SonicCooldown == 0
		&& Across <= McWarden::SonicRange && Up <= McWarden::SonicHeight)
	{
		Manager.PlaySound(FName("entity.warden.sonic_charge"), Body.Position, 3.0f, 1.0f);
		EnterState(EState::Charging, McWarden::SonicTotalTicks);
	}
}
