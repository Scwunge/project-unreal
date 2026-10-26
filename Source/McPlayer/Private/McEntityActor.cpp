#include "McEntityActor.h"
#include "McChunk.h"
#include "McEntityManager.h"
#include "McMob.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

AMcEntityActor::AMcEntityActor()
{
	bReplicates = true;
	SetReplicateMovement(true);
	// A moving entity should be seen from far off but needn't be updated more than the movement needs.
	SetNetUpdateFrequency(20.0f);
	NetCullDistanceSquared = FMath::Square(100.0 * 16.0 * 12.0);
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Feet = CreateDefaultSubobject<USceneComponent>(TEXT("Feet"));
	SetRootComponent(Feet);

	// A box the size of the hitbox until the entity models are drawn.
	Stand = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Stand"));
	Stand->SetupAttachment(Feet);
	Stand->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Stand->SetStaticMesh(Cube.Object);
	}
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
}

void AMcEntityActor::SyncFromEntity(const FMcEntity& Entity)
{
	SetActorLocation(McCoords::BlockToUnreal(Entity.Body.Position));
	Kind = Entity.GetType();
	Size = FVector2D(Entity.Body.Width, Entity.Body.Height);
	BodyYaw = Entity.Yaw;
	HeadYaw = Entity.Yaw;
	HeadPitch = Entity.Pitch;
	if (Kind == TEXT("minecraft:item"))
	{
		const FMcItemEntity& Item = static_cast<const FMcItemEntity&>(Entity);
		ItemId = Item.Stack.Item;
		ItemCount = Item.Stack.Count;
	}
	else if (Entity.IsLiving())
	{
		const FMcMob& Mob = static_cast<const FMcMob&>(Entity);
		if (Mob.HurtTime == 10)
		{
			++HurtCount;
		}
	}
	ApplyLook();
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
