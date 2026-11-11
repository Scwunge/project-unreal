#include "McLlama.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McMob.h"
#include "McMount.h"
#include "McPhysics.h"

const FName& FMcLlamaSpit::StaticType()
{
	static const FName Name(TEXT("minecraft:llama_spit"));
	return Name;
}

FMcLlamaSpit::FMcLlamaSpit()
{
	Body.Width = 0.25f;
	Body.Height = 0.25f;
	Body.StepHeight = 0.0f;
}

void FMcLlamaSpit::Tick(FMcEntityManager& Manager)
{
	if (++Age >= LifeTicks)
	{
		bRemoved = true;
		return;
	}
	FMcBlockWorld& World = Manager.GetWorld();
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	const FVector Next = Body.Position + Body.Velocity;
	if (!World.HasChunk(FIntPoint(FMath::FloorToInt32(Next.X) >> 4, FMath::FloorToInt32(Next.Z) >> 4)))
	{
		bRemoved = true;
		return;
	}
	// In quarter-block steps so it can't skip a wall or a player.
	const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Body.Velocity.Size() / 0.25));
	const FVector Step = Body.Velocity / Steps;
	FVector At = Body.Position;
	for (int32 Index = 0; Index < Steps; ++Index)
	{
		At += Step;
		const FIntVector Where(FMath::FloorToInt32(At.X), FMath::FloorToInt32(At.Y), FMath::FloorToInt32(At.Z));
		const FVector3f Local(At.X - Where.X, At.Y - Where.Y, At.Z - Where.Z);
		for (const FBox3f& Box : Shapes.GetCollision(World.GetBlock(Where)))
		{
			if (Box.IsInside(Local))
			{
				bRemoved = true;
				return;
			}
		}
		for (const FVector& Player : Manager.PlayerPositions)
		{
			const FBox PlayerBox(Player + FVector(-0.4, -0.1, -0.4), Player + FVector(0.4, 1.9, 0.4));
			if (PlayerBox.IsInside(At))
			{
				Manager.HurtPlayer(Player, Damage);
				bRemoved = true;
				return;
			}
		}
	}
	Body.Position = Next;
	Body.Velocity *= Drag;
	Body.Velocity.Y -= Gravity;
	Yaw = FMath::RadiansToDegrees(FMath::Atan2(-Body.Velocity.X, Body.Velocity.Z));
}

bool McLlama::Tick(FMcEntityManager& Manager, FMcMob& Llama)
{
	FMcMountState& State = McMount::Ensure(Llama);

	// Spit at whoever hurt it: the nearest player within range, for a while after the hit.
	if (Llama.HurtEvents != State.LastHurtEvents)
	{
		State.LastHurtEvents = Llama.HurtEvents;
		State.SpitMemory = SpitMemory;
	}
	if (State.SpitCooldown > 0)
	{
		--State.SpitCooldown;
	}
	if (State.SpitMemory > 0)
	{
		--State.SpitMemory;
		FVector Target = FVector::ZeroVector;
		bool bFound = false;
		double Best = SpitRange * SpitRange;
		for (const FVector& Player : Manager.PlayerPositions)
		{
			const double D = FVector::DistSquared(Player, Llama.Body.Position);
			if (D < Best)
			{
				Best = D;
				Target = Player;
				bFound = true;
			}
		}
		if (bFound && State.SpitCooldown == 0)
		{
			State.SpitCooldown = SpitInterval;
			FMcLlamaSpit* Spit = new FMcLlamaSpit();
			Spit->Body.Position = Llama.Body.Position + FVector(0.0, 1.6765, 0.0);
			const double Dx = Target.X - Spit->Body.Position.X;
			const double Dz = Target.Z - Spit->Body.Position.Z;
			const double Flat = FMath::Sqrt(Dx * Dx + Dz * Dz);
			const double Dy = (Target.Y + 0.6667 * 1.8) - Spit->Body.Position.Y + Flat * 0.2;
			FVector Aim = FVector(Dx, Dy, Dz).GetSafeNormal();
			FRandomStream& Random = Manager.GetRandom();
			const auto Gauss = [&]() { return (Random.FRand() + Random.FRand() + Random.FRand() + Random.FRand() - 2.0) * 1.7320508 * 0.5; };
			Aim += FVector(Gauss(), Gauss(), Gauss()) * SpitSpread;
			Spit->Body.Velocity = Aim.GetSafeNormal() * SpitSpeed;
			Manager.Add(TUniquePtr<FMcEntity>(Spit));
		}
	}

	// Caravan: follow the llama ahead.
	if (Llama.bLeashed || State.RiderId != 0)
	{
		State.CaravanHead = 0;
		return false;
	}
	FMcEntity* Head = State.CaravanHead != 0 ? Manager.Find(State.CaravanHead) : nullptr;
	if (Head && (Head->bRemoved || FVector::DistSquared(Head->Body.Position, Llama.Body.Position) > 16.0 * 16.0))
	{
		Head = nullptr;
	}
	if (!Head)
	{
		State.CaravanHead = 0;
		// Only entities already in the list are looked at: the loop below adds none.
		for (const TUniquePtr<FMcEntity>& Other : Manager.GetEntities())
		{
			if (Other.Get() == &Llama || Other->bRemoved || Other->GetType() != Llama.GetType()
				|| FVector::DistSquared(Other->Body.Position, Llama.Body.Position) > JoinRange * JoinRange)
			{
				continue;
			}
			// The lead of a caravan is the one on a lead; each follower takes the one before it, which has no follower yet.
			bool bTaken = false;
			bool bInCaravan = false;
			for (const TUniquePtr<FMcEntity>& Taker : Manager.GetEntities())
			{
				if (!Taker->bRemoved && Taker->Mount.IsValid() && Taker.Get() != &Llama)
				{
					bTaken |= Taker->Mount->CaravanHead == Other->Id;
				}
			}
			if (Other->Mount.IsValid())
			{
				bInCaravan = Other->Mount->CaravanHead != 0;
			}
			if ((Other->bLeashed || bInCaravan) && !bTaken)
			{
				State.CaravanHead = Other->Id;
				Head = Other.Get();
				break;
			}
		}
	}
	if (!Head)
	{
		return false;
	}
	FVector To = Head->Body.Position - Llama.Body.Position;
	To.Y = 0.0;
	FMcMoveInput Input;
	Input.Speed = 0.0f;
	if (To.Size() > StopDistance)
	{
		Llama.Yaw = FMath::RadiansToDegrees(FMath::Atan2(-To.X, To.Z));
		Input.Forward = 1.0f;
		Input.Yaw = Llama.Yaw;
		Input.Speed = static_cast<float>(FollowInputSpeed);
		Input.bJump = Llama.Body.bOnGround && Llama.Body.bHorizontalCollision;
	}
	Llama.Goal.Reset();
	McPhysics::TickPlayer(Manager.GetWorld(), Llama.Body, Input);
	return true;
}
