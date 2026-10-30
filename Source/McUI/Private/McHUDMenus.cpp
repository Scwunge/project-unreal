// The title screen, the screens behind it and the pause menu, laid out as the game's are (minecraft.wiki, Menu screen
// and its pages; sizes and places from watching the game): 200 by 20 buttons, 150 wide in pairs, the list of worlds in
// rows 36 high.

#include "McHUD.h"
#include "McGameViewportClient.h"
#include "McLaunch.h"
#include "McLocalPlayer.h"
#include "McRenderMode.h"
#include "McUI.h"
#include "McPlayerPawn.h"
#include "McWorldActor.h"
#include "McWorldSave.h"
#include "AudioDevice.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FString AMcHUD::ConnectError;

namespace
{
	constexpr uint32 White = 0xFFFFFF;
	constexpr uint32 Grey = 0xA0A0A0;
	constexpr uint32 DarkGrey = 0x808080;
	constexpr uint32 Red = 0xFF5555;
	// Text field ids, in Tab order on each screen.
	constexpr int32 FieldWorldName = 0;
	constexpr int32 FieldSeed = 1;
	constexpr int32 FieldPlayerName = 0;
	constexpr int32 FieldAddress = 1;

	FString FormatLastPlayed(const FDateTime& Utc)
	{
		const FDateTime Local = Utc + (FDateTime::Now() - FDateTime::UtcNow());
		return Local.ToString(TEXT("%d/%m/%Y %H:%M"));
	}
}

void AMcHUD::SetConnectError(const FString& Error)
{
	ConnectError = Error.IsEmpty() ? TEXT("Connection lost") : Error;
}

void AMcHUD::OpenMenu(EMenu NewMenu)
{
	if (ActiveMenu == EMenu::Message && NewMenu != EMenu::Message)
	{
		ConnectError.Reset();
	}
	ActiveMenu = NewMenu;
	FocusedField = 0;
	UMcGameViewportClient::TakeTyped();
	if (NewMenu == EMenu::Worlds)
	{
		WorldRows.Reset();
		for (const McWorldSave::FWorldSummary& World : McWorldSave::ListWorlds())
		{
			WorldRows.Add({ World.Folder, World.Level.Name, World.Level.GameMode, World.LastPlayed });
		}
		SelectedWorld = WorldRows.Num() > 0 ? 0 : INDEX_NONE;
		WorldScroll = 0;
		if (WorldRows.Num() == 0)
		{
			// With no worlds yet the game goes straight on to making one.
			OpenMenu(EMenu::NewWorld);
		}
	}
	else if (NewMenu == EMenu::NewWorld)
	{
		NewWorldName = TEXT("New World");
		NewWorldSeed.Reset();
		bNewWorldCreative = false;
		bNewWorldFlat = false;
	}
	else if (NewMenu == EMenu::Join)
	{
		LoadClientOptions();
	}
	else if (NewMenu == EMenu::Chat)
	{
		ChatDraft.Reset();
		SentChatIndex = SentChat.Num();
	}
	if (NewMenu == EMenu::None)
	{
		SetMenuInput(false);
	}
}

void AMcHUD::SetMenuInput(bool bMenu)
{
	APlayerController* Player = GetOwningPlayerController();
	if (!Player || bMenuInput == bMenu)
	{
		return;
	}
	bMenuInput = bMenu;
	Player->bShowMouseCursor = bMenu;
	if (bMenu)
	{
		Player->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
	}
	else
	{
		Player->SetInputMode(FInputModeGameOnly());
	}
	Player->SetIgnoreLookInput(bMenu);
	Player->SetIgnoreMoveInput(bMenu);
}

