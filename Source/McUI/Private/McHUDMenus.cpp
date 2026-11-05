// The title screen, the screens behind it and the pause menu, laid out as the game's are (minecraft.wiki, Menu screen
// and its pages; sizes and places from watching the game): 200 by 20 buttons, 150 wide in pairs, the list of worlds in
// rows 36 high.

#include "McHUD.h"
#include "McCache.h"
#include "Engine/PostProcessVolume.h"
#include "McLang.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "McGameViewportClient.h"
#include "McJavaImport.h"
#include "McLaunch.h"
#include "McLocalPlayer.h"
#include "McCore.h"
#include "McBlockModels.h"
#include "McBlockTextures.h"
#include "McFont.h"
#include "McHdBlocks.h"
#include "McResourcePacks.h"
#include "McRenderMode.h"
#include "McSound.h"
#include "McUI.h"
#include "McPlayerPawn.h"
#include "McWorldActor.h"
#include "McWorldSave.h"
#include "AudioDevice.h"
#include "Camera/CameraComponent.h"
#include "EngineUtils.h"
#include "CanvasTypes.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
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
	bSplashChosen = false;
	// The Esc that opened a menu this frame is not also its Back.
	MenuOpenedFrame = GFrameCounter;
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
	else if (NewMenu == EMenu::ImportJava)
	{
		JavaRows.Reset();
		FString Saves = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA")) / TEXT(".minecraft") / TEXT("saves");
#if !UE_BUILD_SHIPPING
		// Development: -McJavaSaves=<folder> lists the worlds of another folder (for a -McShot of the screen).
		FParse::Value(FCommandLine::Get(), TEXT("-McJavaSaves="), Saves);
#endif
		TArray<FString> Folders;
		IFileManager::Get().FindFiles(Folders, *(Saves / TEXT("*")), false, true);
		for (const FString& Folder : Folders)
		{
			FMcLevelData Level;
			if (McJavaImport::IsJavaWorld(Saves / Folder) && McJavaImport::ReadLevel(Saves / Folder, Level))
			{
				JavaRows.Add({ Saves / Folder, Level.Name.IsEmpty() ? Folder : Level.Name, Level.GameMode,
					IFileManager::Get().GetTimeStamp(*(Saves / Folder / TEXT("level.dat"))) });
			}
		}
		JavaRows.Sort([](const FWorldRow& A, const FWorldRow& B) { return A.LastPlayed > B.LastPlayed; });
		SelectedJava = JavaRows.Num() > 0 ? 0 : INDEX_NONE;
		ImportMessage = JavaRows.Num() > 0 ? FString() : FString::Printf(TEXT("No Java worlds found in %s"), *Saves);
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
		// Development: -McMenu=worlds, newworld, join or importjava opens that screen first (for a -McShot of it).
		static bool bDevMenuDone = false;
		FString DevMenu;
		if (!bDevMenuDone && FParse::Value(FCommandLine::Get(), TEXT("-McMenu="), DevMenu))
		{
			bDevMenuDone = true;
			OpenMenu(DevMenu == TEXT("worlds") ? EMenu::Worlds : DevMenu == TEXT("newworld") ? EMenu::NewWorld : DevMenu == TEXT("join") ? EMenu::Join : DevMenu == TEXT("importjava") ? EMenu::ImportJava : EMenu::Title);
			if (DevMenu == TEXT("options"))
			{
				OpenOptions(EMenu::Title);
			}
			else if (DevMenu == TEXT("packs"))
			{
				OpenOptions(EMenu::Title);
				OpenResourcePacks();
			}
			else if (DevMenu == TEXT("language"))
			{
				OpenOptions(EMenu::Title);
				OpenLanguage();
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
	const bool bEscape = Player && Player->WasInputKeyJustPressed(EKeys::Escape) && MenuOpenedFrame != GFrameCounter;
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
		case EMenu::Statistics: case EMenu::Advancements: OpenMenu(EMenu::Pause); break;
		case EMenu::Worlds: case EMenu::Join: case EMenu::Message: OpenMenu(EMenu::Title); break;
		case EMenu::NewWorld: case EMenu::ConfirmDelete: case EMenu::ImportJava: OpenMenu(EMenu::Worlds); break;
		case EMenu::Options: OpenMenu(OptionsBack); return IsWorldMenu() ? false : true;
		case EMenu::ResourcePacks: ApplyResourcePacks(); OpenMenu(EMenu::Options); break;
		case EMenu::Language: OpenMenu(EMenu::Options); break;
		case EMenu::ConfirmPacks: case EMenu::ConfirmIncompatible: ActiveMenu = EMenu::ResourcePacks; break;
		default: break;
		}
	}

	switch (ActiveMenu)
	{
	case EMenu::Title: DrawTitle(Width, Height); break;
	case EMenu::Worlds: DrawWorlds(Width, Height); break;
	case EMenu::ConfirmDelete: DrawConfirmDelete(Width, Height); break;
	case EMenu::ImportJava: DrawImportJava(Width, Height); break;
	case EMenu::NewWorld: DrawNewWorld(Width, Height); break;
	case EMenu::Join: DrawJoin(Width, Height); break;
	case EMenu::Message: DrawMessage(Width, Height); break;
	case EMenu::Pause: DrawPause(Width, Height); break;
	case EMenu::Statistics: DrawStatistics(Width, Height); break;
	case EMenu::Advancements: DrawAdvancements(Width, Height); break;
	case EMenu::Chat: DrawChatInput(Width, Height); break;
	case EMenu::Options: DrawOptions(Width, Height); break;
	case EMenu::ResourcePacks: DrawResourcePacks(Width, Height); break;
	case EMenu::Language: DrawLanguage(Width, Height); break;
	case EMenu::ConfirmPacks: DrawConfirmPacks(Width, Height); break;
	case EMenu::ConfirmIncompatible: DrawConfirmIncompatible(Width, Height); break;
	default: break;
	}
	return !IsWorldMenu() && ActiveMenu != EMenu::None;
}

// ---- Widgets ----

void AMcHUD::DrawNineSlice(UTexture2D* Texture, float X, float Y, float W, float H, float TexW, float TexH, float Border)
{
	DrawNineSlice(Texture, X, Y, W, H, TexW, TexH, Border, Border, Border, Border);
}

void AMcHUD::DrawNineSlice(UTexture2D* Texture, float X, float Y, float W, float H, float TexW, float TexH, float Left, float Top, float Right, float Bottom)
{
	if (!Texture)
	{
		return;
	}
	// The picture may be drawn at a higher resolution than its GUI size (a resource pack's); scale the UVs to it.
	const float Ku = Texture->GetSizeX() / TexW;
	const float Kv = Texture->GetSizeY() / TexH;
	const float BL = FMath::Min(Left, W / 2.0f), BR = FMath::Min(Right, W / 2.0f);
	const float BT = FMath::Min(Top, H / 2.0f), BB = FMath::Min(Bottom, H / 2.0f);
	const float Xs[3] = { X, X + BL, X + W - BR };
	const float Ws[3] = { BL, W - BL - BR, BR };
	const float Us[3] = { 0.0f, Left, TexW - Right };
	const float UWs[3] = { Left, TexW - Left - Right, Right };
	const float Ys[3] = { Y, Y + BT, Y + H - BB };
	const float Hs[3] = { BT, H - BT - BB, BB };
	const float Vs[3] = { 0.0f, Top, TexH - Bottom };
	const float VHs[3] = { Top, TexH - Top - Bottom, Bottom };
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

// The "gui" section of a sprite's .png.mcmeta: { "scaling": { "type": "stretch" | "tile" | "nine_slice", "width", "height",
// "border": n or { left, top, right, bottom } } }. The mcmeta of the sprite's own pack counts; a pack's sprite without one is stretched.
AMcHUD::FGuiScaling AMcHUD::GetGuiScaling(const FString& Sprite, UTexture2D* Texture)
{
	if (const FGuiScaling* Found = GuiScalings.Find(Sprite))
	{
		return *Found;
	}
	FGuiScaling Scaling;
	FString Meta;
	const bool bPackPicture = McPacks::IsFromPack(Sprite);
	const FString MetaPath = McCache::Asset(Sprite + TEXT(".mcmeta"));
	if ((!bPackPicture || McPacks::IsFromPack(Sprite + TEXT(".mcmeta"))) && FFileHelper::LoadFileToString(Meta, *MetaPath))
	{
		Scaling = ParseGuiScaling(Meta, Texture ? Texture->GetSizeX() : 0.0f, Texture ? Texture->GetSizeY() : 0.0f);
	}
	GuiScalings.Add(Sprite, Scaling);
	return Scaling;
}

AMcHUD::FGuiScaling AMcHUD::ParseGuiScaling(const FString& Meta, float TexW, float TexH)
{
	FGuiScaling Scaling;
	TSharedPtr<FJsonObject> Root;
	const TSharedPtr<FJsonObject>* Gui = nullptr;
	const TSharedPtr<FJsonObject>* Object = nullptr;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Meta), Root) && Root.IsValid() && Root->TryGetObjectField(TEXT("gui"), Gui)
		&& (*Gui)->TryGetObjectField(TEXT("scaling"), Object))
	{
		FString Type;
		(*Object)->TryGetStringField(TEXT("type"), Type);
		Type.RemoveFromStart(TEXT("minecraft:"));
		double Width = TexW, Height = TexH;
		(*Object)->TryGetNumberField(TEXT("width"), Width);
		(*Object)->TryGetNumberField(TEXT("height"), Height);
		Scaling.Width = static_cast<float>(Width);
		Scaling.Height = static_cast<float>(Height);
		if (Type == TEXT("tile"))
		{
			Scaling.Type = FGuiScaling::EType::Tile;
		}
		else if (Type == TEXT("nine_slice"))
		{
			Scaling.Type = FGuiScaling::EType::NineSlice;
			double Border = 0.0;
			const TSharedPtr<FJsonObject>* Sides;
			if ((*Object)->TryGetNumberField(TEXT("border"), Border))
			{
				Scaling.Left = Scaling.Top = Scaling.Right = Scaling.Bottom = static_cast<float>(Border);
			}
			else if ((*Object)->TryGetObjectField(TEXT("border"), Sides))
			{
				double Left = 0, Top = 0, Right = 0, Bottom = 0;
				(*Sides)->TryGetNumberField(TEXT("left"), Left);
				(*Sides)->TryGetNumberField(TEXT("top"), Top);
				(*Sides)->TryGetNumberField(TEXT("right"), Right);
				(*Sides)->TryGetNumberField(TEXT("bottom"), Bottom);
				Scaling.Left = static_cast<float>(Left);
				Scaling.Top = static_cast<float>(Top);
				Scaling.Right = static_cast<float>(Right);
				Scaling.Bottom = static_cast<float>(Bottom);
			}
		}
	}
	return Scaling;
}

