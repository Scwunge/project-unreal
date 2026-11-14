// The warden (minecraft.wiki, "Warden"). Blind: it goes by the vibrations it hears and by smell, keeps an anger number for each
// thing it has noticed, and hunts the angriest. Every number here is from the wiki page and UNVERIFIED against the reference
// client (which cannot yet summon one and log its anger; see PLAN.md "Warden" for the verbs that would be needed), except the
// box, health and bite taken into the mob table.
//
// FMcWardenBrain is the anger table and timers on their own, so it can be tested without a world; FMcWarden is the mob that uses it.

#pragma once

#include "CoreMinimal.h"
#include "McMob.h"

namespace McWarden
{
	constexpr int32 MaxAnger = 150;
	// At this anger a suspect is a target and the warden turns hostile.
	constexpr int32 HostileAnger = 80;
	// What each kind of notice adds ("Anger": vibration 35, a projectile's 10 or 35 if a second comes within 5 seconds, contact 35, an attack 100,
	// turning hostile 20, the sniff's nearest 35).
	constexpr int32 VibrationAnger = 35;
	constexpr int32 ProjectileAnger = 10;
	constexpr int32 ContactAnger = 35;
	constexpr int32 AttackAnger = 100;
	constexpr int32 RoarAnger = 20;
	constexpr int32 SniffAnger = 35;
	// Anger falls by 1 a second (20 ticks).
	constexpr int32 DecayTicks = 20;
	// Two seconds between noticing vibrations; a second between contact angers.
	constexpr int32 ListenCooldownTicks = 40;
	constexpr int32 ContactCooldownTicks = 20;
	// A second projectile within 5 seconds counts as a full vibration.
	constexpr int32 ProjectileWindowTicks = 100;
	// A projectile shot from further than this adds nothing (but still counts toward the second).
	constexpr double ProjectileFarRange = 30.0;
	// It hears within 15 blocks (an ovoid on the wiki; McGameEvents::WardenCanHear uses a sphere).
	constexpr double ListenRange = 15.0;
	// A sniff takes 4.2 s, has a 5 to 10 s wait, and ends by angering the nearest within 6 across and 20 up and down.
	constexpr int32 SniffTicks = 84;
	constexpr int32 SniffWaitMin = 100;
	constexpr int32 SniffWaitMax = 200;
	constexpr double SniffRadius = 6.0;
	constexpr double SniffHeight = 20.0;
	// Turning hostile it roars for 4.2 s; hit from within 5 blocks it skips the roar.
	constexpr int32 RoarTicks = 84;
	constexpr double RoarSkipRange = 5.0;
	// The melee swing waits 36 ticks.
	constexpr int32 MeleeCooldownTicks = 36;
	// The sonic boom: 10 s after it noticed the target, 5 s after its last attack, within 14 across and 20 up and down;
	// 1.7 s to charge, 3 s from the start to the next attack. 10 damage on Normal (6 Easy, 15 Hard), through armor.
	constexpr int32 SonicNoticeTicks = 200;
	constexpr int32 SonicAttackTicks = 100;
	constexpr double SonicRange = 14.0;
	constexpr double SonicHeight = 20.0;
	constexpr int32 SonicChargeTicks = 34;
	constexpr int32 SonicTotalTicks = 60;
	// It digs back into the ground after 60 s with nothing noticed.
	constexpr int32 CalmDespawnTicks = 1200;
	// Darkness for 13 s to every player within 20 blocks, renewed every 6 s.
	constexpr int32 DarknessEveryTicks = 120;
	constexpr int32 DarknessTicks = 260;
	constexpr double DarknessRadius = 20.0;
	// Rising out of the ground: the page gives no time (UNVERIFIED, 134 ticks is a guess), and it hears nothing meanwhile.
	constexpr int32 EmergeTicks = 134;
	// The hits of the table are Normal's; Easy 16, Hard 45 (melee) and 6, 15 (sonic boom).
	MCENTITY_API float GetMeleeDamage(int32 Difficulty);
	MCENTITY_API float GetSonicDamage(int32 Difficulty);
}

class MCENTITY_API FMcWardenBrain
{
public:
	struct FSuspect
	{
		int32 Id = 0;
		int32 Anger = 0;
		FVector LastSeen = FVector::ZeroVector;
		int32 ContactCooldown = 0;
		// Ticks left in which another projectile from it counts in full.
		int32 ProjectileWindow = 0;
		// Ticks since it was first noticed (for the sonic boom's ten seconds).
		int32 NoticedFor = 0;
	};

