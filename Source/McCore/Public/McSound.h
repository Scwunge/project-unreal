// Which sound to play, and how loud it is where you stand (minecraft.wiki: "Sounds.json", "Sound", "Volume"). Names
// only: the server decides what happened and sends the event (FMcSoundEvent) to the clients, whose McAudio looks the
// name up in sounds.json and plays a file from the cache. Nothing here reads the cache, so the server can use it.

#pragma once

#include "CoreMinimal.h"

// A sound heard somewhere in the world.
struct FMcSoundEvent
{
	// A sounds.json event, e.g. "block.stone.break" or "entity.zombie.hurt".
	FName Name;
	// Block coordinates.
	FVector Position = FVector::ZeroVector;
	// Over 1 carries further: the sound can be heard 16 blocks times the volume away.
	float Volume = 1.0f;
	float Pitch = 1.0f;
	// Silences the sound of this name playing at Position instead of starting one (a jukebox's disc taken out).
	bool bStop = false;
};

// Where events arrive on a client: the world actor broadcasts what the server sent, and the audio listens.
class MCCORE_API FMcSoundBus
{
public:
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSound, const FMcSoundEvent&);

	static FMcSoundBus& Get();
	FOnSound OnSound;
	// A music note rises at Position (a note block played, a jukebox playing), in the colour of this note (0 to 24).
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnNote, const FVector&, int32);
	FOnNote OnNote;
};

namespace McSound
{
	enum class EBlockSound : uint8
	{
		// Breaking a block, and placing one.
		Break,
		Place,
		// Walking over it.
		Step,
		// Hitting it while mining.
		Hit,
		// Landing on it.
		Fall,
	};

	// The sound group of a block, e.g. "stone", "wood", "gravel", "grass", "sand", "glass", "wool", "metal"; empty for
	// a block that makes none (air). The game defines these in code, one group per block; this goes by the block's name.
	MCCORE_API FName GetBlockGroup(FName Block);

	// What a block does to the sound: the game scales every group's sounds by a volume and a pitch (almost all 1; metal
	// plays higher, anvils quieter).
	struct FGroupTuning
	{
		float Volume = 1.0f;
		float Pitch = 1.0f;
	};
	MCCORE_API FGroupTuning GetGroupTuning(FName Group);

	// A sound for a block: "block.<group>.<kind>" with the volume and pitch the game plays it at. Breaking and placing
	// are at (volume + 1) / 2 and pitch * 0.8, steps at volume * 0.15, hits at (volume + 1) / 8 and pitch * 0.5, falls at
	// volume * 0.5. The event name is empty for a block with no sounds.
	MCCORE_API FMcSoundEvent GetBlockSound(FName Block, EBlockSound Kind, const FVector& Position);

	enum class EMobSound : uint8
	{
		Ambient,
		Hurt,
		Death,
		Step,
	};

	// A mob's own sound: "entity.<mob>.<kind>", or none (a creeper, an enderman or a slime has no step of its own, and a
	// creeper no ambient sound). Slimes of size 1 use the "_small" variants of hurt and death. The event name is empty
	// when the mob has no such sound.
	MCCORE_API FName GetMobSoundName(FName Mob, EMobSound Kind, int32 SlimeSize = 1);

	// What a mob's footstep sounds like: its own step sound at volume 0.15 where it has one, else the step of the block
	// under it.
	MCCORE_API FMcSoundEvent GetMobStep(FName Mob, FName BlockBelow, const FVector& Position);

	// Counts walking for footsteps: 0.6 for each block walked on the ground, and a step each time the count passes a
	// mark one further on than the last step's.
	struct FStepCounter
	{
		double Walked = 0.0;
		double NextStep = 0.0;

		// Adds a tick's walking (horizontal blocks); true when it makes a step.
		bool Walk(double Blocks)
		{
			Walked += Blocks * 0.6;
			if (Walked <= NextStep)
			{
				return false;
			}
			NextStep = Walked + 1.0;
			return true;
		}
	};

	// What a player says on landing hard: "entity.player.big_fall" for more than 4 damage, else "small_fall".
	inline FName GetPlayerFallSound(int32 Damage)
	{
		return Damage > 4 ? FName(TEXT("entity.player.big_fall")) : FName(TEXT("entity.player.small_fall"));
	}

	// How far a sound of this volume can be heard: the sound's attenuation distance (16 blocks unless sounds.json says
	// otherwise), times the volume when that is over 1.
	MCCORE_API float GetAudibleRange(float Volume, float AttenuationDistance = 16.0f);

	// How loud a sound is at Distance blocks: the volume, falling in a straight line to nothing at the audible range.
	MCCORE_API float GetGain(float Volume, float Distance, float AttenuationDistance = 16.0f);

	// The voice pitch of a mob: 1 plus up to 0.2 either way from a random draw in 0..1 each, as the game's
	// (nextFloat - nextFloat) * 0.2 + 1.
	inline float GetVoicePitch(float DrawA, float DrawB)
	{
		return (DrawA - DrawB) * 0.2f + 1.0f;
	}

	// Ambient sounds: every tick a mob rolls 1 in 1000 against its counter, which counts up by one; a hit resets the
	// counter to minus the interval (80 ticks), so the sound comes at the earliest a second or so after the last.
	// Returns true when the sound should play.
	MCCORE_API bool TickAmbient(int32& Counter, int32 Roll1000, int32 IntervalTicks = 80);
}
