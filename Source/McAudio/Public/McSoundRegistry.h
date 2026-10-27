// sounds.json: which files an event plays (minecraft.wiki: "Sounds.json"). Each event lists variants, one of which is
// picked at random by weight; a variant has its own volume, pitch and attenuation distance, and may instead point to
// another event ("type": "event"), as the note block's creeper does.

#pragma once

#include "CoreMinimal.h"
#include "McSound.h"

struct FMcSoundVariant
{
	// A file under sounds/ without the extension (e.g. "dig/stone1"), or an event name for a reference.
	FString Name;
	float Volume = 1.0f;
	float Pitch = 1.0f;
	int32 Weight = 1;
	// Blocks over which a sound of volume 1 falls to nothing.
	float AttenuationDistance = 16.0f;
	// Whether Name is another event.
	bool bEvent = false;
};

class MCAUDIO_API FMcSoundRegistry
{
public:
	static FMcSoundRegistry& Get();

	// Reads cache/assets/minecraft/sounds.json, once. False if it is missing or not an object.
	bool EnsureLoaded();
	bool IsLoaded() const { return bLoaded; }
	// Reads the text of a sounds.json (for tests).
	bool Parse(const FString& Json);

	int32 NumEvents() const { return Events.Num(); }
	const TArray<FMcSoundVariant>* Find(FName Event) const;

	// The variant an event plays for a draw in 0..1: by weight, with references to other events followed (up to 4 deep)
	// and their volumes and pitches multiplied in. False if the event is unknown or empty.
	bool Pick(FName Event, float Draw, FMcSoundVariant& Out) const;

	// Where the sound files are.
	static FString GetSoundsDir();

private:
	TMap<FName, TArray<FMcSoundVariant>> Events;
	bool bLoaded = false;
};

namespace McAudio
{
	// What to play of an event once a variant is picked: a sound's volume is the event's times the variant's, its pitch
	// likewise (the game keeps it within 0.5 to 2), and it carries as far as the variant's attenuation distance times
	// the volume (when that is over 1).
	struct FMcSoundPlay
	{
		// What the engine plays at: capped at 1, as the game's volume is.
		float Volume = 1.0f;
		float Pitch = 1.0f;
		// Blocks.
		float Range = 16.0f;
	};

	MCAUDIO_API FMcSoundPlay Resolve(const FMcSoundEvent& Event, const FMcSoundVariant& Variant);

	// How loud the sound is Distance blocks from where it happened (0 past its range).
	MCAUDIO_API float GetGainAt(const FMcSoundEvent& Event, const FMcSoundVariant& Variant, float Distance);

	// 16-bit PCM decoded from an OGG file.
	struct FDecodedSound
	{
		TArray<uint8> Pcm;
		int32 SampleRate = 0;
		int32 NumChannels = 0;
		float Duration = 0.0f;
	};
	MCAUDIO_API bool DecodeOgg(const TArray<uint8>& File, FDecodedSound& Out);
}
