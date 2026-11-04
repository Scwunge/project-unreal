// Things thrown with the use button: a snowball, an egg and an ender pearl. They fly 1.5 blocks a tick under gravity
// 0.03 and drag 0.99 (0.8 in water) and break on the first block or mob they reach (minecraft.wiki, "Snowball", "Egg",
// "Ender Pearl"). A snowball hurts nothing (a blaze takes 3) but knocks back; an egg has a one in eight chance of a
// chick, and one in thirty-two of four; a pearl takes its thrower to where it lands and costs them 5 health.

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

class MCENTITY_API FMcThrownEntity final : public FMcEntity
{
public:
	enum class EKind : uint8 { Snowball, Egg, EnderPearl };

	explicit FMcThrownEntity(EKind InKind);
	static FName GetTypeName(EKind Kind);
	// The kind for an item, if it is thrown.
	static bool FindKind(FName Item, EKind& Out);

	virtual FName GetType() const override { return GetTypeName(Kind); }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool IsPersistent() const override { return false; }

	// Where a pearl's thrower goes (block coordinates, the feet) when it lands.
	TFunction<void(const FVector&)> OnPearlLanded;

	static constexpr double Speed = 1.5;
	static constexpr double Gravity = 0.03;
	static constexpr double Drag = 0.99;
	static constexpr int32 LifeTicks = 1200;

	// What an egg that breaks makes: 0 chicks, 1 or 4. Rolled from Random in [0, 1).
	static int32 GetChickCount(float Roll, float SecondRoll);

private:
	void Break(FMcEntityManager& Manager, const FVector& At, FMcEntity* Hit);

	EKind Kind;
};
