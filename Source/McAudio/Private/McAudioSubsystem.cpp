#include "McAudioSubsystem.h"
#include "McAudio.h"
#include "McBlockLight.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McRenderMode.h"
#include "AudioDevice.h"
#include "Sound/ReverbEffect.h"
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
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMcAudioSubsystem::Tick));
}

void UMcAudioSubsystem::Deinitialize()
{
	FMcSoundBus::Get().OnSound.Remove(BusHandle);
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	ReleaseImmersive();
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
			Self->MusicVolume = Settings.Volume;
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
	Live.RemoveAll([](const FLive& Sound) { return !Sound.Component.IsValid() || !Sound.Component->IsPlaying(); });
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

namespace
{
	const FName WaterName(TEXT("minecraft:water"));
	const FName LavaName(TEXT("minecraft:lava"));
	const FName DripstoneName(TEXT("minecraft:pointed_dripstone"));
	const FName DripstoneBlockName(TEXT("minecraft:dripstone_block"));
	const FName ReverbTag(TEXT("McAcoustics"));

	FIntVector ToCell(const FVector& Position)
	{
		return FIntVector(FMath::FloorToInt32(Position.X), FMath::FloorToInt32(Position.Y), FMath::FloorToInt32(Position.Z));
	}

	FName GetBlockName(FMcStateId State)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		return Registry.IsValidState(State) ? Registry.GetBlockOf(State).Name : NAME_None;
	}

	USoundWaveProcedural* MakeWave(UObject* Outer, const McAudio::FDecodedSound& Sound)
	{
		USoundWaveProcedural* Wave = NewObject<USoundWaveProcedural>(Outer);
		Wave->SetSampleRate(static_cast<uint32>(Sound.SampleRate));
		Wave->NumChannels = Sound.NumChannels;
		Wave->Duration = Sound.Duration;
		Wave->SoundGroup = SOUNDGROUP_Default;
		Wave->bLooping = false;
		Wave->QueueAudio(Sound.Pcm.GetData(), Sound.Pcm.Num());
		return Wave;
	}
}

bool UMcAudioSubsystem::Play(const FMcSoundEvent& Event)
{
	return PlayShaped(Event, FSpawn());
}

