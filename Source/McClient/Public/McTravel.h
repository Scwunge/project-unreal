// Travel between dimensions (server only): standing in a Nether portal for 4 seconds (a moment in creative) or stepping into
// an end portal takes the player to the other dimension. The destination world is made if it isn't there, kept loading
// around the place they will arrive, and when its chunks are in, a portal is found or built there (minecraft.wiki,
// "Nether portal") and the player is moved to it. The rules about the blocks are McSim's McPortal.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "McTravel.generated.h"

class AController;
class AMcPlayerController;
class AMcWorldActor;

namespace McTravel
{
	// The dimension a Nether portal in From leads to: the Nether from any other, the Overworld from the Nether.
	FString GetPortalDestination(const FString& From);
}

UCLASS()
class UMcTravelSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMcTravelSubsystem, STATGROUP_Tickables); }
	virtual void Tick(float DeltaSeconds) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override { return WorldType == EWorldType::Game || WorldType == EWorldType::PIE; }

	// How long a player stands in a Nether portal before it takes them, in game ticks.
	static constexpr int32 SurvivalPortalTicks = 80;
	static constexpr int32 CreativePortalTicks = 1;
	// Seconds a trip waits for the destination's chunks before it is given up.
	static constexpr double TripTimeoutSeconds = 120.0;

	// Sends the player to a place in a dimension: the world and its chunks around the place are got ready first.
	// Returns false if the world couldn't be made.
	bool StartExactTrip(AMcPlayerController& Controller, const FString& Dimension, const FVector& Feet, float McYaw);
	// Sends the player in a Nether portal at Cell to the portal in the other dimension (made if there is none).
	bool StartPortalTrip(AMcPlayerController& Controller, const FIntVector& Cell);
	// Sends the player into the End, onto its platform, or back from it to the spawn.
	bool StartEndTrip(AMcPlayerController& Controller);
	// Whether the end poem and credits were shown this session (the first way out of the End shows them).
	bool bCreditsSeen = false;

	bool IsOnTrip(const AController* Controller) const;
	int32 NumTrips() const { return Trips.Num(); }
	int32 NumEntityTrips() const { return EntityTrips.Num(); }
	// Runs the trips that can finish (a test calls it between loading chunks by hand).
	void TickTrips();

private:
	enum class EKind : uint8
	{
		NetherPortal,
		EndPlatform,
		Exact,
	};
	struct FTrip
	{
		TWeakObjectPtr<AMcPlayerController> Controller;
		TWeakObjectPtr<AMcWorldActor> Dest;
		int32 Anchor = 0;
		EKind Kind = EKind::Exact;
		// The chunk the destination must have around it, and how many.
		FIntPoint Chunk = FIntPoint::ZeroValue;
		int32 ChunkRadius = 0;
		// Where in the destination the search for a portal starts.
		FIntVector Target = FIntVector::ZeroValue;
		// Whether the portal the player went into runs along x, and which way they looked.
		bool bAlongX = true;
		FVector Feet = FVector::ZeroVector;
		float McYaw = 0.0f;
		// The vehicle the player rides, packed for the trip (empty when on foot).
		TArray<uint8> VehicleBytes;
		double StartedAt = 0.0;
	};
	struct FPortalState
	{
		int32 Ticks = 0;
		// After arriving the player stands in a portal: they have to step out before it takes them again.
		bool bMustLeave = false;
	};

	bool AddTrip(AMcPlayerController& Controller, AMcWorldActor& Dest, FTrip Trip);
	void Finish(FTrip& Trip);
	void TickPlayers();

	// Everything but players that stands in a portal goes through (mobs, items, carts and boats without a player in them):
	// at once, then not again for 300 ticks (10 while it still stands in one).
	struct FEntityTrip
	{
		TWeakObjectPtr<AMcWorldActor> Dest;
		int32 Anchor = 0;
		EKind Kind = EKind::NetherPortal;
		FIntPoint Chunk = FIntPoint::ZeroValue;
		int32 ChunkRadius = 0;
		FIntVector Target = FIntVector::ZeroValue;
		bool bAlongX = true;
		FVector Feet = FVector::ZeroVector;
		TArray<uint8> Bytes;
		// Animals that were aboard a vehicle, packed alongside it.
		TArray<TArray<uint8>> Passengers;
		double StartedAt = 0.0;
	};
	void TickEntities();
	void TickEntityTrips();
	TArray<FEntityTrip> EntityTrips;
	int32 EntityTick = 0;

	TArray<FTrip> Trips;
	TMap<TWeakObjectPtr<AController>, FPortalState> States;
	float Accumulator = 0.0f;
};
