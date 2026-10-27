// The parts of the HUD that use the player's data: the bars, the hotbar with its items, and the container screens
// with their mouse and key handling. The sprites are the jar's own (textures/gui/sprites, textures/gui/container).

#include "McHUD.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "InputCoreTypes.h"
#include "McCache.h"
#include "McContainer.h"
#include "McCraftingMenu.h"
#include "McFurnace.h"
#include "McMenuInput.h"
#include "McPlayerData.h"
#include "McPlayerPawn.h"
#include "McScreenLayout.h"
#include "McUI.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

struct AMcHUD::FScreenState
{
	int32 WindowId = -1;
	TSharedPtr<FMcMenuInput> Input;
	McGui::FScreenLayout Layout;
	TSharedPtr<FMcFurnace> Furnace;
	FString Title;
};

namespace
{
	const TCHAR* SpriteRoot = TEXT("textures/gui/sprites/");

	// "oak_log" -> "Oak Log".
	FString TitleCase(const FString& Path)
	{
		FString Out;
		bool bStart = true;
		for (const TCHAR Char : Path)
		{
			Out.AppendChar(Char == TEXT('_') ? TEXT(' ') : bStart ? FChar::ToUpper(Char) : Char);
			bStart = Char == TEXT('_');
		}
		return Out;
	}

	FString GetItemName(const FMcItemStack& Stack)
	{
		const FString Custom = Stack.GetCustomName();
		return Custom.IsEmpty() ? TitleCase(McCache::PathOf(Stack.Item.ToString())) : Custom;
	}

	FLinearColor FromRgb(uint32 Rgb, float Alpha = 1.0f)
	{
		FLinearColor Color(FColor((Rgb >> 16) & 0xFF, (Rgb >> 8) & 0xFF, Rgb & 0xFF));
		Color.A = Alpha;
		return Color;
	}
}

AMcHUD::AMcHUD() = default;
AMcHUD::~AMcHUD() = default;

void AMcHUD::DrawRegion(UTexture2D* Texture, float X, float Y, float Width, float Height, float U, float V, float UW, float VH, const FLinearColor& Tint)
{
	if (!Texture || !Texture->GetResource())
	{
		return;
	}
	const float TexW = static_cast<float>(Texture->GetSizeX());
	const float TexH = static_cast<float>(Texture->GetSizeY());
	FCanvasTileItem Tile(FVector2D(X, Y) * Scale, Texture->GetResource(), FVector2D(Width, Height) * Scale, FVector2D(U / TexW, V / TexH), FVector2D((U + UW) / TexW, (V + VH) / TexH), Tint);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void AMcHUD::DrawFill(float X, float Y, float Width, float Height, const FLinearColor& Color)
{
	DrawRect(Color, X * Scale, Y * Scale, Width * Scale, Height * Scale);
}

// ---- Text ----

void AMcHUD::LoadFont()
{
	if (GlyphWidths.Num() > 0)
	{
		return;
	}
	GlyphWidths.Init(0, 256);
	// A glyph is as wide as its rightmost pixel; a space is 3 and the gap after each glyph is 1.
	TArray<uint8> File;
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	TArray64<uint8> Raw;
	if (!FFileHelper::LoadFileToArray(File, *McCache::Asset(TEXT("textures/font/ascii.png"))) || !Wrapper.IsValid() || !Wrapper->SetCompressed(File.GetData(), File.Num())
		|| !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw) || Wrapper->GetWidth() != 128 || Wrapper->GetHeight() != 128)
	{
		UE_LOG(LogMcUI, Warning, TEXT("Can't read the font"));
		GlyphWidths.Init(5, 256);
		return;
	}
	for (int32 Code = 0; Code < 256; ++Code)
	{
		int32 Widest = 0;
		for (int32 Column = 0; Column < 8; ++Column)
		{
			for (int32 Row = 0; Row < 8; ++Row)
			{
				const int64 Pixel = (static_cast<int64>((Code / 16) * 8 + Row) * 128 + (Code % 16) * 8 + Column) * 4;
				if (Raw[Pixel + 3] != 0)
				{
					Widest = Column + 1;
				}
			}
		}
		GlyphWidths[Code] = static_cast<uint8>(Widest);
	}
	GlyphWidths[' '] = 3;
}