void AMcHUD::DrawGuiSprite(const FString& Sprite, float X, float Y, float W, float H)
{
	UTexture2D* Texture = LoadSprite(Sprite);
	if (!Texture)
	{
		return;
	}
	const FGuiScaling Scaling = GetGuiScaling(Sprite, Texture);
	const float TexW = Texture->GetSizeX(), TexH = Texture->GetSizeY();
	switch (Scaling.Type)
	{
	case FGuiScaling::EType::NineSlice:
		DrawNineSlice(Texture, X, Y, W, H, Scaling.Width, Scaling.Height, Scaling.Left, Scaling.Top, Scaling.Right, Scaling.Bottom);
		break;
	case FGuiScaling::EType::Tile:
	{
		// Tiles of the declared size, the last ones cut off at the edge.
		const float TileW = FMath::Max(Scaling.Width, 1.0f), TileH = FMath::Max(Scaling.Height, 1.0f);
		const float Ku = TexW / TileW, Kv = TexH / TileH;
		for (float Top = 0.0f; Top < H; Top += TileH)
		{
			for (float Left = 0.0f; Left < W; Left += TileW)
			{
				const float DrawW = FMath::Min(TileW, W - Left), DrawH = FMath::Min(TileH, H - Top);
				DrawRegion(Texture, X + Left, Y + Top, DrawW, DrawH, 0.0f, 0.0f, DrawW * Ku, DrawH * Kv);
			}
		}
		break;
	}
	default:
		DrawRegion(Texture, X, Y, W, H, 0.0f, 0.0f, TexW, TexH);
		break;
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
	DrawGuiSprite(Sprite, X, Y, W, H);
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
	if (bOver && bMenuClick)
	{
		// The click is heard where the player is: on the listener, not in the world.
		FVector Eye = FVector::ZeroVector;
		FRotator Look;
		if (APlayerController* Player = GetOwningPlayerController())
		{
			Player->GetPlayerViewPoint(Eye, Look);
		}
		FMcSoundBus::Get().OnSound.Broadcast(FMcSoundEvent{ FName("ui.button.click"), McCoords::UnrealToBlock(Eye), 1.0f, 1.0f });
		return true;
	}
	return false;
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
	DrawGuiSprite(bHot ? TEXT("textures/gui/sprites/widget/slider_highlighted.png") : TEXT("textures/gui/sprites/widget/slider.png"), X, Y, W, H);
	DrawGuiSprite(bHot ? TEXT("textures/gui/sprites/widget/slider_handle_highlighted.png") : TEXT("textures/gui/sprites/widget/slider_handle.png"),
		X + Value * (W - 8.0f), Y, 8.0f, H);
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
	DrawGuiSprite(bFocused ? TEXT("textures/gui/sprites/widget/text_field_highlighted.png") : TEXT("textures/gui/sprites/widget/text_field.png"), X, Y, W, H);
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
	if (MenuButton(McLang::Get(TEXT("menu.singleplayer")), CentreX - 100.0f, Top, 200.0f, 20.0f))
	{
		OpenMenu(EMenu::Worlds);
	}
	else if (MenuButton(McLang::Get(TEXT("menu.multiplayer")), CentreX - 100.0f, Top + 24.0f, 200.0f, 20.0f))
	{
		OpenMenu(EMenu::Join);
	}
	// Not there yet: drawn so the screen has the game's shape, but greyed out.
	MenuButton(McLang::Get(TEXT("menu.online")), CentreX - 100.0f, Top + 48.0f, 200.0f, 20.0f, false);
	if (MenuButton(McLang::Get(TEXT("menu.options")), CentreX - 100.0f, Top + 84.0f, 98.0f, 20.0f))
	{
		OpenOptions(EMenu::Title);
		return;
	}
	if (MenuButton(McLang::Get(TEXT("menu.quit")), CentreX + 2.0f, Top + 84.0f, 98.0f, 20.0f))
	{
		if (APlayerController* Player = GetOwningPlayerController())
		{
			Player->ConsoleCommand(TEXT("quit"));
		}
	}
	DrawSplash(CentreX);
	DrawGuiText(TEXT("Minecraft 26.3 (Unreal port)"), 2.0f, Height - 10.0f, White, true);
}

TArray<FString> AMcHUD::LoadSplashTexts()
{
	TArray<FString> Lines;
	FString Text;
	if (FFileHelper::LoadFileToString(Text, *McCache::Asset(TEXT("texts/splashes.txt"))))
	{
		Text.ParseIntoArrayLines(Lines, true);
		for (FString& Line : Lines)
		{
			Line.TrimStartAndEndInline();
		}
		Lines.RemoveAll([](const FString& Line) { return Line.IsEmpty(); });
	}
	return Lines;
}

// The three dates and their texts are from minecraft.wiki's "Splash" page (Special splashes: Merry X-mas! on December 24, Happy new
// year! on January 1, OOoooOOOoooo! Spooky! on October 31).
FString AMcHUD::PickSplash(const TArray<FString>& Lines, int32 Month, int32 Day, float Roll)
{
	if (Month == 12 && Day == 24)
	{
		return TEXT("Merry X-mas!");
	}
	if (Month == 1 && Day == 1)
	{
		return TEXT("Happy new year!");
	}
	if (Month == 10 && Day == 31)
	{
		return TEXT("OOoooOOOoooo! Spooky!");
	}
	return Lines.IsEmpty() ? FString() : Lines[FMath::Clamp(FMath::FloorToInt(Roll * Lines.Num()), 0, Lines.Num() - 1)];
}

// The splash text. Every number below was measured from the vanilla client's own title screens, not taken from its code:
// 48 shots of it (scripts/respack_splash_pulse.txt in the reference, fitted by pixel analysis) of three splashes of 10, 90 and
// 222 pixels, with the wall clock logged before each.
//  - It is turned 20.0 degrees upward: the text rows are sharpest in a rotated frame at -20.0 (tried 0.02 degrees apart).
//  - Its scale at the biggest of a pulse is 179.3 / (width + 31.1) (least squares over the three widths; the fit is exact to
//    within 0.1% of the scale), and the scale dips by 5.5% of that when the pulse is at its lowest.
//  - The pulse repeats every 999 to 1002 ms (|sine| shape: the dips come every 500 ms, fitted at 0.13 to 0.25 px rms), and
//    it is tied to the clock, not to the screen opening; the phase measured is within the shots' own timing (0 to 135 ms)
//    of the clock's whole seconds, so it is taken as zero there.
//  - It is anchored 123.95 GUI pixels right of the screen's middle and 68.38 down (centroid of its ink against the anchor,
//    over all shots, 0.56 GUI pixels rms), and its first row of text is 7.39 pixels above that point, text and rows
//    turned and scaled about it, with the text centred on it.
// Colour: it is drawn in yellow (255, 255, 0) with the usual shadow, as the pixels show.
void AMcHUD::DrawSplash(float CentreX)
{
	if (!bSplashChosen)
	{
		bSplashChosen = true;
		const FDateTime Now = FDateTime::Now();
		Splash = PickSplash(LoadSplashTexts(), Now.GetMonth(), Now.GetDay(), FMath::FRand());
	}
	if (Splash.IsEmpty() || !Canvas || !Canvas->Canvas)
	{
		return;
	}
	const int64 Millis = (FDateTime::UtcNow() - FDateTime::FromUnixTimestamp(0)).GetTotalMilliseconds();
	const float Pulse = FMath::Abs(FMath::Sin(static_cast<float>((Millis % 1000) / 1000.0 * 2.0 * UE_DOUBLE_PI)));
	const float Width = TextWidth(Splash);
	const float Size = 179.3f * (1.0f - 0.055f * Pulse) / (Width + 31.1f);
	Canvas->Canvas->PushRelativeTransform(FRotationMatrix(FRotator(0.0f, -20.0f, 0.0f)) * FTranslationMatrix(FVector((CentreX + 123.95f) * Scale, 68.38f * Scale, 0.0f)));
	DrawGuiText(Splash, -Width * Size / 2.0f, -7.39f * Size, 0xFFFF00, true, 1.0f, Size);
	Canvas->Canvas->PopTransform();
}

void AMcHUD::DrawWorlds(int32 Width, int32 Height)
{
	DrawMenuBackground(Width, Height, true);
	const float CentreX = Width / 2.0f;
	DrawCentredText(McLang::Get(TEXT("selectWorld.title")), CentreX, 16.0f, White);

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
	if (MenuButton(McLang::Get(TEXT("selectWorld.select")), CentreX - 154.0f, Height - 52.0f, 150.0f, 20.0f, bSelected) || (bSelected && (bPlay || bMenuEnter)))
	{
		PlayWorld(WorldRows[SelectedWorld].Folder, FString());
		return;
	}
	if (MenuButton(McLang::Get(TEXT("selectWorld.create")), CentreX + 4.0f, Height - 52.0f, 150.0f, 20.0f))
	{
		OpenMenu(EMenu::NewWorld);
		return;
	}
	MenuButton(McLang::Get(TEXT("selectWorld.edit")), CentreX - 154.0f, Height - 28.0f, 72.0f, 20.0f, false);
	if (MenuButton(McLang::Get(TEXT("selectWorld.delete")), CentreX - 76.0f, Height - 28.0f, 72.0f, 20.0f, bSelected))
	{
		ActiveMenu = EMenu::ConfirmDelete;
		return;
	}
	if (MenuButton(TEXT("Import Java"), CentreX + 4.0f, Height - 28.0f, 72.0f, 20.0f))
	{
		OpenMenu(EMenu::ImportJava);
		return;
	}
	if (MenuButton(McLang::Get(TEXT("gui.back")), CentreX + 82.0f, Height - 28.0f, 72.0f, 20.0f))
	{
		OpenMenu(EMenu::Title);
	}
}

void AMcHUD::DrawImportJava(int32 Width, int32 Height)
{
	DrawMenuBackground(Width, Height, true);
	const float CentreX = Width / 2.0f;
	DrawCentredText(TEXT("Import Java World"), CentreX, 16.0f, White);
	const float ListTop = 32.0f;
	const float ListBottom = Height - 64.0f;
	const float RowWidth = 270.0f;
	const float RowX = CentreX - RowWidth / 2.0f;
	DrawFill(0.0f, ListTop, static_cast<float>(Width), ListBottom - ListTop, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f));
	const int32 Visible = FMath::Max(1, FMath::FloorToInt((ListBottom - ListTop) / 36.0f));
	for (int32 Index = 0; Index < JavaRows.Num() && Index < Visible; ++Index)
	{
		const FWorldRow& Row = JavaRows[Index];
		const float Y = ListTop + 2.0f + Index * 36.0f;
		if (GuiMouse.X >= RowX && GuiMouse.X < RowX + RowWidth && GuiMouse.Y >= Y && GuiMouse.Y < Y + 36.0f && bMenuClick)
		{
			SelectedJava = Index;
		}
		if (SelectedJava == Index)
		{
			DrawFill(RowX - 2.0f, Y - 2.0f, RowWidth + 4.0f, 36.0f, FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
			DrawFill(RowX - 1.0f, Y - 1.0f, RowWidth + 2.0f, 34.0f, FLinearColor::Black);
		}
		DrawGuiText(Row.Name, RowX + 3.0f, Y + 1.0f, White, true);
		DrawGuiText(FPaths::GetCleanFilename(Row.Folder), RowX + 3.0f, Y + 12.0f, DarkGrey, true);
		DrawGuiText(McLaunch::IsCreativeMode(Row.GameMode) ? TEXT("Creative Mode") : TEXT("Survival Mode"), RowX + 3.0f, Y + 23.0f, DarkGrey, true);
	}
	if (!ImportMessage.IsEmpty())
	{
		DrawCentredText(ImportMessage, CentreX, ListTop + 8.0f, White);
	}
	const bool bSelected = JavaRows.IsValidIndex(SelectedJava);
	if (MenuButton(TEXT("Import Selected World"), CentreX - 154.0f, Height - 28.0f, 150.0f, 20.0f, bSelected))
	{
		// A copy goes into Saved/Worlds; the Java folder is only read. The world is there at once and its chunks are
		// converted while it is played, the ones near the spawn first.
		const FString Folder = McJavaImport::BeginImport(JavaRows[SelectedJava].Folder, FString());
		OpenMenu(EMenu::Worlds);
		const int32 Imported = WorldRows.IndexOfByPredicate([&Folder](const FWorldRow& Row) { return Row.Folder == Folder; });
		if (Imported != INDEX_NONE)
		{
			SelectedWorld = Imported;
		}
		return;
	}
	if (MenuButton(TEXT("Back"), CentreX + 4.0f, Height - 28.0f, 150.0f, 20.0f))
	{
		OpenMenu(EMenu::Worlds);
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
	DrawCentredText(McLang::Get(TEXT("selectWorld.deleteQuestion")), CentreX, 70.0f, White);
	DrawCentredText(FString::Printf(TEXT("'%s' will be lost forever! (A long time!)"), *Row.Name), CentreX, 90.0f, Grey);
	const float Y = Height / 6.0f + 96.0f;
	if (MenuButton(McLang::Get(TEXT("selectWorld.delete")), CentreX - 155.0f, Y, 150.0f, 20.0f))
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
	else if (MenuButton(McLang::Get(TEXT("gui.cancel")), CentreX + 5.0f, Y, 150.0f, 20.0f))
	{
		ActiveMenu = EMenu::Worlds;
	}
}

void AMcHUD::DrawNewWorld(int32 Width, int32 Height)
{
	DrawMenuBackground(Width, Height, true);
	const float CentreX = Width / 2.0f;
	const float Left = CentreX - 100.0f;
	DrawCentredText(McLang::Get(TEXT("selectWorld.create")), CentreX, 20.0f, White);

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

	if (MenuButton(McLang::Get(TEXT("selectWorld.create")), CentreX - 155.0f, Height - 28.0f, 150.0f, 20.0f) || bMenuEnter)
	{
		CreateWorld();
	}
	else if (MenuButton(McLang::Get(TEXT("gui.cancel")), CentreX + 5.0f, Height - 28.0f, 150.0f, 20.0f))
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
	else if (MenuButton(McLang::Get(TEXT("gui.cancel")), Left, Height / 4.0f + 120.0f + 12.0f, 200.0f, 20.0f))
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
	if (MenuButton(McLang::Get(TEXT("gui.toTitle")), CentreX - 100.0f, Height / 2.0f + 20.0f, 200.0f, 20.0f) || bMenuEnter)
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
	DrawWorldMenuDim(Width, Height);
	const float CentreX = Width / 2.0f;
	DrawCentredText(McLang::Get(TEXT("menu.game")), CentreX, 40.0f, White);
	const bool bClient = GetNetMode() == NM_Client;
	// Fitted from the reference's pause menu at 1280 x 720 (GUI scale 3, a 240 high screen): Back to Game at 68, Advancements and
	// Statistics 24 below it, a row of four 20 square buttons (bugs, feedback, social, reporting: none of them do anything here yet)
	// 24 below that, Options and World Options, and Save and Quit; 24 apart.
	const float Top = Height / 4.0f + 8.0f;
	if (MenuButton(McLang::Get(TEXT("menu.returnToGame")), CentreX - 102.0f, Top, 204.0f, 20.0f))
	{
		OpenMenu(EMenu::None);
	}
	else if (MenuButton(McLang::Get(TEXT("gui.advancements")), CentreX - 102.0f, Top + 24.0f, 98.0f, 20.0f))
	{
		OpenMenu(EMenu::Advancements);
	}
	else if (MenuButton(McLang::Get(TEXT("gui.stats")), CentreX + 4.0f, Top + 24.0f, 98.0f, 20.0f))
	{
		StatsScroll = 0;
		OpenMenu(EMenu::Statistics);
	}
	static const TCHAR* PauseIcons[4] = { TEXT("textures/gui/sprites/pause_menu/bug.png"), TEXT("textures/gui/sprites/pause_menu/social_interactions.png"),
		TEXT(""), TEXT("textures/gui/sprites/pause_menu/player_reporting.png") };
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float IconX = CentreX - 46.3f + Index * 24.0f;
		MenuButton(TEXT(""), IconX, Top + 48.0f, 20.0f, 20.0f);
		if (UTexture2D* Icon = PauseIcons[Index][0] ? LoadSprite(PauseIcons[Index]) : nullptr)
		{
			const float IconW = static_cast<float>(Icon->GetSizeX()), IconH = static_cast<float>(Icon->GetSizeY());
			DrawGuiSprite(PauseIcons[Index], IconX + (20.0f - IconW) / 2.0f, Top + 48.0f + (20.0f - IconH) / 2.0f, IconW, IconH);
		}
	}
	if (MenuButton(McLang::Get(TEXT("menu.options")), CentreX - 102.0f, Top + 72.0f, 98.0f, 20.0f))
	{
		OpenOptions(EMenu::Pause);
	}
	else if (MenuButton(bLanOpen ? TEXT("LAN: Open") : TEXT("Open to LAN"), CentreX + 4.0f, Top + 72.0f, 98.0f, 20.0f, !bClient && !bLanOpen))
	{
		OpenToLan();
	}
	else if (MenuButton(bClient ? TEXT("Disconnect") : TEXT("Save and Quit to Title"), CentreX - 102.0f, Top + 96.0f, 204.0f, 20.0f))
	{
		// Leaving the map saves the world and the player (AMcWorldActor and AMcGameMode end play).
		OpenMenu(EMenu::None);
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true, TEXT("McTitle"));
	}
	if (bLanOpen)
	{
		DrawCentredText(FString::Printf(TEXT("Local game hosted on port %d"), LanPort), CentreX, Top + 124.0f, Grey);
	}
}

