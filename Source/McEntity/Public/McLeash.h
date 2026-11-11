// Leads (minecraft.wiki: "Lead"; numbers from the reference client, vehicle_log22.txt): a mob or a boat tied to a player or to a
// knot on a fence post. A mob tied to a player walks after them once they are 3.5 blocks off (a push of 0.0625 a tick for a
// pig); past 5.8 blocks the lead pulls on anything, 0.0625 a tick and 0.088 more for each block over (a boat settles 7.6 behind a player walking at 0.54); past 12 it snaps and the lead drops. A fence
// knot is an entity at the middle of the post, 0.375 above its base.
//
// Not done: the rope is not drawn, nothing is saved (a loaded world has no leashes or knots), and the holder player is found by
// staying near where they were (the manager only knows where players are, not who).

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

// The knot on a fence post.
class MCENTITY_API FMcLeashKnot final : public FMcEntity
{
public:
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual bool IsPersistent() const override { return false; }
};

namespace McLeash
{
	static constexpr double FollowDistance = 3.5;
	static constexpr double PullDistance = 5.8;
	static constexpr double SnapDistance = 12.0;
	static constexpr double FollowPush = 0.0625;
	static constexpr double PullPush = 0.088;
	// How far from the player the mobs are that a fence takes.
	static constexpr double TieRadius = 7.0;

	// Whether a lead can tie it: mobs that are not hostile, and boats.
	MCENTITY_API bool CanLeash(const FMcEntity& Entity);
	// Ties Entity to the player standing at PlayerFeet; false if it can't be (hostile, or tied already).
	MCENTITY_API bool Attach(FMcEntity& Entity, const FVector& PlayerFeet);
	// Unties it, dropping the lead where it stands (unless bDrop is false).
	MCENTITY_API void Detach(FMcEntityManager& Manager, FMcEntity& Entity, bool bDrop = true);
	// Every leashed entity once a tick, before the entities move: holds, pulls and snaps them.
	MCENTITY_API void Tick(FMcEntityManager& Manager);
	// A lead used on a fence post at Cell (the player at PlayerFeet): the mobs they lead within 7 blocks are tied to a knot
	// there; with none led, the mobs on the knot there come back to the player's hand. True if anything happened.
	MCENTITY_API bool UseFence(FMcEntityManager& Manager, const FIntVector& Cell, const FVector& PlayerFeet, bool bHoldingLead);
}
