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
	Live.Reset();
	Super::Deinitialize();
}

void UMcAudioSubsystem::OnSound(const FMcSoundEvent& Event)
{
	Play(Event);
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
