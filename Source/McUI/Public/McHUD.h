// The in-game HUD, drawn like Minecraft's: the crosshair, hotbar and the bars from the jar's own GUI sprites, at
// Minecraft's GUI scale, and the inventory and container screens over them. Item icons are the block models
// drawn at the inventory angle, or the item's flat sprite for things like torches.
//
// The bars and the hotbar's items come from a UMcPlayerDataComponent on the player's pawn when there is one.
// Screens are opened with the Open functions (E opens the player's own); they take the mouse and keys while open,
// polled from the player controller, and hand what a click throws out to DropHandler.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "McCore.h"
#include "McGuiLayout.h"
#include "McHUD.generated.h"

class FMcContainer;
class FMcFurnace;
class FMcMenuInput;
class UMcPlayerDataComponent;
class UTexture2D;
struct FMcItemStack;

UCLASS()
class MCUI_API AMcHUD : public AHUD
{
	GENERATED_BODY()

public:
	AMcHUD();
	virtual ~AMcHUD() override;
	virtual void DrawHUD() override;

	// Minecraft's automatic GUI scale: the largest whole number that keeps the screen at least 320 x 240
	// GUI pixels.
	static int32 GetGuiScale(int32 Width, int32 Height);

	// 0 picks the largest scale that fits; 1, 2, 3... ask for that one, if it fits.
	UPROPERTY(EditAnywhere, Category = "Mc")
	int32 GuiScaleSetting = 0;

	// ---- Screens ----
	// The player's own: 2 x 2 crafting, armour, offhand.
	void OpenInventory();
	// A crafting table's 3 x 3 grid.
	void OpenCraftingTable();
	// A chest, barrel or any plain container of 9 to 54 slots.
	void OpenChest(const TSharedPtr<FMcContainer>& Container, const FString& Title);
	void OpenFurnace(const TSharedPtr<FMcFurnace>& Furnace, const FString& Title);
	void CloseScreen();
	bool IsScreenOpen() const { return Screen.IsValid(); }

	// Told of every stack the screen throws out (Q, a click outside the window, closing with the grid full), to
	// make into item entities at the player.
	TFunction<void(const FMcItemStack&)> DropHandler;

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
	struct FScreenState;

	UTexture2D* LoadSprite(const FString& RelativeAssetPath);
	UTexture2D* GetLayerTexture(FName TextureId);
	const FIcon& GetIcon(FMcStateId State);
	const FIcon& GetItemIcon(FName Item);
	bool BuildFlatIcon(const FString& Name, FIcon& Icon);
	void BuildFlatQuad(const FString& TextureId, FIcon& Icon);
	void BuildBlockIcon(FMcStateId State, FIcon& Icon);
	void DrawSprite(UTexture2D* Texture, float X, float Y, float Width, float Height);
	// A part of a picture: U, V, UW and VH are in pixels of the picture itself.
	void DrawRegion(UTexture2D* Texture, float X, float Y, float Width, float Height, float U, float V, float UW, float VH, const FLinearColor& Tint = FLinearColor::White);
	void DrawIcon(const FIcon& Icon, float X, float Y);
	void DrawFill(float X, float Y, float Width, float Height, const FLinearColor& Color);

	// Text in the game's own bitmap font (textures/font/ascii.png).
	void LoadFont();
	float TextWidth(const FString& Text) const;
	void DrawGuiText(const FString& Text, float X, float Y, uint32 Rgb, bool bShadow);

	void DrawBars(int32 Width, int32 Height, UMcPlayerDataComponent* Data);
	void DrawStack(const FMcItemStack& Stack, float X, float Y);
	void DrawScreen(int32 Width, int32 Height);
	void PollInput(int32 Width, int32 Height, UMcPlayerDataComponent* Data);
	void OpenMenu(TSharedRef<FScreenState> State);
	void FlushDrops();

	int32 Scale = 1;
	int32 ShakeCounter = 0;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> Sprites;
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UTexture2D>> LayerTextures;
	TMap<FMcStateId, FIcon> Icons;
	TMap<FName, FIcon> ItemIcons;

	TArray<uint8> GlyphWidths;

	TSharedPtr<FScreenState> Screen;
	// Where the mouse was, in GUI pixels, and the slot it is over.
	FVector2D GuiMouse = FVector2D::ZeroVector;
	int32 HoverSlot = INDEX_NONE;
};
