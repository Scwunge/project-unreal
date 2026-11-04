#include "McSoundRegistry.h"
#include "McAudio.h"
#include "McCache.h"
#include "McResourcePacks.h"
#include "AudioDecompress.h"
#include "Interfaces/IAudioFormat.h"
#include "Decoders/VorbisAudioInfo.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundWave.h"

FMcSoundRegistry& FMcSoundRegistry::Get()
{
	static FMcSoundRegistry Registry;
	return Registry;
}

FString FMcSoundRegistry::GetSoundsDir()
{
	return FPaths::Combine(McCache::Root(), TEXT("assets/minecraft/sounds"));
}

FString FMcSoundRegistry::ResolveSoundFile(const FString& Name)
{
	FString FromPack;
	if (McPacks::HasPacks() && McPacks::TryResolve(TEXT("sounds/") + Name + TEXT(".ogg"), FromPack))
	{
		return FromPack;
	}
	return FPaths::Combine(GetSoundsDir(), Name + TEXT(".ogg"));
}

bool FMcSoundRegistry::EnsureLoaded()
{
	if (bLoaded && LoadedGeneration == McPacks::GetGeneration())
	{
		return true;
	}
	const FString Path = FPaths::Combine(McCache::Root(), TEXT("assets/minecraft/sounds.json"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogMcAudio, Warning, TEXT("No sounds.json at %s: there will be no sound. Run tools/fetch_cache.py."), *Path);
		return false;
	}
	LoadedGeneration = McPacks::GetGeneration();
	if (!Parse(Text))
	{
		return false;
	}
	TArray<TArray<uint8>> FromPacks;
	McPacks::ReadAll(TEXT("sounds.json"), FromPacks);
	for (const TArray<uint8>& Bytes : FromPacks)
	{
		const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		Merge(FString(Converted.Length(), Converted.Get()));
	}
	return true;
}

bool FMcSoundRegistry::Parse(const FString& Json)
{
	return ParseInto(Json, false);
}

bool FMcSoundRegistry::Merge(const FString& Json)
{
	return ParseInto(Json, true);
}

bool FMcSoundRegistry::ParseInto(const FString& Json, bool bMerge)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogMcAudio, Warning, TEXT("sounds.json is not a JSON object."));
		return false;
	}
	if (!bMerge)
	{
		Events.Reset();
		Subtitles.Reset();
	}
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : Root->Values)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Sounds = nullptr;
		if (!Entry.Value->TryGetObject(Object) || !(*Object)->TryGetArrayField(TEXT("sounds"), Sounds))
		{
			continue;
		}
		TArray<FMcSoundVariant>& Variants = Events.FindOrAdd(FName(*Entry.Key));
		bool bReplace = false;
		if (bMerge && (*Object)->TryGetBoolField(TEXT("replace"), bReplace) && bReplace)
		{
			Variants.Reset();
		}
		FString Subtitle;
		if ((*Object)->TryGetStringField(TEXT("subtitle"), Subtitle) && (bReplace || !Subtitles.Contains(FName(*Entry.Key))))
		{
			Subtitles.Add(FName(*Entry.Key), Subtitle);
		}
		for (const TSharedPtr<FJsonValue>& Sound : *Sounds)
		{
			FMcSoundVariant Variant;
			FString Text;
			const TSharedPtr<FJsonObject>* Detail = nullptr;
			if (Sound->TryGetString(Text))
			{
				Variant.Name = Text;
			}
			else if (Sound->TryGetObject(Detail))
			{
				double Number = 0.0;
				if (!(*Detail)->TryGetStringField(TEXT("name"), Variant.Name))
				{
					continue;
				}
				if ((*Detail)->TryGetNumberField(TEXT("volume"), Number))
				{
					Variant.Volume = static_cast<float>(Number);
				}
				if ((*Detail)->TryGetNumberField(TEXT("pitch"), Number))
				{
					Variant.Pitch = static_cast<float>(Number);
				}
				if ((*Detail)->TryGetNumberField(TEXT("weight"), Number))
				{
					Variant.Weight = FMath::Max(1, static_cast<int32>(Number));
				}
				if ((*Detail)->TryGetNumberField(TEXT("attenuation_distance"), Number))
				{
					Variant.AttenuationDistance = static_cast<float>(Number);
				}
				FString Type;
				Variant.bEvent = (*Detail)->TryGetStringField(TEXT("type"), Type) && Type == TEXT("event");
			}
			else
			{
				continue;
			}
			Variants.Add(MoveTemp(Variant));
		}
	}
	bLoaded = true;
	return true;
}

