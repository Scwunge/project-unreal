#include "McTravel.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McClient.h"
#include "McEntityHost.h"
#include "McMob.h"
#include "McVehicle.h"
#include "McGameMode.h"
#include "McPlayerController.h"
#include "McPlayerData.h"
#include "McPlayerPawn.h"
#include "McPortal.h"
#include "McWorldActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	const TCHAR* const Overworld = TEXT("minecraft:overworld");
	const TCHAR* const Nether = TEXT("minecraft:the_nether");
	const TCHAR* const End = TEXT("minecraft:the_end");

	FIntVector GetCell(const FVector& Feet)
	{
		return FIntVector(FMath::FloorToInt32(Feet.X), FMath::FloorToInt32(Feet.Y), FMath::FloorToInt32(Feet.Z));
	}

	// How far around the arrival point the destination's chunks are wanted before the player goes: the whole area a portal is looked for in.
	int32 GetChunkRadius(double DestinationScale)
	{
		return FMath::Max(McPortal::GetSearchRadius(DestinationScale), 16) / 16 + 1;
	}
}

FString McTravel::GetPortalDestination(const FString& From)
{
	return From == Nether ? FString(Overworld) : FString(Nether);
}

void UMcTravelSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!GetWorld() || !GetWorld()->GetAuthGameMode<AMcGameMode>())
	{
		return;
	}
	Accumulator = FMath::Min(Accumulator + DeltaSeconds, 0.25f);
	for (; Accumulator >= 0.05f; Accumulator -= 0.05f)
	{
		TickPlayers();
		TickEntities();
	}
	TickTrips();
	TickEntityTrips();
}

bool UMcTravelSubsystem::IsOnTrip(const AController* Controller) const
{
	return Trips.ContainsByPredicate([Controller](const FTrip& Trip) { return Trip.Controller.Get() == Controller; });
}

void UMcTravelSubsystem::TickPlayers()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
		AMcPlayerPawn* Pawn = Player ? Cast<AMcPlayerPawn>(Player->GetPawn()) : nullptr;
		AMcWorldActor* Here = Player ? Player->GetWorldActor() : nullptr;
		FMcBlockWorld* Blocks = Here ? Here->GetBlockWorld() : nullptr;
		if (!Pawn || !Blocks || !Pawn->Data || Pawn->IsServerDead() || IsOnTrip(Player))
		{
			continue;
		}
		const FIntVector Cell = GetCell(Pawn->GetFeetPosition());
		bool bInNether = false;
		bool bInEnd = false;
		for (const int32 Rise : { 0, 1 })
		{
			const FMcStateId State = Blocks->GetBlock(Cell + FIntVector(0, Rise, 0));
			bInNether |= McPortal::IsPortalBlock(State);
			bInEnd |= FMcBlockRegistry::Get().GetBlockOf(State).Name == TEXT("minecraft:end_portal");
		}
		FPortalState& State = States.FindOrAdd(Player);
		if (!bInNether && !bInEnd)
		{
			State = FPortalState();
			continue;
		}
		if (State.bMustLeave)
		{
			continue;
		}
		if (!bInEnd && State.Ticks == 0 && Pawn->Data->ServerSound)
		{
			// The portal's hum as it takes hold.
			Pawn->Data->ServerSound(FName("block.portal.trigger"), 0.25f, FMath::FRand() * 0.4f + 0.8f);
		}
		if (bInEnd)
		{
			State.bMustLeave = StartEndTrip(*Player);
		}
		else if (++State.Ticks >= (Pawn->Data->bCreative ? CreativePortalTicks : SurvivalPortalTicks))
		{
			// A frame broken some other way (an explosion) is found out here, before it takes anyone.
			const McPortal::FSetBlock Place = [Here](const FIntVector& At, FMcStateId Block) { Here->SetBlock(At, Block, false); };
			McPortal::RemoveBroken(*Blocks, Cell, Place);
			McPortal::RemoveBroken(*Blocks, Cell + FIntVector(0, 1, 0), Place);
			if (McPortal::IsPortalBlock(Blocks->GetBlock(Cell)) || McPortal::IsPortalBlock(Blocks->GetBlock(Cell + FIntVector(0, 1, 0))))
			{
				const FIntVector At = McPortal::IsPortalBlock(Blocks->GetBlock(Cell)) ? Cell : Cell + FIntVector(0, 1, 0);
				State.bMustLeave = StartPortalTrip(*Player, At);
			}
			State.Ticks = 0;
		}
	}
}

