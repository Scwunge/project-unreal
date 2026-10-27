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

private:
	void OnSound(const FMcSoundEvent& Event);
	const McAudio::FDecodedSound* GetDecoded(const FString& Name);

	FDelegateHandle BusHandle;
	TMap<FString, TOptional<McAudio::FDecodedSound>> Decoded;
	TArray<TWeakObjectPtr<UAudioComponent>> Live;
	FRandomStream Random{ 11 };
};
