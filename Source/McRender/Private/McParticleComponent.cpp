#include "McParticleComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McRenderMode.h"
#include "McSectionMesher.h"
#include "McWorldRenderComponent.h"

namespace
{
	// The colours biome-tinted bits take, the game's defaults (minecraft.wiki, "Color"): grass 91BD59, foliage 48B518.
	const FColor GrassTint(0x91, 0xBD, 0x59);
	const FColor FoliageTint(0x48, 0xB5, 0x18);

	FColor Mix(const FColor& A, const FColor& B, float T)
	{
		return FColor(
			static_cast<uint8>(FMath::Lerp(static_cast<float>(A.R), static_cast<float>(B.R), T)),
			static_cast<uint8>(FMath::Lerp(static_cast<float>(A.G), static_cast<float>(B.G), T)),
			static_cast<uint8>(FMath::Lerp(static_cast<float>(A.B), static_cast<float>(B.B), T)), 255);
	}

	TArray<TWeakObjectPtr<UMcParticleComponent>>& GetLive()
	{
		static TArray<TWeakObjectPtr<UMcParticleComponent>> Live;
		return Live;
	}

	FIntVector StepOf(FName Direction)
	{
		if (Direction == TEXT("north")) return FIntVector(0, 0, -1);
		if (Direction == TEXT("south")) return FIntVector(0, 0, 1);
		if (Direction == TEXT("west")) return FIntVector(-1, 0, 0);
		if (Direction == TEXT("east")) return FIntVector(1, 0, 0);
		return FIntVector::ZeroValue;
	}
}

UMcParticleComponent::UMcParticleComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	bUseAsyncCooking = false;
	Random.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
}

UMcParticleComponent* UMcParticleComponent::FindFor(const FMcBlockWorld* InWorld)
{
	for (const TWeakObjectPtr<UMcParticleComponent>& Live : GetLive())
	{
		if (Live.IsValid() && InWorld && Live->World == InWorld)
		{
			return Live.Get();
		}
	}
	return nullptr;
}

void UMcParticleComponent::OnUnregister()
{
	GetLive().RemoveAll([this](const TWeakObjectPtr<UMcParticleComponent>& Live) { return !Live.IsValid() || Live.Get() == this; });
	Super::OnUnregister();
}

void UMcParticleComponent::SetWorld(const FMcBlockWorld* InWorld, UMcWorldRenderComponent* InRenderer)
{
	GetLive().AddUnique(this);
	World = InWorld;
	Renderer = InRenderer;
	Particles.Reset();
	ClearAllMeshSections();
	if (World)
	{
		BuildEmitters();
	}
}

void UMcParticleComponent::BuildEmitters()
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	FMcBlockTags& Tags = FMcBlockTags::Get();
	Tags.EnsureLoaded();
	const int32 Leaves = Tags.Find(TEXT("leaves"));
	Emitters.Init(EEmitter::None, Registry.NumStates());
	Facing.Init(FIntVector::ZeroValue, Registry.NumStates());
	for (int32 Id = 0; Id < Registry.NumStates(); ++Id)
	{
		const FMcStateId State = static_cast<FMcStateId>(Id);
		if (!Registry.IsValidState(State))
		{
			continue;
		}
		const FName Name = Registry.GetBlockOf(State).Name;
		const bool bLit = Registry.GetPropertyValue(State, TEXT("lit")) == TEXT("true");
		Facing[Id] = StepOf(Registry.GetPropertyValue(State, TEXT("facing")));
		EEmitter& Emitter = Emitters[Id];
		if (Name == TEXT("minecraft:torch")) Emitter = EEmitter::Torch;
		else if (Name == TEXT("minecraft:wall_torch")) Emitter = EEmitter::WallTorch;
		else if (Name == TEXT("minecraft:soul_torch")) Emitter = EEmitter::SoulTorch;
		else if (Name == TEXT("minecraft:soul_wall_torch")) Emitter = EEmitter::SoulWallTorch;
		else if (Name == TEXT("minecraft:lava")) Emitter = EEmitter::Lava;
		else if (Name == TEXT("minecraft:campfire") && bLit) Emitter = EEmitter::Campfire;
		else if (Name == TEXT("minecraft:soul_campfire") && bLit) Emitter = EEmitter::SoulCampfire;
		else if ((Name == TEXT("minecraft:furnace") || Name == TEXT("minecraft:blast_furnace") || Name == TEXT("minecraft:smoker")) && bLit) Emitter = EEmitter::Furnace;
		else if (Tags.Has(Leaves, State)) Emitter = EEmitter::Leaves;
	}
}