bool UMcTravelSubsystem::AddTrip(AMcPlayerController& Controller, AMcWorldActor& Dest, FTrip Trip)
{
	if (IsOnTrip(&Controller) || !Dest.GetBlockWorld())
	{
		return false;
	}
	Trip.Controller = &Controller;
	Trip.Dest = &Dest;
	Trip.StartedAt = GetWorld()->GetTimeSeconds();
	Trip.Chunk = FIntPoint(McCoords::BlockToChunk(Trip.Target.X), McCoords::BlockToChunk(Trip.Target.Z));
	Trip.Anchor = Dest.AddAnchor(Trip.Chunk, Trip.ChunkRadius);
	// A rider takes the boat or cart (vehicle_log46): it is packed now and set down with the rider on arrival.
	const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(Controller.GetPawn());
	FMcEntityHost* Host = Controller.GetWorldActor() ? Controller.GetWorldActor()->GetEntityHost() : nullptr;
	if (Trip.Kind != EKind::Exact && Pawn && Pawn->GetRidingId() != 0 && Host)
	{
		if (FMcEntity* Ridden = Host->GetManager().Find(Pawn->GetRidingId()))
		{
			Trip.VehicleBytes = Host->PackEntity(*Ridden);
			if (!Trip.VehicleBytes.IsEmpty())
			{
				Ridden->bRemoved = true;
			}
		}
	}
	Trips.Add(Trip);
	return true;
}

bool UMcTravelSubsystem::StartExactTrip(AMcPlayerController& Controller, const FString& Dimension, const FVector& Feet, float McYaw)
{
	AMcGameMode* Mode = GetWorld()->GetAuthGameMode<AMcGameMode>();
	AMcWorldActor* Dest = Mode ? Mode->GetOrCreateWorld(Dimension) : nullptr;
	if (!Dest)
	{
		return false;
	}
	FTrip Trip;
	Trip.Kind = EKind::Exact;
	Trip.Feet = Feet;
	Trip.McYaw = McYaw;
	Trip.Target = GetCell(Feet);
	Trip.ChunkRadius = 3;
	return AddTrip(Controller, *Dest, Trip);
}

bool UMcTravelSubsystem::StartPortalTrip(AMcPlayerController& Controller, const FIntVector& Cell)
{
	AMcGameMode* Mode = GetWorld()->GetAuthGameMode<AMcGameMode>();
	AMcWorldActor* Here = Controller.GetWorldActor();
	if (!Mode || !Here || !Here->GetBlockWorld())
	{
		return false;
	}
	AMcWorldActor* Dest = Mode->GetOrCreateWorld(McTravel::GetPortalDestination(Here->Dimension));
	if (!Dest || !Dest->GetBlockWorld())
	{
		return false;
	}
	const FMcDimensionType& From = Here->GetBlockWorld()->GetDimension();
	const FMcDimensionType& To = Dest->GetBlockWorld()->GetDimension();
	FTrip Trip;
	Trip.Kind = EKind::NetherPortal;
	Trip.Target = McPortal::ScalePosition(Cell, From.CoordinateScale, To.CoordinateScale);
	Trip.Target.Y = FMath::Clamp(Trip.Target.Y, To.MinY + 1, To.MinY + To.LogicalHeight - 2);
	Trip.bAlongX = McPortal::IsAlongX(Here->GetBlockWorld()->GetBlock(Cell));
	Trip.ChunkRadius = GetChunkRadius(To.CoordinateScale);
	if (const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(Controller.GetPawn()))
	{
		Trip.McYaw = Pawn->GetLookYaw();
	}
	UE_LOG(LogMcClient, Display, TEXT("%s goes through a portal at %s in %s to %s in %s"), *Controller.GetName(), *Cell.ToString(), *Here->Dimension, *Trip.Target.ToString(), *Dest->Dimension);
	return AddTrip(Controller, *Dest, Trip);
}

