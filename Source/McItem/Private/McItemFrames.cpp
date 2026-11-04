#include "McItemFrames.h"
#include "Dom/JsonObject.h"

namespace
{
	// A needle on a spring: each tick the speed gains Pull times the way left to the target and is then scaled by
	// Damping, and the needle moves by the speed. Fitted to the logged vanilla runs frames1 and frames2 (the picture the
	// game picks every tick for a compass and a clock after a turn or a change of time): the compass fits at 0.1 and
	// 0.8 exactly (the one optimum, every frame of the first 240 ticks), the clock only inside 0.11 to 0.12 and 0.89 to
	// 0.90 (frames within one of the log everywhere), so the middle of that is used.
	struct FNeedle
	{
		double Value = 0.0;
		double Speed = 0.0;
		bool bSet = false;
		double Pull = 0.1;
		double Damping = 0.8;

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
			Speed += Difference * Pull;
			Speed *= Damping;
			Value += Speed;
			if (bWrap)
			{
				Value -= FMath::FloorToDouble(Value);
			}
		}
	};

	// The needles of compasses that point at a place of their own, by place (a lodestone or a death).
	struct FTracked
	{
		FNeedle Needle;
		int64 LastTicks = INT64_MIN;
	};
	TMap<uint64, FTracked> Tracked;

McItemFrames::FContext Current;
	FNeedle CompassNeedle;
	FNeedle ClockNeedle = []() { FNeedle Needle; Needle.Pull = 0.115; Needle.Damping = 0.895; return Needle; }();
	// A spinning needle (another dimension, or no target): a random drift. Not measured at all: a placeholder
	// until a run logs a clock or compass out of the overworld.
	FNeedle SpinNeedle;
	int64 LastTicks = INT64_MIN;
	FRandomStream Random(1234);
	double SpinTarget = 0.0;
}

namespace McItemFrames
{
	double ClockValue(double DayTime)
	{
		// The frame the clock settles on every thousand ticks of the day (run frames2, each held for 120 ticks; the
		// frame at 24000 is the one at 0 a turn on). Between them the value is interpolated.
		static const int32 Frames[25] = { 50, 53, 55, 58, 60, 62, 64, 66, 68, 70, 73, 75, 78, 81, 84, 87, 90, 93, 96, 99, 102, 105, 108, 111, 114 };
		double Position = FMath::Fmod(DayTime, 24000.0) / 1000.0;
		if (Position < 0.0)
		{
			Position += 24.0;
		}
		const int32 Index = FMath::Clamp(FMath::FloorToInt32(Position), 0, 23);
		const double Fraction = Position - Index;
		const double Value = (Frames[Index] * (1.0 - Fraction) + Frames[Index + 1] * Fraction) / 64.0;
		return Value - FMath::FloorToDouble(Value);
	}

	double CompassValue(const FVector2D& From, double Yaw, const FVector2D& Target)
	{
		// Fitted to run frames1: with the target at a block centre, the frame the compass settles on is
		// 16 + 32 * (0.5 - (yaw / 360 - 0.25 - atan2(dz, dx) / 2pi)) rounded, mod 32 (yaw 90 at 20 blocks east of the spawn
		// settled on 16, yaw 0 on 24, yaw 180 on 8, yaw -90 on 0, and the other places and turns tried agreed).
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
		const double ClockTarget = ClockValue(Context.DayTime);
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

	bool GetLodestone(const FMcItemStack& Stack, FIntVector& OutPos, FName& OutDimension)
	{
		const TSharedPtr<FJsonValue> Value = Stack.Get(TEXT("minecraft:lodestone_tracker"));
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Target;
		if (!Value->AsObject()->TryGetObjectField(TEXT("target"), Target))
		{
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Pos;
		FString Dimension;
		if (!(*Target)->TryGetArrayField(TEXT("pos"), Pos) || Pos->Num() != 3 || !(*Target)->TryGetStringField(TEXT("dimension"), Dimension))
		{
			return false;
		}
		OutPos = FIntVector(static_cast<int32>((*Pos)[0]->AsNumber()), static_cast<int32>((*Pos)[1]->AsNumber()), static_cast<int32>((*Pos)[2]->AsNumber()));
		OutDimension = FName(*Dimension);
		return true;
	}

	void SetLodestone(FMcItemStack& Stack, const FIntVector& Pos, FName Dimension)
	{
		const TSharedRef<FJsonObject> Target = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Place;
		Place.Add(MakeShared<FJsonValueNumber>(Pos.X));
		Place.Add(MakeShared<FJsonValueNumber>(Pos.Y));
		Place.Add(MakeShared<FJsonValueNumber>(Pos.Z));
		Target->SetArrayField(TEXT("pos"), Place);
		Target->SetStringField(TEXT("dimension"), Dimension.ToString());
		const TSharedRef<FJsonObject> Tracker = MakeShared<FJsonObject>();
		Tracker->SetObjectField(TEXT("target"), Target);
		Tracker->SetBoolField(TEXT("tracked"), true);
		Stack.Set(TEXT("minecraft:lodestone_tracker"), MakeShared<FJsonValueObject>(Tracker));
	}

	FName ResolveStack(const FMcItemStack& Stack)
	{
		const bool bRecovery = Stack.Item == TEXT("minecraft:recovery_compass");
		if (Stack.Item != TEXT("minecraft:compass") && !bRecovery)
		{
			return Resolve(Stack.Item);
		}
		FIntVector Place;
		FName Dimension;
		bool bPointed = false;
		FVector2D Target = FVector2D::ZeroVector;
		if (bRecovery)
		{
			bPointed = Current.bHasDeath && Current.DeathDimension == Current.Dimension;
			Target = Current.DeathPosition;
		}
		else if (GetLodestone(Stack, Place, Dimension))
		{
			bPointed = Dimension == Current.Dimension;
			Target = FVector2D(Place.X + 0.5, Place.Z + 0.5);
		}
		else
		{
			return Resolve(Stack.Item);
		}
		const FString Prefix = bRecovery ? TEXT("recovery_compass") : TEXT("compass");
		if (!bPointed)
		{
			// Nowhere to point in this dimension: the needle drifts about (not measured, see SpinNeedle).
			return FName(*FString::Printf(TEXT("minecraft:%s_%02d"), *Prefix, (16 + FrameOf(SpinNeedle.Value, 32)) % 32));
		}
		const uint64 Key = (static_cast<uint64>(GetTypeHash(Target.X)) << 32) ^ GetTypeHash(Target.Y) ^ (bRecovery ? 1ull : 0ull);
		FTracked& Needle = Tracked.FindOrAdd(Key);
		const int64 Steps = !Needle.Needle.bSet ? 1 : FMath::Clamp<int64>(Current.GameTicks - Needle.LastTicks, 0, 20);
		Needle.LastTicks = Current.GameTicks;
		const double Value = CompassValue(Current.Position, Current.Yaw, Target);
		for (int64 Step = 0; Step < Steps; ++Step)
		{
			Needle.Needle.Step(Value, true);
		}
		return FName(*FString::Printf(TEXT("minecraft:%s_%02d"), *Prefix, (16 + FrameOf(Needle.Needle.Value, 32)) % 32));
	}
}
