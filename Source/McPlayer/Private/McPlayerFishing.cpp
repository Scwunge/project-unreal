// The player's side of fishing (see McFishing.h): the rod casts and reels in, the bobber follows the angler, the
// catch is thrown back to them.

#include "McPlayerPawn.h"
#include "McBiomeRegistry.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McEntityManager.h"
#include "McFishing.h"
#include "McLoot.h"
#include "McPlayerData.h"
#include "McRandom.h"
#include "McXpOrb.h"
#include "GameFramework/PlayerState.h"

namespace
{
	const FName RodItem(TEXT("minecraft:fishing_rod"));
}

FMcFishingBobber* AMcPlayerPawn::FindBobber(FMcEntityManager& Entities) const
{
	FMcEntity* Entity = BobberId != 0 ? Entities.Find(BobberId) : nullptr;
	return Entity && !Entity->bRemoved && Entity->GetType() == FMcFishingBobber::StaticType() ? static_cast<FMcFishingBobber*>(Entity) : nullptr;
}

void AMcPlayerPawn::ServerUseItem_Implementation()
{
	FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr;
	// A firework rocket used while gliding pulls (McPlayerElytra.cpp).
	if (!Data || bServerDead || !Entities || ServerBoost() || Data->GetSelected().Item != RodItem)
	{
		return;
	}
	if (FMcFishingBobber* Bobber = FindBobber(*Entities))
	{
		ServerReel(*Entities, *Bobber);
		return;
	}
	const FMcItemStack& Rod = Data->GetSelected();
	TUniquePtr<FMcFishingBobber> Bobber = MakeUnique<FMcFishingBobber>();
	const FVector Eye = Body.Position + FVector(0.0, GetEyeHeight(), 0.0);
	Bobber->Cast(Eye, McLookYaw, McLookPitch, DropRandom);
	Bobber->AnglerEye = Eye;
	Bobber->Lure = Rod.GetEnchantmentLevel(TEXT("minecraft:lure"));
	Bobber->Luck = Rod.GetEnchantmentLevel(TEXT("minecraft:luck_of_the_sea"));
	Bobber->AnglerId = GetPlayerState() ? GetPlayerState()->GetPlayerId() : 0;
	BobberId = Entities->Add(MoveTemp(Bobber)).Id;
	bRodCast = true;
	Entities->PlaySound(FName("entity.fishing_bobber.throw"), Eye, 0.5f, 0.4f / (DropRandom.FRand() * 0.4f + 0.8f));
}

void AMcPlayerPawn::ServerReel(FMcEntityManager& Entities, FMcFishingBobber& Bobber)
{
	const FVector Eye = Body.Position + FVector(0.0, GetEyeHeight(), 0.0);
	const FVector At = Bobber.Body.Position;
	Bobber.AnglerEye = Eye;
	const FMcFishingBobber::FReel Reel = Bobber.Reel(Entities);
	BobberId = 0;
	bRodCast = false;
	if (Reel.bCatch)
	{
		McLoot::FFishingContext Context;
		Context.bOpenWater = Bobber.bOpenWater;
		Context.Luck = Bobber.Luck;
		const FIntVector Cell(FMath::FloorToInt32(At.X), FMath::FloorToInt32(At.Y), FMath::FloorToInt32(At.Z));
		if (const FMcChunk* Chunk = BlockWorld ? BlockWorld->FindChunk(FIntPoint(McCoords::BlockToChunk(Cell.X), McCoords::BlockToChunk(Cell.Z))) : nullptr)
		{
			Context.Biome = FMcBiomeRegistry::Get().GetName(Chunk->GetBiome(Cell.X & 15, Cell.Y, Cell.Z & 15));
		}
		FMcRandom Random(static_cast<uint64>(DropRandom.GetUnsignedInt()) << 32 | DropRandom.GetUnsignedInt());
		TArray<FMcItemStack> Catch;
		McLoot::RollFishing(Context, Random, Catch);
		for (const FMcItemStack& Stack : Catch)
		{
			// Flung out of the water in an arc that lands at the angler's feet.
			FMcItemEntity& Item = Entities.DropItem(At, Stack);
			Item.Body.Velocity = FMcFishingBobber::ThrowToward(At, Eye);
			FMcTriggerEvent Hooked;
			Hooked.Trigger = TEXT("minecraft:fishing_rod_hooked");
			Hooked.Item = Stack.Item;
			FireTrigger(Hooked);
		}
		FMcXpOrb::Spawn(Entities, Body.Position + FVector(0.5, 0.5, 0.5), DropRandom.RandRange(1, 6), DropRandom);
	}
	Entities.PlaySound(FName("entity.fishing_bobber.retrieve"), Eye, 1.0f, 0.8f + DropRandom.FRand() * 0.4f);
	if (Reel.Damage > 0 && !Data->bCreative)
	{
		FMcItemStack& Rod = Data->Inventory->GetMutable(Data->SelectedSlot);
		if (Rod.Item == RodItem && Rod.HurtAndBreak(Reel.Damage))
		{
			Rod.Clear();
			Entities.PlaySound(FName("entity.item.break"), Eye, 0.8f, 0.8f + DropRandom.FRand() * 0.4f);
		}
	}
}

void AMcPlayerPawn::ServerTickFishing(FMcEntityManager& Entities)
{
	if (BobberId == 0)
	{
		return;
	}
	FMcFishingBobber* Bobber = FindBobber(Entities);
	const FVector Eye = Body.Position + FVector(0.0, GetEyeHeight(), 0.0);
	// Gone on its own (it lay too long, its chunk went), or let go: the rod put away, or left more than 32 blocks off.
	if (!Bobber || bServerDead || Data->GetSelected().Item != RodItem || FVector::DistSquared(Eye, Bobber->Body.Position) > FMath::Square(McFishing::MaxDistance))
	{
		if (Bobber)
		{
			Bobber->bRemoved = true;
		}
		BobberId = 0;
		bRodCast = false;
		return;
	}
	Bobber->AnglerEye = Eye;
}