bool UMcTravelSubsystem::StartEndTrip(AMcPlayerController& Controller)
{
	AMcGameMode* Mode = GetWorld()->GetAuthGameMode<AMcGameMode>();
	AMcWorldActor* Here = Controller.GetWorldActor();
	if (!Mode || !Here)
	{
		return false;
	}
	if (Here->Dimension == End)
	{
		// Out of the End: back to the spawn of the first world. (The credits are not shown yet.)
		AMcWorldActor* Home = Mode->GetPrimaryWorld();
		if (!Home || Home == Here)
		{
			Home = Mode->GetOrCreateWorld(Overworld);
		}
		if (!Home)
		{
			return false;
		}
		const FVector Spawn = McCoords::UnrealToBlock(Mode->GetSpawnTransform().GetLocation());
		FTrip Trip;
		Trip.Kind = EKind::Exact;
		Trip.Feet = Spawn;
		Trip.Target = GetCell(Spawn);
		Trip.ChunkRadius = 3;
		UE_LOG(LogMcClient, Display, TEXT("%s leaves the End for %s"), *Controller.GetName(), *Home->Dimension);
		return AddTrip(Controller, *Home, Trip);
	}
	AMcWorldActor* Dest = Mode->GetOrCreateWorld(End);
	if (!Dest)
	{
		return false;
	}
	FTrip Trip;
	Trip.Kind = EKind::EndPlatform;
	Trip.Target = FIntVector(McPortal::EndPlatformX, McPortal::EndPlatformY + 1, McPortal::EndPlatformZ);
	Trip.ChunkRadius = 3;
	UE_LOG(LogMcClient, Display, TEXT("%s goes through an end portal to the End"), *Controller.GetName());
	return AddTrip(Controller, *Dest, Trip);
}

void UMcTravelSubsystem::TickTrips()
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (int32 Index = Trips.Num() - 1; Index >= 0; --Index)
	{
		FTrip& Trip = Trips[Index];
		AMcWorldActor* Dest = Trip.Dest.Get();
		AMcPlayerController* Player = Trip.Controller.Get();
		const AMcPlayerPawn* Pawn = Player ? Cast<AMcPlayerPawn>(Player->GetPawn()) : nullptr;
		const bool bGone = !Dest || !Pawn || Pawn->IsServerDead();
		const bool bLate = Now - Trip.StartedAt > TripTimeoutSeconds;
		if (bGone || bLate)
		{
			if (bLate && !bGone)
			{
				UE_LOG(LogMcClient, Warning, TEXT("%s waited %.0f seconds for %s to load around %s; the trip is cancelled"), *Player->GetName(), TripTimeoutSeconds, *Dest->Dimension, *Trip.Target.ToString());
			}
			if (Dest)
			{
				Dest->RemoveAnchor(Trip.Anchor);
			}
			Trips.RemoveAt(Index);
			continue;
		}
		if (!Dest->AreChunksLoaded(Trip.Chunk, Trip.ChunkRadius))
		{
			continue;
		}
		Finish(Trip);
		Dest->RemoveAnchor(Trip.Anchor);
		Trips.RemoveAt(Index);
	}
}

void UMcTravelSubsystem::Finish(FTrip& Trip)
{
	AMcWorldActor* Dest = Trip.Dest.Get();
	AMcPlayerController* Player = Trip.Controller.Get();
	AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(Player->GetPawn());
	FMcBlockWorld& Blocks = *Dest->GetBlockWorld();
	const McPortal::FSetBlock Place = [Dest](const FIntVector& At, FMcStateId State) { Dest->SetBlock(At, State, false); };

	FVector Feet = Trip.Feet;
	float Yaw = Trip.McYaw;
	if (Trip.Kind == EKind::NetherPortal)
	{
		bool bBuilt = false;
		const McPortal::FFound Portal = McPortal::FindOrBuildPortal(Blocks, Trip.Target, Trip.bAlongX, Place, bBuilt);
		Feet = McPortal::GetStandingPoint(Portal);
		// A portal that runs the other way turns the player with it.
		if (Portal.bAlongX != Trip.bAlongX)
		{
			Yaw += 90.0f;
		}
		UE_LOG(LogMcClient, Display, TEXT("%s arrives at the %s portal at %s in %s"), *Player->GetName(), bBuilt ? TEXT("new") : TEXT("existing"), *Portal.Pos.ToString(), *Dest->Dimension);
	}
	else if (Trip.Kind == EKind::EndPlatform)
	{
		McPortal::BuildEndPlatform(Blocks, Place);
		// On the platform, facing west.
		Feet = FVector(McPortal::EndPlatformX + 0.5, McPortal::EndPlatformY + 1, McPortal::EndPlatformZ + 0.5);
		Yaw = 90.0f;
	}
	Player->EnterWorld(*Dest);
	Pawn->ServerTeleport(Feet, Yaw);
	if (!Trip.VehicleBytes.IsEmpty() && Dest->GetEntityHost())
	{
		// The vehicle comes out at the portal's standing point with no speed (vehicle_log46); the rider sits in it again.
		if (FMcEntity* Arrived = Dest->GetEntityHost()->UnpackEntity(Trip.VehicleBytes, Feet))
		{
			Arrived->PortalCooldown = 300;
			Arrived->Body.Velocity = FVector::ZeroVector;
			Pawn->ServerRide(Arrived->Id);
		}
	}
	States.FindOrAdd(Player).bMustLeave = true;
	if (Trip.Kind == EKind::NetherPortal)
	{
		if (FMcEntityHost* Host = Dest->GetEntityHost())
		{
			Host->GetManager().PlaySound(FName("block.portal.travel"), Feet + FVector(0.0, 1.0, 0.0), 0.25f, FMath::FRand() * 0.4f + 0.8f);
		}
	}
}

