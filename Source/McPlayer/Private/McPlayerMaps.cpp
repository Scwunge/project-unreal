// The player's side of maps (see McMapData.h and McMapStore.h): an empty map in hand makes a filled one centred where the
// player stands, and a filled map held fills in a row of its picture every few ticks, until the whole map has been seen
// (about 500 ticks, run mapexp2 and 3: a 128 row map took that long to fill while held).

#include "McPlayerPawn.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McBundle.h"
#include "McEntityManager.h"
#include "McMapStore.h"
#include "McPlayerData.h"
#include "McSurvival.h"

namespace
{
	const FName EmptyMapItem(TEXT("minecraft:map"));
	const FName FilledMapItem(TEXT("minecraft:filled_map"));
	const FName WaterBlock(TEXT("minecraft:water"));

	// What the sky sees in one block column: the topmost block that has a map colour, and the water over the floor.
	FMcMapData::FColumn SampleColumn(const FMcBlockWorld& World, int32 X, int32 Z)
	{
		FMcMapData::FColumn Column;
		if (!World.HasChunk(FIntPoint(McCoords::BlockToChunk(X), McCoords::BlockToChunk(Z))))
		{
			return Column;
		}
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		const int32 Min = World.GetDimension().MinY;
		for (int32 Y = World.GetDimension().MaxY(); Y >= Min; --Y)
		{
			const FMcStateId State = World.GetBlock(FIntVector(X, Y, Z));
			if (McMapColors::ColorOf(State) == 0)
			{
				continue;
			}
			Column.Top = State;
			Column.Height = Y;
			if (Registry.GetBlockOf(State).Name == WaterBlock)
			{
				for (int32 Below = Y; Below >= Min && Registry.GetBlockOf(World.GetBlock(FIntVector(X, Below, Z))).Name == WaterBlock; --Below)
				{
					++Column.WaterDepth;
				}
			}
			return Column;
		}
		return Column;
	}
}

void AMcPlayerPawn::ServerUseMap()
{
	if (!Data || !BlockWorld || Data->GetSelected().Item != EmptyMapItem)
	{
		return;
	}
	const int32 Id = McMapStore::Create(0, FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Z), BlockWorld->GetDimension().Name);
	FMcItemStack Filled(FilledMapItem, 1);
	McMapStore::SetId(Filled, Id);
	FMcItemStack& Held = Data->Inventory->GetMutable(Data->SelectedSlot);
	if (Data->bCreative)
	{
		McSurvival::GiveItem(*Data->Inventory, Filled);
		return;
	}
	Held.Count -= 1;
	if (Held.Count <= 0)
	{
		Held = Filled;
		return;
	}
	const FMcItemStack Left = McSurvival::GiveItem(*Data->Inventory, Filled);
	FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr;
	if (!Left.IsEmpty() && Entities)
	{
		Entities->DropItem(Body.Position, Left);
	}
}

void AMcPlayerPawn::ServerTickMaps()
{
	if (!Data || !BlockWorld || bServerDead || Data->GetSelected().Item != FilledMapItem)
	{
		MapTicks = 0;
		return;
	}
	FMcMapData* Map = McMapStore::Find(McMapStore::GetId(Data->GetSelected()));
	if (!Map || Map->bLocked || Map->Dimension != BlockWorld->GetDimension().Name)
	{
		return;
	}
	// Only the area the player stands in is seen.
	const int32 Half = 64 * Map->BlocksPerPixel();
	if (FMath::Abs(Body.Position.X - Map->CenterX) > Half || FMath::Abs(Body.Position.Z - Map->CenterZ) > Half)
	{
		return;
	}
	// Measured (ref runs mapfill1 and mapfill2, scales 0 to 4): a held map is complete at once but for its column 0, which
	// follows 11 to 22 ticks later; the same fill reaches column 0 as every 16th column is gone over each tick (the first
	// tick all of them but column 0). Ours: the first tick every column but 0, then the columns with x mod 16 equal to
	// the tick mod 16 (so column 0 at tick 16, and the others are refreshed every 16 ticks).
	const int32 Step = ++MapTicks;
	const FMcBlockWorld& World = *BlockWorld;
	const auto Sample = [&World](int32 X, int32 Z) { return SampleColumn(World, X, Z); };
	if (Step == 1)
	{
		for (int32 PixelX = 1; PixelX < FMcMapData::Size; ++PixelX)
		{
			Map->UpdateColumn(PixelX, Sample);
		}
		return;
	}
	for (int32 PixelX = Step & 15; PixelX < FMcMapData::Size; PixelX += 16)
	{
		Map->UpdateColumn(PixelX, Sample);
	}
}

void AMcPlayerPawn::GiveShotMap()
{
	if (!Data || !BlockWorld)
	{
		return;
	}
	const int32 Id = McMapStore::Create(0, FMath::FloorToInt32(Body.Position.X), FMath::FloorToInt32(Body.Position.Z), BlockWorld->GetDimension().Name);
	FMcItemStack Filled(FilledMapItem, 1);
	McMapStore::SetId(Filled, Id);
	Data->Inventory->GetMutable(Data->SelectedSlot) = Filled;
}

void AMcPlayerPawn::ServerUseBundle()
{
	FMcEntityManager* Manager = GetEntities ? GetEntities() : nullptr;
	if (!Manager || !Data || bServerDead || !McBundle::IsBundle(Data->GetSelected().Item))
	{
		return;
	}
	FMcItemStack Bundle = Data->Inventory->Get(Data->SelectedSlot);
	const FMcItemStack Thrown = McBundle::RemoveNewest(Bundle);
	if (Thrown.IsEmpty())
	{
		return;
	}
	Data->Inventory->Set(Data->SelectedSlot, Bundle);
	// Thrown as the Q key throws: from just under the eyes, along the look.
	const double McYaw = FMath::DegreesToRadians(static_cast<double>(McLookYaw));
	const double McPitch = FMath::DegreesToRadians(static_cast<double>(McLookPitch));
	const FVector Look(-FMath::Sin(McYaw) * FMath::Cos(McPitch), -FMath::Sin(McPitch), FMath::Cos(McYaw) * FMath::Cos(McPitch));
	FMcItemEntity& Item = Manager->DropItem(Body.Position + FVector(0.0, GetEyeHeight() - 0.3, 0.0), Thrown, 40);
	Item.Body.Velocity = Look * 0.3 + FVector(0.0, 0.1, 0.0);
	Manager->PlaySound(FName("item.bundle.drop_contents"), Body.Position + FVector(0.0, GetEyeHeight(), 0.0), 0.8f, 0.8f + FMath::FRand() * 0.5f);
}
