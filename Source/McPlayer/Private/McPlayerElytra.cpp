// The player's side of elytra (see McElytra.h): the glide itself runs on the owner's machine like the rest of their
// movement; the server wears the wings, spends the rockets and hurts the flier who hits a wall.

#include "McPlayerPawn.h"
#include "McBlockWorld.h"
#include "McElytra.h"
#include "McEntityManager.h"
#include "McPhysics.h"
#include "McPlayerData.h"
#include "McSurvival.h"
#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
	const FName RocketItem(TEXT("minecraft:firework_rocket"));
	// Gliding, the body is a 0.6 block cube with the eyes 0.4 up.
	constexpr float GlidingHeight = 0.6f;
	// The wings' chest slot in the worn armour (head, chest, legs, feet).
	constexpr int32 ChestSlot = 1;

	bool WornWingsWork(const UMcPlayerDataComponent* Data)
	{
		return Data && Data->Armor && McElytra::CanGlide(Data->Armor->Get(ChestSlot));
	}
}

bool AMcPlayerPawn::TickGlide(const FMcMoveInput& Input, bool bJumpPressed)
{
	const McSurvival::FEnvironment Here = McSurvival::SampleEnvironment(*BlockWorld, Body.Position, GetEyeHeight());
	const bool bInFluid = Here.bFeetInWater || Here.bInLava;
	if (!bGliding)
	{
		// A jump in the air, falling or rising, with working wings on and nothing to hold on to.
		if (!bJumpPressed || Body.bOnGround || bFlying || bInFluid || Here.bClimbing || !WornWingsWork(Data))
		{
			return false;
		}
		bGliding = true;
		bSprinting = false;
		bSneaking = false;
		Body.Height = GlidingHeight;
	}
	else if (Body.bOnGround || bFlying || bInFluid || Here.bClimbing || !WornWingsWork(Data))
	{
		bGliding = false;
		BoostTicks = 0;
		// Standing up again needs the room; until there is, the body stays small.
		FMcBody Standing = Body;
		Standing.Height = 1.8f;
		if (!McPhysics::Collides(*BlockWorld, Standing.GetBox()))
		{
			Body.Height = Standing.Height;
		}
		return false;
	}

	Body.Velocity = McElytra::Glide(Body.Velocity, Input.Yaw, McLookPitch);
	if (BoostTicks > 0)
	{
		--BoostTicks;
		Body.Velocity = McElytra::Boost(Body.Velocity, Input.Yaw, McLookPitch);
	}
	const double Before = FVector2D(Body.Velocity.X, Body.Velocity.Z).Size();
	McPhysics::Move(*BlockWorld, Body, Body.Velocity, false);
	if (Body.bHorizontalCollision)
	{
		const float Damage = McElytra::WallDamage(Before, FVector2D(Body.Velocity.X, Body.Velocity.Z).Size());
		if (Damage > 0.0f)
		{
			ServerFlewIntoWall(Damage);
		}
	}
	return true;
}

void AMcPlayerPawn::StartGlide()
{
	if (!WornWingsWork(Data))
	{
		return;
	}
	bFlying = false;
	bGliding = true;
	bSprinting = false;
	bSneaking = false;
	Body.Height = GlidingHeight;
	Body.bOnGround = false;
	Body.Velocity = McElytra::LookOf(McLookYaw, 0.0f) * 0.6;
}

void AMcPlayerPawn::ServerFlewIntoWall_Implementation(float Damage)
{
	if (!Data || bServerDead || Data->bCreative || !FMath::IsFinite(Damage))
	{
		return;
	}
	McSurvival::Hurt(Data->Stats, FMath::Clamp(Damage, 0.0f, 100.0f), McSurvival::EDamage::Fall, Data->Armor.Get(), &GameRules);
}

void AMcPlayerPawn::ClientBoost_Implementation(int32 Ticks)
{
	if (bGliding)
	{
		BoostTicks = Ticks;
	}
}

bool AMcPlayerPawn::ServerBoost()
{
	FMcEntityManager* Entities = GetEntities ? GetEntities() : nullptr;
	if (!Data || !Entities || !bServerGliding || Data->GetSelected().Item != RocketItem)
	{
		return false;
	}
	FMcItemStack& Rocket = Data->Inventory->GetMutable(Data->SelectedSlot);
	int32 Flight = 1;
	if (const TSharedPtr<FJsonValue> Fireworks = Rocket.Get(TEXT("minecraft:fireworks")))
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		double Value = 0.0;
		if (Fireworks->TryGetObject(Object) && (*Object)->TryGetNumberField(TEXT("flight_duration"), Value))
		{
			Flight = FMath::Clamp(static_cast<int32>(Value), 0, 3);
		}
	}
	const int32 Ticks = McElytra::RollBoostTicks(DropRandom, Flight);
	if (!Data->bCreative && --Rocket.Count <= 0)
	{
		Rocket.Clear();
	}
	Entities->PlaySound(FName("entity.firework_rocket.launch"), Body.Position, 3.0f, 1.0f);
	if (IsLocallyControlled())
	{
		ClientBoost_Implementation(Ticks);
	}
	else
	{
		ClientBoost(Ticks);
	}
	return true;
}

void AMcPlayerPawn::ServerTickElytra()
{
	bWearsElytra = Data && Data->Armor && Data->Armor->Get(ChestSlot).Item == FName(TEXT("minecraft:elytra"));
#if !UE_BUILD_SHIPPING
	// Development: -McWear=minecraft:zombie_head puts that in the head slot once (for a -McSelfShot of it).
	static bool bWearDone = false;
	FString Wear;
	if (!bWearDone && Data && Data->Armor && FParse::Value(FCommandLine::Get(), TEXT("-McWear="), Wear))
	{
		bWearDone = true;
		FMcItemStack Hat;
		Hat.Item = FName(*Wear);
		Hat.Count = 1;
		Data->Armor->GetMutable(0) = Hat;
	}
#endif
	// The head slot (armor slot 0) too: everyone sees a worn head.
	WornHead = Data && Data->Armor ? Data->Armor->Get(0).Item : NAME_None;
	bGlidePose = bServerGliding;
	if (!bServerGliding || !Data || !Data->Armor)
	{
		ServerGlideTicks = 0;
		return;
	}
	// A point of wear a second, never past the last.
	if (++ServerGlideTicks % McElytra::WearTicks == 0 && !Data->bCreative)
	{
		FMcItemStack& Wings = Data->Armor->GetMutable(ChestSlot);
		if (McElytra::CanGlide(Wings) && Wings.IsDamageable())
		{
			Wings.SetDamage(Wings.GetDamage() + 1);
		}
	}
}
