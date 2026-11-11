// The creaking (minecraft.wiki, "Creaking"). It moves only while no player looks at it: a Survival or Adventure player facing within 60
// degrees of it (yaw and pitch both), with nothing solid between, freezes it. One tied to a creaking heart takes no damage (a hit makes
// it sway and the heart grow resin at night) and is never more than 32 blocks from the heart. Every number is the wiki's and
// UNVERIFIED against the reference client (see PLAN.md "Creaking"); the box, health and the Normal bite are in the mob table.

#pragma once

#include "CoreMinimal.h"
#include "McMob.h"

namespace McCreaking
{
	// A creaking within this many blocks of a player who looks at it becomes alerted.
	constexpr double AlertRange = 12.0;
	// It loses interest when every player is further than this (the wiki gives no figure; UNVERIFIED, the heart's own radius is used).
	constexpr double LoseRange = 32.0;
	// A creaking tied to a heart is never past this from it (the Creaking page says 32, the Creaking Heart page 34; 32 taken).
	constexpr double LeashRange = 32.0;
	// How far the heart looks for a place for one: 16 across, 8 up and down.
	constexpr int32 SpawnAcross = 16;
	constexpr int32 SpawnUpDown = 8;
	// Sharing a block with a player for longer than 5 s despawns it (and the heart makes another).
	constexpr int32 SameBlockTicks = 100;
	// Gaps between bites (not on the page; UNVERIFIED, the zombie's 20).
	constexpr int32 BiteIntervalTicks = 20;
	// The bite: 2.5 Easy, 3 Normal, 4 Hard (the infobox says 4.5; the text 4).
	MCENTITY_API float GetBiteDamage(int32 Difficulty);
}

class MCENTITY_API FMcCreaking : public FMcMob
{
public:
	explicit FMcCreaking(const FMcMobType& InType);
	static const FName& StaticType();

	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool Serialize(FArchive& Ar) override;

	// Ties it to the heart at that block: it takes no damage from now on.
	void LinkTo(const FIntVector& InHeart);
	bool IsLinked() const { return bLinked; }
	const FIntVector& GetHeart() const { return Heart; }
	bool IsAlerted() const { return bAlerted; }
	// Whether a player is looking at it now (so it holds still).
	bool IsObserved() const { return bObserved; }
	// Distance from its heart, or -1 if it is not tied to one.
	double GetHeartDistance() const;

	// A hit on a creaking that is tied: the host tells the heart (resin at night).
	TFunction<void(const FIntVector&)> OnHit;
	// It left the heart's reach or sat in a player's block too long: the host puts another near the heart. Not called when the
	// heart is gone or it died.
	TFunction<void(const FIntVector&)> OnLost;

private:
	FIntVector Heart = FIntVector::ZeroValue;
	bool bLinked = false;
	bool bAlerted = false;
	bool bObserved = false;
	int32 BiteWait = 0;
	int32 SameBlock = 0;
};
