// The in-game HUD, drawn like Minecraft's: the crosshair, hotbar and the bars from the jar's own GUI sprites, at
// Minecraft's GUI scale, and the inventory and container screens over them. Item icons are the block models
// drawn at the inventory angle, or the item's flat sprite for things like torches.
//
// The bars and the hotbar's items come from a UMcPlayerDataComponent on the player's pawn when there is one.
// The screens show the player's window (E opens their own, the server opens the others); they take the mouse and keys
// while open, and clicks go to the window's menu and on to the server. What a click throws out is handed to the data
// component, which makes item entities of it on the server.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "McCore.h"
#include "McGuiLayout.h"
#include "McHUD.generated.h"

class FMcCacheBuilder;
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
	// The screen follows the player's window (UMcPlayerDataComponent::GetWindow), so the server opens a crafting table,
	// chest or furnace by opening the window.
	void CloseScreen();
	bool IsScreenOpen() const { return Screen.IsValid(); }

	// ---- Menus (McHUDMenus.cpp) ----
	// The title screen and those behind it (the world list, New World, joining a server), and the pause menu in a
	// world, drawn with the game's button and text field sprites.
	enum class EMenu : uint8 { None, Setup, Title, Worlds, ConfirmDelete, NewWorld, Join, Message, Pause, Chat, Options };
	EMenu GetMenu() const { return ActiveMenu; }
	// Shows a menu (None closes the pause menu). The world list and the join screen read what they show afresh.
	void OpenMenu(EMenu NewMenu);
	// Why the last attempt to join a server failed, shown on the title screen the failure lands on. Kept across maps.
	static void SetConnectError(const FString& Error);

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
	// A banner is drawn as its flag: the base colour, then each pattern in its dye's colour. Kept by what it looks like.
	const FIcon& GetBannerIcon(const FMcItemStack& Banner);
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
	void DrawGalacticText(const FString& Text, float X, float Y, float Width, uint32 Rgb);
	void DrawGuiText(const FString& Text, float X, float Y, uint32 Rgb, bool bShadow, float Alpha = 1.0f);

	void DrawBars(int32 Width, int32 Height, UMcPlayerDataComponent* Data);
	// The icons of the player's effects along the top right: good ones on the first row, harmful ones on the second.
	void DrawEffects(int32 Width, int32 Height, UMcPlayerDataComponent* Data);
	void DrawStack(const FMcItemStack& Stack, float X, float Y);
	void DrawScreen(int32 Width, int32 Height);
	// The creative screen's search, item list and scroll bar.
	void DrawCreative(FIntPoint Origin);
	void DrawEnchanting(FIntPoint Origin, FIntPoint Local);
	void DrawStonecutter(FIntPoint Origin, FIntPoint Local);
	void DrawLoom(FIntPoint Origin, FIntPoint Local);
	void DrawAnvil(FIntPoint Origin);
	void PollInput(int32 Width, int32 Height, UMcPlayerDataComponent* Data);
	// The creative screen's own input: typing into the search, scrolling, and clicks on the item list. True if it
	// took this frame's click.
	bool PollCreative(APlayerController& Player, FIntPoint Local, UMcPlayerDataComponent& Data);
	void SyncScreen(UMcPlayerDataComponent* Data);
	void ShowScreen(TSharedRef<FScreenState> State);
	void HideScreen();
	void FlushDrops();
	// The death screen: shown while the player is dead, with a button that asks the server to bring them back.
	void ShowDeathScreen(int32 Width, int32 Height, class AMcPlayerPawn& Player);

	// The menus: the frame's input, then the screen. True if a menu took the frame (the world's HUD is not drawn).
	bool DrawMenus(int32 Width, int32 Height, bool bTitle);
	void DrawMenuBackground(int32 Width, int32 Height, bool bDim);
	void DrawTitle(int32 Width, int32 Height);
	// First run, before the cache exists (McCacheSetup.h): none of the game's pictures or font are there to draw with, so
	// this screen uses the engine's font and plain rectangles. Sizes are in screen pixels.
	void DrawSetup(int32 Width, int32 Height);
	void FinishSetup();
	void DrawWorlds(int32 Width, int32 Height);
	void DrawConfirmDelete(int32 Width, int32 Height);
	void DrawNewWorld(int32 Width, int32 Height);
	void DrawJoin(int32 Width, int32 Height);
	void DrawMessage(int32 Width, int32 Height);
	void DrawPause(int32 Width, int32 Height);
	// The settings: Unreal Mode and what comes after it. Opened from the title screen or the pause menu.
	void DrawOptions(int32 Width, int32 Height);
	// The menus drawn over a world being played (the rest replace the screen).
	bool IsWorldMenu() const { return ActiveMenu == EMenu::Pause || ActiveMenu == EMenu::Chat || (ActiveMenu == EMenu::Options && OptionsBack == EMenu::Pause); }
	void OpenOptions(EMenu Back);
	// Chat: the lines the player has been told, above the hotbar (the last ten, fading after ten seconds; with the
	// box open, up to twenty stay), and the box being typed into (T, or / for a command; Enter sends, Up and Down go
	// back through what was sent).
	void DrawChat(int32 Width, int32 Height, bool bOpen);
	void DrawChatInput(int32 Width, int32 Height);
	TArray<FString> WrapText(const FString& Text, float MaxWidth) const;
	// The game's 9-sliced widgets: a button (true when clicked this frame) and a text field (Id says which is
	// focused; a click focuses it, Tab moves on).
	bool MenuButton(const FString& Label, float X, float Y, float W, float H, bool bEnabled = true);
	void MenuTextField(FString& Text, int32 Id, float X, float Y, float W, float H, int32 MaxLength);
	void DrawNineSlice(UTexture2D* Texture, float X, float Y, float W, float H, float TexW, float TexH, float Border);
	void DrawCentredText(const FString& Text, float CentreX, float Y, uint32 Rgb);
	void SetMenuInput(bool bMenu);
	void CreateWorld();
	void PlayWorld(const FString& Folder, const FString& ExtraOptions);
	void JoinServer();
	void LoadClientOptions();
	void SaveClientOptions() const;
	// Vanilla to enhanced or back, remembered in options.json.
	void ToggleGraphics();
	void ToggleHd();

	struct FWorldRow
	{
		FString Folder;
		FString Name;
		FString GameMode;
		FDateTime LastPlayed;
	};

	EMenu ActiveMenu = EMenu::None;
	// Where the options screen's Done goes back to.
	EMenu OptionsBack = EMenu::Title;
	bool bMenuInput = false;
	// This frame's input for the menus.
	bool bMenuClick = false;
	FString MenuTyped;
	bool bMenuBackspace = false;
	bool bMenuEnter = false;
	bool bMenuTab = false;
	int32 FocusedField = 0;
	int32 FieldCount = 0;
	TArray<FWorldRow> WorldRows;
	int32 SelectedWorld = INDEX_NONE;
	int32 WorldScroll = 0;
	double LastRowClick = 0.0;
	FString NewWorldName;
	FString NewWorldSeed;
	bool bNewWorldCreative = false;
	bool bNewWorldFlat = false;
	FString PlayerName;
	FString ServerAddress;
	FString MessageTitle;
	// First-run setup: whether the cache was found complete (looked at once), the folder typed in, what went wrong, and
	// the build once it is started.
	// The world list's switch: play the world as a listen server on the game's port, so friends can join by address.
	bool bHostWorld = false;
	bool bCacheChecked = false;
	bool bCacheReady = false;
	FString SetupFolder;
	FString SetupError;
	TSharedPtr<FMcCacheBuilder> CacheBuild;
	FString MessageText;
	FString ChatDraft;
	TArray<FString> SentChat;
	int32 SentChatIndex = 0;
	static FString ConnectError;

	int32 Scale = 1;
	int32 ShakeCounter = 0;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> Sprites;
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UTexture2D>> LayerTextures;
	TMap<FMcStateId, FIcon> Icons;
	TMap<FName, FIcon> ItemIcons;
	TMap<FString, FIcon> BannerIcons;

	TArray<uint8> GlyphWidths;
	TArray<uint8> GalacticWidths;

	TSharedPtr<FScreenState> Screen;
	// Where the mouse was, in GUI pixels, and the slot it is over.
	FVector2D GuiMouse = FVector2D::ZeroVector;
	int32 HoverSlot = INDEX_NONE;
	bool bDeathShown = false;
};