bool UMcAudioSubsystem::PlayShaped(const FMcSoundEvent& Event, const FSpawn& Spawn)
{
	UWorld* World = GetWorld();
	APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	if (!GEngine || !GEngine->UseSound() || !World || !Player || !FMcSoundRegistry::Get().EnsureLoaded())
	{
		return false;
	}
	FMcSoundVariant Variant;
	if (!FMcSoundRegistry::Get().Pick(Event.Name, Random.FRand(), Variant))
	{
		return false;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	Player->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Listener = McCoords::UnrealToBlock(ViewLocation);

	// Unreal Mode shapes the event; vanilla plays it as it came.
	const bool bUnreal = ListenerWorld && McRenderMode::IsEnhanced();
	FMcSoundEvent Shaped = Event;
	const float Distance = static_cast<float>(FVector::Dist(Listener, Event.Position));
	if (bUnreal)
	{
		const FString Name = Event.Name.ToString();
		if (Name.StartsWith(TEXT("block.")) && Name.EndsWith(TEXT(".step")) && Distance < 2.5f)
		{
			// Quick steps are louder and a touch higher.
			Shaped.Volume *= FMath::Clamp(0.4f + 0.15f * Speed, 0.4f, 1.3f);
			Shaped.Pitch *= FMath::Clamp(0.92f + 0.02f * Speed, 0.92f, 1.08f);
		}
		else if (Name.EndsWith(TEXT("big_fall")) && Distance < 3.0f)
		{
			Shaped.Volume *= 1.4f;
		}
		else if (Name.EndsWith(TEXT("small_fall")) && Distance < 3.0f)
		{
			Shaped.Volume *= 1.2f;
		}
		else if (Name == TEXT("entity.generic.explode") || Name == TEXT("entity.lightning_bolt.thunder") || Name == TEXT("entity.lightning_bolt.impact"))
		{
			DuckHold = 2.5f;
		}
		if (Name.StartsWith(TEXT("weather.rain")))
		{
			// Rain is dull indoors: the less sky, the quieter.
			Shaped.Volume *= FMath::Lerp(0.25f, 1.0f, Enclosure.SkyOpenness);
		}
		Shaped.Volume *= Spawn.Volume;
	}

	if (McAudio::GetGainAt(Shaped, Variant, Distance) <= 0.0f || GetNumPlaying() >= MaxPlaying)
	{
		return false;
	}
	const McAudio::FDecodedSound* Sound = GetDecoded(Variant.Name);
	if (!Sound)
	{
		return false;
	}
	const McAudio::FMcSoundPlay Play = McAudio::Resolve(Shaped, Variant);

	USoundAttenuation* Attenuation = NewObject<USoundAttenuation>(this);
	FSoundAttenuationSettings& Settings = Attenuation->Attenuation;
	Settings.bAttenuate = true;
	Settings.bSpatialize = true;
	Settings.bEnableOcclusion = false;
	Settings.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
	Settings.AttenuationShape = EAttenuationShape::Sphere;
	Settings.AttenuationShapeExtents = FVector(1.0);
	Settings.FalloffDistance = Play.Range * McCoords::UnitsPerBlock;
	if (bUnreal)
	{
		// Far sounds lose their highs.
		Settings.bAttenuateWithLPF = true;
		Settings.LPFRadiusMin = 8.0f * McCoords::UnitsPerBlock;
		Settings.LPFRadiusMax = FMath::Max(Play.Range, 12.0f) * McCoords::UnitsPerBlock;
		Settings.LPFFrequencyAtMin = 20000.0f;
		Settings.LPFFrequencyAtMax = 3000.0f;
		// And the reverb has more of them the further they are.
		Settings.bEnableReverbSend = true;
		Settings.ReverbWetLevelMin = Spawn.Reverb > 0.0f ? Spawn.Reverb * 0.6f : 0.15f;
		Settings.ReverbWetLevelMax = Spawn.Reverb > 0.0f ? Spawn.Reverb : 0.6f;
		Settings.ReverbDistanceMin = 4.0f * McCoords::UnitsPerBlock;
		Settings.ReverbDistanceMax = FMath::Max(Play.Range, 12.0f) * McCoords::UnitsPerBlock;
		const FAudioDevice* Device = World->GetAudioDeviceRaw();
		if (Device && Device->IsSpatializationPluginEnabled())
		{
			Settings.SpatializationAlgorithm = ESoundSpatializationAlgorithm::SPATIALIZATION_HRTF;
		}
	}

	UAudioComponent* Component = UGameplayStatics::SpawnSoundAtLocation(World, MakeWave(this, *Sound),
		McCoords::BlockToUnreal(Event.Position), FRotator::ZeroRotator, Play.Volume, Play.Pitch, 0.0f, Attenuation);
	if (!Component)
	{
		return false;
	}
	FLive Entry;
	Entry.Component = Component;
	Entry.Position = Event.Position;
	Entry.bPositional = true;
	Entry.BaseVolume = Play.Volume;
	if (bUnreal)
	{
		const FString Name = Event.Name.ToString();
		Entry.bAmbience = Spawn.bAmbience || Name.StartsWith(TEXT("weather.rain")) || Name.StartsWith(TEXT("ambient."));
		Entry.LowpassCap = Spawn.bEcho ? 3500.0f : 20000.0f;
		UpdateOne(Entry, Listener, true);
		// A far mob in a cave comes back off the walls.
		if (!Spawn.bEcho && Name.StartsWith(TEXT("entity.")) && Distance >= 10.0f)
		{
			const McAcoustics::FEcho Echo = McAcoustics::GetEcho(Distance, Enclosure);
			if (Echo.Volume > 0.0f)
			{
				FMcSoundEvent Back = Event;
				Back.Pitch = FMath::Clamp(Event.Pitch * 0.97f, 0.5f, 2.0f);
				FSpawn BackSpawn;
				BackSpawn.Volume = Echo.Volume;
				BackSpawn.Reverb = 0.9f;
				BackSpawn.bEcho = true;
				FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Back, BackSpawn](float)
				{
					PlayShaped(Back, BackSpawn);
					return false;
				}), Echo.DelaySeconds);
			}
		}
	}
	Live.Add(MoveTemp(Entry));
	return true;
}