namespace
{
	struct FStatRow
	{
		FString Name;
		// The statistic's id ("jump", "walk_one_cm").
		FString Id;
	};

	// Seconds as Java prints a double: the shortest digits that read back, with ".0" on a whole number ("0.0", "20.25").
	FString ShortSeconds(double Seconds)
	{
		FString Text = FString::SanitizeFloat(Seconds, 0);
		if (!Text.Contains(TEXT(".")))
		{
			Text += TEXT(".0");
		}
		return Text;
	}

	// A statistic's value in the game's format for it (distances in cm, m or km; times in s, min, h or d; damage in hearts).
	FString FormatStat(const FString& Id, int64 Value)
	{
		if (Id.EndsWith(TEXT("_one_cm")))
		{
			const double Metres = Value / 100.0;
			return Metres * 0.001 >= 0.5 ? FString::Printf(TEXT("%.2f km"), Metres * 0.001) : Metres >= 0.5 ? FString::Printf(TEXT("%.2f m"), Metres) : FString::Printf(TEXT("%lld cm"), Value);
		}
		if (Id.StartsWith(TEXT("damage_")))
		{
			return FString::Printf(TEXT("%.2f"), Value / 10.0);
		}
		if (Id == TEXT("sneak_time") || Id == TEXT("play_time") || Id == TEXT("time_since_death") || Id == TEXT("time_since_rest") || Id == TEXT("total_world_time"))
		{
			const double Seconds = Value / 20.0;
			const double Days = Seconds / 86400.0;
			const double Hours = Seconds / 3600.0;
			const double Minutes = Seconds / 60.0;
			return Days >= 0.5 ? FString::Printf(TEXT("%.2f d"), Days) : Hours >= 0.5 ? FString::Printf(TEXT("%.2f h"), Hours) : Minutes >= 0.5 ? FString::Printf(TEXT("%.2f min"), Minutes) : ShortSeconds(Seconds) + TEXT(" s");
		}
		return FString::Printf(TEXT("%lld"), Value);
	}

