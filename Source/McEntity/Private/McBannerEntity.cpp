#include "McBannerEntity.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McCache.h"

const FName& FMcBannerEntity::StaticType()
{
	static const FName Name(TEXT("mc:banner"));
	return Name;
}

FMcBannerEntity::FMcBannerEntity()
{
	Body.Width = 0.5f;
	Body.Height = 1.75f;
	Body.StepHeight = 0.0f;
}

bool FMcBannerEntity::Serialize(FArchive& Ar)
{
	return FMcEntity::Serialize(Ar) && (Ar << Layers, !Ar.IsError());
}

FMcBannerEntity* FMcBannerEntity::Stand(FMcEntityManager& Manager, const FIntVector& Pos, const TArray<McBanner::FLayer>& Layers)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcStateId State = Manager.GetWorld().GetBlock(Pos);
	FString Base = McCache::PathOf(Registry.GetBlockOf(State).Name.ToString());
	if (!Base.EndsWith(TEXT("_banner")))
	{
		return nullptr;
	}
	const bool bWall = Base.EndsWith(TEXT("_wall_banner"));
	Base.RemoveFromEnd(bWall ? TEXT("_wall_banner") : TEXT("_banner"));
	float Yaw = 0.0f;
	if (bWall)
	{
		const FName Facing = Registry.GetPropertyValue(State, TEXT("facing"));
		Yaw = Facing == TEXT("west") ? 90.0f : Facing == TEXT("north") ? 180.0f : Facing == TEXT("east") ? 270.0f : 0.0f;
	}
	else
	{
		Yaw = FCString::Atof(*Registry.GetPropertyValue(State, TEXT("rotation")).ToString()) * 22.5f;
	}
	if (FMcBannerEntity* Old = FindAt(Manager, Pos))
	{
		Old->bRemoved = true;
	}
	TUniquePtr<FMcBannerEntity> Flag = MakeUnique<FMcBannerEntity>();
	Flag->Layers = McBanner::Encode(Base, bWall, Layers);
	Flag->Body.Position = FVector(Pos) + FVector(0.5, 0.0, 0.5);
	Flag->Yaw = Yaw;
	return static_cast<FMcBannerEntity*>(&Manager.Add(MoveTemp(Flag)));
}

FMcBannerEntity* FMcBannerEntity::FindAt(FMcEntityManager& Manager, const FIntVector& Pos)
{
	for (const TUniquePtr<FMcEntity>& Entity : Manager.GetEntities())
	{
		if (Entity->GetType() == StaticType() && !Entity->bRemoved
			&& FMath::FloorToInt(Entity->Body.Position.X) == Pos.X && FMath::FloorToInt(Entity->Body.Position.Y + 0.01) == Pos.Y && FMath::FloorToInt(Entity->Body.Position.Z) == Pos.Z)
		{
			return static_cast<FMcBannerEntity*>(Entity.Get());
		}
	}
	return nullptr;
}
