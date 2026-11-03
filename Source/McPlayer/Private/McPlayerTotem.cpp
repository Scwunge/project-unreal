// The player's side of the Totem of Undying (see McTotem.h): the server spends the totem when a blow would kill, and
// every machine shows it going off: the sparks round the body, and on the holder's own screen the totem's flight.

#include "McPlayerPawn.h"
#include "McChunk.h"
#include "McEntityManager.h"
#include "McPlayerData.h"
#include "McTotem.h"

#if MC_WITH_RENDER
#include "McParticleComponent.h"
#endif

bool AMcPlayerPawn::ServerTryTotem()
{
	if (!Data || Data->Stats.Health > 0.0f || !McTotem::CanSave(Data->Stats.LastHurtKind))
	{
		return false;
	}
	if (!McTotem::TryUse(Data->Stats, Data->Inventory->GetMutable(Data->SelectedSlot), Data->Offhand->GetMutable(0)))
	{
		return false;
	}
	if (const FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr)
	{
		Entities->PlaySound(FName(TEXT("item.totem.use")), Body.Position, 1.0f, 1.0f);
	}
	MulticastTotem();
	return true;
}

void AMcPlayerPawn::MulticastTotem_Implementation()
{
	TotemAge = 0.0f;
}

void AMcPlayerPawn::TickTotem(float DeltaSeconds)
{
	if (TotemAge < 0.0f)
	{
		return;
	}
	const float Before = TotemAge;
	TotemAge += DeltaSeconds * 20.0f;
#if MC_WITH_RENDER
	// A burst of sparks each tick for the first ParticleTicks.
	if (UMcParticleComponent* Particles = UMcParticleComponent::FindFor(BlockWorld))
	{
		// Only the owner's and the server's copies keep the body's size; the others are as tall as a standing player.
		const FVector Feet = McCoords::UnrealToBlock(GetActorLocation());
		const float Height = IsLocallyControlled() || HasAuthority() ? Body.Height : 1.8f;
		for (int32 Tick = FMath::CeilToInt32(Before); Tick < FMath::Min(TotemAge, static_cast<float>(McTotem::ParticleTicks)); ++Tick)
		{
			Particles->TotemBurst(Feet, Height);
		}
	}
#endif
	if (TotemAge >= FMath::Max(McTotem::AnimationTicks, McTotem::ParticleTicks))
	{
		TotemAge = -1.0f;
	}
}
