// Music discs and the jukebox (minecraft.wiki, "Jukebox", "Music Disc"). A disc item says which song it plays in its
// jukebox_playable component (reports/minecraft/components/item/music_disc_<name>.json); the song's own file
// (data/minecraft/jukebox_song/<id>.json) gives its length, the sound event and the comparator output. Nothing is
// hard-coded: both come from the cache.

#pragma once

#include "CoreMinimal.h"

struct MCITEM_API FMcSong
{
	// "13", "cat", "creator_music_box": the song's id, which names its file and its title key.
	FName Id;
	// The sound event, e.g. "minecraft:music_disc.13" (sounds.json has it without the namespace).
	FName Sound;
	// How long it plays, in game ticks.
	int32 LengthTicks = 0;
	// The signal a comparator reads from a jukebox playing (or holding) it, 1 to 15.
	int32 ComparatorOutput = 0;
};

namespace McJukebox
{
	// The song an item plays, if it is a music disc; false for any other item.
	MCITEM_API bool FindSong(FName Item, FMcSong& Out);

	inline bool IsDisc(FName Item)
	{
		FMcSong Song;
		return FindSong(Item, Song);
	}

	// What a comparator reads from a jukebox holding this item: the song's output, 0 for no disc.
	MCITEM_API int32 GetComparatorOutput(FName Item);

	// The sounds.json event of a song, without the namespace ("music_disc.13").
	MCITEM_API FName GetSoundEvent(const FMcSong& Song);

	// The text "Now Playing: C418 - 13", from the lang file's record.nowPlaying and the song's title.
	MCITEM_API FString GetNowPlaying(const FMcSong& Song);

	// How loud a jukebox plays (the sound can be heard 16 blocks times this away: 64).
	constexpr float Volume = 4.0f;
}
