#include "McEntityActor.h"
#include "McChunk.h"
#include "McEntityManager.h"
#include "McMob.h"
#include "McPotion.h"
#include "McFireball.h"
#include "McVillager.h"
#include "Components/StaticMeshComponent.h"
#if MC_WITH_RENDER
#include "McEntityModel.h"
#endif
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMcEntityActor::AMcEntityActor()
{
	bReplicates = true;
	SetReplicateMovement(true);
	// A moving entity should be seen from far off but needn't be updated more than the movement needs.
	SetNetUpdateFrequency(20.0f);
	NetCullDistanceSquared = FMath::Square(100.0 * 16.0 * 12.0);
	PrimaryActorTick.bCanEverTick = MC_WITH_RENDER != 0;

	USceneComponent* Feet = CreateDefaultSubobject<USceneComponent>(TEXT("Feet"));
	SetRootComponent(Feet);

	// A box the size of the hitbox for what has no model.
	Stand = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Stand"));
	Stand->SetupAttachment(Feet);
	Stand->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Stand->SetStaticMesh(Cube.Object);
	}
#if MC_WITH_RENDER
	Model = CreateDefaultSubobject<UMcEntityModelComponent>(TEXT("Model"));
	Model->SetupAttachment(Feet);
#endif
}

void AMcEntityActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMcEntityActor, Kind);
	DOREPLIFETIME(AMcEntityActor, BodyYaw);
	DOREPLIFETIME(AMcEntityActor, HeadYaw);
	DOREPLIFETIME(AMcEntityActor, HeadPitch);
	DOREPLIFETIME(AMcEntityActor, Size);
	DOREPLIFETIME(AMcEntityActor, ItemId);
	DOREPLIFETIME(AMcEntityActor, ItemCount);
	DOREPLIFETIME(AMcEntityActor, HurtCount);
	DOREPLIFETIME(AMcEntityActor, Swelling);
	DOREPLIFETIME(AMcEntityActor, Variant);
	DOREPLIFETIME(AMcEntityActor, Profession);
	DOREPLIFETIME(AMcEntityActor, VillagerLevel);
	DOREPLIFETIME(AMcEntityActor, bBaby);
	DOREPLIFETIME(AMcEntityActor, bOffering);
	DOREPLIFETIME(AMcEntityActor, HeldItem);
	DOREPLIFETIME(AMcEntityActor, ArmPose);
	DOREPLIFETIME(AMcEntityActor, LookSeed);
}

void AMcEntityActor::SyncFromEntity(const FMcEntity& Entity)
{
	LookSeed = Entity.IsLiving() && static_cast<const FMcMob&>(Entity).LookSeed >= 0 ? static_cast<const FMcMob&>(Entity).LookSeed : 0;
	SetActorLocation(McCoords::BlockToUnreal(Entity.Body.Position));
	Kind = Entity.GetType();
	Size = FVector2D(Entity.Body.Width, Entity.Body.Height);
	BodyYaw = Entity.Yaw;
	HeadYaw = Entity.Yaw;
	HeadPitch = Entity.Pitch;
	if (Kind == FMcFireball::GhastType() || Kind == FMcFireball::SmallType())
	{
		// Drawn as the fire charge item (the game's own picture for it); its size and spin are not matched.
		Kind = FMcItemEntity::StaticType();
		ItemId = FName(TEXT("minecraft:fire_charge"));
		ItemCount = 1;
	}
	else if (Kind == FMcThrownPotion::StaticType())
	{
		// Drawn as the splash potion bottle in flight.
		Kind = FMcItemEntity::StaticType();
		ItemId = FName(TEXT("minecraft:splash_potion"));
		ItemCount = 1;
	}
	else if (Kind == FMcItemEntity::StaticType())
	{
		const FMcItemEntity& Item = static_cast<const FMcItemEntity&>(Entity);
		ItemId = Item.Stack.Item;
		ItemCount = Item.Stack.Count;
	}
	else if (Entity.IsLiving())
	{
		// The mob counts its hits; clients flash it whenever this changes.
		const FMcMob& Mob = static_cast<const FMcMob&>(Entity);
		HurtCount = Mob.HurtEvents;
		bOffering = Mob.OfferTicks > 0;
		HeldItem = Mob.HeldItem;
		ArmPose = Mob.ArmPose;
		if (Kind != FMcVillager::StaticType())
		{
			bBaby = Mob.IsYoung();
		}
		// The game's swell runs over the fuse less 2 ticks.
		if (Kind == FMcVillager::StaticType())
		{
			const FMcVillager& Villager = static_cast<const FMcVillager&>(Entity);
			Variant = Villager.GetVariant();
			Profession = Villager.GetProfession();
			VillagerLevel = Villager.GetLevel();
			bBaby = Villager.IsBaby();
		}
		if (Kind == FMcZombieVillager::StaticType())
		{
			const FMcZombieVillager& Zombie = static_cast<const FMcZombieVillager&>(Entity);
			Variant = Zombie.GetVariant();
			Profession = Zombie.GetProfession();
			VillagerLevel = Zombie.GetLevel();
		}
		Swelling = Mob.Type.bSwells ? FMath::Clamp(static_cast<float>(Mob.SwellTicks) / (FMcMob::FuseTicks - 2), 0.0f, 1.0f) : 0.0f;
	}
	ApplyLook();
}

void AMcEntityActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if MC_WITH_RENDER
	if (Model && !Kind.IsNone())
	{
		Model->SetKind(Kind, ItemId, ItemCount);
		Model->SetLook(BodyYaw, HeadYaw, HeadPitch);
		Model->SetHurtCount(HurtCount);
		Model->SetHitboxWidth(Size.X);
		Model->SetSwelling(Swelling);
		Model->SetVillagerLook(Variant, Profession, VillagerLevel, bBaby);
		Model->SetVariantSeed(LookSeed);
		Model->SetBaby(bBaby);
		Model->SetOffering(bOffering);
		Model->SetHeldItem(HeldItem);
		Model->SetPose(ArmPose);
		if (Stand)
		{
			Stand->SetVisibility(!Model->HasModel());
		}
	}
#endif
}

void AMcEntityActor::OnRep_Look()
{
	ApplyLook();
}

void AMcEntityActor::ApplyLook()
{
	if (!Stand)
	{
		return;
	}
	// The engine's cube is 100 units on a side, centred; stand it on the feet.
	const double Units = McCoords::UnitsPerBlock;
	Stand->SetRelativeScale3D(FVector(Size.X, Size.X, Size.Y));
	Stand->SetRelativeLocation(FVector(0.0, 0.0, Size.Y * Units * 0.5));
}