float AMcHUD::TextWidth(const FString& Text) const
{
	float Width = 0.0f;
	for (const TCHAR Char : Text)
	{
		Width += (GlyphWidths.IsValidIndex(Char) ? GlyphWidths[Char] : 5) + 1;
	}
	return Width;
}

void AMcHUD::DrawGuiText(const FString& Text, float X, float Y, uint32 Rgb, bool bShadow)
{
	LoadFont();
	UTexture2D* Font = LoadSprite(TEXT("textures/font/ascii.png"));
	const auto Draw = [&](float OffsetX, float OffsetY, const FLinearColor& Tint)
	{
		float At = X + OffsetX;
		for (const TCHAR Char : Text)
		{
			const int32 Code = Char < 256 ? Char : '?';
			DrawRegion(Font, At, Y + OffsetY, 8, 8, (Code % 16) * 8, (Code / 16) * 8, 8, 8, Tint);
			At += GlyphWidths[Code] + 1;
		}
	};
	if (bShadow)
	{
		// The shadow is the colour at a quarter of its brightness, one pixel down and right.
		Draw(1, 1, FromRgb(Rgb) * 0.25f);
	}
	Draw(0, 0, FromRgb(Rgb));
}

// ---- Items ----

void AMcHUD::DrawStack(const FMcItemStack& Stack, float X, float Y)
{
	if (Stack.IsEmpty())
	{
		return;
	}
	DrawIcon(GetItemIcon(Stack.Item), X, Y);
	const McGui::FDurabilityBar Bar = McGui::GetDurabilityBar(Stack.GetDamage(), Stack.GetMaxDamage());
	if (Bar.bShow)
	{
		DrawFill(X + 2, Y + 13, 13, 2, FLinearColor::Black);
		DrawFill(X + 2, Y + 13, static_cast<float>(Bar.Width), 1, FromRgb(Bar.Color));
	}
	if (McGui::ShowsCount(Stack.Count))
	{
		const FString Count = FString::FromInt(Stack.Count);
		DrawGuiText(Count, X + 17 - TextWidth(Count) + 1, Y + 9, 0xFFFFFF, true);
	}
}

// ---- The bars ----