bool AMcHUD::DrawMenus(int32 Width, int32 Height, bool bTitle)
{
	static bool bWatchingFailures = false;
	if (!bWatchingFailures && GEngine)
	{
		// A failed join (refused, wrong address, server gone) lands back on the title screen, which says why.
		bWatchingFailures = true;
		GEngine->OnNetworkFailure().AddLambda([](UWorld*, UNetDriver*, ENetworkFailure::Type, const FString& Error) { SetConnectError(Error); });
		GEngine->OnTravelFailure().AddLambda([](UWorld*, ETravelFailure::Type, const FString& Error) { SetConnectError(Error); });
	}
	if (bTitle && !ConnectError.IsEmpty() && (ActiveMenu != EMenu::Message || MessageText != ConnectError))
	{
		// The failure comes while the title is still up, which the engine then loads afresh, so it stays to be shown
		// on the new one until the player leaves the message (OpenMenu).
		OpenMenu(EMenu::Message);
		MessageTitle = TEXT("Failed to connect to the server");
		MessageText = ConnectError;
	}
	if (bTitle && (ActiveMenu == EMenu::None || IsWorldMenu()))
	{
		OpenMenu(EMenu::Title);
#if !UE_BUILD_SHIPPING
		// Development: -McMenu=worlds, newworld or join opens that screen first (for a -McShot of it).
		static bool bDevMenuDone = false;
		FString DevMenu;
		if (!bDevMenuDone && FParse::Value(FCommandLine::Get(), TEXT("-McMenu="), DevMenu))
		{
			bDevMenuDone = true;
			OpenMenu(DevMenu == TEXT("worlds") ? EMenu::Worlds : DevMenu == TEXT("newworld") ? EMenu::NewWorld : DevMenu == TEXT("join") ? EMenu::Join : EMenu::Title);
			if (DevMenu == TEXT("options"))
			{
				OpenOptions(EMenu::Title);
			}
		}
#endif
	}
	if (ActiveMenu == EMenu::None)
	{
		return false;
	}
	SetMenuInput(true);

	APlayerController* Player = GetOwningPlayerController();
	float MouseX = 0.0f, MouseY = 0.0f;
	if (Player)
	{
		Player->GetMousePosition(MouseX, MouseY);
	}
	GuiMouse = FVector2D(MouseX, MouseY) / Scale;
	bMenuClick = Player && Player->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	bMenuBackspace = Player && Player->WasInputKeyJustPressed(EKeys::BackSpace);
	bMenuEnter = Player && Player->WasInputKeyJustPressed(EKeys::Enter);
	bMenuTab = Player && Player->WasInputKeyJustPressed(EKeys::Tab);
	MenuTyped = UMcGameViewportClient::TakeTyped();
	const bool bEscape = Player && Player->WasInputKeyJustPressed(EKeys::Escape);
	if (bMenuTab && FieldCount > 0)
	{
		FocusedField = (FocusedField + 1) % FieldCount;
	}
	FieldCount = 0;

	if (bEscape)
	{
		// Esc is the screen's Cancel or Back.
		switch (ActiveMenu)
		{
		case EMenu::Pause: case EMenu::Chat: OpenMenu(EMenu::None); return false;
		case EMenu::Worlds: case EMenu::Join: case EMenu::Message: OpenMenu(EMenu::Title); break;
		case EMenu::NewWorld: case EMenu::ConfirmDelete: OpenMenu(EMenu::Worlds); break;
		case EMenu::Options: OpenMenu(OptionsBack); return IsWorldMenu() ? false : true;
		default: break;
		}
	}

	switch (ActiveMenu)
	{
	case EMenu::Title: DrawTitle(Width, Height); break;
	case EMenu::Worlds: DrawWorlds(Width, Height); break;
	case EMenu::ConfirmDelete: DrawConfirmDelete(Width, Height); break;
	case EMenu::NewWorld: DrawNewWorld(Width, Height); break;
	case EMenu::Join: DrawJoin(Width, Height); break;
	case EMenu::Message: DrawMessage(Width, Height); break;
	case EMenu::Pause: DrawPause(Width, Height); break;
	case EMenu::Chat: DrawChatInput(Width, Height); break;
	case EMenu::Options: DrawOptions(Width, Height); break;
	default: break;
	}
	return !IsWorldMenu() && ActiveMenu != EMenu::None;
}

// ---- Widgets ----

void AMcHUD::DrawNineSlice(UTexture2D* Texture, float X, float Y, float W, float H, float TexW, float TexH, float Border)
{
	if (!Texture)
	{
		return;
	}
	// The picture may be drawn at a higher resolution than its GUI size (a resource pack's); scale the UVs to it.
	const float Ku = Texture->GetSizeX() / TexW;
	const float Kv = Texture->GetSizeY() / TexH;
	const float B = FMath::Min(Border, FMath::Min(W, H) / 2.0f);
	const float Xs[3] = { X, X + B, X + W - B };
	const float Ws[3] = { B, W - 2 * B, B };
	const float Us[3] = { 0.0f, Border, TexW - Border };
	const float UWs[3] = { Border, TexW - 2 * Border, Border };
	const float Ys[3] = { Y, Y + B, Y + H - B };
	const float Hs[3] = { B, H - 2 * B, B };
	const float Vs[3] = { 0.0f, Border, TexH - Border };
	const float VHs[3] = { Border, TexH - 2 * Border, Border };
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 Column = 0; Column < 3; ++Column)
		{
			if (Ws[Column] > 0.0f && Hs[Row] > 0.0f)
			{
				DrawRegion(Texture, Xs[Column], Ys[Row], Ws[Column], Hs[Row], Us[Column] * Ku, Vs[Row] * Kv, UWs[Column] * Ku, VHs[Row] * Kv);
			}
		}
	}
}

void AMcHUD::DrawCentredText(const FString& Text, float CentreX, float Y, uint32 Rgb)
{
	LoadFont();
	DrawGuiText(Text, FMath::RoundToFloat(CentreX - TextWidth(Text) / 2.0f), Y, Rgb, true);
}

