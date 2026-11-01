#include "McTnt.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"

FMcPrimedTnt::FMcPrimedTnt()
{
	Body.Width = 0.98f;
	Body.Height = 0.98f;
	Body.StepHeight = 0.0f;
}

const FName& FMcPrimedTnt::StaticType()
{
	static const FName Name(TEXT("minecraft:tnt"));
	return Name;
}

int32 FMcPrimedTnt::GetChainFuse(float Roll)
{
	return FMath::Clamp(10 + FMath::FloorToInt32(Roll * 19.0f), 10, 28);
}

void FMcPrimedTnt::Tick(FMcEntityManager& Manager)
{
	FMcBlockWorld& World = Manager.GetWorld();
	if (!World.HasChunk(FIntPoint(FMath::FloorToInt32(Body.Position.X) >> 4, FMath::FloorToInt32(Body.Position.Z) >> 4)))
	{
		return;
	}
	Body.Velocity.Y -= Gravity;
	McPhysics::Move(World, Body, Body.Velocity, false);
	Body.Velocity *= Drag;
	if (Body.bOnGround)
	{
		Body.Velocity *= FVector(GroundFriction, -0.5, GroundFriction);
	}
	if (--Fuse <= 0)
	{
		bRemoved = true;
		Manager.Explode(Body.Position + FVector(0.0, Body.Height * 0.0625, 0.0), Power);
	}
}

bool FMcPrimedTnt::Serialize(FArchive& Ar)
{
	return FMcEntity::Serialize(Ar) && (Ar << Fuse, !Ar.IsError()) && (Ar.IsSaving() || (Fuse >= 0 && Fuse <= 32767));
}

FMcPrimedTnt* FMcPrimedTnt::Ignite(FMcEntityManager& Manager, const FIntVector& Cell, int32 InFuse, bool bSound)
{
	FMcBlockWorld& World = Manager.GetWorld();
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!IsTnt(Registry.GetBlockOf(World.GetBlock(Cell)).Name))
	{
		return nullptr;
	}
	World.SetBlock(Cell, Registry.FindDefaultState(TEXT("minecraft:air")).Get(0));
	TUniquePtr<FMcPrimedTnt> Tnt = MakeUnique<FMcPrimedTnt>();
	Tnt->Fuse = InFuse;
	Tnt->Body.Position = FVector(Cell.X + 0.5, Cell.Y, Cell.Z + 0.5);
	const double Angle = Manager.GetRandom().FRand() * 2.0 * PI;
	Tnt->Body.Velocity = FVector(-FMath::Sin(Angle) * 0.02, 0.2, -FMath::Cos(Angle) * 0.02);
	if (bSound)
	{
		Manager.PlaySound(FName(TEXT("entity.tnt.primed")), FVector(Cell.X + 0.5, Cell.Y, Cell.Z + 0.5));
	}
	return static_cast<FMcPrimedTnt*>(&Manager.Add(MoveTemp(Tnt)));
}
