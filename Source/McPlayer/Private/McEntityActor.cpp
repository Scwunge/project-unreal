#include "McEntityActor.h"
#include "McChunk.h"
#include "McEntityManager.h"
#include "McMob.h"
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
}

void AMcEntityActor::SyncFromEntity(const FMcEntity& Entity)
{
	SetActorLocation(McCoords::BlockToUnreal(Entity.Body.Position));
	Kind = Entity.GetType();
	Size = FVector2D(Entity.Body.Width, Entity.Body.Height);
	BodyYaw = Entity.Yaw;
	HeadYaw = Entity.Yaw;
	HeadPitch = Entity.Pitch;
	if (Kind == FMcItemEntity::StaticType())
	{
		const FMcItemEntity& Item = static_cast<const FMcItemEntity&>(Entity);
		ItemId = Item.Stack.Item;
		ItemCount = Item.Stack.Count;
	}
	else if (Entity.IsLiving())
	{
		// The mob counts its hits; clients flash it whenever this changes.
		HurtCount = static_cast<const FMcMob&>(Entity).HurtEvents;
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