bool AMcHUD::MenuButton(const FString& Label, float X, float Y, float W, float H, bool bEnabled)
{
	const bool bOver = bEnabled && GuiMouse.X >= X && GuiMouse.X < X + W && GuiMouse.Y >= Y && GuiMouse.Y < Y + H;
	const TCHAR* Sprite = !bEnabled ? TEXT("textures/gui/sprites/widget/button_disabled.png")
		: bOver ? TEXT("textures/gui/sprites/widget/button_highlighted.png") : TEXT("textures/gui/sprites/widget/button.png");
	DrawNineSlice(LoadSprite(Sprite), X, Y, W, H, 200.0f, 20.0f, 3.0f);
	DrawCentredText(Label, X + W / 2.0f, Y + (H - 8.0f) / 2.0f, bEnabled ? White : Grey);
#if !UE_BUILD_SHIPPING
	// Development: -McMenuPress=Singleplayer,Create_New_World presses those buttons in turn, a second apart ('_' for a
	// space), to run the menus without a hand on the mouse.
	static TArray<FString> Presses;
	static bool bPressesRead = false;
	static double NextPress = 0.0;
	if (!bPressesRead)
	{
		bPressesRead = true;
		FString Spec;
		if (FParse::Value(FCommandLine::Get(), TEXT("-McMenuPress="), Spec, /*bShouldStopOnSeparator=*/false))
		{
			Spec.ReplaceInline(TEXT("_"), TEXT(" "));
			Spec.ParseIntoArray(Presses, TEXT(","));
			NextPress = FPlatformTime::Seconds() + 2.0;
		}
	}
	if (bEnabled && Presses.Num() > 0 && Presses[0] == Label && FPlatformTime::Seconds() >= NextPress)
	{
		UE_LOG(LogMcUI, Display, TEXT("McMenuPress: %s"), *Label);
		Presses.RemoveAt(0);
		NextPress = FPlatformTime::Seconds() + 1.0;
		return true;
	}
#endif
	return bOver && bMenuClick;
}

float AMcHUD::MenuSlider(int32 Id, const FString& Text, float Value, float X, float Y, float W, float H)
{
	const bool bOver = GuiMouse.X >= X && GuiMouse.X < X + W && GuiMouse.Y >= Y && GuiMouse.Y < Y + H;
	APlayerController* Player = GetOwningPlayerController();
	const bool bDown = Player && Player->IsInputKeyDown(EKeys::LeftMouseButton);
	if (bOver && bMenuClick)
	{
		DraggedSlider = Id;
	}
	if (DraggedSlider == Id)
	{
		if (bDown)
		{
			// The handle is 8 wide and its middle follows the mouse.
			Value = FMath::Clamp(static_cast<float>((GuiMouse.X - X - 4.0) / (W - 8.0)), 0.0f, 1.0f);
		}
		else
		{
			DraggedSlider = INDEX_NONE;
		}
	}
	const bool bHot = bOver || DraggedSlider == Id;
	DrawNineSlice(LoadSprite(bHot ? TEXT("textures/gui/sprites/widget/slider_highlighted.png") : TEXT("textures/gui/sprites/widget/slider.png")), X, Y, W, H, 200.0f, 20.0f, 3.0f);
	DrawNineSlice(LoadSprite(bHot ? TEXT("textures/gui/sprites/widget/slider_handle_highlighted.png") : TEXT("textures/gui/sprites/widget/slider_handle.png")),
		X + Value * (W - 8.0f), Y, 8.0f, H, 8.0f, 20.0f, 2.0f);
	DrawCentredText(Text, X + W / 2.0f, Y + (H - 8.0f) / 2.0f, White);
	return Value;
}

void AMcHUD::MenuTextField(FString& Text, int32 Id, float X, float Y, float W, float H, int32 MaxLength)
{
	FieldCount = FMath::Max(FieldCount, Id + 1);
	const bool bOver = GuiMouse.X >= X && GuiMouse.X < X + W && GuiMouse.Y >= Y && GuiMouse.Y < Y + H;
	if (bOver && bMenuClick)
	{
		FocusedField = Id;
	}
	const bool bFocused = FocusedField == Id;
	if (bFocused)
	{
		for (const TCHAR Char : MenuTyped)
		{
			if (Text.Len() < MaxLength)
			{
				Text.AppendChar(Char);
			}
		}
		if (bMenuBackspace && !Text.IsEmpty())
		{
			Text.LeftChopInline(1);
		}
	}
	DrawNineSlice(LoadSprite(bFocused ? TEXT("textures/gui/sprites/widget/text_field_highlighted.png") : TEXT("textures/gui/sprites/widget/text_field.png")),
		X, Y, W, H, 200.0f, 20.0f, 1.0f);
	// The end of what's typed shows if it is longer than the field.
	FString Shown = Text;
	while (!Shown.IsEmpty() && TextWidth(Shown) > W - 10.0f)
	{
		Shown.RightChopInline(1);
	}
	const float TextY = Y + (H - 8.0f) / 2.0f;
	DrawGuiText(Shown, X + 4.0f, TextY, 0xE0E0E0, true);
	// The cursor blinks: on for 6 frames of 12 at the game's 20 a second, about half a second each.
	if (bFocused && FMath::FloorToInt(FPlatformTime::Seconds() * 1.6) % 2 == 0)
	{
		DrawGuiText(TEXT("_"), X + 4.0f + TextWidth(Shown), TextY, 0xE0E0E0, true);
	}
}

