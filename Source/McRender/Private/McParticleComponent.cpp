#include "McParticleComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McBlockWorld.h"
#include "McCache.h"
#include "McChunk.h"
#include "McRender.h"
#include "McRenderMode.h"
#include "McNoteBlock.h"
#include "McParticleDefs.h"
#include "McBlockTextures.h"
#include "McRenderTextures.h"
#include "McCache.h"
#include "McSound.h"
#include "McRenderTextures.h"
#include "McSkyComponent.h"
#include "Engine/Texture2DArray.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
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
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Cutout(TEXT("/Game/Mc/Materials/M_McBlockCutout.M_McBlockCutout"));
	SpriteBase = Cutout.Object;
}

UMaterialInstanceDynamic* UMcParticleComponent::EnsureGlitter()
{
	if (GlitterMaterial && GlitterMaterial->Parent == McRenderMode::Pick(SpriteBase.Get()))
	{
		return GlitterMaterial;
	}
	if (bGlitterFailed || !SpriteBase)
	{
		return nullptr;
	}
	if (!Glitter)
	{
		// The frames, each 8 pixels square, put side by side.
		constexpr int32 Frame = 8;
		TArray<FColor> Strip;
		Strip.Init(FColor(0, 0, 0, 0), Frame * GlitterFrames * Frame);
		for (int32 Index = 0; Index < GlitterFrames; ++Index)
		{
			TArray<FColor> Pixels;
			int32 Width = 0, Height = 0;
			const FString Path = McCache::Asset(FString::Printf(TEXT("textures/particle/glitter_%d.png"), Index));
			if (!McRenderTextures::LoadPngPixels(Path, Pixels, Width, Height) || Width != Frame || Height != Frame)
			{
				UE_LOG(LogMcRender, Error, TEXT("Can't use %s (an 8x8 png); totem sparks stay hidden"), *Path);
				bGlitterFailed = true;
				return nullptr;
			}
			for (int32 Y = 0; Y < Frame; ++Y)
			{
				for (int32 X = 0; X < Frame; ++X)
				{
					Strip[Y * Frame * GlitterFrames + Index * Frame + X] = Pixels[Y * Frame + X];
				}
			}
		}
		Glitter = McRenderTextures::MakeArray(this, Strip, Frame * GlitterFrames, Frame);
		if (!Glitter)
		{
			bGlitterFailed = true;
			return nullptr;
		}
	}
	GlitterMaterial = UMaterialInstanceDynamic::Create(McRenderMode::Pick(SpriteBase.Get()), this);
	GlitterMaterial->SetTextureParameterValue(TEXT("Blocks"), Glitter);
	return GlitterMaterial;
}

UMaterialInstanceDynamic* UMcParticleComponent::EnsureCosySmoke()
{
	if (CosyMaterial && CosyMaterial->Parent == McRenderMode::Pick(SpriteBase.Get()))
	{
		return CosyMaterial;
	}
	if (bCosyFailed || !SpriteBase)
	{
		return nullptr;
	}
	if (!CosySheet)
	{
		constexpr int32 Frame = 16;
		TArray<FColor> Strip;
		Strip.Init(FColor(0, 0, 0, 0), Frame * CosyFrames * Frame);
		for (int32 Index = 0; Index < CosyFrames; ++Index)
		{
			TArray<FColor> Pixels;
			int32 Width = 0, Height = 0;
			const FString Path = McCache::Asset(FString::Printf(TEXT("textures/particle/big_smoke_%d.png"), Index));
			if (!McRenderTextures::LoadPngPixels(Path, Pixels, Width, Height) || Width != Frame || Height != Frame)
			{
				UE_LOG(LogMcRender, Error, TEXT("Can't use %s (a 16x16 png); campfire smoke stays hidden"), *Path);
				bCosyFailed = true;
				return nullptr;
			}
			for (int32 Y = 0; Y < Frame; ++Y)
			{
				for (int32 X = 0; X < Frame; ++X)
				{
					Strip[Y * Frame * CosyFrames + Index * Frame + X] = Pixels[Y * Frame + X];
				}
			}
		}
		CosySheet = McRenderTextures::MakeArray(this, Strip, Frame * CosyFrames, Frame);
		if (!CosySheet)
		{
			bCosyFailed = true;
			return nullptr;
		}
	}
	CosyMaterial = UMaterialInstanceDynamic::Create(McRenderMode::Pick(SpriteBase.Get()), this);
	CosyMaterial->SetTextureParameterValue(TEXT("Blocks"), CosySheet);
	return CosyMaterial;
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
	FMcSoundBus::Get().OnNote.Remove(NoteHandle);
	NoteHandle.Reset();
	FMcSoundBus::Get().OnParticle.Remove(ParticleHandle);
	ParticleHandle.Reset();
	GetLive().RemoveAll([this](const TWeakObjectPtr<UMcParticleComponent>& Live) { return !Live.IsValid() || Live.Get() == this; });
	Super::OnUnregister();
}