	// The General tab's rows: every "stat.minecraft.*" text of the language, sorted by the shown name (character codes, so
	// "Distance Walked" comes before "Distance by Boat"), each at zero in the game's format for it: counts "0", damage "0.00",
	// distances "0 cm", times "0.0 s" (the rows the vanilla client shows for a world that has none yet).
	const TArray<FStatRow>& GetStatRows()
	{
		static TArray<FStatRow> Rows;
		static uint32 Generation = 0xFFFFFFFFu;
		if (Generation != McLang::GetGeneration())
		{
			Generation = McLang::GetGeneration();
			Rows.Reset();
			for (const FString& Key : McLang::GetKeysWithPrefix(TEXT("stat.minecraft.")))
			{
				Rows.Add({ McLang::Get(Key), Key.RightChop(15) });
			}
			Rows.Sort([](const FStatRow& A, const FStatRow& B) { return A.Name.Compare(B.Name, ESearchCase::CaseSensitive) < 0; });
		}
		return Rows;
	}
}

void AMcHUD::DrawStatistics(int32 Width, int32 Height)
{
	DrawWorldMenuDim(Width, Height);
	LoadFont();
	const float CentreX = Width / 2.0f;
	// Fitted from the reference's screen at 1280 x 720 (GUI scale 3): the tabs 122 wide and 2 apart over a 22 high header, the
	// list from 24 to 32 above the bottom with rows 14 apart, names 136 left of the middle and values ending 133 right of it, a 6
	// wide scroll bar, and Done 200 wide 26 above the bottom.
	constexpr float HeaderBottom = 22.0f;
	constexpr float ListTop = 24.0f;
	constexpr float RowHeight = 14.0f;
	const float ListBottom = Height - 32.0f;

	if (UTexture2D* Tile = LoadSprite(TEXT("textures/gui/tab_header_background.png")))
	{
		for (float TileX = 0.0f; TileX < Width; TileX += 16.0f)
		{
			for (float TileY = 0.0f; TileY < HeaderBottom; TileY += 16.0f)
			{
				const float TileW = FMath::Min(16.0f, Width - TileX);
				const float TileH = FMath::Min(16.0f, HeaderBottom - TileY);
				const float Ku = Tile->GetSizeX() / 16.0f;
				DrawRegion(Tile, TileX, TileY, TileW, TileH, 0.0f, 0.0f, TileW * Ku, TileH * Ku);
			}
		}
	}
	if (UTexture2D* Tile = LoadSprite(TEXT("textures/gui/inworld_menu_list_background.png")))
	{
		for (float TileY = ListTop; TileY < ListBottom; TileY += 16.0f)
		{
			for (float TileX = 0.0f; TileX < Width; TileX += 16.0f)
			{
				const float TileW = FMath::Min(16.0f, Width - TileX);
				const float TileH = FMath::Min(16.0f, ListBottom - TileY);
				const float Ku = Tile->GetSizeX() / 16.0f;
				DrawRegion(Tile, TileX, TileY, TileW, TileH, 0.0f, 0.0f, TileW * Ku, TileH * Ku);
			}
		}
	}

	// The list.
	static const TArray<FStatRow> NoRows;
	const TArray<FStatRow>& Rows = StatsTab == 0 ? GetStatRows() : NoRows;
	const float Content = Rows.Num() * RowHeight + 10.0f;
	const float MaxScroll = FMath::Max(0.0f, Content - (ListBottom - ListTop));
	APlayerController* Player = GetOwningPlayerController();
	const AMcPlayerPawn* StatsPawn = Player ? Cast<AMcPlayerPawn>(Player->GetPawn()) : nullptr;
	const UMcPlayerDataComponent* Counted = StatsPawn ? StatsPawn->Data : nullptr;
	if (Player && GuiMouse.Y >= ListTop && GuiMouse.Y < ListBottom)
	{
		StatsScroll += 7 * ((Player->WasInputKeyJustPressed(EKeys::MouseScrollDown) ? 1 : 0) - (Player->WasInputKeyJustPressed(EKeys::MouseScrollUp) ? 1 : 0));
	}
	StatsScroll = FMath::Clamp(StatsScroll, 0, FMath::RoundToInt(MaxScroll));
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		const float Y = ListTop + 5.0f + Index * RowHeight - StatsScroll;
		if (Y + 8.0f <= ListTop || Y >= ListBottom)
		{
			continue;
		}
		const uint32 Colour = (Index & 1) ? 0xBABABAu : 0xFFFFFFu;
		DrawGuiText(Rows[Index].Name, CentreX - 135.6f, Y, Colour, true);
		const FString Value = FormatStat(Rows[Index].Id, Counted ? Counted->GetStat(TEXT("custom:") + Rows[Index].Id) : 0);
		DrawGuiText(Value, CentreX + 133.3f - GetTextWidth(Value), Y, Colour, true);
	}
	if (MaxScroll > 0.0f)
	{
		const float TrackH = ListBottom - ListTop;
		const float ThumbH = FMath::Clamp(TrackH * TrackH / Content, 32.0f, TrackH - 8.0f);
		const float ThumbY = ListTop + (TrackH - ThumbH) * StatsScroll / MaxScroll;
		const float BarX = CentreX + 147.7f;
		DrawFill(BarX, ListTop, 6.0f, TrackH, FLinearColor::Black);
		DrawFill(BarX, ThumbY, 6.0f, ThumbH, FLinearColor::FromSRGBColor(FColor(128, 128, 128)));
		DrawFill(BarX, ThumbY, 5.0f, ThumbH - 1.0f, FLinearColor::FromSRGBColor(FColor(192, 192, 192)));
	}

	for (const bool bHeader : { true, false })
	{
		if (UTexture2D* Line = LoadSprite(bHeader ? TEXT("textures/gui/inworld_header_separator.png") : TEXT("textures/gui/inworld_footer_separator.png")))
		{
			const float LineY = bHeader ? ListTop - 2.0f : ListBottom;
			const float Ku = Line->GetSizeX() / 32.0f;
			for (float LineX = 0.0f; LineX < Width; LineX += 32.0f)
			{
				const float LineW = FMath::Min(32.0f, Width - LineX);
				DrawRegion(Line, LineX, LineY, LineW, 2.0f, 0.0f, 0.0f, LineW * Ku, 2.0f * Ku);
			}
		}
	}

	// The tabs.
	const TCHAR* Labels[3] = { TEXT("stat.generalButton"), TEXT("stat.itemsButton"), TEXT("stat.mobsButton") };
	for (int32 Tab = 0; Tab < 3; ++Tab)
	{
		const float TabX = CentreX - 184.3f + Tab * 124.0f;
		const bool bSelected = Tab == StatsTab;
		const bool bOver = GuiMouse.X >= TabX && GuiMouse.X < TabX + 122.0f && GuiMouse.Y >= (bSelected ? 1.0f : 5.0f) && GuiMouse.Y < HeaderBottom;
		const FString Sprite = FString(TEXT("textures/gui/sprites/widget/tab")) + (bSelected ? TEXT("_selected") : TEXT("")) + (bOver ? TEXT("_highlighted") : TEXT("")) + TEXT(".png");
		DrawGuiSprite(Sprite, TabX, bSelected ? 1.0f : 5.0f, 122.0f, bSelected ? 22.0f : 18.0f);
		const FString Text = McLang::Get(Labels[Tab]);
		DrawCentredText(Text, TabX + 61.0f, bSelected ? 8.0f : 10.0f, bSelected ? White : Grey);
		if (bSelected)
		{
			const float W = GetTextWidth(Text) + 2.0f;
			DrawFill(TabX + 61.0f - W / 2.0f, 21.5f, W, 1.0f, FLinearColor::White);
		}
		if (bOver && bMenuClick)
		{
			StatsTab = Tab;
			StatsScroll = 0;
		}
	}

	if (MenuButton(McLang::Get(TEXT("gui.done")), CentreX - 100.0f, Height - 26.0f, 200.0f, 20.0f))
	{
		OpenMenu(EMenu::Pause);
	}
}

