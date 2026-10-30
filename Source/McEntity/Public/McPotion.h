// A potion in flight: a witch's throw. It flies under gravity (0.05 a tick) and drag, and breaks on the first block or
// player it reaches (or after ten seconds). The players within 4 blocks of where it breaks get its effect, by how
// close they are (the instant harming potion's 6 damage, or the effect for a share of its time); the other mobs near
// it take the harming one, the raiders and the witch's own side excepted.

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

class MCENTITY_API FMcThrownPotion final : public FMcEntity
{
public:
	FMcThrownPotion();
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual void Tick(FMcEntityManager& Manager) override;
	virtual bool IsPersistent() const override { return false; }

	// The effect it carries (a name like "poison"; "harming" for the instant 6 damage), its level less one and its ticks.
	FName Effect = TEXT("harming");
	int32 Amplifier = 0;
	int32 Duration = 0;

	static constexpr double Gravity = 0.05;
	static constexpr double Drag = 0.99;
	static constexpr double SplashRadius = 4.0;
	static constexpr int32 LifeTicks = 200;

private:
	void Break(FMcEntityManager& Manager);
};
