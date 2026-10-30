#include "McElytra.h"
#include "McItemStack.h"

bool McElytra::CanGlide(const FMcItemStack& Chest)
{
	static const FName Elytra(TEXT("minecraft:elytra"));
	return Chest.Item == Elytra &&(!Chest.IsDamageable() || Chest.GetDamage() < Chest.GetMaxDamage() - 1);
}

FVector McElytra::LookOf(float Yaw, float Pitch)
{
	const double YawRad = FMath::DegreesToRadians(static_cast<double>(Yaw));
	const double PitchRad = FMath::DegreesToRadians(static_cast<double>(Pitch));
	return FVector(-FMath::Sin(YawRad) * FMath::Cos(PitchRad), -FMath::Sin(PitchRad), FMath::Cos(YawRad) * FMath::Cos(PitchRad));
}

FVector McElytra::Glide(const FVector& Velocity, float Yaw, float Pitch)
{
	const FVector Look = LookOf(Yaw, Pitch);
	const double CosPitch = FMath::Cos(FMath::DegreesToRadians(static_cast<double>(Pitch)));
	FVector Out = Velocity;
	const double Speed = Velocity.Size();
	const double Along = FVector::DotProduct(Velocity, Look);
	if (Speed > 1e-9 && Along > 0.0)
	{
		// Lift, square across the way it moves (so it turns the motion and adds no speed), toward up.
		const FVector Way = Velocity / Speed;
		const FVector Across = FVector(0.0, 1.0, 0.0) - Way * Way.Y;
		if (Across.SizeSquared() > 1e-12)
		{
			Out += Across.GetSafeNormal() * (FMath::Min(LiftPerSpeed * Along, MaxLift) * CosPitch * CosPitch);
		}
	}
	Out -= Velocity * (Drag + DragPerSpeed * Speed);
	Out.Y -= Gravity;
	// The body swings round to fly the way it looks, keeping its speed.
	const double After = Out.Size();
	return Out + (Look * After - Out) * Turn;
}

FVector McElytra::Boost(const FVector& Velocity, float Yaw, float Pitch)
{
	return Velocity + (LookOf(Yaw, Pitch) * BoostSpeed - Velocity) * BoostPull;
}

int32 McElytra::RollBoostTicks(FRandomStream& Random, int32 FlightDuration)
{
	return 10 * (FMath::Max(FlightDuration, 0) + 1) + Random.RandRange(0, 5) + Random.RandRange(0, 6);
}

float McElytra::WallDamage(double SpeedBefore, double SpeedAfter)
{
	return FMath::Max(0.0f, static_cast<float>(10.0 * (SpeedBefore - SpeedAfter) - 3.0));
}