void AMcHUD::DrawBars(int32 Width, int32 Height, UMcPlayerDataComponent* Data)
{
	const FMcPlayerStats& Stats = Data->Stats;
	const McGui::FHudLayout Layout = McGui::ComputeHud(Width, Height, Stats, ShakeCounter / 3);
	const FString Hud = FString(SpriteRoot) + TEXT("hud/");
	const FString Hardcore = Stats.bHardcore ? TEXT("hardcore_") : TEXT("");

	for (const McGui::FBarIcon& Heart : Layout.Hearts)
	{
		const FString Blink = Heart.bBlink ? TEXT("_blinking") : TEXT("");
		DrawSprite(LoadSprite(Hud + TEXT("heart/container") + (Stats.bHardcore ? TEXT("_hardcore") : TEXT("")) + Blink + TEXT(".png")), Heart.Pos.X, Heart.Pos.Y, 9, 9);
		if (Heart.Fill != McGui::EIcon::Empty)
		{
			DrawSprite(LoadSprite(Hud + TEXT("heart/") + Hardcore + (Heart.Fill == McGui::EIcon::Full ? TEXT("full") : TEXT("half")) + Blink + TEXT(".png")), Heart.Pos.X, Heart.Pos.Y, 9, 9);
		}
	}
	for (const McGui::FBarIcon& Heart : Layout.Absorption)
	{
		if (Heart.Fill != McGui::EIcon::Empty)
		{
			DrawSprite(LoadSprite(Hud + TEXT("heart/absorbing_") + Hardcore + (Heart.Fill == McGui::EIcon::Full ? TEXT("full") : TEXT("half")) + TEXT(".png")), Heart.Pos.X, Heart.Pos.Y, 9, 9);
		}
	}
	if (Layout.bShowArmor)
	{
		for (const McGui::FBarIcon& Icon : Layout.Armor)
		{
			const TCHAR* Name = Icon.Fill == McGui::EIcon::Full ? TEXT("armor_full") : Icon.Fill == McGui::EIcon::Half ? TEXT("armor_half") : TEXT("armor_empty");
			DrawSprite(LoadSprite(Hud + Name + TEXT(".png")), Icon.Pos.X, Icon.Pos.Y, 9, 9);
		}
	}
	for (const McGui::FBarIcon& Icon : Layout.Food)
	{
		DrawSprite(LoadSprite(Hud + TEXT("food_empty.png")), Icon.Pos.X, Icon.Pos.Y, 9, 9);
		if (Icon.Fill != McGui::EIcon::Empty)
		{
			DrawSprite(LoadSprite(Hud + (Icon.Fill == McGui::EIcon::Full ? TEXT("food_full.png") : TEXT("food_half.png"))), Icon.Pos.X, Icon.Pos.Y, 9, 9);
		}
	}
	if (Layout.bShowAir)
	{
		for (const McGui::FBarIcon& Icon : Layout.Air)
		{
			DrawSprite(LoadSprite(Hud + (Icon.Fill == McGui::EIcon::Full ? TEXT("air.png") : TEXT("air_bursting.png"))), Icon.Pos.X, Icon.Pos.Y, 9, 9);
		}
	}
	DrawSprite(LoadSprite(Hud + TEXT("experience_bar_background.png")), Layout.XpBar.X, Layout.XpBar.Y, 182, 5);
	if (Layout.XpFill > 0)
	{
		DrawRegion(LoadSprite(Hud + TEXT("experience_bar_progress.png")), Layout.XpBar.X, Layout.XpBar.Y, static_cast<float>(Layout.XpFill), 5, 0, 0, static_cast<float>(Layout.XpFill), 5);
	}
	if (Layout.bShowLevel)
	{
		LoadFont();
		const FString Level = FString::FromInt(Stats.XpLevel);
		const float X = Layout.LevelText.X - TextWidth(Level) / 2.0f;
		const float Y = static_cast<float>(Layout.LevelText.Y);
		for (const FVector2D& Offset : { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) })
		{
			DrawGuiText(Level, X + Offset.X, Y + Offset.Y, 0x000000, false);
		}
		DrawGuiText(Level, X, Y, 0x80FF20, false);
	}
}

// ---- Screens ----

void AMcHUD::ShowScreen(TSharedRef<FScreenState> State)
{
	Screen = State;
	if (APlayerController* Player = GetOwningPlayerController())
	{
		Player->bShowMouseCursor = true;
		Player->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
		Player->SetIgnoreLookInput(true);
		Player->SetIgnoreMoveInput(true);
	}
}

void AMcHUD::HideScreen()
{
	if (!Screen.IsValid())
	{
		return;
	}
	Screen.Reset();
	HoverSlot = INDEX_NONE;
	if (APlayerController* Player = GetOwningPlayerController())
	{
		Player->bShowMouseCursor = false;
		Player->SetInputMode(FInputModeGameOnly());
		Player->SetIgnoreLookInput(false);
		Player->SetIgnoreMoveInput(false);
	}
}