FString FMcSoundRegistry::FindSubtitle(FName Event) const
{
	const FString* Key = Subtitles.Find(Event);
	return Key ? *Key : FString();
}

namespace McAudio
{
	static TArray<FMcPlayedSound> GRecent;

	void NoteSound(FName Event, const FVector& Position, bool bPositional)
	{
		const double Now = FPlatformTime::Seconds();
		GRecent.RemoveAll([Now](const FMcPlayedSound& Sound) { return Now - Sound.Time > 4.0; });
		FMcPlayedSound Played;
		Played.Event = Event;
		Played.Position = Position;
		Played.bPositional = bPositional;
		Played.Time = Now;
		GRecent.Add(Played);
	}

	const TArray<FMcPlayedSound>& GetRecentSounds()
	{
		return GRecent;
	}
}

const TArray<FMcSoundVariant>* FMcSoundRegistry::Find(FName Event) const
{
	return Events.Find(Event);
}

bool FMcSoundRegistry::Pick(FName Event, float Draw, FMcSoundVariant& Out) const
{
	float Volume = 1.0f;
	float Pitch = 1.0f;
	for (int32 Depth = 0; Depth < 4; ++Depth)
	{
		const TArray<FMcSoundVariant>* Variants = Events.Find(Event);
		if (!Variants || Variants->IsEmpty())
		{
			return false;
		}
		int32 Total = 0;
		for (const FMcSoundVariant& Variant : *Variants)
		{
			Total += Variant.Weight;
		}
		int32 Roll = FMath::Clamp(FMath::FloorToInt32(Draw * Total), 0, Total - 1);
		const FMcSoundVariant* Chosen = &Variants->Last();
		for (const FMcSoundVariant& Variant : *Variants)
		{
			if (Roll < Variant.Weight)
			{
				Chosen = &Variant;
				break;
			}
			Roll -= Variant.Weight;
		}
		Volume *= Chosen->Volume;
		Pitch *= Chosen->Pitch;
		if (!Chosen->bEvent)
		{
			Out = *Chosen;
			Out.Volume = Volume;
			Out.Pitch = Pitch;
			return true;
		}
		Event = FName(*Chosen->Name);
	}
	return false;
}

McAudio::FMcSoundPlay McAudio::Resolve(const FMcSoundEvent& Event, const FMcSoundVariant& Variant)
{
	const float Volume = Event.Volume * Variant.Volume;
	FMcSoundPlay Play;
	Play.Volume = FMath::Clamp(Volume, 0.0f, 1.0f);
	Play.Pitch = FMath::Clamp(Event.Pitch * Variant.Pitch, 0.5f, 2.0f);
	Play.Range = McSound::GetAudibleRange(Volume, Variant.AttenuationDistance);
	return Play;
}

float McAudio::GetGainAt(const FMcSoundEvent& Event, const FMcSoundVariant& Variant, float Distance)
{
	return McSound::GetGain(Event.Volume * Variant.Volume, Distance, Variant.AttenuationDistance);
}

bool McAudio::DecodeOgg(const TArray<uint8>& File, FDecodedSound& Out)
{
	if (File.IsEmpty())
	{
		return false;
	}
	FVorbisAudioInfo Info;
	FSoundQualityInfo Quality;
	if (!Info.ReadCompressedInfo(File.GetData(), File.Num(), &Quality) || Quality.SampleDataSize == 0)
	{
		return false;
	}
	Out.Pcm.SetNumUninitialized(Quality.SampleDataSize);
	Info.ExpandFile(Out.Pcm.GetData(), &Quality);
	Out.SampleRate = static_cast<int32>(Quality.SampleRate);
	Out.NumChannels = static_cast<int32>(Quality.NumChannels);
	Out.Duration = Quality.Duration;
	return Out.SampleRate > 0 && Out.NumChannels > 0;
}
