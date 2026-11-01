#include "McEntityActor.h"
#include "McChunk.h"
#include "McEntityManager.h"
#include "McBannerEntity.h"
#include "McFishing.h"
#include "McPlayerPawn.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerState.h"
#include "EngineUtils.h"
#include "McMob.h"
#include "McTnt.h"
#include "McPot.h"
#include "McVehicle.h"
#include "Components/StaticMeshComponent.h"
#if MC_WITH_RENDER
#include "McEntityModel.h"
#include "McParticleComponent.h"
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
	DOREPLIFETIME(AMcEntityActor, Wake);
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
		// A pot shows its sides when dropped too.
		static const FName Pot(TEXT("minecraft:decorated_pot"));
		if (ItemId == Pot)
		{
			const TArray<FName> Sides = McPot::GetSides(Item.Stack);
			if (Sides.ContainsByPredicate([](FName Side) { return Side != TEXT("minecraft:brick"); }))
			{
				ItemId = FName(Pot.ToString() + TEXT("=") + FString::JoinBy(Sides, TEXT(","), [](FName Side) { return Side.ToString(); }));
			}
		}
	}
	else if (Entity.GetType() == FMcFallingBlockEntity::StaticType())
	{
		// The falling block's picture is its block's.
		ItemId = static_cast<const FMcFallingBlockEntity&>(Entity).Block;
	}
	else if (Kind == TEXT("minecraft:snowball") || Kind == TEXT("minecraft:egg") || Kind == TEXT("minecraft:ender_pearl"))
	{
		// A thrown item's picture is its item's.
		ItemId = Kind;
	}
	else if (Entity.GetType() == FMcBannerEntity::StaticType())
	{
		ItemId = FName(*static_cast<const FMcBannerEntity&>(Entity).Layers);
	}
	else if (Entity.GetType() == FMcPrimedTnt::StaticType())
	{
		// The model flashes by the fuse left.
		Swelling = static_cast<const FMcPrimedTnt&>(Entity).Fuse / static_cast<float>(FMcPrimedTnt::FullFuse);
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
	else if (Kind == FMcFishingBobber::StaticType())
	{
		// Whose rod it hangs from, its bites (a splash whenever they change), and the wake of a fish on its way in.
		const FMcFishingBobber& Bobber = static_cast<const FMcFishingBobber&>(Entity);
		ItemCount = Bobber.AnglerId;
		HurtCount = Bobber.Bites;
		Wake = FVector(Bobber.FishOffset.X, Bobber.State == FMcFishingBobber::EState::Bobbing ? 1.0 : 0.0, Bobber.FishOffset.Y);
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
		Model->SetHitboxHeight(Size.Y);
		Model->SetSwelling(Swelling);
		if (Kind == FMcFishingBobber::StaticType())
		{
			Model->SetFishingLine(GetRodTip());
			TickBobberParticles(DeltaSeconds);
		}
		if (Stand)
		{
			// The model draws a kind with no model yet as a magenta box; it is said once a kind in the log, so it is found and not missed.
			Stand->SetVisibility(false);
			if (!Model->HasModel() && !bStandInMarked)
			{
				bStandInMarked = true;
				static TSet<FName> Reported;
				if (!Reported.Contains(Kind))
				{
					Reported.Add(Kind);
					UE_LOG(LogTemp, Warning, TEXT("McEntityActor: no model for %s (first at %s); drawn as a magenta box"), *Kind.ToString(), *GetActorLocation().ToString());
				}
			}
		}
	}
#endif
}

void AMcEntityActor::TickBobberParticles(float DeltaSeconds)
{
#if MC_WITH_RENDER
	const AMcPlayerPawn* Viewer = nullptr;
	if (const APlayerController* Player = GetWorld()->GetFirstPlayerController())
	{
		Viewer = Cast<AMcPlayerPawn>(Player->GetPawn());
	}
	UMcParticleComponent* Particles = Viewer ? UMcParticleComponent::FindFor(Viewer->GetBlockWorld()) : nullptr;
	if (!Particles)
	{
		return;
	}
	const FVector Bobber = McCoords::UnrealToBlock(GetActorLocation()) + FVector(0.0, Size.Y * 0.5, 0.0);
	// A bite (the count of them went up since the last look): the splash.
	if (LastBites >= 0 && HurtCount != LastBites)
	{
		Particles->FishingSplash(Bobber + FVector(0.0, 0.1, 0.0));
	}
	LastBites = HurtCount;
	// While a fish swims in, its wake each tick where it is.
	WakeAccumulator += DeltaSeconds;
	for (; WakeAccumulator >= 0.05f; WakeAccumulator -= 0.05f)
	{
		if (Wake.Y > 0.0 && (Wake.X != 0.0 || Wake.Z != 0.0))
		{
			Particles->FishingWake(Bobber + FVector(Wake.X, 0.05, Wake.Z));
		}
	}
	WakeAccumulator = FMath::Min(WakeAccumulator, 0.05f);
#endif
}

TOptional<FVector> AMcEntityActor::GetRodTip() const
{
	// The angler, by their player id. No held items are drawn yet, so the tip is where the rod would hold it.
	for (TActorIterator<AMcPlayerPawn> It(GetWorld()); It; ++It)
	{
		const APlayerState* State = It->GetPlayerState();
		if (!State || State->GetPlayerId() != ItemCount)
		{
			continue;
		}
		const double YawRad = FMath::DegreesToRadians(static_cast<double>(It->GetLookYaw()));
		const double PitchRad = FMath::DegreesToRadians(static_cast<double>(It->GetLookPitch()));
		const FVector Forward(-FMath::Sin(YawRad), 0.0, FMath::Cos(YawRad));
		// The player's right hand, facing south (+z), is to the west.
		const FVector Right(-FMath::Cos(YawRad), 0.0, -FMath::Sin(YawRad));
		if (It->IsLocallyControlled() && It->Camera)
		{
			// Seen from the eyes: from low on the right of the view, out along the look.
			const FVector Look(-FMath::Sin(YawRad) * FMath::Cos(PitchRad), -FMath::Sin(PitchRad), FMath::Cos(YawRad) * FMath::Cos(PitchRad));
			const FVector Up = FVector::CrossProduct(Look, Right).GetSafeNormal() * -1.0;
			return McCoords::UnrealToBlock(It->Camera->GetComponentLocation()) + Look * 0.6 + Right * 0.35 - Up * 0.3;
		}
		// Held out in front at shoulder height, a little lower crouched.
		const FVector Feet = McCoords::UnrealToBlock(It->GetActorLocation());
		return Feet + Right * 0.35 + Forward * 0.8 + FVector(0.0, It->IsSneakPosed() ? 1.2 : 1.45, 0.0);
	}
	return TOptional<FVector>();
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