void AMcHUD::SyncScreen(UMcPlayerDataComponent* Data)
{
	// The window is the data component's: the player opens their own (E) or the server opens one on a block, and it goes
	// when either side closes it. The screen is only how it looks and where the mouse goes.
	const FMcWindow& Window = Data ? Data->GetWindow() : FMcWindow();
	if (!Window.IsOpen())
	{
		HideScreen();
		return;
	}
	if (Screen.IsValid() && Screen->WindowId == Window.Id)
	{
		return;
	}
	HideScreen();
	TSharedRef<FScreenState> State = MakeShared<FScreenState>();
	State->WindowId = Window.Id;
	State->Input = MakeShared<FMcMenuInput>(Window.Menu, Data->bCreative);
	State->Input->OnClick = [WeakData = TWeakObjectPtr<UMcPlayerDataComponent>(Data)](int32 Slot, int32 Button, EMcClickType Type)
	{
		if (UMcPlayerDataComponent* Owner = WeakData.Get())
		{
			Owner->NotifyClick(Slot, Button, Type);
		}
	};
	State->Input->OnDrag = [WeakData = TWeakObjectPtr<UMcPlayerDataComponent>(Data)](int32 Button, const TArray<int32>& Slots)
	{
		if (UMcPlayerDataComponent* Owner = WeakData.Get())
		{
			Owner->NotifyDrag(Button, Slots);
		}
	};
	State->Title = Window.Title;
	State->Furnace = Window.Furnace;
	switch (Window.Kind)
	{
	case EMcWindowKind::CraftingTable:
		State->Layout = McGui::MakeCraftingTableLayout();
		break;
	case EMcWindowKind::Chest:
		State->Layout = McGui::MakeChestLayout(FMath::Clamp(Window.Container.IsValid() ? Window.Container->Num() / 9 : 3, 1, 6));
		break;
	case EMcWindowKind::Furnace:
		State->Layout = McGui::MakeFurnaceLayout();
		break;
	default:
		State->Layout = McGui::MakeInventoryLayout();
		break;
	}
	ShowScreen(State);
}

void AMcHUD::OpenInventory()
{
	const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwningPawn());
	if (UMcPlayerDataComponent* Data = Pawn ? Pawn->FindComponentByClass<UMcPlayerDataComponent>() : nullptr)
	{
		Data->OpenInventoryWindow();
	}
}

void AMcHUD::FlushDrops()
{
	if (!Screen.IsValid())
	{
		return;
	}
	const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwningPawn());
	UMcPlayerDataComponent* Data = Pawn ? Pawn->FindComponentByClass<UMcPlayerDataComponent>() : nullptr;
	for (const FMcItemStack& Stack : Screen->Input->TakeDropped())
	{
		// Only the server makes items; on a client the server's copy of the same click throws the same stack.
		if (Data && !Stack.IsEmpty())
		{
			Data->DropFromWindow(Stack);
		}
	}
}

void AMcHUD::CloseScreen()
{
	if (!Screen.IsValid())
	{
		return;
	}
	FlushDrops();
	const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwningPawn());
	if (UMcPlayerDataComponent* Data = Pawn ? Pawn->FindComponentByClass<UMcPlayerDataComponent>() : nullptr)
	{
		Data->CloseWindow();
	}
	HideScreen();
}

