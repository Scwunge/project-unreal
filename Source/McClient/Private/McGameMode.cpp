#include "McGameMode.h"
#include "McChunk.h"
#include "McClient.h"
#include "McFreeCameraPawn.h"
#include "McWorldActor.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

#if MC_WITH_RENDER
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "McWorldRenderComponent.h"
#endif

AMcGameMode::AMcGameMode()
{
	DefaultPawnClass = AMcFreeCameraPawn::StaticClass();
}

void AMcGameMode::StartPlay()
{
	Super::StartPlay();

	// Spawned after Super::StartPlay so the world has begun play and BeginPlay runs straight away.
	WorldActor = GetWorld()->SpawnActor<AMcWorldActor>();
	if (!WorldActor || !WorldActor->GetBlockWorld())
	{
		UE_LOG(LogMcClient, Error, TEXT("No world to play in; see the McServer and McCore errors above."));
		return;
	}

#if MC_WITH_RENDER
	if (GetNetMode() != NM_DedicatedServer)
	{
		UMcWorldRenderComponent* Renderer = NewObject<UMcWorldRenderComponent>(WorldActor, TEXT("Renderer"));
		Renderer->SetupAttachment(WorldActor->GetRootComponent());
		Renderer->RegisterComponent();
		Renderer->SetWorld(WorldActor->GetBlockWorld());
		SpawnEnvironment();
	}
#endif

	// Players that joined before the world existed started at a guessed height; move them above the surface.
	const FTransform Spawn = GetSpawnTransform();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr)
		{
			Pawn->SetActorLocation(Spawn.GetLocation());
		}
	}
}

void AMcGameMode::RestartPlayer(AController* NewPlayer)
{
	// There are no PlayerStart actors: players start above the middle of block 0, 0.
	if (NewPlayer && !NewPlayer->IsPendingKillPending())
	{
		RestartPlayerAtTransform(NewPlayer, GetSpawnTransform());
	}
}

FTransform AMcGameMode::GetSpawnTransform() const
{
	// Before the world exists, guess the classic flat surface (y = -61).
	const int32 SurfaceY = WorldActor && WorldActor->GetBlockWorld() ? WorldActor->GetSurfaceY() : -61;
	const FVector Block(0.5, SurfaceY + 1 + SpawnHeightAboveSurface, 0.5);
	return FTransform(FRotator::ZeroRotator, McCoords::BlockToUnreal(Block));
}

void AMcGameMode::SpawnEnvironment()
{
#if MC_WITH_RENDER
	// A plain daylight sky to see the greybox by. M8 replaces it with Minecraft's sky, sun, moon and fog.
	UWorld* World = GetWorld();
	const FTransform SunTransform(FRotator(-50.0f, 30.0f, 0.0f));

	if (ADirectionalLight* Sun = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), SunTransform))
	{
		if (UDirectionalLightComponent* SunLight = Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
		{
			SunLight->SetMobility(EComponentMobility::Movable);
			SunLight->SetAtmosphereSunLight(true);
		}
		Sun->FinishSpawning(SunTransform);
	}

	World->SpawnActor<ASkyAtmosphere>();

	if (ASkyLight* Sky = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity))
	{
		if (USkyLightComponent* SkyLight = Sky->GetLightComponent())
		{
			SkyLight->SetMobility(EComponentMobility::Movable);
			SkyLight->bRealTimeCapture = true;
		}
		Sky->FinishSpawning(FTransform::Identity);
	}
#endif
}
