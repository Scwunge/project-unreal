// Particles (minecraft.wiki, "Particles"): small squares facing the camera, each moving on its own, drawn with the
// cutout block material (so in the current look; McRenderMode). What is drawn:
//   - a block broken: 4x4x4 bits of its texture fly out ("block" particles), each a quarter of the texture across;
//   - a block being dug: a bit chips off the face hit, every tick;
//   - near the player, as the game picks blocks at random round them each tick (667 within 16 and 667 within 32):
//     torches and lit campfires and furnaces smoke and burn, lava throws up drops, leaves let a leaf fall.
// Bits of block and leaves land on what is below them. Flames and lava drops are one colour and glow.
//
// Unreal Mode adds to this: more and bigger bits that last longer and shrink away, a puff of dust where a block
// breaks, sparks off lava, and the flames glow enough to bloom.
//
// Client only; positions are in block coordinates. The component must be attached to something at the origin, as
// the world's meshes are.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"
#include "ProceduralMeshComponent.h"
#include "McParticleComponent.generated.h"

class FMcBlockWorld;
class UMcWorldRenderComponent;

UCLASS(ClassGroup = (Mc))
class MCRENDER_API UMcParticleComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UMcParticleComponent(const FObjectInitializer& ObjectInitializer);

	// The world the particles are in and the drawer whose material and palette they use; both must outlive this, or
	// be unset first with nulls.
	void SetWorld(const FMcBlockWorld* InWorld, UMcWorldRenderComponent* InRenderer);

	// How a box (in blocks) moving by Motion (blocks) is stopped by the blocks in its way: the motion it can make. The
	// view sets this (McPhysics); unset, particles fall through everything.
	TFunction<FVector(const FBox& Box, const FVector& Motion)> Collide;

	// The block State at Pos was broken.
	void BlockBroken(const FIntVector& Pos, FMcStateId State);
	// The block at Pos is being dug on its Face side (a unit step out of it).
	void BlockHit(const FIntVector& Pos, const FIntVector& Face);

	// Food being eaten: a few bits of its picture fly from the mouth at Eye (blocks) looking along Forward. Colours are the opaque pixels
	// of the food's sprite; each bit takes one at random.
	void ItemEaten(const FVector& Eye, const FVector& Forward, const TArray<FColor>& Colours);

	int32 NumParticles() const { return Particles.Num(); }

	// The component drawing particles for World on this machine, if any (the player's digging finds it this way).
	static UMcParticleComponent* FindFor(const FMcBlockWorld* World);

	// Most that may be alive at once; the oldest give way.
	static constexpr int32 MaxParticles = 6000;

protected:
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	enum class EStyle : uint8
	{
		Debris,  // a bit of a block's texture; falls and lands
		Dust,    // a bigger bit, drifting up and fading (Unreal Mode)
		Flame,
		SoulFlame,
		Smoke,
		BigSmoke,  // a campfire's
		Lava,      // a burning drop that falls back
		Spark,     // a tiny glowing fleck off lava (Unreal Mode)
		Leaf,
		Crumb,     // a bit of eaten food: one colour, falls and lands
	};

	struct FParticle
	{
		FVector Pos = FVector::ZeroVector;   // blocks
		FVector Prev = FVector::ZeroVector;  // where it was last tick, to draw between ticks
		FVector Velocity = FVector::ZeroVector;  // blocks a tick
		float Age = 0.0f;   // ticks
		float Life = 20.0f;
		float Size = 0.1f;  // half the width, in blocks
		float Gravity = 0.0f;  // blocks a tick, each tick
		float Friction = 0.98f;
		FVector2D UVMin = FVector2D::ZeroVector;
		float UVSize = 0.25f;
		FVector2f Light = FVector2f(1.0f, 0.0f);  // sky and block light, 0 to 1
		FColor Colour = FColor::White;
		uint16 Layer = 0;
		EStyle Style = EStyle::Debris;
		bool bPhysics = false;
		bool bOnGround = false;
		float Phase = 0.0f;  // leaves: where in their sway
	};

	FParticle& Add(EStyle Style, const FVector& Pos, const FVector& Velocity);
	void Step();
	void Ambient();
	void AmbientAt(const FIntVector& Pos);
	void Draw(float Partial);

	// The texture layer and tint bits of State are drawn with.
	bool GetTexture(FMcStateId State, uint16& Layer, FColor& Tint) const;
	// The sky light at Pos, roughly: full under open sky, low under cover.
	float SkyLightAt(const FIntVector& Pos) const;

	// What each state does when picked near the player.
	enum class EEmitter : uint8 { None, Torch, WallTorch, SoulTorch, SoulWallTorch, Lava, Campfire, SoulCampfire, Furnace, Leaves };
	void BuildEmitters();
	TArray<EEmitter> Emitters;  // by state id
	TArray<FIntVector> Facing;  // by state id: a wall torch's or furnace's facing

	const FMcBlockWorld* World = nullptr;
	TWeakObjectPtr<UMcWorldRenderComponent> Renderer;
	TArray<FParticle> Particles;
	FRandomStream Random;
	double TickTime = 0.0;  // ticks run, and the part of one since
	FVector CameraBlock = FVector::ZeroVector;
	uint32 MaterialGeneration = 0;
};
