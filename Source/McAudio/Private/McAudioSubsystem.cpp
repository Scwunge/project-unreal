#include "McAudioSubsystem.h"
#include "McAudio.h"
#include "McChunk.h"
#include "Components/AudioComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWaveProcedural.h"
#include "Async/Async.h"

bool UMcAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// The dedicated server has no one to hear anything.
	return !IsRunningDedicatedServer() && !IsRunningCommandlet();
}

void UMcAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BusHandle = FMcSoundBus::Get().OnSound.AddUObject(this, &UMcAudioSubsystem::OnSound);
}

void UMcAudioSubsystem::Deinitialize()
{
	FMcSoundBus::Get().OnSound.Remove(BusHandle);
	StopMusic();
	Live.Reset();
	Super::Deinitialize();
}

void UMcAudioSubsystem::OnSound(const FMcSoundEvent& Event)
{
	Play(Event);
}

void UMcAudioSubsystem::PlayMusic(FName Event)
{
	StopMusic();
	FMcSoundVariant Variant;
	if (!GEngine || !GEngine->UseSound() || !FMcSoundRegistry::Get().EnsureLoaded() || !FMcSoundRegistry::Get().Pick(Event, Random.FRand(), Variant))
	{
		return;
	}
	MusicEvent = Event;
	bMusicDecoding = true;
	const int32 Request = ++MusicRequest;
	const FString Path = FPaths::Combine(FMcSoundRegistry::GetSoundsDir(), Variant.Name + TEXT(".ogg"));
	const McAudio::FMcSoundPlay Settings = McAudio::Resolve(FMcSoundEvent{ Event, FVector::ZeroVector, 1.0f, 1.0f }, Variant);
	UE_LOG(LogMcAudio, Log, TEXT("Music: %s (%s)"), *Event.ToString(), *Variant.Name);
	// A track is minutes long: decoded off the game thread, and not kept afterwards.
	Async(EAsyncExecution::ThreadPool, [WeakThis = TWeakObjectPtr<UMcAudioSubsystem>(this), Path, Request, Settings]()
	{
		TSharedPtr<McAudio::FDecodedSound> Sound = MakeShared<McAudio::FDecodedSound>();
		TArray<uint8> File;
		if (!FFileHelper::LoadFileToArray(File, *Path) || !McAudio::DecodeOgg(File, *Sound))
		{
			UE_LOG(LogMcAudio, Warning, TEXT("Can't play music file %s."), *Path);
			Sound.Reset();
		}
		AsyncTask(ENamedThreads::GameThread, [WeakThis, Sound, Request, Settings]()
		{
			UMcAudioSubsystem* Self = WeakThis.Get();
			if (!Self || Self->MusicRequest != Request || !Self->bMusicDecoding)
			{
				return;
			}
			Self->bMusicDecoding = false;
			UWorld* World = Self->GetWorld();
			if (!Sound || !World)
			{
				Self->MusicEvent = NAME_None;
				return;
			}
			USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(Self);
			Wave->SetSampleRate(static_cast<uint32>(Sound->SampleRate));
			Wave->NumChannels = Sound->NumChannels;
			Wave->Duration = Sound->Duration;
			Wave->SoundGroup = SOUNDGROUP_Music;
			Wave->bLooping = false;
			Wave->QueueAudio(Sound->Pcm.GetData(), Sound->Pcm.Num());
			Self->Music = UGameplayStatics::SpawnSound2D(World, Wave, Settings.Volume, Settings.Pitch, 0.0f, nullptr, true);
			if (!Self->Music)
			{
				Self->MusicEvent = NAME_None;
			}
		});
	});
}

void UMcAudioSubsystem::StopMusic()
{
	if (Music)
	{
		Music->Stop();
		Music = nullptr;
	}
	MusicEvent = NAME_None;
	bMusicDecoding = false;
	++MusicRequest;
}