UAudioComponent* UMcAudioSubsystem::Play2D(FName EventName, float Volume, bool bAmbience)
{
	UWorld* World = GetWorld();
	FMcSoundVariant Variant;
	if (!GEngine || !GEngine->UseSound() || !World || !FMcSoundRegistry::Get().EnsureLoaded()
		|| !FMcSoundRegistry::Get().Pick(EventName, Random.FRand(), Variant) || GetNumPlaying() >= MaxPlaying)
	{
		return nullptr;
	}
	const McAudio::FDecodedSound* Sound = GetDecoded(Variant.Name);
	if (!Sound)
	{
		return nullptr;
	}
	const McAudio::FMcSoundPlay Play = McAudio::Resolve(FMcSoundEvent{ EventName, FVector::ZeroVector, Volume, 1.0f }, Variant);
	UAudioComponent* Component = UGameplayStatics::SpawnSound2D(World, MakeWave(this, *Sound), Play.Volume, Play.Pitch);
	if (Component)
	{
		FLive Entry;
		Entry.Component = Component;
		Entry.bAmbience = bAmbience;
		Entry.bUnfiltered = true;
		Entry.BaseVolume = Play.Volume;
		Live.Add(MoveTemp(Entry));
	}
	return Component;
}

void UMcAudioSubsystem::SetListenerWorld(const FMcBlockWorld* InWorld)
{
	ListenerWorld = InWorld;
	MoodMeter = 0.0f;
	bHaveLast = false;
	if (!InWorld)
	{
		ReleaseImmersive();
	}
}

FMcStateId UMcAudioSubsystem::GetBlockAt(const FIntVector& Cell) const
{
	return ListenerWorld ? ListenerWorld->GetBlock(Cell) : 0;
}

bool UMcAudioSubsystem::IsSolidAt(const FIntVector& Cell) const
{
	const FMcBlockLight& Light = FMcBlockLight::Get();
	return ListenerWorld && Light.IsLoaded() && Light.GetDampening(GetBlockAt(Cell)) >= 15;
}

int32 UMcAudioSubsystem::EstimateLight(const FIntVector& Cell) const
{
	const FMcBlockLight& Light = FMcBlockLight::Get();
	if (!ListenerWorld || !Light.IsLoaded())
	{
		return 15;
	}
	if (McAcoustics::IsSkyOpen(Cell, [this](const FIntVector& At) { return IsSolidAt(At); }))
	{
		return 15;
	}
	return McAcoustics::EstimateBlockLight(Cell, [this, &Light](const FIntVector& At) { return static_cast<int32>(Light.GetEmission(GetBlockAt(At))); });
}

bool UMcAudioSubsystem::FindDarkSpot(const FVector& Head, float MinDistance, float MaxDistance, FVector& Out)
{
	for (int32 Try = 0; Try < 12; ++Try)
	{
		const FVector Offset = Random.VRand() * Random.FRandRange(MinDistance, MaxDistance);
		const FIntVector Cell = ToCell(Head + Offset);
		if (IsSolidAt(Cell) || EstimateLight(Cell) > 7)
		{
			continue;
		}
		Out = FVector(Cell.X + 0.5, Cell.Y + 0.5, Cell.Z + 0.5);
		return true;
	}
	return false;
}