bool UMcParticleComponent::GetTexture(FMcStateId State, uint16& Layer, FColor& Tint) const
{
	const UMcWorldRenderComponent* Drawer = Renderer.Get();
	const FMcRenderPalette* Palette = Drawer ? Drawer->GetPalette() : nullptr;
	if (!Palette || !Palette->Models || !Palette->States.IsValidIndex(State))
	{
		return false;
	}
	const FMcBakedModel& Model = Palette->Models->GetModel(State);
	if (Model.Quads.IsEmpty())
	{
		return false;
	}
	// A side of the block, as the game's particle texture mostly is (a grass block's bits are dirt and grass, a
	// log's its bark); else its first quad.
	int32 Pick = 0;
	for (int32 Index = 0; Index < Model.Quads.Num(); ++Index)
	{
		if (Model.Quads[Index].Face == EMcDirection::North)
		{
			Pick = Index;
			break;
		}
	}
	const FMcRenderPalette::FStateInfo& Info = Palette->States[State];
	Layer = Palette->Quads[Info.FirstQuad + Pick].Layer;
	Tint = FColor::White;
	if (Model.Quads[Pick].TintIndex >= 0)
	{
		Tint = Info.TintSource == EMcTintSource::Fixed ? Info.Tint
			: Info.TintSource == EMcTintSource::Foliage ? FoliageTint
			: Info.TintSource == EMcTintSource::Grass ? GrassTint
			: FColor::White;
	}
	return true;
}

float UMcParticleComponent::SkyLightAt(const FIntVector& Pos) const
{
	if (!World || World->GetDimension().bHasCeiling || !World->GetDimension().bHasSkylight)
	{
		return 0.0f;
	}
	// Leaves cost the light one level each (as in the game); anything else solid above is a roof.
	static const int32 Leaves = FMcBlockTags::Get().Find(TEXT("leaves"));
	int32 Foliage = 0;
	int32 Roof = 0;
	for (int32 Up = 1; Up <= 24; ++Up)
	{
		const FMcStateId Above = World->GetBlock(Pos + FIntVector(0, Up, 0));
		if (Above != 0)
		{
			(FMcBlockTags::Get().Has(Leaves, Above) ? Foliage : Roof) += 1;
		}
	}
	if (Roof > 0)
	{
		return Roof < 3 ? 0.5f : 0.15f;
	}
	return FMath::Max(0.6f, 1.0f - Foliage / 15.0f);
}

UMcParticleComponent::FParticle& UMcParticleComponent::Add(EStyle Style, const FVector& Pos, const FVector& Velocity)
{
	if (Particles.Num() >= MaxParticles)
	{
		Particles.RemoveAt(0, Particles.Num() - MaxParticles + 1, EAllowShrinking::No);
	}
	FParticle& Particle = Particles.AddDefaulted_GetRef();
	Particle.Style = Style;
	Particle.Pos = Pos;
	Particle.Prev = Pos;
	Particle.Velocity = Velocity;
	return Particle;
}

