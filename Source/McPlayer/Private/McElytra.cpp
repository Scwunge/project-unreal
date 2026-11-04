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
	const double PitchRad = FMath::DegreesToRadians(static_cast<double>(Pitch));
	const double CosPitch = FMath::Abs(FMath::Cos(PitchRad));
	double Up = Velocity.Y;
	double Ahead = FVector2D(Velocity.X, Velocity.Z).Size();
	// The wings hold up most of the weight looking level and none looking straight down.
	Up -= Gravity * (1.0 - Carry * FMath::Pow(CosPitch, CarryPower));
	if (Up < 0.0)
	{
		// Some of the fall is turned into speed ahead, less the steeper the look.
		const double Gain = -Up * FallToAhead * CosPitch;
		Up += Gain;
		Ahead += Gain;
	}
	if (Pitch < 0.0f)
	{
		// Looking up trades speed ahead for height.
		const double Climb = Ahead * FMath::Sin(-PitchRad) * AheadToClimb;
		Up += Climb;
		Ahead -= Climb;
	}
	const double Keep = 1.0 - DragPerSpeed * FMath::Sqrt(Up * Up + Ahead * Ahead);
	Up *= KeepUp * Keep;
	Ahead *= Keep;
	// The motion along the ground is pulled toward the look's heading at the same speed, so a sharp turn loses speed.
	const FVector Look = LookOf(Yaw, 0.0f);
	const FVector2D Was(Velocity.X, Velocity.Z);
	const FVector2D Way = Was.SizeSquared() > 1e-12 ? Was.GetSafeNormal() * Ahead : FVector2D(Look.X, Look.Z) * Ahead;
	const FVector2D Flat = Way + (FVector2D(Look.X, Look.Z) * Ahead - Way) * Turn;
	return FVector(Flat.X, Up, Flat.Y);
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