	// One tick: anger falls, the timers run down.
	void Tick();

	// A vibration (not a projectile's) from the suspect, at Pos. False if it was ignored (the 2 second wait).
	bool Hear(int32 Id, const FVector& Pos);
	// A projectile heard; Distance is how far from the warden it was shot.
	bool HearProjectile(int32 Id, const FVector& Pos, double Distance);
	// A mob or player bumping into it.
	void Touch(int32 Id, const FVector& Pos);
	// A hit from the suspect.
	void Attacked(int32 Id, const FVector& Pos);
	// Adds to the anger directly (the sniff's 35, the roar's 20).
	void AddAnger(int32 Id, const FVector& Pos, int32 Amount);
	// The suspect died or left the world, or is in Creative: all anger at it goes.
	void Clear(int32 Id);
	void ClearAll() { Suspects.Reset(); }

	// The angriest suspect, or null. IsAngry: its anger has reached 80.
	const FSuspect* GetTarget() const;
	bool IsAngry() const { const FSuspect* Target = GetTarget(); return Target && Target->Anger >= McWarden::HostileAnger; }
	int32 GetAngerAt(int32 Id) const;
	bool HasSuspects() const { return !Suspects.IsEmpty(); }
	// Ticks since anything was noticed; calm for CalmDespawnTicks lets the warden dig away.
	int32 GetCalmTicks() const { return CalmTicks; }
	// Ticks since a vibration was last accepted (the wait that follows it).
	bool CanListen() const { return ListenCooldown == 0; }
	const TArray<FSuspect>& GetSuspects() const { return Suspects; }

private:
	FSuspect& Get(int32 Id, const FVector& Pos);

	TArray<FSuspect> Suspects;
	int32 ListenCooldown = 0;
	int32 DecayCounter = 0;
	int32 CalmTicks = 0;
};

class MCENTITY_API FMcWarden : public FMcMob
{
public:
	explicit FMcWarden(const FMcMobType& InType);
	static const FName& StaticType();

	virtual void Tick(FMcEntityManager& Manager) override;
	virtual void OnHurt(float Amount, const FVector* Source) override;

	// A vibration the simulation says reaches it: Id is the player's (0 for something that is not a player), Pos where it was.
	// The caller has already checked range, wool and the sources a warden ignores (McGameEvents::WardenCanHear).
	void HearVibration(int32 SuspectId, const FVector& Pos, bool bProjectile, const FVector& FromPos);
	// Rises out of the ground for EmergeTicks (a shrieker's call, or a command).
	void StartEmerging();

	enum class EState : uint8
	{
		Emerging,
		Idle,
		Sniffing,
		Roaring,
		Charging,
		Digging,
	};
	EState GetState() const { return State; }
	FMcWardenBrain& GetBrain() { return Brain; }
	const FMcWardenBrain& GetBrain() const { return Brain; }
	int32 GetStateTicks() const { return StateTicks; }
	// Which of the players (by position) is the player of that id, for its suspect list: the host fills it in. Left unset, a
	// suspect's own last place is the goal.
	TFunction<bool(int32 Id, FVector& OutFeet)> FindPlayer;
	// A player's anger id for the player standing at that feet position (0 if not a player).
	TFunction<int32(const FVector&)> IdOfPlayerAt;
	// The sonic boom's hit on a player (feet, damage): it goes through armor. Unset: it falls back to OnAttackPlayer.
	TFunction<void(const FVector&, float)> OnSonicHitPlayer;
	// Called when it digs away: the host removes it.
	TFunction<void(FMcWarden&)> OnDug;

	bool IsAngryNow() const;

private:
	void TickIdle(FMcEntityManager& Manager, const FMcWardenBrain::FSuspect* Target);
	void EnterState(EState Next, int32 Ticks);
	FVector GetTargetPlace(const FMcWardenBrain::FSuspect& Target, FMcEntityManager& Manager) const;

	FMcWardenBrain Brain;
	EState State = EState::Idle;
	int32 StateTicks = 0;
	int32 SniffWait = 0;
	int32 SonicCooldown = 0;
	int32 MeleeWait = 0;
	int32 NoticedTicks = 0;
	int32 SinceAttack = 100000;
	int32 DarknessTimer = 0;
	int32 HeartTimer = 0;
	bool bWasAngry = false;
};