void AMcHUD::DrawMenuBackground(int32 Width, int32 Height, bool bDim)
{
	// The panorama, its front picture drawn over the whole screen and drifting slowly sideways (the game turns a cube of
	// six; this is the flat part of that).
	if (UTexture2D* Panorama = LoadSprite(TEXT("resources:textures/gui/title/background/panorama_0.png")))
	{
		const float Side = FMath::Max(Width, Height) * 1.25f;
		const float Drift = FMath::Sin(FPlatformTime::Seconds() * 0.05) * (Side - Width) / 2.0f;
		DrawSprite(Panorama, (Width - Side) / 2.0f + Drift, (Height - Side) / 2.0f, Side, Side);
	}
	if (bDim)
	{
		DrawFill(0, 0, static_cast<float>(Width), static_cast<float>(Height), FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	}
}

// ---- Screens ----

void AMcHUD::DrawTitle(int32 Width, int32 Height)
{
	DrawMenuBackground(Width, Height, false);
	const float CentreX = Width / 2.0f;
	// The logo: 256 by 44 of its 256 by 64 picture, and the edition banner under it.
	if (UTexture2D* Logo = LoadSprite(TEXT("textures/gui/title/minecraft.png")))
	{
		const float K = Logo->GetSizeX() / 256.0f;
		DrawRegion(Logo, CentreX - 128.0f, 30.0f, 256.0f, 44.0f, 0.0f, 0.0f, 256.0f * K, 44.0f * K);
	}
	if (UTexture2D* Edition = LoadSprite(TEXT("textures/gui/title/edition.png")))
	{
		const float K = Edition->GetSizeX() / 128.0f;
		DrawRegion(Edition, CentreX - 64.0f, 67.0f, 128.0f, 14.0f, 0.0f, 0.0f, 128.0f * K, 14.0f * K);
	}

	const float Top = Height / 4.0f + 48.0f;
	if (MenuButton(TEXT("Singleplayer"), CentreX - 100.0f, Top, 200.0f, 20.0f))
	{
		OpenMenu(EMenu::Worlds);
	}
	else if (MenuButton(TEXT("Multiplayer"), CentreX - 100.0f, Top + 24.0f, 200.0f, 20.0f))
	{
		OpenMenu(EMenu::Join);
	}
	// Not there yet: drawn so the screen has the game's shape, but greyed out.
	MenuButton(TEXT("Minecraft Realms"), CentreX - 100.0f, Top + 48.0f, 200.0f, 20.0f, false);
	if (MenuButton(TEXT("Options..."), CentreX - 100.0f, Top + 84.0f, 98.0f, 20.0f))
	{
		OpenOptions(EMenu::Title);
		return;
	}
	if (MenuButton(TEXT("Quit Game"), CentreX + 2.0f, Top + 84.0f, 98.0f, 20.0f))
	{
		if (APlayerController* Player = GetOwningPlayerController())
		{
			Player->ConsoleCommand(TEXT("quit"));
		}
	}
	DrawGuiText(TEXT("Minecraft 26.3 (Unreal port)"), 2.0f, Height - 10.0f, White, true);
}

void AMcHUD::DrawWorlds(int32 Width, int32 Height)
{
	DrawMenuBackground(Width, Height, true);
	const float CentreX = Width / 2.0f;
	DrawCentredText(TEXT("Select World"), CentreX, 16.0f, White);

	// The list, between the title and the buttons.
	const float ListTop = 32.0f;
	const float ListBottom = Height - 64.0f;
	const float RowWidth = 270.0f;
	const float RowX = CentreX - RowWidth / 2.0f;
	const int32 Visible = FMath::Max(1, FMath::FloorToInt((ListBottom - ListTop) / 36.0f));
	APlayerController* Player = GetOwningPlayerController();
	if (Player && Player->WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		--WorldScroll;
	}
	if (Player && Player->WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		++WorldScroll;
	}
	WorldScroll = FMath::Clamp(WorldScroll, 0, FMath::Max(0, WorldRows.Num() - Visible));
	DrawFill(0.0f, ListTop, static_cast<float>(Width), ListBottom - ListTop, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
	bool bPlay = false;
	for (int32 Index = WorldScroll; Index < WorldRows.Num() && Index < WorldScroll + Visible; ++Index)
	{
		const FWorldRow& Row = WorldRows[Index];
		const float Y = ListTop + 2.0f + (Index - WorldScroll) * 36.0f;
		const bool bOver = GuiMouse.X >= RowX && GuiMouse.X < RowX + RowWidth && GuiMouse.Y >= Y && GuiMouse.Y < Y + 36.0f;
		if (bOver && bMenuClick)
		{
			// A second click on the same row soon after plays it.
			const double Now = FPlatformTime::Seconds();
			bPlay = SelectedWorld == Index && Now - LastRowClick < 0.25;
			SelectedWorld = Index;
			LastRowClick = Now;
		}
		if (SelectedWorld == Index)
		{
			DrawFill(RowX - 2.0f, Y - 2.0f, RowWidth + 4.0f, 36.0f, FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
			DrawFill(RowX - 1.0f, Y - 1.0f, RowWidth + 2.0f, 34.0f, FLinearColor::Black);
		}
		DrawGuiText(Row.Name, RowX + 3.0f, Y + 1.0f, White, true);
		DrawGuiText(FString::Printf(TEXT("%s (%s)"), *Row.Folder, *FormatLastPlayed(Row.LastPlayed)), RowX + 3.0f, Y + 12.0f, DarkGrey, true);
		DrawGuiText(McLaunch::IsCreativeMode(Row.GameMode) ? TEXT("Creative Mode") : TEXT("Survival Mode"), RowX + 3.0f, Y + 23.0f, DarkGrey, true);
	}

	const bool bSelected = WorldRows.IsValidIndex(SelectedWorld);
	if (MenuButton(TEXT("Play Selected World"), CentreX - 154.0f, Height - 52.0f, 150.0f, 20.0f, bSelected) || (bSelected && (bPlay || bMenuEnter)))
	{
		PlayWorld(WorldRows[SelectedWorld].Folder, FString());
		return;
	}
	if (MenuButton(TEXT("Create New World"), CentreX + 4.0f, Height - 52.0f, 150.0f, 20.0f))
	{
		OpenMenu(EMenu::NewWorld);
		return;
	}
	MenuButton(TEXT("Edit"), CentreX - 154.0f, Height - 28.0f, 72.0f, 20.0f, false);
	if (MenuButton(TEXT("Delete"), CentreX - 76.0f, Height - 28.0f, 72.0f, 20.0f, bSelected))
	{
		ActiveMenu = EMenu::ConfirmDelete;
		return;
	}
	MenuButton(TEXT("Re-Create"), CentreX + 4.0f, Height - 28.0f, 72.0f, 20.0f, false);
	if (MenuButton(TEXT("Back"), CentreX + 82.0f, Height - 28.0f, 72.0f, 20.0f))
	{
		OpenMenu(EMenu::Title);
	}
}

void AMcHUD::DrawConfirmDelete(int32 Width, int32 Height)
{
	DrawMenuBackground(Width, Height, true);
	const float CentreX = Width / 2.0f;
	if (!WorldRows.IsValidIndex(SelectedWorld))
	{
		OpenMenu(EMenu::Worlds);
		return;
	}
	const FWorldRow& Row = WorldRows[SelectedWorld];
	DrawCentredText(TEXT("Are you sure you want to delete this world?"), CentreX, 70.0f, White);
	DrawCentredText(FString::Printf(TEXT("'%s' will be lost forever! (A long time!)"), *Row.Name), CentreX, 90.0f, Grey);
	const float Y = Height / 6.0f + 96.0f;
	if (MenuButton(TEXT("Delete"), CentreX - 155.0f, Y, 150.0f, 20.0f))
	{
		if (!McWorldSave::DeleteWorld(Row.Folder))
		{
			UE_LOG(LogMcUI, Error, TEXT("Can't delete world %s"), *Row.Folder);
		}
		OpenMenu(EMenu::Worlds);
		if (ActiveMenu == EMenu::NewWorld)
		{
			// That was the last world: back to the list's place, the title, rather than New World.
			OpenMenu(EMenu::Title);
		}
	}
	else if (MenuButton(TEXT("Cancel"), CentreX + 5.0f, Y, 150.0f, 20.0f))
	{
		ActiveMenu = EMenu::Worlds;
	}
}

void AMcHUD::DrawNewWorld(int32 Width, int32 Height)
{
	DrawMenuBackground(Width, Height, true);
	const float CentreX = Width / 2.0f;
	const float Left = CentreX - 100.0f;
	DrawCentredText(TEXT("Create New World"), CentreX, 20.0f, White);

	DrawGuiText(TEXT("World Name"), Left, 47.0f, Grey, true);
	MenuTextField(NewWorldName, FieldWorldName, Left, 60.0f, 200.0f, 20.0f, 32);
	DrawGuiText(FString::Printf(TEXT("Will be saved in: %s"), *McWorldSave::MakeNewFolder(NewWorldName)), Left, 85.0f, DarkGrey, true);

	if (MenuButton(bNewWorldCreative ? TEXT("Game Mode: Creative") : TEXT("Game Mode: Survival"), Left, 100.0f, 200.0f, 20.0f))
	{
		bNewWorldCreative = !bNewWorldCreative;
	}
	DrawGuiText(bNewWorldCreative ? TEXT("Unlimited resources, free flying") : TEXT("Search for resources, craft, gain levels"), Left, 123.0f, DarkGrey, true);
	if (MenuButton(bNewWorldFlat ? TEXT("World Type: Superflat") : TEXT("World Type: Default"), Left, 136.0f, 200.0f, 20.0f))
	{
		bNewWorldFlat = !bNewWorldFlat;
	}

	DrawGuiText(TEXT("Seed for the World Generator"), Left, 164.0f, Grey, true);
	MenuTextField(NewWorldSeed, FieldSeed, Left, 176.0f, 200.0f, 20.0f, 32);
	DrawGuiText(TEXT("Leave blank for a random seed"), Left, 200.0f, DarkGrey, true);

	if (MenuButton(TEXT("Create New World"), CentreX - 155.0f, Height - 28.0f, 150.0f, 20.0f) || bMenuEnter)
	{
		CreateWorld();
	}
	else if (MenuButton(TEXT("Cancel"), CentreX + 5.0f, Height - 28.0f, 150.0f, 20.0f))
	{
		OpenMenu(EMenu::Worlds);
		if (ActiveMenu == EMenu::NewWorld)
		{
			OpenMenu(EMenu::Title);
		}
	}
}

void AMcHUD::DrawJoin(int32 Width, int32 Height)
{
	DrawMenuBackground(Width, Height, true);
	const float CentreX = Width / 2.0f;
	const float Left = CentreX - 100.0f;
	DrawCentredText(TEXT("Join Server"), CentreX, 20.0f, White);

	DrawGuiText(TEXT("Player Name"), Left, 60.0f, Grey, true);
	MenuTextField(PlayerName, FieldPlayerName, Left, 72.0f, 200.0f, 20.0f, 16);
	const bool bNameOk = FMcClientOptions::IsValidPlayerName(PlayerName);
	if (!bNameOk)
	{
		DrawGuiText(TEXT("3 to 16 letters, digits or _"), Left, 96.0f, Red, true);
	}
	DrawGuiText(TEXT("Server Address"), Left, 110.0f, Grey, true);
	MenuTextField(ServerAddress, FieldAddress, Left, 122.0f, 200.0f, 20.0f, 128);

	const bool bCanJoin = bNameOk && !ServerAddress.TrimStartAndEnd().IsEmpty();
	if (MenuButton(TEXT("Join Server"), Left, Height / 4.0f + 96.0f + 12.0f, 200.0f, 20.0f, bCanJoin) || (bCanJoin && bMenuEnter))
	{
		JoinServer();
	}
	else if (MenuButton(TEXT("Cancel"), Left, Height / 4.0f + 120.0f + 12.0f, 200.0f, 20.0f))
	{
		OpenMenu(EMenu::Title);
	}
}

void AMcHUD::DrawMessage(int32 Width, int32 Height)
{
	DrawMenuBackground(Width, Height, true);
	const float CentreX = Width / 2.0f;
	DrawCentredText(MessageTitle, CentreX, Height / 2.0f - 30.0f, Grey);
	DrawCentredText(MessageText, CentreX, Height / 2.0f - 10.0f, White);
	if (MenuButton(TEXT("Back to Title Screen"), CentreX - 100.0f, Height / 2.0f + 20.0f, 200.0f, 20.0f) || bMenuEnter)
	{
		if (GEngine)
		{
			// Gives up on a join still being tried.
			GEngine->CancelPending(GetWorld());
		}
		OpenMenu(EMenu::Title);
	}
}

void AMcHUD::DrawPause(int32 Width, int32 Height)
{
	// The world goes on behind (a server doesn't stop for one player, and a world of one doesn't stop yet either).
	DrawFill(0, 0, static_cast<float>(Width), static_cast<float>(Height), FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	const float CentreX = Width / 2.0f;
	DrawCentredText(TEXT("Game Menu"), CentreX, 40.0f, White);
	const bool bClient = GetNetMode() == NM_Client;
	const float Top = Height / 4.0f + 24.0f;
	if (MenuButton(TEXT("Back to Game"), CentreX - 102.0f, Top, 204.0f, 20.0f))
	{
		OpenMenu(EMenu::None);
	}
	else if (MenuButton(TEXT("Options..."), CentreX - 102.0f, Top + 48.0f, 98.0f, 20.0f))
	{
		OpenOptions(EMenu::Pause);
	}
	else if (MenuButton(bLanOpen ? TEXT("LAN: Open") : TEXT("Open to LAN"), CentreX + 4.0f, Top + 48.0f, 98.0f, 20.0f, !bClient && !bLanOpen))
	{
		OpenToLan();
	}
	else if (MenuButton(bClient ? TEXT("Disconnect") : TEXT("Save and Quit to Title"), CentreX - 102.0f, Top + 72.0f, 204.0f, 20.0f))
	{
		// Leaving the map saves the world and the player (AMcWorldActor and AMcGameMode end play).
		OpenMenu(EMenu::None);
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true, TEXT("McTitle"));
	}
	if (bLanOpen)
	{
		DrawCentredText(FString::Printf(TEXT("Local game hosted on port %d"), LanPort), CentreX, Top + 100.0f, Grey);
	}
}

void AMcHUD::OpenOptions(EMenu Back)
{
	OptionsBack = Back;
	Settings = FMcClientOptions::Load();
	OpenMenu(EMenu::Options);
}

void AMcHUD::DrawOptions(int32 Width, int32 Height)
{
	if (OptionsBack == EMenu::Pause)
	{
		// Over the world, like the pause menu it came from.
		DrawFill(0, 0, static_cast<float>(Width), static_cast<float>(Height), FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	}
	else
	{
		DrawMenuBackground(Width, Height, true);
	}
	const float CentreX = Width / 2.0f;
	const float Top = Height / 6.0f;
	const float Left = CentreX - 155.0f;
	const float Right = CentreX + 5.0f;
	DrawCentredText(TEXT("Options"), CentreX, 15.0f, White);

	const int32 Dragging = DraggedSlider;
	const FMcClientOptions Before = Settings;

	// Field of View 30 to 110 (70 is "Normal"), render distance 2 to 32 chunks, sensitivity 0 to 200%, volume 0 to 100%.
	const int32 Fov = FMath::RoundToInt(Settings.FieldOfView);
	const FString FovText = Fov == 70 ? FString(TEXT("FOV: Normal")) : Fov == 110 ? FString(TEXT("FOV: Quake Pro")) : FString::Printf(TEXT("FOV: %d"), Fov);
	Settings.FieldOfView = FMath::RoundToFloat(30.0f + 80.0f * MenuSlider(0, FovText, (Settings.FieldOfView - 30.0f) / 80.0f, Left, Top + 24.0f, 150.0f, 20.0f));
	Settings.RenderDistance = 2 + FMath::RoundToInt(30.0f * MenuSlider(1, FString::Printf(TEXT("Render Distance: %d chunks"), Settings.RenderDistance),
		(Settings.RenderDistance - 2) / 30.0f, Right, Top + 24.0f, 150.0f, 20.0f));
	const int32 SensPercent = FMath::RoundToInt(Settings.Sensitivity * 100.0f);
	const FString SensText = SensPercent == 0 ? FString(TEXT("Sensitivity: *yawn*")) : SensPercent == 200 ? FString(TEXT("Sensitivity: HYPERSPEED!!!"))
		: FString::Printf(TEXT("Sensitivity: %d%%"), SensPercent);
	Settings.Sensitivity = FMath::RoundToFloat(200.0f * MenuSlider(2, SensText, Settings.Sensitivity / 2.0f, Left, Top + 48.0f, 150.0f, 20.0f)) / 100.0f;
	const int32 VolPercent = FMath::RoundToInt(Settings.MasterVolume * 100.0f);
	const FString VolText = VolPercent == 0 ? FString(TEXT("Master Volume: OFF")) : FString::Printf(TEXT("Master Volume: %d%%"), VolPercent);
	Settings.MasterVolume = FMath::RoundToFloat(100.0f * MenuSlider(3, VolText, Settings.MasterVolume, Right, Top + 48.0f, 150.0f, 20.0f)) / 100.0f;
	if (Before.FieldOfView != Settings.FieldOfView || Before.RenderDistance != Settings.RenderDistance
		|| Before.Sensitivity != Settings.Sensitivity || Before.MasterVolume != Settings.MasterVolume)
	{
		ApplyOptions();
	}
	if (Dragging != INDEX_NONE && DraggedSlider == INDEX_NONE)
	{
		// Let go: remembered.
		FMcClientOptions Saved = FMcClientOptions::Load();
		Saved.FieldOfView = Settings.FieldOfView;
		Saved.RenderDistance = Settings.RenderDistance;
		Saved.Sensitivity = Settings.Sensitivity;
		Saved.MasterVolume = Settings.MasterVolume;
		Saved.Save();
	}

	// Unreal Mode: the game lit by Unreal (Lumen, real shadows, fog, bloom) instead of Minecraft's own light.
	const bool bUnreal = McRenderMode::IsEnhanced();
	if (MenuButton(bUnreal ? TEXT("Unreal Mode: ON") : TEXT("Unreal Mode: OFF"), Left, Top + 84.0f, 150.0f, 20.0f))
	{
		ToggleGraphics();
	}
	// HD Textures: generated high-resolution textures and materials (or a resource pack), on top of Unreal Mode.
	const bool bHd = McRenderMode::GetHdSwitch();
	if (MenuButton(bHd ? TEXT("HD Textures: ON") : TEXT("HD Textures: OFF"), Right, Top + 84.0f, 150.0f, 20.0f, bUnreal))
	{
		ToggleHd();
	}
	DrawCentredText(bUnreal ? TEXT("Unreal Mode: Lumen lighting, real shadows, volumetric fog and bloom (F7 switches it)")
		: TEXT("Unreal Mode: off, Minecraft's own look (F7 switches it)"), CentreX, Top + 110.0f, Grey);
	DrawCentredText(!bUnreal ? TEXT("HD Textures need Unreal Mode") : bHd ? TEXT("HD Textures: sharper textures with bump, shine and depth")
		: TEXT("HD Textures: Minecraft's own textures"), CentreX, Top + 122.0f, Grey);

	if (MenuButton(TEXT("Done"), CentreX - 100.0f, Top + 150.0f, 200.0f, 20.0f))
	{
		OpenMenu(OptionsBack);
	}
}

void AMcHUD::ApplyOptions()
{
	bSettingsApplied = true;
	if (APlayerController* Controller = GetOwningPlayerController())
	{
		// The game's near plane is 0.05 blocks; the engine's 10 units would cut the hand held up to the camera.
		Controller->ConsoleCommand(TEXT("r.SetNearClipPlane 2"));
	}
	APawn* Pawn = GetOwningPawn();
	SettingsPawn = Pawn;
	if (AMcPlayerPawn* Player = Cast<AMcPlayerPawn>(Pawn))
	{
		if (Player->Camera)
		{
			Player->Camera->SetFieldOfView(Settings.FieldOfView);
		}
		// 100% is the 0.15 degrees per count the player started with.
		Player->MouseSensitivity = 0.15f * Settings.Sensitivity;
	}
	if (GEngine)
	{
		if (FAudioDevice* Device = GEngine->GetMainAudioDevice().GetAudioDevice())
		{
			Device->SetTransientPrimaryVolume(Settings.MasterVolume);
		}
	}
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AMcWorldActor> It(World); It; ++It)
		{
			if (It->HasAuthority())
			{
				It->ViewDistance = Settings.RenderDistance;
			}
		}
	}
}

void AMcHUD::OpenToLan()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() != NM_Standalone)
	{
		return;
	}
	// The same listen socket a hosted game opens: other players join with this machine's address on the game's port.
	FURL Url(nullptr, TEXT("?listen"), TRAVEL_Absolute);
	if (World->Listen(Url))
	{
		bLanOpen = true;
		LanPort = Url.Port;
		UE_LOG(LogMcUI, Display, TEXT("Open to LAN: listening on port %d"), LanPort);
	}
	else
	{
		UE_LOG(LogMcUI, Warning, TEXT("Open to LAN: the game could not listen on port %d"), Url.Port);
	}
}