void AMcHUD::DrawAdvancements(int32 Width, int32 Height)
{
	DrawWorldMenuDim(Width, Height);
	LoadFont();
	const float CentreX = Width / 2.0f;
	// The window is the sprite's 252 x 140 corner, in the middle; its inner 234 x 113 is the black map, here with the game's
	// empty-state text (no advancement tree is drawn yet).
	const float WindowX = FMath::FloorToFloat(CentreX - 126.0f + 0.3f);
	const float WindowY = FMath::FloorToFloat((Height - 140.0f) / 2.0f);
	DrawCentredText(McLang::Get(TEXT("gui.advancements")), CentreX, 12.0f, White);
	if (UTexture2D* Window = LoadSprite(TEXT("textures/gui/advancements/window.png")))
	{
		const float Ku = Window->GetSizeX() / 256.0f;
		DrawRegion(Window, WindowX, WindowY, 252.0f, 140.0f, 0.0f, 0.0f, 252.0f * Ku, 140.0f * Ku);
	}
	DrawFill(WindowX + 9.0f, WindowY + 18.0f, 234.0f, 113.0f, FLinearColor::Black);
	DrawGuiText(McLang::Get(TEXT("gui.advancements")), WindowX + 8.0f, WindowY + 6.0f, 0xFFFFFFu, true);
	DrawCentredText(McLang::Get(TEXT("advancements.empty")), WindowX + 9.0f + 117.0f, WindowY + 18.0f + 56.5f - 4.0f, White);
	DrawCentredText(McLang::Get(TEXT("advancements.sad_label")), WindowX + 9.0f + 117.0f, WindowY + 18.0f + 113.0f - 9.0f, White);
	if (MenuButton(McLang::Get(TEXT("gui.done")), CentreX - 100.0f, Height - 26.0f, 200.0f, 20.0f))
	{
		OpenMenu(EMenu::Pause);
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
		DrawWorldMenuDim(Width, Height);
	}
	else
	{
		DrawMenuBackground(Width, Height, true);
	}
	const float CentreX = Width / 2.0f;
	const float Top = Height / 6.0f;
	const float Left = CentreX - 155.0f;
	const float Right = CentreX + 5.0f;
	DrawCentredText(McLang::Get(TEXT("options.title")), CentreX, 15.0f, White);

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
	// Quality, for Unreal Mode: cycles Low, Medium, High, Ultra; the FPS readout works in both looks.
	static const TCHAR* const QualityNames[4] = { TEXT("Quality: Low"), TEXT("Quality: Medium"), TEXT("Quality: High"), TEXT("Quality: Ultra") };
	if (MenuButton(QualityNames[FMath::Clamp(McRenderMode::GetQuality(), 0, 3)], Left, Top + 132.0f, 150.0f, 20.0f, bUnreal))
	{
		McRenderMode::SetQuality((McRenderMode::GetQuality() + 1) % 4);
		FMcClientOptions Options = FMcClientOptions::Load();
		Options.Quality = McRenderMode::GetQuality();
		Options.Save();
	}
	if (MenuButton(FMcClientOptions::Load().bShowSubtitles ? TEXT("Subtitles: ON") : TEXT("Subtitles: OFF"), Right, Top + 108.0f, 150.0f, 20.0f))
	{
		FMcClientOptions Options = FMcClientOptions::Load();
		Options.bShowSubtitles = !Options.bShowSubtitles;
		Options.Save();
	}
	if (MenuButton(FMcClientOptions::Load().bShowFps ? TEXT("FPS counter: ON") : TEXT("FPS counter: OFF"), Right, Top + 132.0f, 150.0f, 20.0f))
	{
		FMcClientOptions Options = FMcClientOptions::Load();
		Options.bShowFps = !Options.bShowFps;
		Options.Save();
		GetOwningPlayerController()->ConsoleCommand(TEXT("stat fps"));
	}
	// Ray tracing: greyed out, with the reason, without Unreal Mode or a card (and DX12) that traces rays.
	const bool bRaysCan = McRenderMode::IsRayTracingSupported();
	const bool bRays = McRenderMode::GetRayTracingSwitch();
	if (MenuButton(bRays ? TEXT("Ray Tracing: ON") : TEXT("Ray Tracing: OFF"), Left, Top + 156.0f, 150.0f, 20.0f, bUnreal && bRaysCan))
	{
		McRenderMode::SetRayTracing(!bRays);
		FMcClientOptions Options = FMcClientOptions::Load();
		Options.bRayTracing = McRenderMode::GetRayTracingSwitch();
		Options.Save();
	}
	DrawCentredText(!bUnreal ? TEXT("Ray Tracing needs Unreal Mode") : !bRaysCan ? TEXT("Ray Tracing: this card or graphics API can't trace rays (needs DX12)")
		: TEXT("Ray Tracing: hardware Lumen, ray traced shadows and reflections (heavy)"), CentreX, Top + 180.0f, Grey);
	DrawCentredText(bUnreal ? TEXT("Unreal Mode: Lumen lighting, real shadows, volumetric fog and bloom (F7 switches it)")
		: TEXT("Unreal Mode: off, Minecraft's own look (F7 switches it)"), CentreX, Top + 110.0f, Grey);
	DrawCentredText(!bUnreal ? TEXT("HD Textures need Unreal Mode") : bHd ? TEXT("HD Textures: sharper textures with bump, shine and depth")
		: TEXT("HD Textures: Minecraft's own textures"), CentreX, Top + 122.0f, Grey);

	if (MenuButton(McLang::Get(TEXT("options.resourcepack")), Right, Top + 156.0f, 150.0f, 20.0f))
	{
		OpenResourcePacks();
		return;
	}
	if (bPacksNeedRestart)
	{
		DrawCentredText(TEXT("Resource pack changes apply the next time the game starts"), CentreX, Top + 246.0f, Grey);
	}

	if (MenuButton(McLang::Get(TEXT("options.language")), CentreX - 100.0f, Top + 196.0f, 200.0f, 20.0f))
	{
		OpenLanguage();
		return;
	}
	if (MenuButton(McLang::Get(TEXT("gui.done")), CentreX - 100.0f, Top + 220.0f, 200.0f, 20.0f))
	{
		OpenMenu(OptionsBack);
	}
}

void AMcHUD::OpenResourcePacks()
{
	PacksAvailable.Reset();
	PacksSelected.Reset();
	PacksScrollAvailable = 0;
	PacksScrollSelected = 0;
	PacksSearch.Reset();

	auto MakeRow = [this](const FString& Name, const TSharedPtr<FMcResourcePack>& Pack, const FString& Error)
	{
		FPackRow Row;
		Row.Name = Name;
		Row.Title = Name;
		Row.Error = Error;
		const bool bBuiltIn = McPacks::IsBuiltIn(Name);
		if (bBuiltIn)
		{
			Row.Title = McLang::Get(TEXT("resourcePack.") + Name + TEXT(".name"));
		}
		if (Pack.IsValid())
		{
			Row.Description = Pack->GetInfo().Description;
			if (bBuiltIn)
			{
				Row.Description += TEXT(" (") + McLang::Get(TEXT("pack.source.builtin")) + TEXT(")");
			}
			switch (McPacks::GetCompatibility(Pack->GetInfo()))
			{
			case -1: Row.Warning = TEXT("(Made for an older version of Minecraft)"); break;
			case 1: Row.Warning = TEXT("(Made for a newer version of Minecraft)"); break;
			case 2: Row.Warning = TEXT("(Broken or incompatible)"); break;
			default: break;
			}
			TArray<uint8> Icon;
			if (Pack->GetInfo().bHasIcon && Pack->Read(TEXT("pack.png"), Icon) && LoadPackIcon(TEXT("pack:") + Name, Icon))
			{
				Row.IconKey = TEXT("pack:") + Name;
			}
		}
		return Row;
	};

	const TArray<McPacks::FFolderEntry> InFolder = McPacks::ScanFolder();
	TSet<FString> Taken;
	// The selection is kept lowest first; the screen lists the topmost first.
	const TArray<FString> Chosen = McPacks::GetSelectedNames();
	for (int32 Index = Chosen.Num() - 1; Index >= 0; --Index)
	{
		const FString& Name = Chosen[Index];
		Taken.Add(Name);
		const McPacks::FFolderEntry* Found = InFolder.FindByPredicate([&Name](const McPacks::FFolderEntry& Entry) { return Entry.Name == Name; });
		if (Found)
		{
			PacksSelected.Add(MakeRow(Name, Found->Pack, Found->Error));
		}
		else
		{
			// Named with -McPacks as a path somewhere else.
			FString Error;
			const TSharedPtr<FMcResourcePack> Pack = FMcResourcePack::Open(McPacks::ResolveName(Name), &Error);
			PacksSelected.Add(MakeRow(Name, Pack, Error));
		}
	}
	for (const McPacks::FFolderEntry& Entry : InFolder)
	{
		if (!Taken.Contains(Entry.Name))
		{
			PacksAvailable.Add(MakeRow(Entry.Name, Entry.Pack, Entry.Error));
		}
	}
	// The game's own assets: always selected, always at the bottom.
	FPackRow Default;
	Default.Name = TEXT("vanilla");
	Default.Title = McLang::Get(TEXT("resourcePack.vanilla.name"));
	Default.Description = McLang::Get(TEXT("resourcePack.vanilla.description")) + TEXT(" (") + McLang::Get(TEXT("pack.source.builtin")) + TEXT(")");
	Default.bFixed = true;
	TArray<uint8> DefaultIcon;
	if (FFileHelper::LoadFileToArray(DefaultIcon, *(McCache::Root() / TEXT("client/pack.png"))) && LoadPackIcon(TEXT("pack:vanilla"), DefaultIcon))
	{
		Default.IconKey = TEXT("pack:vanilla");
	}
	PacksSelected.Add(Default);
	OpenMenu(EMenu::ResourcePacks);
}

void AMcHUD::OpenLanguage()
{
	LanguageSearch.Reset();
	bLanguageScrollSet = false;
	OpenMenu(EMenu::Language);
}

