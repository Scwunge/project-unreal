// Plays the sounds the server sends: every FMcSoundEvent that reaches the FMcSoundBus is looked up in sounds.json, one of
// its files is decoded (once; they are kept) and played where it happened, quieter the further the listener is and
// not at all beyond its range. Clients only.
//
// It also has the cave's mood sound (ambient.cave: it comes when it has been dark around the player for a while), in both
// looks. In Unreal Mode (McRenderMode::IsEnhanced) it makes the sound immersive with the engine's own audio features
// and the same sounds: a reverb that follows how enclosed the listener is, muffling of sounds behind solid blocks, a
// strong low-pass and the vanilla ambience under water, air absorption and HRTF with distance, rain muffled indoors,
// drips in caves, echoes of far mobs, and small cues (steps by speed, a landing thud, music ducked by a blast).
// The rules are in McAcoustics. Vanilla sounds exactly as before.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "McAcoustics.h"
#include "McCore.h"
#include "McSound.h"
#include "McSoundRegistry.h"
#include "McAudioSubsystem.generated.h"

class UAudioComponent;
class UReverbEffect;
class FMcBlockWorld;

UCLASS()
class MCAUDIO_API UMcAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Plays one event for the local player; what the bus does, callable directly. True if a sound started.
	bool Play(const FMcSoundEvent& Event);

	// Sounds playing now, and how many may at once (the game allows 247 of all kinds; the rest are dropped).
	int32 GetNumPlaying();
	static constexpr int32 MaxPlaying = 64;

	// Background music: one track of a music event (e.g. "music.game"), everywhere at once rather than somewhere. Its
	// file is decoded off the game thread, so it starts a moment later; it counts as playing from the call. Starting
	// another stops the first.
	void PlayMusic(FName Event);
	void StopMusic();
	// The event of the track playing (or being decoded), or none.
	FName GetMusic();
	// /stopsound: stops sounds of a source ("" or "master" for all; "music", "record", or one named for the events it plays:
	// "weather" weather.*, "block" block.*, "ambient" ambient.*, "ui" ui.*, "player" entity.player.*; the game's hostile, neutral and voice
	// sources are not told apart by the event name, so those stop nothing) and, if Event is not none, only that event. Returns how many.
	int32 StopSounds(const FString& Source, FName Event);

	// The blocks the listener is in, for the mood sound and Unreal Mode's acoustics. Null when there is no world; set
	// again with the player's view. Owner is what holds the world (its world actor): once Owner is destroyed the world
	// is let go of, since this subsystem outlives every level and the world goes with its actor.
	void SetListenerWorld(const FMcBlockWorld* InWorld, const UObject* Owner = nullptr);

private:
	// A sound that is playing, with what is needed to keep shaping it.
	struct FLive
	{
		TWeakObjectPtr<UAudioComponent> Component;
		FName Event;
		FVector Position = FVector::ZeroVector;
		bool bPositional = false;
		// The part of the world's ambience (rain, the cave): music-like, it ducks for a blast.
		bool bAmbience = false;
		// Beds and cues inside the listener's head are not muffled by water.
		bool bUnfiltered = false;
		float BaseVolume = 1.0f;
		// What the blocks in the way take, 1 for none; eased towards the target.
		float Occlusion = 1.0f;
		float Lowpass = 20000.0f;
		float TargetOcclusion = 1.0f;
		float TargetLowpass = 20000.0f;
		// The highest the sound may go (an echo is dull).
		float LowpassCap = 20000.0f;
	};
	struct FSpawn
	{
		float Volume = 1.0f;
		bool bAmbience = false;
		// Reverb send wanted (0 for the usual).
		float Reverb = 0.0f;
		// An echo of another sound: it makes no echo of its own.
		bool bEcho = false;
	};

	void OnSound(const FMcSoundEvent& Event);
	const McAudio::FDecodedSound* GetDecoded(const FString& Name);
	bool PlayShaped(const FMcSoundEvent& Event, const FSpawn& Spawn);
	// A jukebox's record: minutes of music decoded off the game thread, then played from the jukebox; or silenced.
	void PlayRecord(const FMcSoundEvent& Event);
	void StopRecord(const FMcSoundEvent& Event);
	// A sound in the listener's head, not in the world (a bed, the cues of going under water).
	UAudioComponent* Play2D(FName Event, float Volume, bool bAmbience);

	bool Tick(float DeltaTime);
	void TickMood(float DeltaTime, const FVector& Head, bool bUnreal);
	void TickImmersive(float DeltaTime, const FVector& Head);
	void ReleaseImmersive();
	// Lets go of the listener's world when the actor that holds it is gone (left to the title, another world, a server).
	void DropStaleListener();
	void UpdateLive(const FVector& Head);
	void DripNear(const FVector& Head);
	FMcStateId GetBlockAt(const FIntVector& Cell) const;
	bool IsSolidAt(const FIntVector& Cell) const;
	void UpdateOne(FLive& Sound, const FVector& Head, bool bSnap);
	int32 EstimateLight(const FIntVector& Cell) const;
	bool FindDarkSpot(const FVector& Head, float MinDistance, float MaxDistance, FVector& Out);

	FDelegateHandle BusHandle;
	FTSTicker::FDelegateHandle TickHandle;
	TMap<FString, TOptional<McAudio::FDecodedSound>> Decoded;
	TArray<FLive> Live;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Music;
	UPROPERTY(Transient)
	TObjectPtr<UReverbEffect> Reverb;
	TWeakObjectPtr<UAudioComponent> UnderwaterBed;
	FName MusicEvent;
	float MusicVolume = 1.0f;
	// Counts up with each track asked for, so a decode that finishes after a newer one was asked for is dropped.
	int32 MusicRequest = 0;
	bool bMusicDecoding = false;
	FRandomStream Random{ 11 };
	// The records playing, by the jukebox's block; Request drops a decode that finishes after the record was taken out.
	struct FRecord
	{
		TWeakObjectPtr<UAudioComponent> Component;
		int32 Request = 0;
	};
	TMap<FIntVector, FRecord> Records;
	int32 RecordRequest = 0;

	const FMcBlockWorld* ListenerWorld = nullptr;
	TWeakObjectPtr<const UObject> ListenerOwner;
	bool bListenerOwned = false;
	// The cave's mood meter and when it was last read.
	float MoodMeter = 0.0f;
	float MoodClock = 0.0f;
	int32 MoodLight = 15;
	// Unreal Mode's state.
	bool bImmersive = false;
	bool bUnderwater = false;
	float ReverbClock = 0.0f;
	float ReverbVolume = 0.0f;
	float LiveClock = 0.0f;
	float DripClock = 2.0f;
	float BubbleClock = 5.0f;
	int32 LiveFrame = 0;
	float Duck = 1.0f;
	float DuckHold = 0.0f;
	float Speed = 0.0f;
	bool bHaveLast = false;
	FVector LastHead = FVector::ZeroVector;
	McAcoustics::FEnclosure Enclosure;
};