void AMcHUD::PollInput(int32 Width, int32 Height, UMcPlayerDataComponent* Data)
{
	APlayerController* Player = GetOwningPlayerController();
	if (!Player || !Data)
	{
		return;
	}
	if (Player->WasInputKeyJustPressed(EKeys::E))
	{
		if (Screen.IsValid())
		{
			CloseScreen();
		}
		else
		{
			Data->OpenInventoryWindow();
		}
		return;
	}
	if (!Screen.IsValid())
	{
		return;
	}
	if (Player->WasInputKeyJustPressed(EKeys::Escape))
	{
		CloseScreen();
		return;
	}

	float MouseX = 0.0f, MouseY = 0.0f;
	Player->GetMousePosition(MouseX, MouseY);
	GuiMouse = FVector2D(MouseX, MouseY) / Scale;
	const FIntPoint Local = FIntPoint(FMath::FloorToInt(GuiMouse.X), FMath::FloorToInt(GuiMouse.Y)) - Screen->Layout.Origin(Width, Height);
	const bool bInside = Local.X >= 0 && Local.Y >= 0 && Local.X < Screen->Layout.Size.X && Local.Y < Screen->Layout.Size.Y;
	HoverSlot = bInside ? Screen->Layout.FindSlot(Local) : FMcMenuInput::Outside;
	FMcMenuInput& Input = *Screen->Input;
	// Over the picture but no slot (a label, the gap between slots): clicks do nothing, rather than count as outside.
	const int32 Slot = HoverSlot;
	const bool bShift = Player->IsInputKeyDown(EKeys::LeftShift) || Player->IsInputKeyDown(EKeys::RightShift);
	const double Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	Input.MouseMove(Slot);
	const FKey Buttons[3] = { EKeys::LeftMouseButton, EKeys::RightMouseButton, EKeys::MiddleMouseButton };
	for (int32 Button = 0; Button < 3; ++Button)
	{
		if (Player->WasInputKeyJustPressed(Buttons[Button]) && !(bInside && Slot < 0))
		{
			Input.MouseDown(Slot, Button, bShift, Time);
		}
		if (Player->WasInputKeyJustReleased(Buttons[Button]))
		{
			Input.MouseUp(Slot, Button);
		}
	}
	const FKey Numbers[9] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 Number = 0; Number < 9; ++Number)
	{
		if (Player->WasInputKeyJustPressed(Numbers[Number]))
		{
			Input.NumberKey(Slot, Number);
		}
	}
	if (Player->WasInputKeyJustPressed(EKeys::Q))
	{
		Input.DropKey(Slot, Player->IsInputKeyDown(EKeys::LeftControl));
	}
	FlushDrops();
}

