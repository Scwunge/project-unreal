// Fishing (minecraft.wiki: "Fishing", "Fishing Rod"): the bobber a rod casts. It flies under gravity (0.03 a tick) and
// drag (0.99 across, 0.92 up and down), hooks the first mob, vehicle or item it touches, and otherwise lands: on a
// block, where it lies (and goes after 1200 ticks), or in water, where it floats at the surface and waits for a bite.
//
// The wait is 100 to 600 ticks, 100 less at each end for each level of Lure (a wait that comes out at nothing is
// over at once). Each tick counts it down by 1, but under a roof (no sky) only half the ticks count, and in rain each
// tick has a quarter chance of counting 2. Then a fish swims in from a little way off for 20 to 80 ticks, leaving a
// wake, and bites: the bobber dips with a splash, and for 20 to 40 ticks reeling in catches something. Missed, the
// wait starts again.
//
// Reeling in (Reel) is what the rod's use does while the bobber is out. The rod wears by what was reeled: 1 for a
// catch, 2 for a bobber in the ground, 3 for an item and 5 for anything else hooked, which is pulled toward the
// angler by a tenth of the way between. Whoever knows the loot tables rolls the catch (McLoot::RollFishing) and throws
// it to the angler. The angler's server code keeps the bobber's Angler fields up to date, and takes it away when they
// stop holding the rod or go more than 32 blocks from it.

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

namespace McFishing
{
	constexpr int32 MinWait = 100;
	constexpr int32 MaxWait = 600;
	constexpr int32 LureStep = 100;
	constexpr int32 MinApproach = 20;
	constexpr int32 MaxApproach = 80;
	constexpr int32 MinBite = 20;
	constexpr int32 MaxBite = 40;
	// The angler loses the bobber beyond this, and anything it holds is let go.
	constexpr double MaxDistance = 32.0;
	// Ticks a bobber may lie on the ground before it goes.
	constexpr int32 GroundTicks = 1200;

	// What reeling in costs the rod.
	constexpr int32 CatchDamage = 1;
	constexpr int32 GroundDamage = 2;
	constexpr int32 ItemDamage = 3;
	constexpr int32 EntityDamage = 5;

	// A wait for a bite with Lure at this level, in ticks (at least 1).
	MCENTITY_API int32 RollWait(FRandomStream& Random, int32 Lure);
	// How far one tick counts the wait down: 0 or 1 without sky over the bobber, 1 or 2 in rain, else 1.
	MCENTITY_API int32 CountdownStep(FRandomStream& Random, bool bSky, bool bRaining);

	// Open water (needed for treasure): the 5 by 4 by 5 blocks round the bobber's block, from one below it to two
	// above, each layer all air (and lily pads) or all still water (sources, and waterlogged blocks with nothing to
	// stand on), with no bubble columns.
	MCENTITY_API bool IsOpenWater(const FMcBlockWorld& World, const FIntVector& Block);
}

class MCENTITY_API FMcFishingBobber final : public FMcEntity
{
public:
	FMcFishingBobber();
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool IsPersistent() const override { return false; }

	enum class EState : uint8
	{
		Flying,
		// Hooked on HookedId, riding along with it.
		Hooked,
		// Lying in water, waiting for a fish.
		Bobbing,
	};

	// Casts from the angler's eye (block coordinates) along their look (Minecraft yaw and pitch, degrees): a little
	// below and ahead of the eye, at about 0.6 a tick with a small spread.
	void Cast(const FVector& Eye, float LookYaw, float LookPitch, FRandomStream& Random);

	struct FReel
	{
		// What the rod is worn by.
		int32 Damage = 0;
		// A fish was on: the caller rolls the catch and throws it from the bobber to the angler.
		bool bCatch = false;
	};
	// The angler reels in: pulls what is hooked, lands a fish that is biting, or just winds in. The bobber is gone
	// after (the caller needn't remove it).
	FReel Reel(FMcEntityManager& Manager);

	// Set by the angler's server code: where their eye is (for pulls) and the rod's Lure and Luck of the Sea.
	FVector AnglerEye = FVector::ZeroVector;
	int32 Lure = 0;
	int32 Luck = 0;
	// The angler's player id (their player state's), for clients to find whose rod the line runs to.
	int32 AnglerId = 0;

	EState State = EState::Flying;
	int32 HookedId = 0;
	// Bobbing: ticks left of the wait, the fish's approach and the bite (only one runs at a time).
	int32 WaitTicks = 0;
	int32 ApproachTicks = 0;
	int32 BiteTicks = 0;
	// Where the approaching fish is, across the water from the bobber (blocks), for the wake clients draw.
	FVector2D FishOffset = FVector2D::ZeroVector;
	// Counts bites, so clients can splash when it changes.
	int32 Bites = 0;
	int32 GroundTime = 0;
	bool bOpenWater = true;

	// The thrown catch's speed toward the angler from the bobber at From: a tenth of the way, with a lift by the
	// square root of the distance so it arcs over to them.
	static FVector ThrowToward(const FVector& From, const FVector& To);

private:
	// The height of the water's top in the bobber's column at its block or the one below; unset out of water.
	TOptional<double> WaterSurface(const FMcBlockWorld& World) const;
};