FName UMcAudioSubsystem::GetMusic()
{
	if (!bMusicDecoding && (!Music || !Music->IsPlaying()))
	{
		Music = nullptr;
		MusicEvent = NAME_None;
	}
	return MusicEvent;
}

int32 UMcAudioSubsystem::GetNumPlaying()
{
	Live.RemoveAll([](const TWeakObjectPtr<UAudioComponent>& Component) { return !Component.IsValid() || !Component->IsPlaying(); });
	return Live.Num();
}

const McAudio::FDecodedSound* UMcAudioSubsystem::GetDecoded(const FString& Name)
{
	if (const TOptional<McAudio::FDecodedSound>* Found = Decoded.Find(Name))
	{
		return Found->GetPtrOrNull();
	}
	TOptional<McAudio::FDecodedSound>& Slot = Decoded.Add(Name);
	TArray<uint8> File;
	const FString Path = FPaths::Combine(FMcSoundRegistry::GetSoundsDir(), Name + TEXT(".ogg"));
	McAudio::FDecodedSound Sound;
	if (!FFileHelper::LoadFileToArray(File, *Path))
	{
		UE_LOG(LogMcAudio, Warning, TEXT("Sound file %s is missing from the cache."), *Path);
	}
	else if (!McAudio::DecodeOgg(File, Sound))
	{
		UE_LOG(LogMcAudio, Warning, TEXT("Can't decode sound file %s."), *Path);
	}
	else
	{
		Slot = MoveTemp(Sound);
	}
	return Slot.GetPtrOrNull();
}

bool UMcAudioSubsystem::Play(const FMcSoundEvent& Event)
{
	UWorld* World = GetWorld();
	if (!World || !GEngine || !GEngine->UseSound() || !FMcSoundRegistry::Get().EnsureLoaded())
	{
		return false;
	}
	FMcSoundVariant Variant;
	if (!FMcSoundRegistry::Get().Pick(Event.Name, Random.FRand(), Variant))
	{
		return false;
	}
	// Sounds beyond their range are never started.
	FVector ListenerBlock = Event.Position;
	if (const APlayerController* Player = World->GetFirstPlayerController())
	{
		FVector ViewLocation;
		FRotator ViewRotation;
		Player->GetPlayerViewPoint(ViewLocation, ViewRotation);
		ListenerBlock = McCoords::UnrealToBlock(ViewLocation);
	}
	const float Distance = static_cast<float>(FVector::Dist(ListenerBlock, Event.Position));
	if (McAudio::GetGainAt(Event, Variant, Distance) <= 0.0f || GetNumPlaying() >= MaxPlaying)
	{
		return false;
	}
	const McAudio::FDecodedSound* Pcm = GetDecoded(Variant.Name);
	if (!Pcm)
	{
		return false;
	}
	const McAudio::FMcSoundPlay Play = McAudio::Resolve(Event, Variant);

	USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(this);
	Wave->SetSampleRate(static_cast<uint32>(Pcm->SampleRate));
	Wave->NumChannels = Pcm->NumChannels;
	Wave->Duration = Pcm->Duration;
	Wave->SoundGroup = SOUNDGROUP_Default;
	Wave->bLooping = false;
	Wave->QueueAudio(Pcm->Pcm.GetData(), Pcm->Pcm.Num());

	// Linear from full volume at the source to nothing at the range, as the game's.
	USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(this);
	Attenuation->Attenuation.bAttenuate = true;
	Attenuation->Attenuation.bSpatialize = true;
	Attenuation->Attenuation.bEnableOcclusion = false;
	Attenuation->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
	Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation->Attenuation.AttenuationShapeExtents = FVector::ZeroVector;
	Attenuation->Attenuation.FalloffDistance = Play.Range * McCoords::UnitsPerBlock;

	UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(World, Wave, McCoords::BlockToUnreal(Event.Position), FRotator::ZeroRotator, Play.Volume, Play.Pitch, 0.0f, Attenuation);
	if (!Component)
	{
		return false;
	}
	Live.Add(Component);
	return true;
}