bool UMcAudioSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
	if (!ListenerWorld || !Player || !FMcBlockRegistry::Get().IsLoaded())
	{
		if (bImmersive)
		{
			ReleaseImmersive();
		}
		bHaveLast = false;
		return true;
	}
	FVector ViewLocation;
	FRotator ViewRotation;
	Player->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector Head = McCoords::UnrealToBlock(ViewLocation);
	if (bHaveLast && DeltaTime > 0.0f)
	{
		// Blocks a second along the ground (X and Z in block coordinates); a jump of a few blocks is a teleport.
		const FVector2D Moved(Head.X - LastHead.X, Head.Z - LastHead.Z);
		const float Along = Moved.Size() < 4.0 ? static_cast<float>(Moved.Size()) / DeltaTime : 0.0f;
		Speed = FMath::Lerp(Speed, Along, FMath::Clamp(DeltaTime * 8.0f, 0.0f, 1.0f));
	}
	LastHead = Head;
	bHaveLast = true;

	const bool bUnreal = McRenderMode::IsEnhanced();
	TickMood(DeltaTime, Head, bUnreal);
	if (bUnreal)
	{
		TickImmersive(DeltaTime, Head);
		UpdateLive(Head);
	}
	else if (bImmersive)
	{
		ReleaseImmersive();
	}
	return true;
}

void UMcAudioSubsystem::TickMood(float DeltaTime, const FVector& Head, bool bUnreal)
{
	MoodClock += DeltaTime;
	if (MoodClock < 0.5f)
	{
		return;
	}
	const float Elapsed = MoodClock;
	MoodClock = 0.0f;
	MoodLight = EstimateLight(ToCell(Head));
	// The game reads the light every tick (20 a second) and plays after 6000 ticks of dark; Unreal Mode is quicker.
	const int32 DelayTicks = bUnreal ? 2400 : 6000;
	bool bFire = false;
	for (int32 Tick = 0, Ticks = FMath::RoundToInt32(Elapsed * 20.0f); Tick < Ticks; ++Tick)
	{
		bFire |= McAcoustics::TickMood(MoodMeter, MoodLight, DelayTicks);
	}
	if (!bFire)
	{
		return;
	}
	FVector Spot;
	if (!FindDarkSpot(Head, bUnreal ? 8.0f : 0.0f, bUnreal ? 18.0f : 8.0f, Spot))
	{
		return;
	}
	const FMcSoundEvent Mood{ TEXT("ambient.cave"), Spot, bUnreal ? 1.3f : 1.0f, 1.0f };
	FSpawn Spawn;
	Spawn.bAmbience = true;
	Spawn.Reverb = 0.9f;
	PlayShaped(Mood, Spawn);
}