void AMcHUD::DrawLanguage(int32 Width, int32 Height)
{
	if (OptionsBack == EMenu::Pause)
	{
		DrawWorldMenuDim(Width, Height);
	}
	else
	{
		DrawMenuBackground(Width, Height, true);
	}
	LoadFont();
	const float CentreX = Width / 2.0f;
	DrawCentredText(McLang::Get(TEXT("options.language.title")), CentreX, 4.0f, White);
	MenuTextField(LanguageSearch, 0, CentreX - 100.0f, 17.0f, 200.0f, 15.0f, 40);

	// Fitted from the reference's screen at 1280 x 720 (GUI scale 3): the list from y 36 to 188 (a line above and below), rows 18
	// apart and 270 wide, the chosen one outlined and kept in view, a 6 wide scroll bar 8 to the right of the rows.
	constexpr float RowHeight = 18.0f;
	constexpr float RowWidth = 270.0f;
	const float ListTop = 36.0f;
	const float ListBottom = Height - 52.0f;
	const bool bInWorld = OptionsBack == EMenu::Pause;

	if (UTexture2D* Tile = LoadSprite(bInWorld ? TEXT("textures/gui/inworld_menu_list_background.png") : TEXT("textures/gui/menu_list_background.png")))
	{
		for (float TileY = ListTop; TileY < ListBottom; TileY += 16.0f)
		{
			for (float TileX = 0.0f; TileX < Width; TileX += 16.0f)
			{
				const float TileW = FMath::Min(16.0f, Width - TileX);
				const float TileH = FMath::Min(16.0f, ListBottom - TileY);
				const float Ku = Tile->GetSizeX() / 16.0f;
				DrawRegion(Tile, TileX, TileY, TileW, TileH, 0.0f, 0.0f, TileW * Ku, TileH * Ku);
			}
		}
	}

	const FString Chosen = McLang::GetLanguage();
	TArray<McLang::FLanguage> Shown;
	int32 ChosenAt = INDEX_NONE;
	for (const McLang::FLanguage& Language : McLang::GetLanguages())
	{
		if (LanguageSearch.IsEmpty() || Language.GetLabel().Contains(LanguageSearch, ESearchCase::IgnoreCase))
		{
			if (Language.Code == Chosen)
			{
				ChosenAt = Shown.Num();
			}
			Shown.Add(Language);
		}
	}
	// The chosen row shows at the same height as the game does (clamped where the list is too short to scroll that far).
	const float Content = Shown.Num() * RowHeight;
	const float MaxScroll = FMath::Max(0.0f, Content - (ListBottom - ListTop));
	if (!bLanguageScrollSet)
	{
		bLanguageScrollSet = true;
		LanguageScroll = ChosenAt == INDEX_NONE ? 0 : FMath::RoundToInt(ChosenAt * RowHeight - 72.0f);
	}
	APlayerController* Player = GetOwningPlayerController();
	const bool bInList = GuiMouse.Y >= ListTop && GuiMouse.Y < ListBottom;
	if (bInList && Player)
	{
		LanguageScroll += FMath::RoundToInt(RowHeight) * ((Player->WasInputKeyJustPressed(EKeys::MouseScrollDown) ? 1 : 0) - (Player->WasInputKeyJustPressed(EKeys::MouseScrollUp) ? 1 : 0));
	}
	LanguageScroll = FMath::Clamp(LanguageScroll, 0, FMath::RoundToInt(MaxScroll));

	const float RowX = CentreX - RowWidth / 2.0f;
	FString Picked;
	for (int32 Slot = 0; Slot < Shown.Num(); ++Slot)
	{
		const float Y = ListTop - 2.3f + Slot * RowHeight - LanguageScroll;
		if (Y + RowHeight <= ListTop || Y >= ListBottom)
		{
			continue;
		}
		const McLang::FLanguage& Language = Shown[Slot];
		if (Language.Code == Chosen)
		{
			DrawFill(RowX, Y + 1.0f, RowWidth, 17.0f, FLinearColor::FromSRGBColor(FColor(128, 128, 128)));
			DrawFill(RowX + 1.0f, Y + 2.0f, RowWidth - 2.0f, 15.0f, FLinearColor::Black);
		}
		DrawCentredText(Language.GetLabel(), CentreX, Y + 5.0f, White);
		if (bMenuClick && GuiMouse.X >= RowX && GuiMouse.X < RowX + RowWidth && GuiMouse.Y >= FMath::Max(Y, ListTop) && GuiMouse.Y < FMath::Min(Y + RowHeight, ListBottom))
		{
			Picked = Language.Code;
		}
	}
	if (MaxScroll > 0.0f)
	{
		const float TrackH = ListBottom - ListTop;
		const float ThumbH = FMath::Clamp(TrackH * TrackH / Content, 32.0f, TrackH - 8.0f);
		const float ThumbY = ListTop + (TrackH - ThumbH) * LanguageScroll / MaxScroll;
		const float BarX = CentreX + RowWidth / 2.0f + 8.0f;
		DrawFill(BarX, ListTop, 6.0f, TrackH, FLinearColor::Black);
		DrawFill(BarX, ThumbY, 6.0f, ThumbH, FLinearColor::FromSRGBColor(FColor(128, 128, 128)));
		DrawFill(BarX, ThumbY, 5.0f, ThumbH - 1.0f, FLinearColor::FromSRGBColor(FColor(192, 192, 192)));
	}

	for (const bool bHeader : { true, false })
	{
		const TCHAR* Path = bHeader ? (bInWorld ? TEXT("textures/gui/inworld_header_separator.png") : TEXT("textures/gui/header_separator.png"))
			: (bInWorld ? TEXT("textures/gui/inworld_footer_separator.png") : TEXT("textures/gui/footer_separator.png"));
		if (UTexture2D* Line = LoadSprite(Path))
		{
			const float LineY = bHeader ? ListTop - 2.0f : ListBottom;
			const float Ku = Line->GetSizeX() / 32.0f;
			for (float LineX = 0.0f; LineX < Width; LineX += 32.0f)
			{
				const float LineW = FMath::Min(32.0f, Width - LineX);
				DrawRegion(Line, LineX, LineY, LineW, 2.0f, 0.0f, 0.0f, LineW * Ku, 2.0f * Ku);
			}
		}
	}
	DrawCentredText(McLang::Get(TEXT("options.languageAccuracyWarning")).Replace(TEXT("%%"), TEXT("%")), CentreX, ListBottom + 7.0f, Grey);

	// The game's second button here is Font Settings, which this game has no screen for: shown, and off.
	MenuButton(McLang::Get(TEXT("options.font")), CentreX - 154.0f, Height - 28.0f, 150.0f, 20.0f, false);
	if (MenuButton(McLang::Get(TEXT("gui.done")), CentreX + 4.0f, Height - 28.0f, 150.0f, 20.0f))
	{
		OpenMenu(EMenu::Options);
		return;
	}
	if (!Picked.IsEmpty() && Picked != Chosen)
	{
		McLang::SetLanguage(Picked);
	}
}

void AMcHUD::DrawWorldMenuDim(int32 Width, int32 Height)
{
	// Measured on the game's Options screen over a flat scene: every colour, at every height and side, is 0.748 of what the
	// plain view shows (grass 85,110,55 -> 63,82,41; wool 232,235,237 -> 174,176,177).
	DrawFill(0, 0, static_cast<float>(Width), static_cast<float>(Height), FLinearColor(0.0f, 0.0f, 0.0f, 0.252f));
}

void AMcHUD::UpdateMenuBlur(bool bOn)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!MenuBlur.IsValid())
	{
		if (!bOn)
		{
			return;
		}
		// Depth of field with the focus right in front of the camera, so everything beyond is "out of focus" and the whole
		// picture blurs; the HUD is drawn after it and stays sharp. The game blurs with three box passes each way (an edge
		// goes from sharp to soft over about 12 pixels at 1280 x 720). With a 24.576 mm sensor and the 70 degree view the
		// lens is about 17.5 mm; focused at 300 mm, an aperture of f/0.8 turns a distant point into a disc 1.4 mm across,
		// 5.7% of the width. -McMenuBlur=<f-stop> tunes it (a smaller number blurs more).
		APostProcessVolume* Volume = World->SpawnActor<APostProcessVolume>();
		Volume->bUnbound = true;
		Volume->Priority = 50.0f;
		float FStop = 0.8f;
		FParse::Value(FCommandLine::Get(), TEXT("-McMenuBlur="), FStop);
		FPostProcessSettings& Blur = Volume->Settings;
		Blur.bOverride_DepthOfFieldEnabled = true;
		Blur.DepthOfFieldEnabled = true;
		Blur.bOverride_DepthOfFieldSensorWidth = true;
		Blur.DepthOfFieldSensorWidth = 24.576f;
		Blur.bOverride_DepthOfFieldFocalDistance = true;
		Blur.DepthOfFieldFocalDistance = 30.0f;
		Blur.bOverride_DepthOfFieldFstop = true;
		Blur.DepthOfFieldFstop = FStop;
		Blur.bOverride_DepthOfFieldMinFstop = true;
		Blur.DepthOfFieldMinFstop = FStop;
		MenuBlur = Volume;
	}
	MenuBlur->bEnabled = bOn;
}

void AMcHUD::ClearPackCaches()
{
	// The menu pictures come from the packs too.
	Sprites.Empty();
	LayerTextures.Empty();
	GuiScalings.Empty();
	Glyphs.Reset();
	RasterFonts.Reset();
	RasterAsked.Reset();
	OtherFonts.Empty();
	GlyphTextures.Reset();
	GalacticWidths.Reset();
	McFont::ClearCache();
	bSplashChosen = false;
}

// F3+T: the packs are read again from disk, as the game's reload does; the pictures, models and sounds follow the new
// generation. A world holds baked models and a texture array, so a world is opened again from its save (the player and
// the chunks are saved on leaving the map); a game joined to a server has to be joined again.
void AMcHUD::ReloadResources()
{
	TArray<FString> Names = McPacks::GetSelectedNames();
#if !UE_BUILD_SHIPPING
	// Development: -McReloadPacks=a,b makes the reload pick these packs (a pack folder edited between runs, in a script).
	FString DevList;
	if (FParse::Value(FCommandLine::Get(), TEXT("-McReloadPacks="), DevList, /*bShouldStopOnSeparator=*/false))
	{
		Names = McPacks::ParseList(DevList);
	}
#endif
	McPacks::SetPacks(Names);
	UE_LOG(LogMcUI, Display, TEXT("Reloading resource packs: %s"), *FString::Join(Names, TEXT(", ")));
	ClearPackCaches();
	FMcBlockModels::Get().Unload();
	FMcBlockTextures::Get().Unload();
	FMcHdBlocks::Get().Unload();
	UWorld* World = GetWorld();
	if (!World || McLaunch::IsMenu(World))
	{
		return;
	}
	if (World->GetNetMode() == NM_Standalone)
	{
		// The options of this run: the URL's, or none where the world came from the command line, which is read again.
		FString Options;
		for (const FString& Option : World->URL.Op)
		{
			Options += TEXT("?") + Option;
		}
		OpenMenu(EMenu::None);
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true, Options.RightChop(1));
	}
	else
	{
		bPacksNeedRestart = true;
	}
}

