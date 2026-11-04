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
	// The language key of an event's caption ("subtitles.block.generic.break"), empty for an event with none. The first
	// definition keeps it; a pack's that says "replace": true takes it over.
	FString FindSubtitle(FName Event) const;

	// The variant an event plays for a draw in 0..1: by weight, with references to other events followed (up to 4 deep)
	// and their volumes and pitches multiplied in. False if the event is unknown or empty.
	bool Pick(FName Event, float Draw, FMcSoundVariant& Out) const;

	// Where the sound files are.
	static FString GetSoundsDir();
	// The file of a sound by its name ("mob/cow/say1"): a pack's if one has sounds/<name>.ogg, else the cache's.
	static FString ResolveSoundFile(const FString& Name);
	// Merges a pack's sounds.json into the events: its events are added to the game's, or replace them when they say
	// "replace": true.
	bool Merge(const FString& Json);

private:
	TMap<FName, TArray<FMcSoundVariant>> Events;
	TMap<FName, FString> Subtitles;
	bool bLoaded = false;
	uint32 LoadedGeneration = 0;
	bool ParseInto(const FString& Json, bool bMerge);
};

namespace McAudio
{
	// A sound that has played lately, for the closed captions.
	struct FMcPlayedSound
	{
		FName Event;
		// Blocks.
		FVector Position = FVector::ZeroVector;
		// False for a sound in the listener's head (no arrow).
		bool bPositional = false;
		double Time = 0.0;
	};
	// Notes a sound that started (FPlatformTime::Seconds); entries older than a few seconds are dropped.
	MCAUDIO_API void NoteSound(FName Event, const FVector& Position, bool bPositional);
	MCAUDIO_API const TArray<FMcPlayedSound>& GetRecentSounds();

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