void UMcAudioSubsystem::TickImmersive(float DeltaTime, const FVector& Head)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	bImmersive = true;

	// Under water everything is dull, and the sea has its own sounds.
	const bool bWet = GetBlockName(GetBlockAt(ToCell(Head))) == WaterName;
	if (bWet != bUnderwater)
	{
		bUnderwater = bWet;
		Play2D(bWet ? TEXT("ambient.underwater.enter") : TEXT("ambient.underwater.exit"), 1.0f, false);
		if (!bWet)
		{
			if (UAudioComponent* Bed = UnderwaterBed.Get())
			{
				Bed->Stop();
			}
			UnderwaterBed.Reset();
		}
		BubbleClock = 5.0f;
	}
	if (bUnderwater)
	{
		if (!UnderwaterBed.IsValid() || !UnderwaterBed->IsPlaying())
		{
			UnderwaterBed = Play2D(TEXT("ambient.underwater.loop"), 0.6f, true);
		}
		BubbleClock -= DeltaTime;
		if (BubbleClock <= 0.0f)
		{
			const float Roll = Random.FRand();
			const TCHAR* Addition = Roll < 0.02f ? TEXT("ambient.underwater.loop.additions.ultra_rare")
				: Roll < 0.12f ? TEXT("ambient.underwater.loop.additions.rare") : TEXT("ambient.underwater.loop.additions");
			Play2D(Addition, 0.8f, true);
			BubbleClock = Random.FRandRange(4.0f, 10.0f);
		}
	}

	// The reverb of the space around the listener, a few times a second.
	ReverbClock -= DeltaTime;
	if (ReverbClock <= 0.0f)
	{
		ReverbClock = 0.35f;
		Enclosure = McAcoustics::EstimateEnclosure(Head, [this](const FIntVector& At) { return IsSolidAt(At); });
		const McAcoustics::FReverb Wanted = McAcoustics::ToReverb(Enclosure, ListenerWorld->GetDimension().bHasCeiling);
		if (!Reverb)
		{
			Reverb = NewObject<UReverbEffect>(this);
		}
		Reverb->Density = Wanted.Density;
		Reverb->Diffusion = Wanted.Diffusion;
		Reverb->Gain = 0.32f;
		Reverb->GainHF = Wanted.GainHF;
		Reverb->DecayTime = Wanted.DecayTime;
		Reverb->DecayHFRatio = 0.8f;
		Reverb->ReflectionsGain = Wanted.ReflectionsGain;
		Reverb->ReflectionsDelay = Wanted.ReflectionsDelay;
		Reverb->LateGain = Wanted.LateGain;
		Reverb->LateDelay = Wanted.LateDelay;
		ReverbVolume = Wanted.Volume;
		UGameplayStatics::ActivateReverbEffect(World, Reverb, ReverbTag, 1.0f, ReverbVolume, 0.6f);
	}

	DripClock -= DeltaTime;
	if (DripClock <= 0.0f)
	{
		DripClock = Random.FRandRange(1.5f, 4.0f);
		DripNear(Head);
	}
}

void UMcAudioSubsystem::DripNear(const FVector& Head)
{
	const FIntVector Centre = ToCell(Head);
	for (int32 Try = 0; Try < 30; ++Try)
	{
		const FIntVector Cell = Centre + FIntVector(Random.RandRange(-12, 12), Random.RandRange(-8, 8), Random.RandRange(-12, 12));
		const FMcStateId State = GetBlockAt(Cell);
		const FName Name = GetBlockName(State);
		FName Above = NAME_None;
		FVector Where = FVector::ZeroVector;
		if (Name == DripstoneName && FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("vertical_direction")) == TEXT("down"))
		{
			// The tip of a stalactite: up through its stone to what is over it (water unless that is lava).
			int32 Up = 1;
			while (Up < 12)
			{
				const FName Block = GetBlockName(GetBlockAt(Cell + FIntVector(0, Up, 0)));
				if (Block != DripstoneName && Block != DripstoneBlockName)
				{
					break;
				}
				++Up;
			}
			Above = GetBlockName(GetBlockAt(Cell + FIntVector(0, Up, 0))) == LavaName ? LavaName : WaterName;
			Where = FVector(Cell.X + 0.5, Cell.Y + 0.2, Cell.Z + 0.5);
		}
		else if (!IsSolidAt(Cell) && Name != WaterName && Name != LavaName && IsSolidAt(Cell + FIntVector(0, 1, 0)))
		{
			// Open air under a ceiling with a pool or lava behind it.
			Above = GetBlockName(GetBlockAt(Cell + FIntVector(0, 2, 0)));
			Where = FVector(Cell.X + 0.5, Cell.Y + 0.9, Cell.Z + 0.5);
			if (Above != WaterName && Above != LavaName)
			{
				continue;
			}
		}
		else
		{
			continue;
		}
		FSpawn Spawn;
		Spawn.bAmbience = true;
		Spawn.Volume = 0.8f;
		const FName Drip = Above == LavaName ? FName(TEXT("block.pointed_dripstone.drip_lava")) : FName(TEXT("block.pointed_dripstone.drip_water"));
		PlayShaped(FMcSoundEvent{ Drip, Where, 1.0f, static_cast<float>(Random.FRandRange(0.9f, 1.1f)) }, Spawn);
		return;
	}
}