void UMcParticleComponent::BlockBroken(const FIntVector& Pos, FMcStateId State)
{
	uint16 Layer = 0;
	FColor Tint;
	if (!World || !GetTexture(State, Layer, Tint))
	{
		return;
	}
	const bool bUnreal = McRenderMode::IsEnhanced();
	const float Sky = SkyLightAt(Pos);
	// The game's bits: four along each side of the block, each thrown out from the middle, quick at first.
	const int32 PerSide = bUnreal ? 5 : 4;
	for (int32 X = 0; X < PerSide; ++X)
	{
		for (int32 Y = 0; Y < PerSide; ++Y)
		{
			for (int32 Z = 0; Z < PerSide; ++Z)
			{
				const FVector Inside((X + 0.5) / PerSide, (Y + 0.5) / PerSide, (Z + 0.5) / PerSide);
				FVector Velocity = (Inside - FVector(0.5)) + FVector(Random.FRandRange(-0.4f, 0.4f), Random.FRandRange(-0.4f, 0.4f), Random.FRandRange(-0.4f, 0.4f));
				const double Speed = (Random.FRand() + Random.FRand() + 1.0) * 0.15;
				Velocity = Velocity.GetSafeNormal() * Speed * 0.4 + FVector(0.0, 0.1, 0.0);
				FParticle& Bit = Add(EStyle::Debris, FVector(Pos) + Inside, Velocity);
				Bit.Gravity = 0.04f;
				Bit.bPhysics = true;
				Bit.Life = FMath::Floor(4.0f / (Random.FRand() * 0.9f + 0.1f)) * (bUnreal ? 1.6f : 1.0f);
				Bit.Size = 0.05f * (Random.FRand() * 0.5f + 0.5f) * 2.0f * (bUnreal ? 1.15f : 1.0f);
				Bit.Layer = Layer;
				Bit.Colour = Tint;
				Bit.UVSize = 0.25f;
				Bit.UVMin = FVector2D(Random.FRand() * 0.75f, Random.FRand() * 0.75f);
				Bit.Light = FVector2f(Sky, 0.0f);
			}
		}
	}
	if (bUnreal)
	{
		// A puff of dust, the block's own colours, lifting and spreading as it fades.
		for (int32 Puff = 0; Puff < 10; ++Puff)
		{
			const FVector At = FVector(Pos) + FVector(Random.FRandRange(0.15f, 0.85f), Random.FRandRange(0.1f, 0.8f), Random.FRandRange(0.15f, 0.85f));
			FParticle& Dust = Add(EStyle::Dust, At, FVector(Random.FRandRange(-0.03f, 0.03f), Random.FRandRange(0.0f, 0.03f), Random.FRandRange(-0.03f, 0.03f)));
			Dust.Gravity = -0.002f;
			Dust.Friction = 0.9f;
			Dust.Life = Random.FRandRange(18.0f, 34.0f);
			Dust.Size = Random.FRandRange(0.12f, 0.2f);
			Dust.Layer = Layer;
			Dust.Colour = Mix(Tint, FColor(200, 200, 200), 0.25f);
			Dust.UVSize = 0.5f;
			Dust.UVMin = FVector2D(Random.FRand() * 0.5f, Random.FRand() * 0.5f);
			Dust.Light = FVector2f(Sky, 0.0f);
		}
	}
}

void UMcParticleComponent::BlockHit(const FIntVector& Pos, const FIntVector& Face)
{
	uint16 Layer = 0;
	FColor Tint;
	const FMcStateId State = World ? World->GetBlock(Pos) : 0;
	if (State == 0 || !GetTexture(State, Layer, Tint))
	{
		return;
	}
	// On the face, a little out from it, slow.
	FVector At = FVector(Pos) + FVector(Random.FRandRange(0.1f, 0.9f), Random.FRandRange(0.1f, 0.9f), Random.FRandRange(0.1f, 0.9f));
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (Face[Axis] != 0)
		{
			At[Axis] = Pos[Axis] + (Face[Axis] > 0 ? 1.1 : -0.1);
		}
	}
	FVector Velocity(Random.FRandRange(-0.4f, 0.4f), Random.FRandRange(-0.4f, 0.4f) + 0.5f, Random.FRandRange(-0.4f, 0.4f));
	Velocity = Velocity.GetSafeNormal() * (Random.FRand() + Random.FRand() + 1.0) * 0.15 * 0.4 * 0.2;
	FParticle& Bit = Add(EStyle::Debris, At, Velocity);
	Bit.Gravity = 0.04f;
	Bit.bPhysics = true;
	Bit.Life = FMath::Floor(4.0f / (Random.FRand() * 0.9f + 0.1f));
	Bit.Size = 0.05f * (Random.FRand() * 0.5f + 0.5f) * 2.0f * 0.6f;
	Bit.Layer = Layer;
	Bit.Colour = Tint;
	Bit.UVSize = 0.25f;
	Bit.UVMin = FVector2D(Random.FRand() * 0.75f, Random.FRand() * 0.75f);
	Bit.Light = FVector2f(SkyLightAt(Pos + Face), 0.0f);
}

