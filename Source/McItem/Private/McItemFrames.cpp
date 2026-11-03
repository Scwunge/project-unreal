#include "McItemFrames.h"

namespace
{
	// A needle on a spring: it chases its target, a tenth of the way each tick with the speed damped by 0.8 (the game's
	// wobble; the constants are not measured).
	struct FNeedle
	{
		double Value = 0.0;
		double Speed = 0.0;
		bool bSet = false;

		void Step(double Target, bool bWrap)
		{
			if (!bSet)
			{
				Value = Target;
				Speed = 0.0;
				bSet = true;
				return;
			}
			double Difference = Target - Value;
			if (bWrap)
			{
				Difference -= FMath::FloorToDouble(Difference + 0.5);
			}
			Speed += Difference * 0.1;
			Speed *= 0.8;
			Value += Speed;
			if (bWrap)
			{
				Value -= FMath::FloorToDouble(Value);
			}
		}
	};

	McItemFrames::FContext Current;
	FNeedle CompassNeedle;
	FNeedle ClockNeedle;
	// A spinning needle (another dimension, or no target): a random drift.
	FNeedle SpinNeedle;
	int64 LastTicks = INT64_MIN;
	FRandomStream Random(1234);
	double SpinTarget = 0.0;
}

namespace McItemFrames
{
	double CompassValue(const FVector2D& From, double Yaw, const FVector2D& Target)
	{
		// The game: the angle to the target from east (atan2 of the z and x differences, in turns), less the yaw from
		// south (a quarter turn off), turned about so that 0.5 is dead ahead.
		const double Facing = FMath::Fmod(Yaw / 360.0, 1.0);
		const double ToTarget = FMath::Atan2(Target.Y - From.Y, Target.X - From.X) / (2.0 * UE_DOUBLE_PI);
		const double Value = 0.5 - (Facing - 0.25 - ToTarget);
		return Value - FMath::FloorToDouble(Value);
	}

	int32 FrameOf(double Value, int32 Count)
	{
		// Thresholds at 0, 0.5, 1.5 ... : the frame is the value times Count, rounded half up, and the last threshold wraps.
		const int32 Frame = FMath::FloorToInt32(Value * Count + 0.5);
		return ((Frame % Count) + Count) % Count;
	}

	void SetContext(const FContext& Context)
	{
		Current = Context;
		int32 Steps = 1;
		if (LastTicks != INT64_MIN)
		{
			Steps = static_cast<int32>(FMath::Clamp<int64>(Context.GameTicks - LastTicks, 0, 20));
		}
		LastTicks = Context.GameTicks;
		const double Target = CompassValue(Context.Position, Context.Yaw, Context.Spawn);
		const double Day = FMath::Fmod(Context.DayTime / 24000.0, 1.0);
		const double ClockTarget = Day - FMath::FloorToDouble(Day);
		if (!CompassNeedle.bSet)
		{
			Steps = FMath::Max(Steps, 1);
		}
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			if (Random.FRand() < 0.05f)
			{
				SpinTarget = Random.FRand();
			}
			CompassNeedle.Step(Target, true);
			ClockNeedle.Step(ClockTarget, true);
			SpinNeedle.Step(SpinTarget, true);
		}
	}

	FName Resolve(FName Item)
	{
		const FString Name = Item.ToString();
		if (Name == TEXT("minecraft:compass") || Name == TEXT("minecraft:recovery_compass"))
		{
			const bool bRecovery = Name == TEXT("minecraft:recovery_compass");
			// The compass points at the spawn in the overworld; the recovery one has no death place to point at here.
			const double Value = (Current.bOverworld && !bRecovery) ? CompassNeedle.Value : SpinNeedle.Value;
			const int32 Frame = (16 + FrameOf(Value, 32)) % 32;
			return FName(*FString::Printf(TEXT("minecraft:%s_%02d"), bRecovery ? TEXT("recovery_compass") : TEXT("compass"), Frame));
		}
		if (Name == TEXT("minecraft:clock"))
		{
			const double Value = Current.bOverworld ? ClockNeedle.Value : SpinNeedle.Value;
			return FName(*FString::Printf(TEXT("minecraft:clock_%02d"), FrameOf(Value, 64)));
		}
		return Item;
	}
}
