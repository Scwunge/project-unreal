#include "McBell.h"

bool McBell::IsBell(FMcStateId State)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	return Registry.IsValidState(State) && Registry.GetBlockOf(State).Name == FName(TEXT("minecraft:bell"));
}

int32 McBell::GetStruckSide(FMcStateId State, const FIntVector& BellPos, const FVector& Eye)
{
	if (!IsBell(State))
	{
		return INDEX_NONE;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FVector Away = Eye - (FVector(BellPos) + FVector(0.5));
	const bool bAlongZ = FMath::Abs(Away.Z) >= FMath::Abs(Away.X);
	const int32 Side = bAlongZ ? (Away.Z < 0.0 ? 0 : 1) : (Away.X < 0.0 ? 2 : 3);

	// Which way the bar runs: a floor bell's across its facing, a wall bell's along it.
	const FName Attachment = Registry.GetPropertyValue(State, TEXT("attachment"));
	if (Attachment == TEXT("ceiling"))
	{
		return Side;
	}
	const FName Facing = Registry.GetPropertyValue(State, TEXT("facing"));
	const bool bFacingAlongZ = Facing == TEXT("north") || Facing == TEXT("south");
	const bool bBarAlongZ = Attachment == TEXT("floor") ? !bFacingAlongZ : bFacingAlongZ;
	return bAlongZ != bBarAlongZ ? Side : INDEX_NONE;
}

float McBell::GetSwing(float Ticks)
{
	if (Ticks < 0.0f || Ticks >= RingTicks)
	{
		return 0.0f;
	}
	// About three swings out and back, starting away from the blow and fading to nothing.
	const float Left = 1.0f - Ticks / RingTicks;
	return FMath::Sin(Ticks * (PI / 8.0f)) * 0.45f * Left * Left;
}