void UMcParticleComponent::Ambient()
{
	// As the game does: a number of blocks at random near the player, within 16 and within 32.
	const FIntVector Centre(FMath::FloorToInt(CameraBlock.X), FMath::FloorToInt(CameraBlock.Y), FMath::FloorToInt(CameraBlock.Z));
	for (int32 Radius : { 16, 32 })
	{
		for (int32 Try = 0; Try < 667; ++Try)
		{
			const FIntVector Pos = Centre + FIntVector(Random.RandRange(-Radius + 1, Radius - 1), Random.RandRange(-Radius + 1, Radius - 1), Random.RandRange(-Radius + 1, Radius - 1));
			AmbientAt(Pos);
		}
	}
}

void UMcParticleComponent::AmbientAt(const FIntVector& Pos)
{
	const FMcStateId State = World->GetBlock(Pos);
	if (!Emitters.IsValidIndex(State) || Emitters[State] == EEmitter::None)
	{
		return;
	}
	const bool bUnreal = McRenderMode::IsEnhanced();
	auto Flame = [&](const FVector& At, bool bSoul)
	{
		FParticle& Fire = Add(bSoul ? EStyle::SoulFlame : EStyle::Flame, At, FVector(Random.FRandRange(-0.002f, 0.002f), Random.FRandRange(0.0f, 0.004f), Random.FRandRange(-0.002f, 0.002f)));
		Fire.Friction = 0.96f;
		Fire.Life = FMath::Floor(8.0f / (Random.FRand() * 0.8f + 0.2f)) + 4.0f;
		Fire.Size = 0.05f * (Random.FRand() * 0.2f + 0.5f);
		Fire.Light = FVector2f(1.0f, 1.0f);
	};
	auto Smoke = [&](const FVector& At, bool bBig)
	{
		FParticle& Puff = Add(bBig ? EStyle::BigSmoke : EStyle::Smoke, At,
			bBig ? FVector(Random.FRandRange(-0.005f, 0.005f), 0.07f, Random.FRandRange(-0.005f, 0.005f)) : FVector(Random.FRandRange(-0.01f, 0.01f), 0.01f, Random.FRandRange(-0.01f, 0.01f)));
		Puff.Gravity = bBig ? -0.0003f : -0.002f;
		Puff.Friction = bBig ? 0.98f : 0.96f;
		Puff.Life = bBig ? Random.FRandRange(80.0f, 160.0f) : FMath::Floor(8.0f / (Random.FRand() * 0.8f + 0.2f));
		Puff.Size = bBig ? Random.FRandRange(0.07f, 0.14f) : 0.05f * (Random.FRand() * 0.5f + 0.5f) * 1.5f;
		const uint8 Grey = static_cast<uint8>(Random.RandRange(bBig ? 100 : 70, bBig ? 135 : 110) + (McRenderMode::IsEnhanced() ? (bBig ? 15 : 50) : 0));
		Puff.Colour = FColor(Grey, Grey, Grey);
		Puff.Light = FVector2f(SkyLightAt(Pos), 0.6f);
	};
	const FVector Centre = FVector(Pos) + FVector(0.5);
	const FIntVector& Facing0 = Facing[State];
	switch (Emitters[State])
	{
	case EEmitter::Torch:
	case EEmitter::SoulTorch:
	{
		const FVector At = FVector(Pos) + FVector(0.5, 0.7, 0.5);
		Smoke(At, false);
		Flame(At, Emitters[State] == EEmitter::SoulTorch);
		break;
	}
	case EEmitter::WallTorch:
	case EEmitter::SoulWallTorch:
	{
		// Leaning out from the wall behind it: its head is towards the wall side of the block and higher.
		const FVector At = FVector(Pos) + FVector(0.5, 0.92, 0.5) - FVector(Facing0) * 0.27;
		Smoke(At, false);
		Flame(At, Emitters[State] == EEmitter::SoulWallTorch);
		break;
	}
	case EEmitter::Campfire:
	case EEmitter::SoulCampfire:
		if (Random.RandRange(0, 9) < (bUnreal ? 6 : 3))
		{
			Smoke(Centre + FVector(Random.FRandRange(-0.33f, 0.33f), 0.0, Random.FRandRange(-0.33f, 0.33f)), true);
		}
		if (Random.RandRange(0, 4) == 0)
		{
			Flame(Centre + FVector(Random.FRandRange(-0.3f, 0.3f), -0.1, Random.FRandRange(-0.3f, 0.3f)), Emitters[State] == EEmitter::SoulCampfire);
		}
		break;
	case EEmitter::Furnace:
		if (Random.RandRange(0, 1) == 0 && Facing0 != FIntVector::ZeroValue)
		{
			// Out of the front, low down.
			const FVector Side = FVector(Facing0.Z, 0, Facing0.X) * Random.FRandRange(-0.3f, 0.3f);
			const FVector At = Centre + FVector(Facing0) * 0.52 + Side + FVector(0.0, Random.FRandRange(-0.5f, -0.1f), 0.0);
			Smoke(At, false);
			Flame(At, false);
		}
		break;
	case EEmitter::Lava:
		// A drop thrown up off the surface, now and again; in Unreal Mode sparks too.
		if (World->GetBlock(Pos + FIntVector(0, 1, 0)) == 0)
		{
			if (Random.RandRange(0, 99) == 0)
			{
				FParticle& Drop = Add(EStyle::Lava, FVector(Pos) + FVector(Random.FRand(), 1.0, Random.FRand()),
					FVector(Random.FRandRange(-0.04f, 0.04f), Random.FRand() * 0.4f + 0.05f, Random.FRandRange(-0.04f, 0.04f)));
				Drop.Gravity = 0.03f;
				Drop.Friction = 0.999f;
				Drop.bPhysics = true;
				Drop.Life = 16.0f / (Random.FRand() * 0.8f + 0.2f);
				Drop.Size = 0.05f * (Random.FRand() * 0.5f + 0.5f) * 2.0f;
				Drop.Light = FVector2f(1.0f, 1.0f);
			}
			if (bUnreal && Random.RandRange(0, 15) == 0)
			{
				FParticle& Spark = Add(EStyle::Spark, FVector(Pos) + FVector(Random.FRand(), 0.95, Random.FRand()),
					FVector(Random.FRandRange(-0.01f, 0.01f), Random.FRandRange(0.02f, 0.06f), Random.FRandRange(-0.01f, 0.01f)));
				Spark.Gravity = -0.001f;
				Spark.Friction = 0.97f;
				Spark.Life = Random.FRandRange(20.0f, 50.0f);
				Spark.Size = Random.FRandRange(0.012f, 0.025f);
				Spark.Light = FVector2f(1.0f, 1.0f);
			}
		}
		break;
	case EEmitter::Leaves:
		// A leaf drifts down from leaves with air below them.
		if (World->GetBlock(Pos + FIntVector(0, -1, 0)) == 0 && Random.RandRange(0, bUnreal ? 14 : 39) == 0)
		{
			uint16 Layer = 0;
			FColor Tint;
			if (GetTexture(State, Layer, Tint))
			{
				FParticle& Leaf = Add(EStyle::Leaf, FVector(Pos) + FVector(Random.FRand(), -0.05, Random.FRand()), FVector(0.0, -0.01, 0.0));
				Leaf.Gravity = 0.0015f;
				Leaf.Friction = 0.94f;
				Leaf.bPhysics = true;
				Leaf.Life = 300.0f;
				Leaf.Size = 0.06f;
				Leaf.Layer = Layer;
				Leaf.Colour = Tint;
				Leaf.UVSize = 0.25f;
				Leaf.UVMin = FVector2D(Random.FRand() * 0.75f, Random.FRand() * 0.75f);
				Leaf.Phase = Random.FRand() * UE_TWO_PI;
				Leaf.Light = FVector2f(SkyLightAt(Pos + FIntVector(0, -1, 0)), 0.0f);
			}
		}
		break;
	default:
		break;
	}
}

