// Llamas (minecraft.wiki: "Llama"; numbers from the reference client, vehicle_log28.txt).
//
// Caravan: a llama that is not tied joins the nearest llama within 9 blocks that is on a lead (or is the last of a caravan) and follows it
// at 0.21 a tick (a player's terminal at speed 0.0973), standing still once within 3 blocks; the log's two followers trailed 2.8 and 3.0
// behind. Spit: a llama that is hurt spits at the player nearest within 20 blocks every 40 ticks for 5 seconds: a spit starts 1.68 above
// the feet, aimed at 0.667 of the target's height plus 0.2 x the horizontal distance of lift, speed 1.5 with a random spread of 0.075 a
// component, flies with drag 0.99 and gravity 0.06, and does 1 damage (Normal) to the first player it reaches.
// Not done: the spit is not drawn, no sound, a spit hitting a mob, wolves, the llama strength and inventory (chest).

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

class FMcMob;

class MCENTITY_API FMcLlamaSpit final : public FMcEntity
{
public:
	FMcLlamaSpit();
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool IsPersistent() const override { return false; }

	float Damage = 1.0f;
	static constexpr int32 LifeTicks = 100;
	static constexpr double Gravity = 0.06;
	static constexpr double Drag = 0.99;
};

namespace McLlama
{
	static constexpr double JoinRange = 9.0;
	static constexpr double StopDistance = 3.0;
	static constexpr double FollowInputSpeed = 0.0973;
	static constexpr double SpitRange = 20.0;
	static constexpr int32 SpitInterval = 40;
	static constexpr int32 SpitMemory = 100;
	static constexpr double SpitSpeed = 1.5;
	static constexpr double SpitSpread = 0.075;

	// A llama tick: caravan and spit. True if it moved the llama itself this tick (the mob own routine is skipped).
	MCENTITY_API bool Tick(FMcEntityManager& Manager, FMcMob& Llama);
}