void UMcTravelSubsystem::TickEntities()
{
	AMcGameMode* Mode = GetWorld()->GetAuthGameMode<AMcGameMode>();
	if (!Mode)
	{
		return;
	}
	++EntityTick;
	for (const TCHAR* Dimension : { Overworld, Nether, End })
	{
		AMcWorldActor* Here = Mode->FindWorld(Dimension);
		FMcEntityHost* Host = Here ? Here->GetEntityHost() : nullptr;
		FMcBlockWorld* Blocks = Here ? Here->GetBlockWorld() : nullptr;
		if (!Host || !Blocks)
		{
			continue;
		}
		TArray<FMcEntity*> Going;
		for (const TUniquePtr<FMcEntity>& Entity : Host->GetManager().GetEntities())
		{
			if (Entity->bRemoved)
			{
				continue;
			}
			const FIntVector Cell = GetCell(Entity->Body.Position);
			bool bInNether = false;
			bool bInEnd = false;
			for (const int32 Rise : { 0, 1 })
			{
				const FMcStateId State = Blocks->GetBlock(Cell + FIntVector(0, Rise, 0));
				bInNether |= McPortal::IsPortalBlock(State);
				bInEnd |= FMcBlockRegistry::Get().GetBlockOf(State).Name == TEXT("minecraft:end_portal");
			}
			if (!bInNether && !bInEnd)
			{
				Entity->PortalCooldown = FMath::Max(0, Entity->PortalCooldown - 1);
				continue;
			}
			if (Entity->PortalCooldown > 0)
			{
				Entity->PortalCooldown = 10;
				continue;
			}
			// Carrying a rider (or riding): it goes with the one that carries it, which is not done yet; left where it is.
			if (Entity->IsVehicle())
			{
				if (static_cast<FMcVehicle&>(*Entity).GetPassengerCount() > 0)
				{
					continue;
				}
			}
			else if (Entity->IsLiving() && static_cast<FMcMob&>(*Entity).RidingId != 0)
			{
				continue;
			}
			Going.Add(Entity.Get());
		}
		for (FMcEntity* Entity : Going)
		{
			const FIntVector Cell = GetCell(Entity->Body.Position);
			const bool bEnd = FMcBlockRegistry::Get().GetBlockOf(Blocks->GetBlock(Cell)).Name == TEXT("minecraft:end_portal")
				|| FMcBlockRegistry::Get().GetBlockOf(Blocks->GetBlock(Cell + FIntVector(0, 1, 0))).Name == TEXT("minecraft:end_portal");
			FEntityTrip Trip;
			AMcWorldActor* Dest = nullptr;
			if (bEnd)
			{
				if (Here->Dimension == End)
				{
					Dest = Mode->GetPrimaryWorld();
					if (!Dest || Dest == Here)
					{
						Dest = Mode->GetOrCreateWorld(Overworld);
					}
					Trip.Kind = EKind::Exact;
					Trip.Feet = McCoords::UnrealToBlock(Mode->GetSpawnTransform().GetLocation());
					Trip.Target = GetCell(Trip.Feet);
					Trip.ChunkRadius = 3;
				}
				else
				{
					Dest = Mode->GetOrCreateWorld(End);
					Trip.Kind = EKind::EndPlatform;
					Trip.Target = FIntVector(McPortal::EndPlatformX, McPortal::EndPlatformY + 1, McPortal::EndPlatformZ);
					Trip.ChunkRadius = 3;
				}
			}
			else
			{
				Dest = Mode->GetOrCreateWorld(McTravel::GetPortalDestination(Here->Dimension));
				if (Dest && Dest->GetBlockWorld())
				{
					const FMcDimensionType& From = Blocks->GetDimension();
					const FMcDimensionType& To = Dest->GetBlockWorld()->GetDimension();
					const FIntVector At = McPortal::IsPortalBlock(Blocks->GetBlock(Cell)) ? Cell : Cell + FIntVector(0, 1, 0);
					Trip.Kind = EKind::NetherPortal;
					Trip.Target = McPortal::ScalePosition(At, From.CoordinateScale, To.CoordinateScale);
					Trip.Target.Y = FMath::Clamp(Trip.Target.Y, To.MinY + 1, To.MinY + To.LogicalHeight - 2);
					Trip.bAlongX = McPortal::IsAlongX(Blocks->GetBlock(At));
					Trip.ChunkRadius = GetChunkRadius(To.CoordinateScale);
				}
			}
			if (!Dest || !Dest->GetBlockWorld() || !Dest->GetEntityHost())
			{
				Entity->PortalCooldown = 10;
				continue;
			}
			Trip.Bytes = Host->PackEntity(*Entity);
			if (Trip.Bytes.IsEmpty())
			{
				Entity->PortalCooldown = 300;
				continue;
			}
			Entity->bRemoved = true;
			Trip.Dest = Dest;
			Trip.StartedAt = GetWorld()->GetTimeSeconds();
			Trip.Chunk = FIntPoint(McCoords::BlockToChunk(Trip.Target.X), McCoords::BlockToChunk(Trip.Target.Z));
			Trip.Anchor = Dest->AddAnchor(Trip.Chunk, Trip.ChunkRadius);
			EntityTrips.Add(MoveTemp(Trip));
		}
	}
}