// ---- What the screens do ----

void AMcHUD::CreateWorld()
{
	const FString Folder = McWorldSave::MakeNewFolder(NewWorldName);
	const FString Name = NewWorldName.TrimStartAndEnd().IsEmpty() ? TEXT("New World") : NewWorldName.TrimStartAndEnd();
	FString Options = FString::Printf(TEXT("?McName=%s?McGameMode=%s"), *McLaunch::EncodeOption(Name), bNewWorldCreative ? TEXT("creative") : TEXT("survival"));
	const int64 Seed = McLaunch::ParseSeed(NewWorldSeed);
	if (Seed != 0)
	{
		Options += FString::Printf(TEXT("?McSeed=%lld"), Seed);
	}
	if (bNewWorldFlat)
	{
		Options += TEXT("?McFlat");
	}
	PlayWorld(Folder, Options);
}

void AMcHUD::PlayWorld(const FString& Folder, const FString& ExtraOptions)
{
	LoadClientOptions();
	SaveClientOptions();
	OpenMenu(EMenu::None);
	// The world actor reads McWorld (and, for a new world, the rest) from the URL.
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true, FString::Printf(TEXT("McWorld=%s%s"), *Folder, *ExtraOptions));
}

void AMcHUD::JoinServer()
{
	SaveClientOptions();
	APlayerController* Player = GetOwningPlayerController();
	if (!Player)
	{
		return;
	}
	// "host" or "host:port"; with no port the engine uses DefaultEngine.ini's, the game's 25565.
	const FString Address = ServerAddress.TrimStartAndEnd();
	// Until the world comes (a new map) or the failure does (ConnectError).
	OpenMenu(EMenu::Message);
	MessageTitle = TEXT("Connecting to the server...");
	MessageText = Address;
	UE_LOG(LogMcUI, Log, TEXT("Joining %s as %s"), *Address, *PlayerName);
	Player->ClientTravel(Address, TRAVEL_Absolute);
}

