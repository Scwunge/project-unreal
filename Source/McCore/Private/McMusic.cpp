#include "McMusic.h"

FMcMusicPicker::EAction FMcMusicPicker::Tick(const FMcDimensionType::FMusic* Wanted, FName Playing, float Draw)
{
	if (!Playing.IsNone())
	{
		bWasPlaying = true;
		if (Wanted && Wanted->bReplaceCurrent && Playing != Wanted->Sound)
		{
			// The new music starts on the next tick.
			Delay = 0;
			bWasPlaying = false;
			return EAction::Stop;
		}
		return EAction::None;
	}
	if (bWasPlaying)
	{
		// A track just ended: wait before the next.
		bWasPlaying = false;
		const int32 Min = Wanted ? Wanted->MinDelay : 12000;
		const int32 Max = Wanted ? FMath::Max(Wanted->MaxDelay, Min) : 24000;
		Delay = Min + FMath::FloorToInt32(FMath::Clamp(Draw, 0.0f, 1.0f) * (Max - Min));
	}
	if (!Wanted || Wanted->Sound.IsNone())
	{
		return EAction::None;
	}
	// Somewhere with shorter waits cuts a long one short.
	Delay = FMath::Min(Delay, Wanted->MaxDelay);
	if (--Delay > 0)
	{
		return EAction::None;
	}
	// Counted as played even if it fails to start, so a missing track waits like any other rather than retrying.
	bWasPlaying = true;
	return EAction::Start;
}

const FMcDimensionType::FMusic* FMcMusicPicker::Choose(const TMap<FName, FMcDimensionType::FMusic>& Lists, bool bCreative)
{
	if (bCreative)
	{
		if (const FMcDimensionType::FMusic* Creative = Lists.Find(TEXT("creative")))
		{
			return Creative;
		}
	}
	return Lists.Find(TEXT("default"));
}