void UMcTravelSubsystem::TickEntityTrips()
{
	const double Now = GetWorld()->GetTimeSeconds();
	for (int32 Index = EntityTrips.Num() - 1; Index >= 0; --Index)
	{
		FEntityTrip& Trip = EntityTrips[Index];
		AMcWorldActor* Dest = Trip.Dest.Get();
		if (!Dest || Now - Trip.StartedAt > TripTimeoutSeconds)
		{
			if (Dest)
			{
				Dest->RemoveAnchor(Trip.Anchor);
			}
			EntityTrips.RemoveAt(Index);
			continue;
		}
		if (!Dest->AreChunksLoaded(Trip.Chunk, Trip.ChunkRadius))
		{
			continue;
		}
		FMcBlockWorld& Blocks = *Dest->GetBlockWorld();
		const McPortal::FSetBlock Place = [Dest](const FIntVector& At, FMcStateId State) { Dest->SetBlock(At, State, false); };
		FVector Feet = Trip.Feet;
		if (Trip.Kind == EKind::NetherPortal)
		{
			bool bBuilt = false;
			const McPortal::FFound Portal = McPortal::FindOrBuildPortal(Blocks, Trip.Target, Trip.bAlongX, Place, bBuilt);
			Feet = McPortal::GetStandingPoint(Portal);
		}
		else if (Trip.Kind == EKind::EndPlatform)
		{
			McPortal::BuildEndPlatform(Blocks, Place);
			Feet = FVector(McPortal::EndPlatformX + 0.5, McPortal::EndPlatformY + 1, McPortal::EndPlatformZ + 0.5);
		}
		if (FMcEntity* Arrived = Dest->GetEntityHost()->UnpackEntity(Trip.Bytes, Feet))
		{
			Arrived->PortalCooldown = 300;
			Arrived->Body.Velocity = FVector::ZeroVector;
			Arrived->Body.bOnGround = false;
		}
		Dest->RemoveAnchor(Trip.Anchor);
		EntityTrips.RemoveAt(Index);
	}
}
