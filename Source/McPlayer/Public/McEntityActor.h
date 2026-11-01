// What players see of a server-side entity (a mob, a dropped item): an actor the server spawns for it, replicated to
// every client, which draws it. Unreal's replicated movement carries the position; the rest is a few fields.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "McEntityProxy.h"
#include "McEntityActor.generated.h"

class UStaticMeshComponent;
class UMcEntityModelComponent;

UCLASS()
class MCPLAYER_API AMcEntityActor : public AActor, public IMcEntityProxy
{
	GENERATED_BODY()

public:
	AMcEntityActor();

	virtual void SyncFromEntity(const FMcEntity& Entity) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// The registry name of the entity, e.g. "minecraft:pig".
	UPROPERTY(ReplicatedUsing = OnRep_Look)
	FName Kind;
	// Minecraft yaw of the body, degrees: 0 faces south, 90 west.
	UPROPERTY(Replicated)
	float BodyYaw = 0.0f;
	UPROPERTY(Replicated)
	float HeadYaw = 0.0f;
	UPROPERTY(Replicated)
	float HeadPitch = 0.0f;
	// Width and height of the hitbox in blocks.
	UPROPERTY(ReplicatedUsing = OnRep_Look)
	FVector2D Size = FVector2D(0.6, 1.8);
	// For a dropped item: the item, and how many.
	UPROPERTY(Replicated)
	FName ItemId;
	UPROPERTY(Replicated)
	int32 ItemCount = 0;
	// The mob's count of hits (FMcMob::HurtEvents), so clients can flash it red whenever it changes.
	UPROPERTY(Replicated)
	int32 HurtCount = 0;
	// A creeper's fuse, 0 (not lit) to 1 (about to go): clients swell it and flash it white.
	UPROPERTY(Replicated)
	float Swelling = 0.0f;
	// A fishing bobber: where the fish coming to it is, across the water (x and z, blocks), and y 1 while it floats.
	UPROPERTY(Replicated)
	FVector Wake = FVector::ZeroVector;

protected:
	UFUNCTION()
	void OnRep_Look();
	void ApplyLook();
	// A fishing bobber's angler's rod tip (block coordinates), if they are here.
	TOptional<FVector> GetRodTip() const;
	// A fishing bobber's splash when a fish bites and the wake of one swimming in.
	void TickBobberParticles(float DeltaSeconds);
	int32 LastBites = -1;
	float WakeAccumulator = 0.0f;

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Stand;
	// Whether the stand-in box has been coloured and reported.
	bool bStandInMarked = false;
	// The mob or item as it looks; null on a server that draws nothing.
	UPROPERTY()
	TObjectPtr<UMcEntityModelComponent> Model;
};