void UMcParticleComponent::Step()
{
	for (int32 Index = Particles.Num() - 1; Index >= 0; --Index)
	{
		FParticle& Particle = Particles[Index];
		Particle.Prev = Particle.Pos;
		Particle.Age += 1.0f;
		// A leaf on the ground stays a while, then goes.
		if (Particle.Age >= Particle.Life || (Particle.Style == EStyle::Leaf && Particle.bOnGround && Particle.Age > 60.0f && Random.RandRange(0, 40) == 0))
		{
			Particles.RemoveAtSwap(Index, EAllowShrinking::No);
			continue;
		}
		Particle.Velocity.Y -= Particle.Gravity;
		if (Particle.Style == EStyle::Leaf && !Particle.bOnGround)
		{
			// Swinging side to side as it falls.
			Particle.Phase += 0.12f;
			Particle.Velocity.X += FMath::Cos(Particle.Phase) * 0.0025;
			Particle.Velocity.Z += FMath::Sin(Particle.Phase * 0.7f) * 0.0025;
		}
		FVector Motion = Particle.Velocity;
		if (Particle.bPhysics && Collide)
		{
			const FBox Box(Particle.Pos - FVector(Particle.Size * 0.5), Particle.Pos + FVector(Particle.Size * 0.5));
			const FVector Allowed = Collide(Box, Motion);
			Particle.bOnGround = Motion.Y < 0.0 && Allowed.Y > Motion.Y + 1e-6;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				if (!FMath::IsNearlyEqual(Allowed[Axis], Motion[Axis], 1e-6))
				{
					Particle.Velocity[Axis] = 0.0;
				}
			}
			Motion = Allowed;
		}
		Particle.Pos += Motion;
		Particle.Velocity *= Particle.Friction;
		if (Particle.bOnGround)
		{
			Particle.Velocity.X *= 0.7;
			Particle.Velocity.Z *= 0.7;
		}
	}
}

