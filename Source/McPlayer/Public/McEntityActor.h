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
	// A villager's look: the biome type ("plains"), the profession ("minecraft:farmer"), the level and whether it is a baby.
	UPROPERTY(Replicated)
	FName Variant;
	UPROPERTY(Replicated)
	FName Profession;
	UPROPERTY(Replicated)
	int32 VillagerLevel = 1;
	UPROPERTY(Replicated)
	bool bBaby = false;
	// A golem holding out a poppy.
	UPROPERTY(Replicated)
	bool bOffering = false;
	// What a raider's arms are doing (McMob ArmPose).
	UPROPERTY(Replicated)
	int32 ArmPose = 0;
	// Which coat or breed the mob has, where its kind has several (McMobLook).
	UPROPERTY(Replicated)
	int32 LookSeed = 0;

protected:
	UFUNCTION()
	void OnRep_Look();
	void ApplyLook();

private:
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Stand;
	// The mob or item as it looks; null on a server that draws nothing.
	UPROPERTY()
	TObjectPtr<UMcEntityModelComponent> Model;
};
