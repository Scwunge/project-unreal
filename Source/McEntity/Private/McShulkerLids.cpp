#include "McShulkerLids.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McPhysics.h"
#include "McShulker.h"

namespace
{
	bool IsBoxAt(const FMcBlockWorld& World, const FIntVector& Pos)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const FMcStateId State = World.GetBlock(Pos);
		return Registry.IsValidState(State) && McShulker::IsBox(Registry.GetBlockOf(State).Name);
	}

	// The block at Pos grown by Amount the way Facing goes.
	FBox Grow(const FIntVector& Pos, const FIntVector& Facing, double Amount)
	{
		FBox Box(FVector(Pos), FVector(Pos) + FVector(1.0));
		const FVector Along = FVector(Facing) * Amount;
		Box.Min += Along.ComponentMin(FVector::ZeroVector);
		Box.Max += Along.ComponentMax(FVector::ZeroVector);
		return Box;
	}
}

void FMcShulkerLids::SetOpeners(const FIntVector& Pos, int32 Count)
{
	if (Count > 0 || Lids.Contains(Pos))
	{
		Lids.FindOrAdd(Pos).Openers = FMath::Max(Count, 0);
	}
}

void FMcShulkerLids::SetOpen(const FIntVector& Pos, int32 Count)
{
	if (Count > 0)
	{
		FLid& Lid = Lids.FindOrAdd(Pos);
		Lid.Openers = Count;
		Lid.Progress = 1.0f;
	}
}

int32 FMcShulkerLids::GetOpeners(const FIntVector& Pos) const
{
	const FLid* Lid = Lids.Find(Pos);
	return Lid ? Lid->Openers : 0;
}

float FMcShulkerLids::GetProgress(const FIntVector& Pos) const
{
	const FLid* Lid = Lids.Find(Pos);
	return Lid ? Lid->Progress : 0.0f;
}

FIntVector FMcShulkerLids::GetFacing(FMcStateId State)
{
	const FName Facing = FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("facing"));
	if (Facing == TEXT("north")) return FIntVector(0, 0, -1);
	if (Facing == TEXT("south")) return FIntVector(0, 0, 1);
	if (Facing == TEXT("west")) return FIntVector(-1, 0, 0);
	if (Facing == TEXT("east")) return FIntVector(1, 0, 0);
	if (Facing == TEXT("down")) return FIntVector(0, -1, 0);
	return FIntVector(0, 1, 0);
}

bool FMcShulkerLids::IsBlocked(const FMcBlockWorld& World, const FIntVector& Pos)
{
	const FIntVector Facing = GetFacing(World.GetBlock(Pos));
	// The half block in front: the box grown by a half, less the box.
	const FBox Grown = Grow(Pos, Facing, 0.5);
	FBox Front = Grown;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Facing[Axis] > 0)
		{
			Front.Min[Axis] = Pos[Axis] + 1.0;
		}
		else if (Facing[Axis] < 0)
		{
			Front.Max[Axis] = Pos[Axis];
		}
	}
	TArray<FBox> Found;
	McPhysics::GatherBoxes(World, Front.ExpandBy(-1e-7), Found);
	// Its own shell, already open for someone else, is no block.
	const FBox Own(FVector(Pos), FVector(Pos) + FVector(1.0));
	return Found.ContainsByPredicate([&](const FBox& Box) { return !Box.IsInsideOrOn(Own.Min) || !Box.IsInsideOrOn(Own.Max); });
}

void FMcShulkerLids::Tick()
{
	++Ticks;
	Pushes.Reset();
	for (auto It = Lids.CreateIterator(); It; ++It)
	{
		const FIntVector Pos = It.Key();
		FLid& Lid = It.Value();
		if (!IsBoxAt(World, Pos))
		{
			// Broken: the block's own change cleared its collision.
			It.RemoveCurrent();
			continue;
		}
		const float Old = Lid.Progress;
		Lid.Progress = FMath::Clamp(Lid.Progress + (Lid.Openers > 0 ? Step : -Step), 0.0f, 1.0f);
		const FIntVector Facing = GetFacing(World.GetBlock(Pos));
		if (Lid.Progress > Old)
		{
			// What the lid meets as it rises goes with it, a hundredth further.
			const FBox Shifted = FBox(FVector(Pos), FVector(Pos) + FVector(1.0)).ShiftBy(FVector(Facing) * Lid.Progress);
			FBox Area = Shifted;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				Area.Max[Axis] += Facing[Axis] > 0 ? 0.01 : 0.0;
				Area.Min[Axis] -= Facing[Axis] < 0 ? 0.01 : 0.0;
			}
			Pushes.Add({ Area, FVector(Facing) * (Lid.Progress - Old + 0.01) });
		}
		if (Lid.Progress <= 0.0f)
		{
			World.ClearCollisionOverride(Pos);
			if (Lid.Openers == 0)
			{
				It.RemoveCurrent();
			}
			continue;
		}
		const FBox Grown = Grow(Pos, Facing, 0.5 * Lid.Progress);
		const FVector Corner(Pos);
		World.SetCollisionOverride(Pos, { FBox3f(FVector3f(Grown.Min - Corner), FVector3f(Grown.Max - Corner)) });
	}
}

FVector FMcShulkerLids::GetPush(const FBox& Box) const
{
	FVector Motion = FVector::ZeroVector;
	for (const FPush& Push : Pushes)
	{
		if (Push.Area.Max.X > Box.Min.X && Push.Area.Min.X < Box.Max.X && Push.Area.Max.Y > Box.Min.Y && Push.Area.Min.Y < Box.Max.Y
			&& Push.Area.Max.Z > Box.Min.Z && Push.Area.Min.Z < Box.Max.Z)
		{
			Motion += Push.Motion;
		}
	}
	return Motion;
}