void UMcParticleComponent::Draw(float Partial)
{
	UMcWorldRenderComponent* Drawer = Renderer.Get();
	UMaterialInterface* Material = Drawer ? Drawer->GetPassMaterial(EMcRenderPass::Cutout) : nullptr;
	if (!Material || Particles.IsEmpty())
	{
		ClearAllMeshSections();
		return;
	}
	const APlayerController* Player = GetWorld()->GetFirstPlayerController();
	if (!Player || !Player->PlayerCameraManager)
	{
		return;
	}
	const FRotator View = Player->PlayerCameraManager->GetCameraRotation();
	const FVector Right = FRotationMatrix(View).GetUnitAxis(EAxis::Y);
	const FVector Up = FRotationMatrix(View).GetUnitAxis(EAxis::Z);
	// Unreal Mode's bits lean towards the sky as well as the camera: they're lit by the sun and the sky above them
	// (a camera-facing flat quad away from the sun gets no light, and they'd be black).
	const bool bSkyLit = McRenderMode::IsEnhanced();
	const FVector ViewFacing = bSkyLit ? (-View.Vector() + FVector(0.0, 0.0, 1.5)).GetSafeNormal() : -View.Vector();
	const bool bUnreal = McRenderMode::IsEnhanced();

	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0, UV1, UV2;
	TArray<FColor> Colours;
	TArray<int32> Triangles;
	const int32 Count = Particles.Num();
	Positions.Reserve(Count * 4);
	Normals.Reserve(Count * 4);
	UV0.Reserve(Count * 4);
	UV1.Reserve(Count * 4);
	UV2.Reserve(Count * 4);
	Colours.Reserve(Count * 4);
	Triangles.Reserve(Count * 6);
	for (const FParticle& Particle : Particles)
	{
		const float T = FMath::Clamp((Particle.Age + Partial) / Particle.Life, 0.0f, 1.0f);
		float Size = Particle.Size;
		FColor Colour = Particle.Colour;
		uint8 Kind = McSurface::Particle;
		bool bTextured = false;
		switch (Particle.Style)
		{
		case EStyle::Debris:
			bTextured = true;
			// Unreal Mode's bits shrink away at the end instead of vanishing.
			if (bUnreal)
			{
				Size *= FMath::Clamp((1.0f - T) * 4.0f, 0.0f, 1.0f);
			}
			break;
		case EStyle::Dust:
			bTextured = true;
			Size *= 0.6f + T * 0.8f;
			Size *= FMath::Clamp((1.0f - T) * 2.5f, 0.0f, 1.0f);
			break;
		case EStyle::Leaf:
			bTextured = true;
			Size *= FMath::Clamp((Particle.Life - Particle.Age) / 10.0f, 0.0f, 1.0f);
			break;
		case EStyle::Flame:
		case EStyle::SoulFlame:
			// Shrinking as it burns, yellow to orange to red (cyan to blue for soul fire).
			Size *= 1.0f - T * T * 0.5f;
			Colour = Particle.Style == EStyle::Flame
				? (T < 0.5f ? Mix(FColor(255, 236, 140), FColor(255, 150, 40), T * 2.0f) : Mix(FColor(255, 150, 40), FColor(200, 50, 20), T * 2.0f - 1.0f))
				: Mix(FColor(150, 255, 255), FColor(40, 140, 220), T);
			Kind = bUnreal ? McSurface::GlowParticle : McSurface::Particle;
			break;
		case EStyle::Lava:
			// In Unreal Mode a drop is a small glowing fleck, not a solid square.
			Size *= (1.0f - T * T) * (bUnreal ? 0.4f : 1.0f);
			Colour = Mix(FColor(255, 200, 60), FColor(220, 70, 10), T);
			Kind = bUnreal ? McSurface::GlowParticle : McSurface::Particle;
			break;
		case EStyle::Spark:
			Size *= FMath::Clamp((1.0f - T) * 3.0f, 0.0f, 1.0f);
			Colour = Mix(FColor(255, 230, 120), FColor(255, 90, 20), T);
			Kind = McSurface::GlowParticle;
			break;
		case EStyle::Smoke:
			Size *= 1.0f + T * 1.5f;
			break;
		case EStyle::BigSmoke:
			Size *= 1.0f + T * 1.2f;
			Size *= FMath::Clamp((1.0f - T) * 5.0f, 0.0f, 1.0f);
			break;
		}
		if (Size <= 0.001f)
		{
			continue;
		}
		const FVector Centre = McCoords::BlockToUnreal(FMath::Lerp(Particle.Prev, Particle.Pos, static_cast<double>(Partial)));
		const double Half = Size * McCoords::UnitsPerBlock;
		const int32 First = Positions.Num();
		const FVector Corners[4] = { -Right + Up, -Right - Up, Right - Up, Right + Up };
		const FVector2D CornerUV[4] = { FVector2D(0, 0), FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0) };
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Positions.Add(Centre + Corners[Corner] * Half);
			Normals.Add(ViewFacing);
			UV0.Add(bTextured ? Particle.UVMin + CornerUV[Corner] * Particle.UVSize : FVector2D(0.5, 0.5));
			UV1.Add(FVector2D(Particle.Layer, bTextured ? McSurface::TexturedParticle : Kind));
			UV2.Add(FVector2D(Particle.Light));
			Colours.Add(Colour);
		}
		Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
	}
	CreateMeshSection(0, Positions, Triangles, Normals, UV0, UV1, UV2, TArray<FVector2D>(), Colours, TArray<FProcMeshTangent>(), false);
	if (GetMaterial(0) != Material)
	{
		SetMaterial(0, Material);
	}
}

void UMcParticleComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!World)
	{
		return;
	}
	if (const APlayerController* Player = GetWorld()->GetFirstPlayerController())
	{
		if (Player->PlayerCameraManager)
		{
			CameraBlock = McCoords::UnrealToBlock(Player->PlayerCameraManager->GetCameraLocation());
		}
	}
	// Twenty ticks a second, at most a second's worth at once.
	TickTime += FMath::Min(DeltaTime, 1.0f) * 20.0;
	while (TickTime >= 1.0)
	{
		TickTime -= 1.0;
		Ambient();
		Step();
	}
	Draw(static_cast<float>(TickTime));
}
