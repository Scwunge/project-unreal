#include "McEntityActor.h"
#include "McChunk.h"
#include "McEntityManager.h"
#include "McBannerEntity.h"
#include "McMob.h"
#include "McVehicle.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
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
	else if (Entity.GetType() == FMcBannerEntity::StaticType())
	{
		ItemId = FName(*static_cast<const FMcBannerEntity&>(Entity).Layers);
	}
	else if (FMcBoatEntity::IsBoatType(Entity.GetType()))
	{
		// Which wood it is made of picks its picture.
		ItemId = static_cast<const FMcBoatEntity&>(Entity).WoodName;
		// Which paddles row (bits).
		ItemCount = static_cast<const FMcBoatEntity&>(Entity).GetPaddleBits();
	}
	else if (FMcMinecartEntity::ParseType(Entity.GetType()).IsSet())
	{
		// A furnace cart tells whether it burns, a TNT cart the fuse it has left (the model lights and flashes by it).
		const FMcMinecartEntity& Cart = static_cast<const FMcMinecartEntity&>(Entity);
		ItemCount = Cart.Variant == FMcMinecartEntity::EVariant::Furnace ? (Cart.Fuel > 0 ? 1 : 0) : Cart.Variant == FMcMinecartEntity::EVariant::Tnt ? FMath::Max(Cart.Fuse, 0) : 0;
	}
	else if (Entity.IsLiving())
	{
		// The mob counts its hits; clients flash it whenever this changes.
		const FMcMob& Mob = static_cast<const FMcMob&>(Entity);
		HurtCount = Mob.HurtEvents;
		// The game's swell runs over the fuse less 2 ticks.
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
		if (Stand)
		{
			const bool bStandIn = !Model->HasModel();
			Stand->SetVisibility(bStandIn);
			if (bStandIn && !bStandInMarked)
			{
				// A kind with no model yet shows as a magenta box, said once a kind in the log, so it is found and not missed.
				bStandInMarked = true;
				static TSet<FName> Reported;
				if (!Reported.Contains(Kind))
				{
					Reported.Add(Kind);
					UE_LOG(LogTemp, Warning, TEXT("McEntityActor: no model for %s (first at %s); drawn as a magenta box"), *Kind.ToString(), *GetActorLocation().ToString());
				}
				if (UMaterialInstanceDynamic* Magenta = Stand->CreateDynamicMaterialInstance(0))
				{
					Magenta->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.0f, 0.0f, 1.0f));
				}
			}
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