void AMcHUD::ApplyResourcePacks()
{
	TArray<FString> Names;
	for (int32 Index = PacksSelected.Num() - 1; Index >= 0; --Index)
	{
		if (!PacksSelected[Index].bFixed)
		{
			Names.Add(PacksSelected[Index].Name);
		}
	}
	if (Names == McPacks::GetSelectedNames())
	{
		return;
	}
	McPacks::ApplySelection(Names);
	ClearPackCaches();
	if (OptionsBack == EMenu::Pause)
	{
		// A world is using the models and the texture array.
		bPacksNeedRestart = true;
		return;
	}
	FMcBlockModels::Get().Unload();
	FMcBlockTextures::Get().Unload();
	FMcHdBlocks::Get().Unload();
}

void AMcHUD::DrawResourcePacks(int32 Width, int32 Height)
{
	if (OptionsBack == EMenu::Pause)
	{
		DrawWorldMenuDim(Width, Height);
	}
	else
	{
		DrawMenuBackground(Width, Height, true);
	}
	LoadFont();
	const float CentreX = Width / 2.0f;
	DrawCentredText(McLang::Get(TEXT("resourcePack.title")), CentreX, 4.0f, White);
	DrawCentredText(McLang::Get(TEXT("pack.dropInfo")), CentreX, 17.0f, Grey);
	MenuTextField(PacksSearch, 0, CentreX - 100.0f, 29.0f, 200.0f, 15.0f, 40);

	// Two lists as wide as half the screen less a gap, between a header and a footer line.
	const float ListWidth = CentreX - 15.0f;
	constexpr float RowHeight = 36.0f;
		// Fitted from the reference's screen at 1280 x 720 (GUI scale 3): list top 49 and bottom 208 of 240, header text at +4,
	// first row's box at +16 (34 high, pitch 36), a 6 wide scroll bar on the list's right edge where the rows overflow.
	const float ListTop = 49.0f;
	const float ListBottom = Height - 32.0f;
	const bool bInWorld = OptionsBack == EMenu::Pause;
	const int32 Visible = FMath::Max(1, FMath::FloorToInt((ListBottom - ListTop - 16.0f - 34.0f) / RowHeight) + 1);

	enum class EOp : uint8 { None, Select, Unselect, Up, Down };
	EOp Op = EOp::None;
	int32 OpIndex = INDEX_NONE;

	APlayerController* Player = GetOwningPlayerController();
	const bool bWheelUp = Player && Player->WasInputKeyJustPressed(EKeys::MouseScrollUp);
	const bool bWheelDown = Player && Player->WasInputKeyJustPressed(EKeys::MouseScrollDown);

	auto Clip = [this](const FString& Text, float MaxWidth)
	{
		if (TextWidth(Text) <= MaxWidth)
		{
			return Text;
		}
		FString Cut = Text;
		while (Cut.Len() > 1 && TextWidth(Cut + TEXT("...")) > MaxWidth)
		{
			Cut.LeftChopInline(1);
		}
		return Cut + TEXT("...");
	};

	auto DrawList = [&](const TArray<FPackRow>& Rows, bool bSelected, float X, int32& Scroll)
	{
		// The list's own background, the picture tiled, with the header's title in bold and underlined.
		if (UTexture2D* Tile = LoadSprite(bInWorld ? TEXT("textures/gui/inworld_menu_list_background.png") : TEXT("textures/gui/menu_list_background.png")))
		{
			for (float TileY = ListTop; TileY < ListBottom; TileY += 16.0f)
			{
				for (float TileX = X; TileX < X + ListWidth; TileX += 16.0f)
				{
					const float TileW = FMath::Min(16.0f, X + ListWidth - TileX);
					const float TileH = FMath::Min(16.0f, ListBottom - TileY);
					const float Ku = Tile->GetSizeX() / 16.0f;
					DrawRegion(Tile, TileX, TileY, TileW, TileH, 0.0f, 0.0f, TileW * Ku, TileH * Ku);
				}
			}
		}
		const FString Header = bSelected ? TEXT("Selected") : TEXT("Available");
		const float HeaderX = FMath::RoundToFloat(X + ListWidth / 2.0f - TextWidth(Header) / 2.0f);
		DrawGuiText(Header, HeaderX, ListTop + 4.0f, White, true);
		DrawGuiText(Header, HeaderX + 1.0f, ListTop + 4.0f, White, false);
		DrawFill(HeaderX, ListTop + 13.0f, TextWidth(Header) + 1.0f, 1.0f, FLinearColor::White);
		TArray<int32> Shown;
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			if (PacksSearch.IsEmpty() || Rows[Index].Title.Contains(PacksSearch, ESearchCase::IgnoreCase))
			{
				Shown.Add(Index);
			}
		}
		const bool bScrollBar = Shown.Num() > Visible;
		const bool bInList = GuiMouse.X >= X && GuiMouse.X < X + ListWidth && GuiMouse.Y >= ListTop && GuiMouse.Y < ListBottom;
		if (bInList)
		{
			Scroll += (bWheelDown ? 1 : 0) - (bWheelUp ? 1 : 0);
		}
		Scroll = FMath::Clamp(Scroll, 0, FMath::Max(0, Shown.Num() - Visible));
		if (Rows.IsEmpty() && !bSelected)
		{
			DrawCentredText(TEXT("Put pack zips or folders in:"), X + ListWidth / 2.0f, ListTop + 24.0f, Grey);
			DrawCentredText(Clip(McPacks::GetFolder(), ListWidth - 8.0f), X + ListWidth / 2.0f, ListTop + 36.0f, DarkGrey);
		}
		if (bScrollBar)
		{
			// A black track, the thumb light grey with a darker bottom and right edge.
			const float TrackH = ListBottom - ListTop;
			const float ThumbH = FMath::Clamp(TrackH * TrackH / (Shown.Num() * RowHeight + 16.0f), 32.0f, TrackH - 8.0f);
			const float MaxScroll = static_cast<float>(FMath::Max(1, Shown.Num() - Visible));
			const float ThumbY = ListTop + (TrackH - ThumbH) * Scroll / MaxScroll;
			const float BarX = X + ListWidth - 6.0f;
			DrawFill(BarX, ListTop, 6.0f, TrackH, FLinearColor::Black);
			DrawFill(BarX, ThumbY, 6.0f, ThumbH, FLinearColor::FromSRGBColor(FColor(128, 128, 128)));
			DrawFill(BarX, ThumbY, 5.0f, ThumbH - 1.0f, FLinearColor::FromSRGBColor(FColor(192, 192, 192)));
		}
		for (int32 Slot = Scroll; Slot < Shown.Num() && Slot < Scroll + Visible; ++Slot)
		{
			const int32 Index = Shown[Slot];
			const FPackRow& Row = Rows[Index];
			const float Y = ListTop + 17.0f + (Slot - Scroll) * RowHeight;
			const bool bOver = GuiMouse.X >= X && GuiMouse.X < X + ListWidth && GuiMouse.Y >= Y - 2.0f && GuiMouse.Y < Y + RowHeight - 2.0f;
			UTexture2D* Icon = Row.IconKey.IsEmpty() ? nullptr : Sprites.FindRef(Row.IconKey).Get();
			if (!Icon)
			{
				Icon = LoadSprite(TEXT("textures/misc/unknown_pack.png"));
			}
			const float BoxX = X + (bScrollBar ? 1.0f : 2.5f);
			const float IconX = BoxX + 1.0f;
			if (!Row.Warning.IsEmpty() || !Row.Error.IsEmpty())
			{
				// Packs the game calls incompatible or broken have a dark red box.
				DrawFill(BoxX, Y - 1.0f, ListWidth - 4.0f - (bScrollBar ? 6.0f : 0.0f), 34.0f, FLinearColor::FromSRGBColor(FColor(119, 0, 0)));
			}
			if (Icon)
			{
				DrawRegion(Icon, IconX, Y, 32.0f, 32.0f, 0.0f, 0.0f, static_cast<float>(Icon->GetSizeX()), static_cast<float>(Icon->GetSizeY()));
			}
			const bool bUsable = Row.Error.IsEmpty();
			const float TextX = IconX + 34.0f;
			const float TextWidthMax = ListWidth - 32.0f - 12.0f - (bScrollBar ? 6.0f : 0.0f);
			DrawGuiText(Clip(Row.Title, TextWidthMax), TextX, Y + 1.0f, bUsable ? White : Red, true);
			if (bUsable && !Row.Warning.IsEmpty() && bOver)
			{
				DrawGuiText(TEXT("Incompatible"), TextX, Y + 12.0f, Red, true);
				DrawGuiText(Clip(Row.Warning, TextWidthMax), TextX, Y + 22.0f, Red, true);
			}
			else if (bUsable)
			{
				const TArray<FString> Lines = WrapText(Row.Description.Replace(TEXT("\n"), TEXT(" ")), TextWidthMax);
				for (int32 Line = 0; Line < 2 && Line < Lines.Num(); ++Line)
				{
					const FString Text = (Line == 1 && Lines.Num() > 2) ? Clip(Lines[Line] + TEXT("..."), TextWidthMax) : Lines[Line];
					DrawGuiText(Text, TextX, Y + 12.0f + Line * 9.0f, DarkGrey, true);
				}
			}
			else
			{
				DrawGuiText(Clip(Row.Error, TextWidthMax), TextX, Y + 12.0f, DarkGrey, true);
			}
			if (bOver && bUsable && !Row.bFixed)
			{
				// The arrows over the icon: left half takes it out of the list it is in, right half moves it up or down.
				const float RelX = static_cast<float>(GuiMouse.X) - IconX;
				const float RelY = static_cast<float>(GuiMouse.Y) - Y;
				const bool bOnIcon = RelX >= 0.0f && RelX < 32.0f && RelY >= 0.0f && RelY < 32.0f;
				DrawFill(IconX, Y, 32.0f, 32.0f, FLinearColor(0.56f, 0.56f, 0.56f, 0.63f));
				auto Arrow = [&](const TCHAR* Name, bool bHot)
				{
					const FString Path = FString::Printf(TEXT("textures/gui/sprites/transferable_list/%s%s.png"), Name, bHot ? TEXT("_highlighted") : TEXT(""));
					if (UTexture2D* Sprite = LoadSprite(Path))
					{
						DrawRegion(Sprite, IconX, Y, 32.0f, 32.0f, 0.0f, 0.0f, static_cast<float>(Sprite->GetSizeX()), static_cast<float>(Sprite->GetSizeY()));
					}
				};
				if (!bSelected)
				{
					Arrow(TEXT("select"), bOnIcon);
					if (bOnIcon && bMenuClick)
					{
						Op = EOp::Select;
						OpIndex = Index;
					}
				}
				else
				{
					const bool bLeft = bOnIcon && RelX < 16.0f;
					const bool bUp = bOnIcon && RelX >= 16.0f && RelY < 16.0f && Index > 0;
					const bool bDown = bOnIcon && RelX >= 16.0f && RelY >= 16.0f && Index < Rows.Num() - 2;
					Arrow(TEXT("unselect"), bLeft);
					if (Index > 0)
					{
						Arrow(TEXT("move_up"), bUp);
					}
					if (Index < Rows.Num() - 2)
					{
						Arrow(TEXT("move_down"), bDown);
					}
					if (bMenuClick)
					{
						OpIndex = Index;
						Op = bLeft ? EOp::Unselect : bUp ? EOp::Up : bDown ? EOp::Down : EOp::None;
					}
				}
			}
		}
	};

	DrawList(PacksAvailable, false, 0.0f, PacksScrollAvailable);
	// The game centres on a whole GUI width (427 here, not 426.67): the right list is 0.67 further right in the reference.
	DrawList(PacksSelected, true, CentreX + 15.67f, PacksScrollSelected);
	// The lines above and below the lists.
	for (const bool bHeader : { true, false })
	{
		const TCHAR* Path = bHeader ? (bInWorld ? TEXT("textures/gui/inworld_header_separator.png") : TEXT("textures/gui/header_separator.png"))
			: (bInWorld ? TEXT("textures/gui/inworld_footer_separator.png") : TEXT("textures/gui/footer_separator.png"));
		if (UTexture2D* Line = LoadSprite(Path))
		{
			const float LineY = bHeader ? ListTop - 2.0f : ListBottom;
			const float Ku = Line->GetSizeX() / 32.0f;
			for (float LineX = 0.0f; LineX < Width; LineX += 32.0f)
			{
				const float LineW = FMath::Min(32.0f, Width - LineX);
				DrawRegion(Line, LineX, LineY, LineW, 2.0f, 0.0f, 0.0f, LineW * Ku, 2.0f * Ku);
			}
		}
	}

	switch (Op)
	{
	case EOp::Select:
		if (!PacksAvailable[OpIndex].Warning.IsEmpty() || !PacksAvailable[OpIndex].Error.IsEmpty())
		{
			PendingSelect = OpIndex;
			ActiveMenu = EMenu::ConfirmIncompatible;
			break;
		}
		PacksSelected.Insert(PacksAvailable[OpIndex], 0);
		PacksAvailable.RemoveAt(OpIndex);
		break;
	case EOp::Unselect:
		{
			// Back into the available list, which is kept by name.
			const FPackRow Row = PacksSelected[OpIndex];
			PacksSelected.RemoveAt(OpIndex);
			int32 At = 0;
			while (At < PacksAvailable.Num() && PacksAvailable[At].Name.Compare(Row.Name, ESearchCase::IgnoreCase) < 0)
			{
				++At;
			}
			PacksAvailable.Insert(Row, At);
		}
		break;
	case EOp::Up:
		PacksSelected.Swap(OpIndex, OpIndex - 1);
		break;
	case EOp::Down:
		PacksSelected.Swap(OpIndex, OpIndex + 1);
		break;
	default:
		break;
	}

	const float ButtonY = Height - 26.0f;
	if (MenuButton(McLang::Get(TEXT("pack.openFolder")), CentreX - 154.0f, ButtonY, 150.0f, 20.0f))
	{
		IFileManager::Get().MakeDirectory(*McPacks::GetFolder(), true);
		FPlatformProcess::ExploreFolder(*McPacks::GetFolder());
	}
	if (MenuButton(McLang::Get(TEXT("gui.done")), CentreX + 4.0f, ButtonY, 150.0f, 20.0f))
	{
		ApplyResourcePacks();
		OpenMenu(EMenu::Options);
	}
}