void UMcAudioSubsystem::UpdateOne(FLive& Sound, const FVector& Head, bool bSnap)
{
	UAudioComponent* Component = Sound.Component.Get();
	if (!Component)
	{
		return;
	}
	if (Sound.bPositional && bSnap)
	{
		const McAcoustics::FOcclusion Wanted = McAcoustics::GetOcclusion(
			McAcoustics::CountSolidBetween(Head, Sound.Position, [this](const FIntVector& At) { return IsSolidAt(At); }));
		Sound.TargetOcclusion = Wanted.Volume;
		Sound.TargetLowpass = Wanted.LowpassHz;
	}
	Sound.Occlusion = FMath::Lerp(Sound.Occlusion, Sound.TargetOcclusion, 0.2f);
	Sound.Lowpass = FMath::Exp(FMath::Lerp(FMath::Loge(Sound.Lowpass), FMath::Loge(Sound.TargetLowpass), 0.2f));
	const bool bDull = bUnderwater && !Sound.bUnfiltered;
	const float Volume = Sound.BaseVolume * Sound.Occlusion * (bDull ? McAcoustics::UnderwaterVolume : 1.0f) * (Sound.bAmbience ? Duck : 1.0f);
	float Lowpass = FMath::Min(Sound.Lowpass, Sound.LowpassCap);
	if (bDull)
	{
		Lowpass = FMath::Min(Lowpass, McAcoustics::UnderwaterLowpassHz);
	}
	Component->SetVolumeMultiplier(Volume);
	Component->SetLowPassFilterEnabled(Lowpass < 19000.0f);
	if (Lowpass < 19000.0f)
	{
		Component->SetLowPassFilterFrequency(Lowpass);
	}
}

void UMcAudioSubsystem::UpdateLive(const FVector& Head)
{
	// Each sound gets its rays every fourth frame, staggered.
	++LiveFrame;
	for (int32 Index = 0; Index < Live.Num(); ++Index)
	{
		UpdateOne(Live[Index], Head, (LiveFrame + Index) % 4 == 0);
	}
	// The ambience and the music duck for a blast and come back slowly.
	const float Dt = FMath::Max(static_cast<float>(FApp::GetDeltaTime()), 0.001f);
	DuckHold = FMath::Max(0.0f, DuckHold - Dt);
	Duck = FMath::FInterpTo(Duck, DuckHold > 0.0f ? 0.4f : 1.0f, Dt, DuckHold > 0.0f ? 10.0f : 0.8f);
	if (Music)
	{
		Music->SetVolumeMultiplier(MusicVolume * Duck * (bUnderwater ? McAcoustics::UnderwaterVolume : 1.0f));
		Music->SetLowPassFilterEnabled(bUnderwater);
		if (bUnderwater)
		{
			Music->SetLowPassFilterFrequency(McAcoustics::UnderwaterLowpassHz);
		}
	}
}

void UMcAudioSubsystem::ReleaseImmersive()
{
	if (!bImmersive)
	{
		return;
	}
	bImmersive = false;
	bUnderwater = false;
	Duck = 1.0f;
	DuckHold = 0.0f;
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::DeactivateReverbEffect(World, ReverbTag);
	}
	if (UAudioComponent* Bed = UnderwaterBed.Get())
	{
		Bed->Stop();
	}
	UnderwaterBed.Reset();
	for (FLive& Sound : Live)
	{
		if (UAudioComponent* Component = Sound.Component.Get())
		{
			Component->SetVolumeMultiplier(Sound.BaseVolume);
			Component->SetLowPassFilterEnabled(false);
		}
	}
	if (Music)
	{
		Music->SetVolumeMultiplier(MusicVolume);
		Music->SetLowPassFilterEnabled(false);
	}
	Enclosure = McAcoustics::FEnclosure();
}
