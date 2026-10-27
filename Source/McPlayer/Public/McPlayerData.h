// What the HUD and the inventory screens read about a player: the numbers on the bars and the item containers
// (minecraft.wiki, "Health", "Hunger", "Experience", "Armor", "Inventory"). Nothing here enforces the rules yet:
// damage, starvation, drowning and experience are the server's job later, and this is the shape it will fill in.
// A component on the player's pawn holds it, so the HUD finds it with FindComponentByClass.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "McContainer.h"
#include "McPlayerData.generated.h"

struct MCPLAYER_API FMcPlayerStats
{
	// Health in half hearts (20 is ten full hearts), and the golden absorption hearts on top of it.
	float Health = 20.0f;
	float MaxHealth = 20.0f;
	float Absorption = 0.0f;
	// Food in half drumsticks, 0 to 20. Saturation is hidden; it is spent before food is.
	int32 Food = 20;
	float Saturation = 5.0f;
	// Breath in ticks: 300 is full, 0 drowns.
	int32 Air = 300;
	int32 MaxAir = 300;
	// Level, and how far to the next as a fraction.
	int32 XpLevel = 0;
	float XpProgress = 0.0f;
	// Armour points, 0 to 20 (a bar of ten icons, two points each).
	int32 Armor = 0;
	// Ticks left of the hurt flash on the hearts; the server sets 10 when damage lands.
	int32 HurtTime = 0;
	bool bHardcore = false;
};

UCLASS(ClassGroup = (Mc))
class MCPLAYER_API UMcPlayerDataComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMcPlayerDataComponent();

	static constexpr int32 InventorySize = 36;
	// Armour slot order: head, chest, legs, feet.
	static constexpr int32 NumArmorSlots = 4;

	FMcPlayerStats Stats;
	// 0 to 8 the hotbar, 9 to 35 the main part (as the game orders them).
	TSharedPtr<FMcContainer> Inventory;
	TSharedPtr<FMcContainer> Armor;
	TSharedPtr<FMcContainer> Offhand;
	int32 SelectedSlot = 0;

	const FMcItemStack& GetSelected() const { return Inventory->Get(SelectedSlot); }
	// The selected slot after turning the wheel by Steps (positive is down), wrapping round the hotbar.
	static int32 Scroll(int32 Selected, int32 Steps);
};
