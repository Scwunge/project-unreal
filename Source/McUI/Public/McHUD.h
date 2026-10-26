// The in-game HUD, drawn like Minecraft's: the crosshair and the hotbar from the jar's own GUI sprites, at
// Minecraft's GUI scale. Item icons are the block models drawn at the inventory angle, or the item's flat
// sprite for things like torches.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "McCore.h"
#include "McHUD.generated.h"

class UTexture2D;

UCLASS()
class MCUI_API AMcHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	// Minecraft's automatic GUI scale: the largest whole number that keeps the screen at least 320 x 240
	// GUI pixels.
	static int32 GetGuiScale(int32 Width, int32 Height);

private:
	struct FIconTriangle
	{
		FVector2D Positions[3];  // in GUI pixels, within the 16 x 16 slot
		FVector2D UVs[3];
		FLinearColor Color;
		UTexture2D* Texture = nullptr;
	};
	struct FIcon
	{
		TArray<FIconTriangle> Triangles;
	};

	UTexture2D* LoadSprite(const FString& RelativeAssetPath);
	UTexture2D* GetLayerTexture(FName TextureId);
	const FIcon& GetIcon(FMcStateId State);
	void BuildBlockIcon(FMcStateId State, FIcon& Icon);
	void DrawSprite(UTexture2D* Texture, float X, float Y, float Width, float Height);
	void DrawIcon(const FIcon& Icon, float X, float Y);

	int32 Scale = 1;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> Sprites;
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UTexture2D>> LayerTextures;
	TMap<FMcStateId, FIcon> Icons;
};