void AMcHUD::DrawConfirmIncompatible(int32 Width, int32 Height)
{
	if (OptionsBack == EMenu::Pause)
	{
		DrawWorldMenuDim(Width, Height);
	}
	else
	{
		DrawMenuBackground(Width, Height, true);
	}
	LoadFont();
	const float CentreX = Width / 2.0f;
	if (!PacksAvailable.IsValidIndex(PendingSelect))
	{
		ActiveMenu = EMenu::ResourcePacks;
		return;
	}
	// The game's lang strings, by how the pack is incompatible.
	const FPackRow& Row = PacksAvailable[PendingSelect];
	const bool bOld = Row.Warning.Contains(TEXT("older")), bNew = Row.Warning.Contains(TEXT("newer"));
	DrawCentredText(McLang::Get(TEXT("pack.incompatible.confirm.title")), CentreX, 70.0f, White);
	const FString Message = bOld ? TEXT("This pack was made for an older version of Minecraft and may no longer work correctly.")
		: bNew ? TEXT("This pack was made for a newer version of Minecraft and may not work correctly.")
		: TEXT("This pack is broken or made for an unknown version of Minecraft and may not work correctly.");
	const TArray<FString> Lines = WrapText(Message, 300.0f);
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		DrawCentredText(Lines[Index], CentreX, 90.0f + 9.0f * Index, Grey);
	}
	const float ButtonY = Height / 6.0f + 96.0f + 9.0f * Lines.Num();
	if (MenuButton(McLang::Get(TEXT("gui.yes")), CentreX - 155.0f, ButtonY, 150.0f, 20.0f))
	{
		PacksSelected.Insert(PacksAvailable[PendingSelect], 0);
		PacksAvailable.RemoveAt(PendingSelect);
		PendingSelect = INDEX_NONE;
		ActiveMenu = EMenu::ResourcePacks;
	}
	else if (MenuButton(McLang::Get(TEXT("gui.no")), CentreX + 5.0f, ButtonY, 150.0f, 20.0f))
	{
		PendingSelect = INDEX_NONE;
		ActiveMenu = EMenu::ResourcePacks;
	}
}

void AMcHUD::OnFilesDropped(const TArray<FString>& Paths)
{
	if (ActiveMenu != EMenu::ResourcePacks)
	{
		return;
	}
	PendingDrop = McPacks::SortDropped(Paths);
	DropFailed.Reset();
	if (PendingDrop.Packs.Num() > 0)
	{
		ActiveMenu = EMenu::ConfirmPacks;
	}
}

void AMcHUD::DrawConfirmPacks(int32 Width, int32 Height)
{
	DrawMenuBackground(Width, Height, true);
	const float CentreX = Width / 2.0f;
	const float ButtonY = Height / 6.0f + 96.0f + 9.0f * PendingDrop.Names.Num();
	if (DropFailed.Num() > 0)
	{
		DrawCentredText(McLang::Get(TEXT("pack.copyFailure")), CentreX, 70.0f, White);
		for (int32 Index = 0; Index < DropFailed.Num(); ++Index)
		{
			DrawCentredText(DropFailed[Index], CentreX, 90.0f + 9.0f * Index, Grey);
		}
		if (MenuButton(McLang::Get(TEXT("gui.back")), CentreX - 75.0f, 100.0f + 9.0f * DropFailed.Num(), 150.0f, 20.0f))
		{
			DropFailed.Reset();
			PendingDrop = McPacks::FDrop();
			OpenResourcePacks();
		}
		return;
	}
	DrawCentredText(McLang::Get(TEXT("pack.dropConfirm")), CentreX, 70.0f, White);
	for (int32 Index = 0; Index < PendingDrop.Names.Num(); ++Index)
	{
		DrawCentredText(PendingDrop.Names[Index], CentreX, 90.0f + 9.0f * Index, Grey);
	}
	if (MenuButton(McLang::Get(TEXT("gui.yes")), CentreX - 155.0f, ButtonY, 150.0f, 20.0f))
	{
		// What was picked on the screen so far stays picked when the lists are read afresh.
		ApplyResourcePacks();
		McPacks::CopyDropped(PendingDrop, DropFailed);
		if (DropFailed.Num() == 0)
		{
			PendingDrop = McPacks::FDrop();
			OpenResourcePacks();
		}
	}
	else if (MenuButton(McLang::Get(TEXT("gui.no")), CentreX + 5.0f, ButtonY, 150.0f, 20.0f))
	{
		PendingDrop = McPacks::FDrop();
		ActiveMenu = EMenu::ResourcePacks;
	}
}

void AMcHUD::ApplyOptions()
{
	bSettingsApplied = true;
	if (APlayerController* Controller = GetOwningPlayerController())
	{
		// The game's near plane is 0.05 blocks; the engine's 10 units would cut the hand held up to the camera, and the drawn bow's string comes within a unit or two of it.
		Controller->ConsoleCommand(TEXT("r.SetNearClipPlane 1"));
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
	if (McRenderMode::GetHdSwitch() && !McPacks::GetUsePbr())
	{
		// HD Textures bring a pack's _n/_s maps; the textures already loaded were read without them.
		McPacks::SetUsePbr(true);
		if (McPacks::HasPacks())
		{
			if (OptionsBack == EMenu::Pause)
			{
				bPacksNeedRestart = true;
			}
			else
			{
				FMcBlockModels::Get().Unload();
				FMcBlockTextures::Get().Unload();
				FMcHdBlocks::Get().Unload();
			}
		}
	}
	FMcClientOptions Options = FMcClientOptions::Load();
	Options.bHdTextures = McRenderMode::GetHdSwitch();
	Options.Save();
}