void UMcParticleComponent::SetWorld(const FMcBlockWorld* InWorld, UMcWorldRenderComponent* InRenderer)
{
	GetLive().AddUnique(this);
	if (!NoteHandle.IsValid())
	{
		NoteHandle = FMcSoundBus::Get().OnNote.AddUObject(this, &UMcParticleComponent::OnNote);
	}
	if (!ParticleHandle.IsValid())
	{
		ParticleHandle = FMcSoundBus::Get().OnParticle.AddUObject(this, &UMcParticleComponent::Emit);
	}
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
		else if (Name == TEXT("minecraft:fire") || Name == TEXT("minecraft:soul_fire")) Emitter = EEmitter::Fire;
		else if (Name == TEXT("minecraft:nether_portal")) Emitter = EEmitter::Portal;
		else if (Name == TEXT("minecraft:water") && Registry.GetPropertyValue(State, TEXT("level")) != TEXT("0")) Emitter = EEmitter::FlowingWater;
		else if (Name == TEXT("minecraft:bubble_column")) Emitter = Registry.GetPropertyValue(State, TEXT("drag")) == TEXT("true") ? EEmitter::BubbleDown : EEmitter::BubbleUp;
		else if (Name == TEXT("minecraft:pointed_dripstone") && Registry.GetPropertyValue(State, TEXT("vertical_direction")) == TEXT("down")
			&& Registry.GetPropertyValue(State, TEXT("thickness")) == TEXT("tip")) Emitter = EEmitter::Drip;
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

void UMcParticleComponent::Emit(FName Name, const FVector& Pos, const FVector& Delta, float Speed, int32 Count)
{
	const McParticleKinds::FKind& Kind = McParticleKinds::Find(Name);
	if (McParticleDefs::GetTextures(Kind.Name).IsEmpty())
	{
		return;
	}
	// /particle: with a count of 0 the deltas are the motion itself (times the speed); otherwise each of Count particles starts
	// a random distance from Pos (spread by the deltas) and moves as its kind does.
	const int32 Number = Count <= 0 ? 1 : FMath::Min(Count, 500);
	for (int32 Index = 0; Index < Number; ++Index)
	{
		FVector At = Pos;
		FVector Velocity;
		if (Count <= 0)
		{
			Velocity = Delta * Speed;
		}
		else
		{
			At += FVector(Delta.X * Random.GetFraction(), Delta.Y * Random.GetFraction(), Delta.Z * Random.GetFraction()) * 2.0 - Delta;
			const FVector Way = Random.GetUnitVector();
			Velocity = Way * Kind.Spread * (Kind.Speed > 0.0f ? FMath::Max(Speed, 1.0e-3f) : 1.0f) + FVector(0.0, Kind.Rise, 0.0);
		}
		FParticle& Particle = Add(EStyle::Sprite, At, Velocity);
		Particle.Row = &Kind;
		Particle.Life = static_cast<float>(Random.RandRange(Kind.LifeMin, FMath::Max(Kind.LifeMin, Kind.LifeMax)));
		Particle.Size = Kind.Size;
		Particle.Gravity = Kind.Gravity;
		Particle.Friction = Kind.Friction;
		Particle.bPhysics = Kind.bPhysics;
		Particle.Colour = Kind.Tint;
		Particle.Light = Kind.bGlow ? FVector2f(1.0f, 1.0f) : FVector2f(1.0f, 0.0f);
		const int32 Frames = McParticleDefs::GetTextures(Kind.Name).Num();
		Particle.Frame = static_cast<uint8>(Kind.Frames == McParticleKinds::EFrames::Random ? Random.RandRange(0, Frames - 1) : 0);
	}
}

void UMcParticleComponent::OnNote(const FVector& Pos, int32 Note)
{
	// Measured against the reference client (jukebox run 7: 70 shots a tick apart, chat cleared, glyph 5 by 8 texels
	// 30 by 48 px at 3 blocks, the jukebox face 181 px at 2.5): shown for 7 ticks, rising 30, 20, 13, 9, 6, 4 px a tick
	// (0.2, 0.133, ... blocks: 0.2 a tick, slowing by a third each tick, 0.54 in all), at the same size throughout.
	FParticle& Mote = Add(EStyle::Note, Pos, FVector(0.0, 0.2, 0.0));
	Mote.Life = 7.0f;
	Mote.Friction = 0.66f;
	Mote.Size = 0.1375f;
	Mote.Colour = McNoteBlock::GetNoteColour(Note);
	Mote.Light = FVector2f(1.0f, 1.0f);
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

void UMcParticleComponent::ItemEaten(const FVector& Eye, const FVector& Forward, const TArray<FColor>& Colours)
{
	if (Colours.IsEmpty())
	{
		return;
	}
	// The game throws a handful from just below the mouth, up and a little ahead.
	const int32 Count = McRenderMode::IsEnhanced() ? 8 : 5;
	const FVector Side = FVector::CrossProduct(FVector(0.0, 1.0, 0.0), Forward).GetSafeNormal();
	const FIntVector EyeBlock(FMath::FloorToInt(Eye.X), FMath::FloorToInt(Eye.Y), FMath::FloorToInt(Eye.Z));
	const float Sky = SkyLightAt(EyeBlock);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector At = Eye + Forward * 0.6 + Side * Random.FRandRange(-0.15f, 0.15f) + FVector(0.0, Random.FRandRange(-0.25f, -0.12f), 0.0);
		FVector Velocity = Forward * Random.FRandRange(0.01f, 0.05f) + Side * Random.FRandRange(-0.03f, 0.03f);
		Velocity.Y += Random.FRandRange(0.08f, 0.14f);
		FParticle& Bit = Add(EStyle::Crumb, At, Velocity);
		Bit.Gravity = 0.04f;
		Bit.bPhysics = true;
		Bit.Life = Random.FRandRange(10.0f, 20.0f);
		Bit.Size = 0.012f * (Random.FRand() * 0.5f + 0.75f);
		Bit.Colour = Colours[Random.RandRange(0, Colours.Num() - 1)];
		Bit.Light = FVector2f(Sky, 0.0f);
	}
}

void UMcParticleComponent::FishingSplash(const FVector& Pos)
{
	const float Light = SkyLightAt(FIntVector(FMath::FloorToInt32(Pos.X), FMath::FloorToInt32(Pos.Y) + 1, FMath::FloorToInt32(Pos.Z)));
	for (int32 Index = 0; Index < 12; ++Index)
	{
		const bool bBubble = Index % 2 == 0;
		const FVector At = Pos + FVector(Random.FRandRange(-0.25f, 0.25f), 0.0, Random.FRandRange(-0.25f, 0.25f));
		FParticle& Drop = Add(bBubble ? EStyle::Bubble : EStyle::Splash, At,
			bBubble ? FVector(Random.FRandRange(-0.02f, 0.02f), Random.FRandRange(0.01f, 0.04f), Random.FRandRange(-0.02f, 0.02f))
				: FVector(Random.FRandRange(-0.08f, 0.08f), Random.FRandRange(0.1f, 0.2f), Random.FRandRange(-0.08f, 0.08f)));
		Drop.Gravity = bBubble ? 0.0f : 0.04f;
		Drop.Life = bBubble ? Random.RandRange(4, 10) : Random.RandRange(8, 16);
		Drop.Size = bBubble ? 0.03f : 0.025f;
		Drop.Colour = bBubble ? FColor(200, 225, 255) : FColor(120, 160, 230);
		Drop.Light = FVector2f(Light, 0.0f);
	}
}

void UMcParticleComponent::FishingWake(const FVector& Pos)
{
	FParticle& Foam = Add(EStyle::Wake, Pos, FVector(0.0, 0.01, 0.0));
	Foam.Life = Random.RandRange(6, 12);
	Foam.Size = 0.03f;
	Foam.Colour = FColor(235, 245, 255);
	Foam.Light = FVector2f(SkyLightAt(FIntVector(FMath::FloorToInt32(Pos.X), FMath::FloorToInt32(Pos.Y) + 1, FMath::FloorToInt32(Pos.Z))), 0.0f);
	if (Random.FRand() < 0.15f)
	{
		FParticle& Drop = Add(EStyle::Splash, Pos, FVector(Random.FRandRange(-0.03f, 0.03f), Random.FRandRange(0.05f, 0.1f), Random.FRandRange(-0.03f, 0.03f)));
		Drop.Gravity = 0.04f;
		Drop.Life = Random.RandRange(6, 10);
		Drop.Size = 0.02f;
		Drop.Colour = FColor(120, 160, 230);
		Drop.Light = Foam.Light;
	}
}

void UMcParticleComponent::TotemBurst(const FVector& Feet, float Height)
{
	const float Light = SkyLightAt(FIntVector(FMath::FloorToInt32(Feet.X), FMath::FloorToInt32(Feet.Y + Height), FMath::FloorToInt32(Feet.Z)));
	// Fitted to vanilla shots every 2 ticks (mc-reference scripts/totem_ticks.txt): a handful in view at a time, lying
	// one to four blocks out once they slow, still drifting high a second later, the last gone about 40 ticks after the
	// blow; about 0.2 blocks across at 2 blocks off; lime-yellow and greens (their pixels sampled, the sprite's shading
	// taken off).
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FVector Way = FVector(Random.FRandRange(-1.0f, 1.0f), Random.FRandRange(-0.5f, 1.0f), Random.FRandRange(-1.0f, 1.0f)).GetSafeNormal();
		const FVector At = Feet + FVector(Random.FRandRange(-0.3f, 0.3f), Random.FRand() * Height, Random.FRandRange(-0.3f, 0.3f));
		FParticle& Spark = Add(EStyle::Totem, At, Way * Random.FRandRange(0.1f, 0.4f));
		Spark.Friction = 0.9f;
		Spark.Gravity = 0.002f;
		Spark.Life = Random.RandRange(10, 25);
		Spark.Size = Random.FRandRange(0.08f, 0.12f);
		Spark.Colour = Random.FRand() < 0.4f
			? Mix(FColor(190, 205, 50), FColor(225, 240, 40), Random.FRand())
			: Mix(FColor(40, 140, 0), FColor(90, 190, 20), Random.FRand());
		Spark.Light = FVector2f(Light, 0.0f);
	}
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
	auto Smoke = [&](const FVector& At, bool bBig, bool bSignal = false)
	{
		FParticle& Puff = Add(bBig ? EStyle::BigSmoke : EStyle::Smoke, At,
			bBig ? FVector(Random.FRandRange(-0.005f, 0.005f), 0.07f, Random.FRandRange(-0.005f, 0.005f)) : FVector(Random.FRandRange(-0.01f, 0.01f), 0.01f, Random.FRandRange(-0.01f, 0.01f)));
		Puff.Gravity = bBig ? -0.0003f : -0.002f;
		Puff.Friction = bBig ? 0.98f : 0.96f;
		Puff.Life = bBig ? (bSignal ? Random.FRandRange(280.0f, 330.0f) : Random.FRandRange(80.0f, 160.0f)) : FMath::Floor(8.0f / (Random.FRand() * 0.8f + 0.2f));
		Puff.Size = bBig ? Random.FRandRange(0.07f, 0.14f) : 0.05f * (Random.FRand() * 0.5f + 0.5f) * 1.5f;
		const uint8 Grey = static_cast<uint8>(Random.RandRange(bBig ? 100 : 70, bBig ? 135 : 110) + (McRenderMode::IsEnhanced() ? (bBig ? 15 : 50) : 0));
		Puff.Colour = FColor(Grey, Grey, Grey);
		Puff.Light = FVector2f(SkyLightAt(Pos), 0.6f);
	};
	// What the block says now and again, as the game's: a sound at the block, a little off at random in pitch.
	auto Say = [&](const TCHAR* Event, float Volume, float Pitch)
	{
		FMcSoundBus::Get().OnSound.Broadcast(FMcSoundEvent{ FName(Event), FVector(Pos) + FVector(0.5), Volume, Pitch });
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
		if (Emitters[State] == EEmitter::Campfire && Random.FRand() < 0.11f)
		{
			Say(TEXT("block.campfire.crackle"), 0.5f + Random.FRand(), Random.FRand() * 0.7f + 0.6f);
		}
		// Its smoke is made every tick for the campfires found so far (TickCampfires).
		Campfires.Add(Pos);
		if (Random.RandRange(0, 4) == 0)
		{
			Flame(Centre + FVector(Random.FRandRange(-0.3f, 0.3f), -0.1, Random.FRandRange(-0.3f, 0.3f)), Emitters[State] == EEmitter::SoulCampfire);
		}
		break;
	case EEmitter::Furnace:
		if (Random.FRand() < 0.1f)
		{
			Say(TEXT("block.furnace.fire_crackle"), 1.0f, 1.0f);
		}
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
				Say(TEXT("block.lava.pop"), Random.FRand() * 0.2f + 0.2f, Random.FRand() * 0.15f + 0.9f);
			}
			if (Random.RandRange(0, 199) == 0)
			{
				Say(TEXT("block.lava.ambient"), Random.FRand() * 0.2f + 0.2f, Random.FRand() * 0.15f + 0.9f);
			}
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
	case EEmitter::Fire:
		if (Random.RandRange(0, 23) == 0)
		{
			Say(TEXT("block.fire.ambient"), Random.FRand() + 1.0f, Random.FRand() * 0.7f + 0.3f);
		}
		break;
	case EEmitter::Portal:
		if (Random.RandRange(0, 99) == 0)
		{
			Say(TEXT("block.portal.ambient"), 0.5f, Random.FRand() * 0.4f + 0.8f);
		}
		break;
	case EEmitter::FlowingWater:
		if (Random.RandRange(0, 63) == 0)
		{
			Say(TEXT("block.water.ambient"), Random.FRand() * 0.25f + 0.75f, Random.FRand() + 0.5f);
		}
		break;
	case EEmitter::BubbleUp:
	case EEmitter::BubbleDown:
		// An ambient sound at the block's corner now and again (about one a 200th of the visits; volume and pitch
		// from the bubble2 vanilla run: 0.12 to 0.23, 0.90 to 0.99).
		{
			// Bubbles: up columns send one up from the middle of the block's floor and one from anywhere in it; down
			// columns send one from near the top (the game's bubble_column_up and current_down, sizes and lives as its own).
			const bool bDown = Emitters[State] == EEmitter::BubbleDown;
			const int32 Count = bDown ? 1 : 2;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FVector At = bDown ? FVector(Pos) + FVector(0.5, 0.8, 0.0)
					: (Index == 0 ? FVector(Pos) + FVector(0.5, 0.0, 0.5) : FVector(Pos) + FVector(Random.FRand(), Random.FRand(), Random.FRand()));
				FParticle& Bubble = Add(EStyle::Bubble, At, FVector(Random.FRandRange(-0.02f, 0.02f), Random.FRand() * 0.02f, Random.FRandRange(-0.02f, 0.02f)));
				Bubble.Gravity = bDown ? 0.024f : -0.005f;
				Bubble.Friction = bDown ? 0.85f : 0.85f;
				Bubble.Life = FMath::Floor((bDown ? 60.0f : 40.0f) / (Random.FRand() * 0.8f + 0.2f));
				Bubble.Size = 0.05f * (Random.FRand() * 0.5f + 0.5f) * 2.0f * (Random.FRand() * 0.6f + 0.2f);
				Bubble.Light = FVector2f(1.0f, 1.0f);
			}
		}
		if (Random.RandRange(0, 199) == 0)
		{
			FMcSoundBus::Get().OnSound.Broadcast(FMcSoundEvent{ FName(Emitters[State] == EEmitter::BubbleDown ? TEXT("block.bubble_column.whirlpool_ambient") : TEXT("block.bubble_column.upwards_ambient")),
				FVector(Pos), Random.FRand() * 0.15f + 0.1f, Random.FRand() * 0.15f + 0.9f });
		}
		break;
	case EEmitter::Drip:
		// A stalactite's tip drips what lies over its column: water or lava above the dripstone it hangs from (about one visit in 50: a guess,
		// not yet fitted to a run).
		if (Random.RandRange(0, 49) == 0)
		{
			const FMcBlockRegistry& Reg = FMcBlockRegistry::Get();
			FIntVector Up = Pos + FIntVector(0, 1, 0);
			FName Above = Reg.GetBlockOf(static_cast<FMcStateId>(World->GetBlock(Up))).Name;
			while ((Above == TEXT("minecraft:pointed_dripstone") || Above == TEXT("minecraft:dripstone_block")) && Up.Y < Pos.Y + 12)
			{
				Up.Y += 1;
				Above = Reg.GetBlockOf(static_cast<FMcStateId>(World->GetBlock(Up))).Name;
			}
			if (Above == TEXT("minecraft:water") || Above == TEXT("minecraft:lava"))
			{
				Emit(Above == TEXT("minecraft:water") ? FName("minecraft:dripping_water") : FName("minecraft:dripping_lava"),
					FVector(Pos) + FVector(0.5, 0.1, 0.5), FVector::ZeroVector, 0.0f, 1);
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

void UMcParticleComponent::TickCampfires()
{
	for (auto It = Campfires.CreateIterator(); It; ++It)
	{
		const FIntVector Pos = *It;
		const FMcStateId State = World->GetBlock(Pos);
		if (!Emitters.IsValidIndex(State) || (Emitters[State] != EEmitter::Campfire && Emitters[State] != EEmitter::SoulCampfire))
		{
			It.RemoveCurrent();
			continue;
		}
		// Rising at 0.0717 a tick without slowing (the signal column of vanilla run camp_a, built 205 ticks before the shot, stood
		// 14.7 blocks tall); a plain campfire's column tops out near 7 blocks (camp_b), so its puffs live about 90 ticks.
		// The rest (the odds, the spread, the size, how many puffs) is fitted by eye to those shots.
		if (FVector::DistSquared(FVector(Pos), CameraBlock) > 64.0 * 64.0 || Random.FRand() >= 0.11f)
		{
			continue;
		}
		const bool bSignal = FMcBlockRegistry::Get().GetPropertyValue(State, TEXT("signal_fire")) == TEXT("true");
		const int32 Puffs = Random.RandRange(2, 3);
		for (int32 Puff = 0; Puff < Puffs; ++Puff)
		{
			const FVector At = FVector(Pos) + FVector(0.5 + Random.FRandRange(-0.33f, 0.33f), 0.5 + Random.FRand() * 0.5, 0.5 + Random.FRandRange(-0.33f, 0.33f));
			FParticle& Smoke = Add(EStyle::BigSmoke, At, FVector(0.0, 0.0717, 0.0));
			Smoke.Gravity = 0.0f;
			Smoke.Friction = 1.0f;
			Smoke.Life = bSignal ? Random.FRandRange(280.0f, 330.0f) : Random.FRandRange(80.0f, 100.0f);
			Smoke.Size = Random.FRandRange(0.25f, 0.45f);
			Smoke.Phase = static_cast<float>(Random.RandRange(0, CosyFrames - 1));
			Smoke.Light = FVector2f(SkyLightAt(Pos), 0.6f);
		}
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
		if (Particle.Style == EStyle::BigSmoke)
		{
			// A campfire's smoke wanders a little sideways.
			Particle.Velocity.X += Random.FRand() / 5000.0f * (Random.RandRange(0, 1) ? 1.0f : -1.0f);
			Particle.Velocity.Z += Random.FRand() / 5000.0f * (Random.RandRange(0, 1) ? 1.0f : -1.0f);
		}
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

	// The totem's sparks, on their own section with their own picture.
	struct FSprites
	{
		TArray<FVector> Positions;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0, UV1, UV2;
		TArray<FColor> Colours;
		TArray<int32> Triangles;
	} Sprites;
	struct
	{
		TArray<FVector> Positions;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0, UV1, UV2;
		TArray<FColor> Colours;
		TArray<int32> Triangles;
	} Cosy;
	UMaterialInstanceDynamic* CosySprites = EnsureCosySmoke();
	if (CosySprites)
	{
		CosySprites->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
		CosySprites->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
	}
	UMaterialInstanceDynamic* GlitterSprites = EnsureGlitter();
	if (GlitterSprites)
	{
		GlitterSprites->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
		GlitterSprites->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
	}

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
		case EStyle::Sprite:
			bTextured = true;
			if (Particle.Row)
			{
				Size *= 1.0f + Particle.Row->Grow * T;
				if (Particle.Row->bShrinkAtEnd)
				{
					Size *= FMath::Clamp((1.0f - T) * 4.0f, 0.0f, 1.0f);
				}
			}
			break;
		case EStyle::Debris:
			bTextured = true;
			// Unreal Mode's bits shrink away at the end instead of vanishing.
			if (bUnreal)
			{
				Size *= FMath::Clamp((1.0f - T) * 4.0f, 0.0f, 1.0f);
			}
			break;
		case EStyle::Crumb:
			Size *= FMath::Clamp((1.0f - T) * 4.0f, 0.0f, 1.0f);
			break;
		case EStyle::Dust:
			bTextured = true;
			Size *= 0.6f + T * 0.8f;
			Size *= FMath::Clamp((1.0f - T) * 2.5f, 0.0f, 1.0f);
			break;
		case EStyle::Bubble:
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
		case EStyle::Splash:
		case EStyle::Wake:
			Size *= FMath::Clamp((1.0f - T) * 3.0f, 0.0f, 1.0f);
			break;
		case EStyle::Totem:
			Size *= FMath::Clamp((1.0f - T) * 2.0f, 0.0f, 1.0f);
			break;
		case EStyle::Smoke:
			// Puffs thin out and take on the colour of the air as they rise, rather than staying flat grey squares.
			Size *= (1.0f + T * 1.5f) * FMath::Clamp((1.0f - T) * 2.5f, 0.0f, 1.0f);
			Colour = Mix(Colour, FColor(190, 205, 230), T * T);
			break;
		case EStyle::BigSmoke:
			// Same size for most of its life, then it thins away.
			Size *= FMath::Clamp((1.0f - T) * 5.0f, 0.0f, 1.0f);
			Colour = FColor::White;
			break;
		}
		if (Size <= 0.001f)
		{
			continue;
		}
		if (Particle.Style == EStyle::Note || Particle.Style == EStyle::Bubble)
		{
			// The game's note.png or bubble.png, a pixel at a time: a note's grey shades take its colour, a bubble keeps its own.
			const bool bBubble = Particle.Style == EStyle::Bubble;
			static TArray<FIntPoint> Pixels[2];
			static TArray<FColor> Texes[2];
			static bool bRead[2] = { false, false };
			if (!bRead[bBubble])
			{
				bRead[bBubble] = true;
				TArray<FColor> Texels;
				int32 Width = 0, Height = 0;
				if (McRenderTextures::LoadPngPixels(McCache::Asset(bBubble ? TEXT("textures/particle/bubble.png") : TEXT("textures/particle/note.png")), Texels, Width, Height) && Width == 8 && Height == 8)
				{
					for (int32 Index = 0; Index < Texels.Num(); ++Index)
					{
						if (Texels[Index].A > 0)
						{
							Pixels[bBubble].Add(FIntPoint(Index % 8, Index / 8));
							Texes[bBubble].Add(Texels[Index]);
						}
					}
				}
			}
			const TArray<FIntPoint>& PixelList = Pixels[bBubble];
			const FVector Middle = McCoords::BlockToUnreal(FMath::Lerp(Particle.Prev, Particle.Pos, static_cast<double>(Partial)));
			const double PixelHalf = Size * McCoords::UnitsPerBlock / 8.0;
			for (int32 Index = 0; Index < PixelList.Num(); ++Index)
			{
				const FVector At = Middle + Right * ((PixelList[Index].X - 3.5) * 2.0 * PixelHalf) - Up * ((PixelList[Index].Y - 3.5) * 2.0 * PixelHalf);
				const FColor Texel = Texes[bBubble][Index];
				const FColor Shade = bBubble ? Texel : FColor(static_cast<uint8>(Colour.R * Texel.R / 255), static_cast<uint8>(Colour.G * Texel.R / 255), static_cast<uint8>(Colour.B * Texel.R / 255));
				const int32 Base = Positions.Num();
				const FVector Pixel[4] = { -Right + Up, -Right - Up, Right - Up, Right + Up };
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					Positions.Add(At + Pixel[Corner] * PixelHalf);
					Normals.Add(ViewFacing);
					UV0.Add(FVector2D(0.5, 0.5));
					UV1.Add(FVector2D(Particle.Layer, McSurface::Particle));
					UV2.Add(FVector2D(Particle.Light));
					Colours.Add(Shade);
				}
				Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
			}
			continue;
		}
		const FVector Centre = McCoords::BlockToUnreal(FMath::Lerp(Particle.Prev, Particle.Pos, static_cast<double>(Partial)));
		const double Half = Size * McCoords::UnitsPerBlock;
		const FVector Corners[4] = { -Right + Up, -Right - Up, Right - Up, Right + Up };
		const FVector2D CornerUV[4] = { FVector2D(0, 0), FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0) };
		if (Particle.Style == EStyle::BigSmoke)
		{
			if (!CosySprites)
			{
				continue;
			}
			const int32 Frame = FMath::Clamp(static_cast<int32>(Particle.Phase), 0, CosyFrames - 1);
			const int32 SpriteFirst = Cosy.Positions.Num();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Cosy.Positions.Add(Centre + Corners[Corner] * Half);
				Cosy.Normals.Add(ViewFacing);
				Cosy.UV0.Add(FVector2D((Frame + CornerUV[Corner].X) / CosyFrames, CornerUV[Corner].Y));
				Cosy.UV1.Add(FVector2D::ZeroVector);
				Cosy.UV2.Add(FVector2D(Particle.Light));
				Cosy.Colours.Add(Colour);
			}
			Cosy.Triangles.Append({ SpriteFirst, SpriteFirst + 1, SpriteFirst + 2, SpriteFirst, SpriteFirst + 2, SpriteFirst + 3 });
			continue;
		}
		if (Particle.Style == EStyle::Totem)
		{
			if (!GlitterSprites)
			{
				continue;
			}
			// glitter_7 when new, down to glitter_0 as it dies.
			const int32 Frame = GlitterFrames - 1 - FMath::Min(static_cast<int32>(T * GlitterFrames), GlitterFrames - 1);
			const int32 SpriteFirst = Sprites.Positions.Num();
			for (int32 Corner = 0; Corner < 4; ++Corner)
			{
				Sprites.Positions.Add(Centre + Corners[Corner] * Half);
				Sprites.Normals.Add(ViewFacing);
				Sprites.UV0.Add(FVector2D((Frame + CornerUV[Corner].X) / GlitterFrames, CornerUV[Corner].Y));
				Sprites.UV1.Add(FVector2D::ZeroVector);
				Sprites.UV2.Add(FVector2D(Particle.Light));
				Sprites.Colours.Add(Colour);
			}
			Sprites.Triangles.Append({ SpriteFirst, SpriteFirst + 1, SpriteFirst + 2, SpriteFirst, SpriteFirst + 2, SpriteFirst + 3 });
			continue;
		}
		uint16 DrawLayer = Particle.Layer;
		FVector2D DrawUVMin = Particle.UVMin;
		float DrawUVSize = Particle.UVSize;
		if (Particle.Style == EStyle::Sprite && Particle.Row)
		{
			// The picture: the kind's frames in order over its life, or the one picked at the start.
			static TMap<const McParticleKinds::FKind*, TArray<uint16>> LayersOf;
			TArray<uint16>& Layers = LayersOf.FindOrAdd(Particle.Row);
			if (Layers.IsEmpty())
			{
				for (const FName& Texture : McParticleDefs::GetTextures(Particle.Row->Name))
				{
					Layers.Add(static_cast<uint16>(FMcBlockTextures::Get().GetLayer(Texture)));
				}
			}
			if (Layers.IsEmpty())
			{
				continue;
			}
			const int32 Frame = Particle.Row->Frames == McParticleKinds::EFrames::ByAge ? FMath::Min(static_cast<int32>(T * Layers.Num()), Layers.Num() - 1)
				: FMath::Min(static_cast<int32>(Particle.Frame), Layers.Num() - 1);
			DrawLayer = Layers[Frame];
			DrawUVMin = FVector2D::ZeroVector;
			DrawUVSize = 1.0f;
		}
		const int32 First = Positions.Num();
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			Positions.Add(Centre + Corners[Corner] * Half);
			Normals.Add(ViewFacing);
			UV0.Add(bTextured ? DrawUVMin + CornerUV[Corner] * DrawUVSize : FVector2D(0.5, 0.5));
			UV1.Add(FVector2D(DrawLayer, bTextured ? McSurface::TexturedParticle : Kind));
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
	CreateMeshSection(1, Sprites.Positions, Sprites.Triangles, Sprites.Normals, Sprites.UV0, Sprites.UV1, Sprites.UV2, TArray<FVector2D>(), Sprites.Colours, TArray<FProcMeshTangent>(), false);
	if (GlitterSprites && GetMaterial(1) != GlitterSprites)
	{
		SetMaterial(1, GlitterSprites);
	}
	CreateMeshSection(2, Cosy.Positions, Cosy.Triangles, Cosy.Normals, Cosy.UV0, Cosy.UV1, Cosy.UV2, TArray<FVector2D>(), Cosy.Colours, TArray<FProcMeshTangent>(), false);
	if (CosySprites && GetMaterial(2) != CosySprites)
	{
		SetMaterial(2, CosySprites);
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
		TickCampfires();
		Step();
	}
	Draw(static_cast<float>(TickTime));
}