void AMcHUD::DrawScreen(int32 Width, int32 Height)
{
	const McGui::FScreenLayout& Layout = Screen->Layout;
	const FMcMenu& Menu = Screen->Input->GetMenu();
	const FIntPoint Origin = Layout.Origin(Width, Height);
	DrawFill(0, 0, static_cast<float>(Width), static_cast<float>(Height), FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));

	UTexture2D* Background = LoadSprite(TEXT("textures/gui/container/") + Layout.Texture);
	if (Layout.Kind == McGui::EScreen::Chest)
	{
		// The picture holds six rows; the top is cut after the rows there are and the bottom (the player's part) follows.
		const float Top = Layout.Rows * 18 + 17;
		DrawRegion(Background, Origin.X, Origin.Y, 176, Top, 0, 0, 176, Top);
		DrawRegion(Background, Origin.X, Origin.Y + Top, 176, 96, 0, 126, 176, 96);
	}
	else
	{
		DrawRegion(Background, Origin.X, Origin.Y, 176, 166, 0, 0, 176, 166);
	}
	if (Layout.Kind == McGui::EScreen::Furnace && Screen->Furnace.IsValid())
	{
		const FMcFurnace& Furnace = *Screen->Furnace;
		const float Flame = McGui::GetFlameFill(Furnace.GetLitTimeRemaining(), Furnace.GetLitTotalTime());
		if (Flame > 0.0f)
		{
			const float Lit = FMath::CeilToFloat(14.0f * Flame);
			DrawRegion(LoadSprite(FString(SpriteRoot) + TEXT("container/furnace/lit_progress.png")), Origin.X + Layout.Flame.X, Origin.Y + Layout.Flame.Y + 14 - Lit, 14, Lit, 0, 14 - Lit, 14, Lit);
		}
		const float Arrow = FMath::CeilToFloat(24.0f * McGui::GetArrowFill(Furnace.GetCookingProgress(), Furnace.GetCookingTotalTime()));
		if (Arrow > 0.0f)
		{
			DrawRegion(LoadSprite(FString(SpriteRoot) + TEXT("container/furnace/burn_progress.png")), Origin.X + Layout.Arrow.X, Origin.Y + Layout.Arrow.Y, Arrow, 16, 0, 0, Arrow, 16);
		}
	}

	// Labels in the dark grey the game uses, without shadow.
	switch (Layout.Kind)
	{
	case McGui::EScreen::Inventory:
		DrawGuiText(TEXT("Crafting"), Origin.X + 97.0f, Origin.Y + 8.0f, 0x404040, false);
		break;
	case McGui::EScreen::CraftingTable:
	case McGui::EScreen::Furnace:
		DrawGuiText(Screen->Title, Origin.X + (Layout.Kind == McGui::EScreen::Furnace ? 60.0f : 28.0f), Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		break;
	case McGui::EScreen::Chest:
		DrawGuiText(Screen->Title, Origin.X + 8.0f, Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + Layout.Rows * 18.0f + 20.0f, 0x404040, false);
		break;
	}

	for (int32 Slot = 0; Slot < Menu.NumSlots() && Slot < Layout.Slots.Num(); ++Slot)
	{
		const FIntPoint At = Origin + Layout.Slots[Slot];
		DrawStack(Menu.GetStack(Slot), static_cast<float>(At.X), static_cast<float>(At.Y));
		if (Slot == HoverSlot)
		{
			DrawFill(static_cast<float>(At.X), static_cast<float>(At.Y), 16, 16, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
		}
	}

	// The stack on the cursor, centred on it; else the name of what is under it.
	if (!Menu.GetCarried().IsEmpty())
	{
		DrawStack(Menu.GetCarried(), static_cast<float>(GuiMouse.X) - 8.0f, static_cast<float>(GuiMouse.Y) - 8.0f);
	}
	else if (HoverSlot >= 0 && HoverSlot < Menu.NumSlots() && !Menu.GetStack(HoverSlot).IsEmpty())
	{
		const FString Name = GetItemName(Menu.GetStack(HoverSlot));
		const float Wide = TextWidth(Name);
		const float X = static_cast<float>(GuiMouse.X) + 12.0f;
		const float Y = static_cast<float>(GuiMouse.Y) - 12.0f;
		DrawFill(X - 3, Y - 3, Wide + 5, 14, FLinearColor(0.06f, 0.0f, 0.06f, 0.9f));
		DrawGuiText(Name, X, Y, 0xFFFFFF, true);
	}
}

void AMcHUD::ShowDeathScreen(int32 Width, int32 Height, AMcPlayerPawn& Dead)
{
	APlayerController* Player = GetOwningPlayerController();
	if (!bDeathShown)
	{
		bDeathShown = true;
		if (Screen.IsValid())
		{
			HideScreen();
		}
		if (Player)
		{
			Player->bShowMouseCursor = true;
			Player->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
			Player->SetIgnoreLookInput(true);
			Player->SetIgnoreMoveInput(true);
		}
	}
	DrawFill(0, 0, static_cast<float>(Width), static_cast<float>(Height), FLinearColor(0.5f, 0.0f, 0.0f, 0.5f));
	const FString Title = TEXT("You died!");
	DrawGuiText(Title, Width / 2.0f - TextWidth(Title) * 1.5f, Height / 4.0f, 0xFFFFFF, true);

	// A 200 by 20 button under the title.
	const float ButtonX = Width / 2.0f - 100.0f;
	const float ButtonY = Height / 4.0f + 72.0f;
	float MouseX = 0.0f, MouseY = 0.0f;
	if (Player)
	{
		Player->GetMousePosition(MouseX, MouseY);
	}
	GuiMouse = FVector2D(MouseX, MouseY) / Scale;
	const bool bOver = GuiMouse.X >= ButtonX && GuiMouse.X < ButtonX + 200.0f && GuiMouse.Y >= ButtonY && GuiMouse.Y < ButtonY + 20.0f;
	DrawFill(ButtonX - 1, ButtonY - 1, 202, 22, FLinearColor::Black);
	DrawFill(ButtonX, ButtonY, 200, 20, bOver ? FLinearColor(0.44f, 0.5f, 0.75f, 1.0f) : FLinearColor(0.44f, 0.44f, 0.44f, 1.0f));
	const FString Label = TEXT("Respawn");
	DrawGuiText(Label, ButtonX + 100.0f - TextWidth(Label) / 2.0f, ButtonY + 6.0f, bOver ? 0xFFFFA0 : 0xFFFFFF, true);
	if (bOver && Player && Player->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		Dead.RequestRespawn();
	}
}

// ---- The HUD ----

void AMcHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	++ShakeCounter;
	Scale = McGui::GetGuiScale(Canvas->SizeX, Canvas->SizeY, GuiScaleSetting);
	const int32 Width = Canvas->SizeX / Scale;
	const int32 Height = Canvas->SizeY / Scale;

	AMcPlayerPawn* Player = Cast<AMcPlayerPawn>(GetOwningPawn());
	UMcPlayerDataComponent* Data = Player ? Player->FindComponentByClass<UMcPlayerDataComponent>() : nullptr;
	const bool bDead = Player && Player->IsDead();
	if (!bDead && bDeathShown)
	{
		// Back from the dead: give the player the world again.
		bDeathShown = false;
		if (APlayerController* Controller = GetOwningPlayerController())
		{
			Controller->bShowMouseCursor = false;
			Controller->SetInputMode(FInputModeGameOnly());
			Controller->SetIgnoreLookInput(false);
			Controller->SetIgnoreMoveInput(false);
		}
	}
	SyncScreen(Data);
	if (!bDead)
	{
		PollInput(Width, Height, Data);
	}
	// A click may have closed the window (or the server did).
	SyncScreen(Data);

	const McGui::FHudLayout Layout = McGui::ComputeHud(Width, Height, Data ? Data->Stats : FMcPlayerStats());
	// Minecraft draws the crosshair inverting what is behind it; plain white for now.
	if (!Screen.IsValid())
	{
		DrawSprite(LoadSprite(TEXT("textures/gui/sprites/hud/crosshair.png")), Layout.Crosshair.X, Layout.Crosshair.Y, 15, 15);
	}
	if (!Player)
	{
		return;
	}
	const int32 Selected = Data ? Data->SelectedSlot : Player->SelectedSlot;
	DrawSprite(LoadSprite(TEXT("textures/gui/sprites/hud/hotbar.png")), Layout.Hotbar.X, Layout.Hotbar.Y, 182, 22);
	DrawSprite(LoadSprite(TEXT("textures/gui/sprites/hud/hotbar_selection.png")), Layout.Hotbar.X - 1 + Selected * 20, Layout.Hotbar.Y - 1, 24, 23);
	if (Data)
	{
		for (int32 Slot = 0; Slot < 9; ++Slot)
		{
			DrawStack(Data->Inventory->Get(Slot), static_cast<float>(Layout.HotbarSlots[Slot].X), static_cast<float>(Layout.HotbarSlots[Slot].Y));
		}
		if (!Data->Offhand->Get(0).IsEmpty())
		{
			DrawSprite(LoadSprite(TEXT("textures/gui/sprites/hud/hotbar_offhand_left.png")), Layout.Hotbar.X - 29, Layout.Hotbar.Y - 1, 29, 24);
			DrawStack(Data->Offhand->Get(0), static_cast<float>(Layout.Hotbar.X - 26), static_cast<float>(Layout.Hotbar.Y + 3));
		}
		DrawBars(Width, Height, Data);
	}
	if (Screen.IsValid() && !bDead)
	{
		DrawScreen(Width, Height);
	}
	if (bDead)
	{
		ShowDeathScreen(Width, Height, *Player);
	}
}
