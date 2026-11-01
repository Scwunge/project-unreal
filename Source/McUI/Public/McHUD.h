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
#include "McLocalPlayer.h"
#include "McResourcePacks.h"
#include "McHUD.generated.h"

class FMcContainer;
class FMcFurnace;
class FMcMenuInput;
class FMcRasterFonts;
class UMcPlayerDataComponent;
class UTexture2D;
struct FMcItemStack;
struct FMcRasterGlyph;

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
	enum class EMenu : uint8 { None, Title, Worlds, ConfirmDelete, NewWorld, Join, Message, Pause, Chat, Options, ResourcePacks, ConfirmPacks, ConfirmIncompatible };
	EMenu GetMenu() const { return ActiveMenu; }
	// Shows a menu (None closes the pause menu). The world list and the join screen read what they show afresh.
	void OpenMenu(EMenu NewMenu);
	// Why the last attempt to join a server failed, shown on the title screen the failure lands on. Kept across maps.
	static void SetConnectError(const FString& Error);

	// For tests: what the font gives a code point, as the text is drawn: its advance and shadow offset in GUI pixels, and
	// the width of a whole string. The glyphs are read (and drawn into their textures) as in the game.
	float GetGlyphAdvance(int32 Char);
	float GetGlyphShadowOffset(int32 Char);
	// The box a glyph's picture covers, in pixels from the pen and the top of the line (the reference's left, right, up and down).
	bool GetGlyphInk(int32 Char, float& Left, float& Right, float& Up, float& Down);
	float GetTextWidth(const FString& Text);

	// The title screen's yellow splash: the lines of texts/splashes.txt (the top pack's copy replaces the game's), trimmed and
	// without empty ones; and the one shown on a day, Roll in 0..1 choosing among the lines. Christmas Eve, New Year's Day and
	// Halloween have a fixed splash of their own.
	static TArray<FString> LoadSplashTexts();
	static FString PickSplash(const TArray<FString>& Lines, int32 Month, int32 Day, float Roll);

	// The "gui" section of a sprite's .png.mcmeta: how the sprite scales. A picture without one is stretched.
	struct FGuiScaling
	{
		enum class EType : uint8 { Stretch, Tile, NineSlice };
		EType Type = EType::Stretch;
		float Width = 0, Height = 0;
		float Left = 0, Top = 0, Right = 0, Bottom = 0;
	};
	static FGuiScaling ParseGuiScaling(const FString& MetaJson, float TexW, float TexH);
	// The file a sprite is read from: a pack's picture where a pack has it, else the game's. "resources:" pictures come
	// from the launcher's asset index (the title panorama), which a pack's copy of the same path still replaces.
	static FString SpritePath(const FString& RelativeAssetPath);

