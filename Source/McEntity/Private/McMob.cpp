#include "McMob.h"
#include "McBlockWorld.h"

const TArray<FMcMobType>& FMcMobType::GetAll()
{
	static TArray<FMcMobType> Types;
	if (Types.IsEmpty())
	{
		const auto Add = [](const TCHAR* Name, float Width, float Height, float Health, float Speed, float Attack, float Follow, bool bPanics, bool bBurns)
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
			Types.Add(Type);
		};
		//  name                width height hp   speed  atk follow panics burns
		Add(TEXT("minecraft:pig"), 0.9f, 0.9f, 10.f, 0.25f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:cow"), 0.9f, 1.4f, 10.f, 0.2f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:sheep"), 0.9f, 1.3f, 8.f, 0.23f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:chicken"), 0.4f, 0.7f, 4.f, 0.25f, 0.f, 0.f, true, false);
		Add(TEXT("minecraft:zombie"), 0.6f, 1.95f, 20.f, 0.23f, 3.f, 35.f, false, true);
		Add(TEXT("minecraft:skeleton"), 0.6f, 1.99f, 20.f, 0.25f, 2.f, 16.f, false, true);
		Add(TEXT("minecraft:spider"), 1.4f, 0.9f, 16.f, 0.3f, 2.f, 16.f, false, false);
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
	if (Type.bPanics)
	{
		PanicTicks = 60;
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

void FMcMob::Tick(FMcEntityManager& Manager)
{
	FMcBlockWorld& World = Manager.GetWorld();
	++Age;
	if (HurtTime > 0)
	{
		--HurtTime;
	}
	if (AttackCooldown > 0)
	{
		--AttackCooldown;
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

	// Goals, in priority order: flee, chase and bite, wander.
	FVector Target = FVector::ZeroVector;
	bool bHasTarget = false;
	bool bBiting = false;
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
	else if (Type.FollowRange > 0.0f)
	{
		double Best = Type.FollowRange * Type.FollowRange;
		for (const FVector& Player : PlayerPositions)
		{
			const double Distance = FVector::DistSquared(Player, Body.Position);
			if (Distance < Best)
			{
				Best = Distance;
				Target = Player;
				bHasTarget = true;
				bBiting = Distance < 2.0 * 2.0;
			}
		}
	}
	if (!bHasTarget)
	{
		if (!Goal.IsSet() && Random.RandHelper(120) == 0)
		{
			Goal = Body.Position + FVector(Random.FRandRange(-10.0, 10.0), 0.0, Random.FRandRange(-10.0, 10.0));
		}
		if (Goal.IsSet())
		{
			Target = Goal.GetValue();
			bHasTarget = true;
		}
	}
	if (bBiting && AttackCooldown == 0 && Type.AttackDamage > 0.0f)
	{
		AttackCooldown = 20;
		if (OnAttackPlayer)
		{
			OnAttackPlayer(Target, Type.AttackDamage);
		}
	}

	FMcMoveInput Input;
	Input.Speed = Type.Speed;
	if (bHasTarget)
	{
		FVector Toward = Target - Body.Position;
		Toward.Y = 0.0;
		if (Toward.SizeSquared() < 0.25 * 0.25)
		{
			Goal.Reset();
		}
		else
		{
			// Minecraft yaw: 0 faces +z, 90 faces -x.
			Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Toward.X, Toward.Z));
			Input.Forward = 1.0f;
			Input.Yaw = Yaw;
			Input.bJump = Body.bOnGround && Body.bHorizontalCollision;
		}
	}
	// Wading mobs paddle upwards rather than sink.
	Input.bJump |= Body.bInWater || Body.bInLava;
	McPhysics::TickPlayer(World, Body, Input);
}