void AMcHUD::LoadClientOptions()
{
	const FMcClientOptions Options = FMcClientOptions::Load();
	PlayerName = Options.PlayerName;
	ServerAddress = Options.LastServer;
	if (!FMcClientOptions::IsValidPlayerName(PlayerName))
	{
		// The game's name for a player without an account.
		PlayerName = FString::Printf(TEXT("Player%d"), FMath::RandRange(100, 999));
	}
}

void AMcHUD::SaveClientOptions() const
{
	// The engine sends this name when it opens a world or joins a server (UMcLocalPlayer).
	FMcClientOptions Options = FMcClientOptions::Load();
	Options.PlayerName = PlayerName;
	Options.LastServer = ServerAddress.TrimStartAndEnd();
	Options.bEnhancedGraphics = McRenderMode::IsEnhanced();
	Options.bHdTextures = McRenderMode::GetHdSwitch();
	Options.Save();
}

void AMcHUD::ToggleGraphics()
{
	McRenderMode::SetEnhanced(!McRenderMode::IsEnhanced());
	FMcClientOptions Options = FMcClientOptions::Load();
	Options.bEnhancedGraphics = McRenderMode::IsEnhanced();
	Options.Save();
}

void AMcHUD::ToggleHd()
{
	McRenderMode::SetHd(!McRenderMode::GetHdSwitch());
	FMcClientOptions Options = FMcClientOptions::Load();
	Options.bHdTextures = McRenderMode::GetHdSwitch();
	Options.Save();
}
