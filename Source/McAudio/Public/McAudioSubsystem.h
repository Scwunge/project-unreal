// Plays the sounds the server sends: every FMcSoundEvent that reaches the FMcSoundBus is looked up in sounds.json, one of
// its files is decoded (once; they are kept) and played where it happened, quieter the further the listener is and
// not at all beyond its range. Clients only.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "McSound.h"
#include "McSoundRegistry.h"
#include "McAudioSubsystem.generated.h"

class UAudioComponent;

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

private:
	void OnSound(const FMcSoundEvent& Event);
	const McAudio::FDecodedSound* GetDecoded(const FString& Name);

	FDelegateHandle BusHandle;
	TMap<FString, TOptional<McAudio::FDecodedSound>> Decoded;
	TArray<TWeakObjectPtr<UAudioComponent>> Live;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Music;
	FName MusicEvent;
	// Counts up with each track asked for, so a decode that finishes after a newer one was asked for is dropped.
	int32 MusicRequest = 0;
	bool bMusicDecoding = false;
	FRandomStream Random{ 11 };
};