private:
	// The splash shown on the title screen, chosen when the screen is opened.
	FString Splash;
	bool bSplashChosen = false;
	void DrawSplash(float CentreX);
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
	// A pack's pack.png from its bytes, kept under Key; null if it isn't a picture.
	UTexture2D* LoadPackIcon(const FString& Key, const TArray<uint8>& Png);
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
	// The cache's screen-edge darkening picture as black with its grey as opacity, smoothed.
	UTexture2D* LoadVignette();
	void DrawRegion(UTexture2D* Texture, float X, float Y, float Width, float Height, float U, float V, float UW, float VH, const FLinearColor& Tint = FLinearColor::White);
	void DrawIcon(const FIcon& Icon, float X, float Y);
	void DrawFill(float X, float Y, float Width, float Height, const FLinearColor& Color);

	// Text in the game's own bitmap font (textures/font/ascii.png).
	void LoadFont();
	float TextWidth(const FString& Text) const;
	void DrawGalacticText(const FString& Text, float X, float Y, float Width, uint32 Rgb);
	void DrawGuiText(const FString& Text, float X, float Y, uint32 Rgb, bool bShadow, float Alpha = 1.0f, float TextScale = 1.0f);

	void DrawBars(int32 Width, int32 Height, UMcPlayerDataComponent* Data);
	// F3: where the player is, what they face and stand in, and what they aim at.
	void DrawDebug(int32 Width, int32 Height, class AMcPlayerPawn& Player);
	// The light and biome at the player's block, worked out about once a second (a region of nine chunks is lit afresh).
	double DebugSampleTime = -10.0;
	int32 DebugSky = 0;
	int32 DebugBlockLight = 0;
	FString DebugBiome;
	float DebugDelta = 0.016f;
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
	void DrawWorlds(int32 Width, int32 Height);
	void DrawConfirmDelete(int32 Width, int32 Height);
	void DrawNewWorld(int32 Width, int32 Height);
	void DrawJoin(int32 Width, int32 Height);
	void DrawMessage(int32 Width, int32 Height);
	void DrawPause(int32 Width, int32 Height);
	// The settings: Unreal Mode and what comes after it. Opened from the title screen or the pause menu.
	void DrawOptions(int32 Width, int32 Height);
	// The menus drawn over a world being played (the rest replace the screen).
	bool IsWorldMenu() const { return ActiveMenu == EMenu::Pause || ActiveMenu == EMenu::Chat || ((ActiveMenu == EMenu::Options || ActiveMenu == EMenu::ResourcePacks || ActiveMenu == EMenu::ConfirmPacks || ActiveMenu == EMenu::ConfirmIncompatible) && OptionsBack == EMenu::Pause); }
	void OpenOptions(EMenu Back);
	// The resource packs screen (from the options): the packs in the packs folder in two lists, available and selected
	// (the top of the selected list wins), moved between them and up and down by the arrows over a pack's icon.
	void OpenResourcePacks();
	// Files dropped on the window from the desktop. On the resource packs screen, the packs among them are offered
	// (ConfirmPacks, "Do you want to add the following packs to Minecraft?"); on Yes they are copied into the packs folder.
	void OnFilesDropped(const TArray<FString>& Paths);
	void DrawConfirmPacks(int32 Width, int32 Height);
	// Selecting a pack the game calls incompatible or broken asks first ("Are you sure you want to load this pack?").
	void DrawConfirmIncompatible(int32 Width, int32 Height);
	int32 PendingSelect = INDEX_NONE;
	void DrawResourcePacks(int32 Width, int32 Height);
	void ApplyResourcePacks();
	void ClearPackCaches();
	void ReloadResources();
	// Chat: the lines the player has been told, above the hotbar (the last ten, fading after ten seconds; with the
	// box open, up to twenty stay), and the box being typed into (T, or / for a command; Enter sends, Up and Down go
	// back through what was sent).
	void DrawChat(int32 Width, int32 Height, bool bOpen);
	void DrawChatInput(int32 Width, int32 Height);
	TArray<FString> WrapText(const FString& Text, float MaxWidth) const;
	// The game's 9-sliced widgets: a button (true when clicked this frame) and a text field (Id says which is
	// focused; a click focuses it, Tab moves on).
	bool MenuButton(const FString& Label, float X, float Y, float W, float H, bool bEnabled = true);
	// A slider (0 to 1) drawn with its label and value as text; returns the value, which follows the mouse while it is
	// held on the bar. Id names the slider being dragged.
	float MenuSlider(int32 Id, const FString& Text, float Value, float X, float Y, float W, float H);
	void MenuTextField(FString& Text, int32 Id, float X, float Y, float W, float H, int32 MaxLength);
	void DrawNineSlice(UTexture2D* Texture, float X, float Y, float W, float H, float TexW, float TexH, float Border);
	void DrawNineSlice(UTexture2D* Texture, float X, float Y, float W, float H, float TexW, float TexH, float Left, float Top, float Right, float Bottom);
	// A GUI sprite (textures/gui/sprites/...) in the box, scaled the way its .png.mcmeta says: stretched (the default),
	// tiled, or as a nine slice with its borders. A sprite the mcmeta of the game does not describe falls to the given nine slice.
	void DrawGuiSprite(const FString& Sprite, float X, float Y, float W, float H);
	FGuiScaling GetGuiScaling(const FString& Sprite, UTexture2D* Texture);
	TMap<FString, FGuiScaling> GuiScalings;
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
	// Sends the saved Field of View, sensitivity, volume and render distance to the pawn, the sound and the world.
	void ApplyOptions();
	// Open to LAN: the world starts listening for other players on the game's port.
	void OpenToLan();
	bool bLanOpen = false;
	int32 DraggedSlider = INDEX_NONE;
	FMcClientOptions Settings;
	bool bSettingsApplied = false;
	TWeakObjectPtr<APawn> SettingsPawn;
	int32 LanPort = 0;

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
	struct FPackRow
	{
		FString Name;         // the file or folder name in the packs folder (what the selection file stores)
		FString Title;
		FString Description;
		FString IconKey;
		FString Error;
		FString Warning;      // made for another version: shown in red, the pack still loads
	};
	// Packs chosen while a world is open wait for the next start: the world is using the textures.
	bool bPacksNeedRestart = false;
	TArray<FPackRow> PacksAvailable;
	TArray<FPackRow> PacksSelected;   // the topmost pack first
	FString PacksSearch;
	McPacks::FDrop PendingDrop;
	TArray<FString> DropFailed;
	TSharedPtr<class SWidget> DropTarget;
	int32 PacksScrollAvailable = 0;
	int32 PacksScrollSelected = 0;
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

	// One character of the font: where its picture is on a sheet (texels), how big it is drawn and how far the next one is,
	// in GUI pixels. A space provider's character has no sprite, only an advance.
	struct FGlyph
	{
		FString Sprite;
		// A character a provider drew itself (ttf, unihex, legacy_unicode) has its own small picture instead of a sheet cell.
		UTexture2D* Texture = nullptr;
		float U = 0, V = 0, CellWidth = 0, CellHeight = 0;
		float DrawWidth = 0, DrawHeight = 0, OffsetX = 0, OffsetY = 0;
		float Advance = 0;
		// How far the shadow is moved, in pixels; half for the half-size glyphs of Unifont.
		float ShadowOffset = 1.0f;
		// The glyph's provider in the font definition: the first provider to have the character wins.
		int32 Priority = 0;
	};
	TMap<int32, FGlyph> Glyphs;   // by code point
	const FGlyph* FindGlyph(int32 Char) const;
	UTexture2D* MakeGlyphTexture(const FMcRasterGlyph& Glyph);
	// The providers that draw their characters when they are first needed, and the characters they were already asked for.
	TSharedPtr<FMcRasterFonts> RasterFonts;
	TSet<int32> RasterAsked;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture2D>> GlyphTextures;
	TArray<uint8> GalacticWidths;

	TSharedPtr<FScreenState> Screen;
	// Where the mouse was, in GUI pixels, and the slot it is over.
	FVector2D GuiMouse = FVector2D::ZeroVector;
	int32 HoverSlot = INDEX_NONE;
	bool bDeathShown = false;
	// When the death screen came up: its buttons wake a second later.
	double DeathShownTime = 0.0;
};
