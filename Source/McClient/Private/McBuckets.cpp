#include "McBuckets.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McEntityHost.h"
#include "McPlayerData.h"
#include "McPlayerPawn.h"
#include "McRaycast.h"
#include "McWorldActor.h"

namespace
{
	void Sound(AMcWorldActor& World, const TCHAR* Name, const FIntVector& Pos, float Pitch = 1.0f)
	{
		World.MulticastSound(FName(Name), FVector_NetQuantize10(FVector(Pos) + FVector(0.5)), 1.0f, Pitch);
	}
}

FName McBuckets::Fill(AMcWorldActor& World, const FIntVector& Pos)
{
	const FMcBlockWorld* Blocks = World.GetBlockWorld();
	const FMcStateId State = Blocks ? Blocks->GetBlock(Pos) : 0;
	if (!Blocks || !McRaycast::IsFluidSource(State))
	{
		return NAME_None;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const bool bLava = Registry.GetBlockOf(State).Name == TEXT("minecraft:lava");
	World.SetBlock(Pos, Registry.FindDefaultState(TEXT("minecraft:air")).Get(0));
	Sound(World, bLava ? TEXT("item.bucket.fill_lava") : TEXT("item.bucket.fill"), Pos);
	return bLava ? FName(TEXT("minecraft:lava_bucket")) : FName(TEXT("minecraft:water_bucket"));
}

bool McBuckets::Pour(AMcWorldActor& World, FName Bucket, const FIntVector& Pos)
{
	const FMcBlockWorld* Blocks = World.GetBlockWorld();
	const bool bLava = Bucket == TEXT("minecraft:lava_bucket");
	if (!Blocks || (!bLava && Bucket != TEXT("minecraft:water_bucket")) || !McRaycast::IsReplaceable(Blocks->GetBlock(Pos)))
	{
		return false;
	}
	if (!bLava && Blocks->GetDimension().bWaterEvaporates)
	{
		Sound(World, TEXT("block.fire.extinguish"), Pos, 2.6f);
		return true;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	World.SetBlock(Pos, Registry.ParseState(bLava ? TEXT("minecraft:lava[level=0]") : TEXT("minecraft:water[level=0]")).Get(0));
	Sound(World, bLava ? TEXT("item.bucket.empty_lava") : TEXT("item.bucket.empty"), Pos);
	return true;
}

bool McBuckets::Use(AMcWorldActor& World, AMcPlayerPawn& User, const FIntVector& Pos, const FIntVector& Beside)
{
	UMcPlayerDataComponent* Data = User.Data;
	const FMcBlockWorld* Blocks = World.GetBlockWorld();
	if (!Data || !Blocks)
	{
		return false;
	}
	FMcItemStack& Held = Data->Inventory->GetMutable(Data->SelectedSlot);
	if (Held.Item == TEXT("minecraft:bucket"))
	{
		const FName Filled = Fill(World, Pos);
		if (Filled.IsNone())
		{
			return false;
		}
		if (Data->bCreative)
		{
			return true;
		}
		if (Held.Count <= 1)
		{
			Held = FMcItemStack(Filled);
			return true;
		}
		--Held.Count;
		// The full bucket goes where it fits, or falls at the player's feet.
		const FMcItemStack Rest = Data->Inventory->AddItem(FMcItemStack(Filled));
		if (!Rest.IsEmpty())
		{
			if (FMcEntityHost* Host = World.GetEntityHost())
			{
				Host->GetManager().DropItem(User.GetFeetPosition() + FVector(0.0, 1.0, 0.0), Rest, 40);
			}
		}
		return true;
	}
	if (Held.Item != TEXT("minecraft:water_bucket") && Held.Item != TEXT("minecraft:lava_bucket"))
	{
		return false;
	}
	const FIntVector Into = McRaycast::IsReplaceable(Blocks->GetBlock(Pos)) ? Pos : Beside;
	if (!Pour(World, Held.Item, Into))
	{
		return false;
	}
	if (!Data->bCreative)
	{
		Held = FMcItemStack(TEXT("minecraft:bucket"));
	}
	return true;
}
