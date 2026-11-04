#include "McJukebox.h"
#include "McCache.h"
#include "McItemRegistry.h"
#include "McLang.h"
#include "HAL/CriticalSection.h"

namespace
{
	FCriticalSection Lock;
	// Songs by id, read from the cache on first use; an id whose file is missing is kept as invalid so it is not read again.
	TMap<FName, TOptional<FMcSong>> Songs;

	const FMcSong* LoadSong(FName Id)
	{
		if (const TOptional<FMcSong>* Found = Songs.Find(Id))
		{
			return Found->GetPtrOrNull();
		}
		TOptional<FMcSong>& Slot = Songs.Add(Id);
		const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(McCache::Data(FString::Printf(TEXT("jukebox_song/%s.json"), *Id.ToString())));
		if (Json.IsValid())
		{
			FMcSong Song;
			Song.Id = Id;
			Song.Sound = FName(*McCache::Namespaced(Json->GetStringField(TEXT("sound_event"))));
			Song.LengthTicks = FMath::RoundToInt32(Json->GetNumberField(TEXT("length_in_seconds")) * 20.0);
			Song.ComparatorOutput = static_cast<int32>(Json->GetNumberField(TEXT("comparator_output")));
			Slot = Song;
		}
		return Slot.GetPtrOrNull();
	}
}

bool McJukebox::FindSong(FName Item, FMcSong& Out)
{
	if (Item.IsNone() || !Item.ToString().Contains(TEXT("music_disc")))
	{
		return false;
	}
	FMcItemRegistry::Get().EnsureLoaded();
	const TSharedPtr<FJsonValue> Playable = FMcItemRegistry::Get().GetDefault(Item, TEXT("minecraft:jukebox_playable"));
	if (!Playable.IsValid() || Playable->Type != EJson::String)
	{
		return false;
	}
	FScopeLock Guard(&Lock);
	const FMcSong* Song = LoadSong(FName(*McCache::PathOf(Playable->AsString())));
	if (!Song)
	{
		return false;
	}
	Out = *Song;
	return true;
}

int32 McJukebox::GetComparatorOutput(FName Item)
{
	FMcSong Song;
	return FindSong(Item, Song) ? Song.ComparatorOutput : 0;
}

FName McJukebox::GetSoundEvent(const FMcSong& Song)
{
	return FName(*McCache::PathOf(Song.Sound.ToString()));
}

FString McJukebox::GetNowPlaying(const FMcSong& Song)
{
	return McLang::Format(TEXT("record.nowPlaying"), { McLang::Get(FString::Printf(TEXT("jukebox_song.minecraft.%s"), *Song.Id.ToString())) });
}
