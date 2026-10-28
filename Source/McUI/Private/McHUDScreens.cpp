// The parts of the HUD that use the player's data: the bars, the hotbar with its items, and the container screens
// with their mouse and key handling. The sprites are the jar's own (textures/gui/sprites, textures/gui/container).

#include "McHUD.h"
#include "McRenderMode.h"
#include "McLocalPlayer.h"
#include "McLaunch.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "InputCoreTypes.h"
#include "McCache.h"
#include "McGameViewportClient.h"
#include "McItemRegistry.h"
#include "McLang.h"
#include "McContainer.h"
#include "McCraftingMenu.h"
#include "McEnchantMenu.h"
#include "McWorkstationMenus.h"
#include "McAnvilMenu.h"
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
	// The enchanting table's menu (Input holds it).
	FMcEnchantMenu* Enchanting = nullptr;
	// The stonecutter's menu and how far down its recipes are scrolled.
	FMcStonecutterMenu* Stonecutter = nullptr;
	int32 RecipeScroll = 0;
	// The anvil's menu, the name typed, and the item the name was set for.
	FMcAnvilMenu* Anvil = nullptr;
	FString AnvilName;
	FMcItemStack AnvilFor;
	// Whether keys go to a text field (the creative search, the anvil's name) rather than the slots.
	bool IsTyping() const { return bCreative || Anvil != nullptr; }
	FString Title;
	// The creative screen: what the search finds, how far down the list is scrolled, the item under the mouse (an
	// index into CreativeItems) and whether the scroll bar is being dragged.
	bool bCreative = false;
	FString Search;
	TArray<FName> CreativeItems;
	int32 CreativeScroll = 0;
	int32 CreativeHover = -1;
	bool bScrollDrag = false;
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

	// The name the jar gives an item (as an item, else as a block), else made from its id.
	FString DisplayName(FName Item)
	{
		const FString Path = McCache::PathOf(Item.ToString());
		for (const TCHAR* Prefix : { TEXT("item.minecraft."), TEXT("block.minecraft.") })
		{
			const FString Key = Prefix + Path;
			FString Name = McLang::Get(Key);
			if (Name != Key)
			{
				return Name;
			}
		}
		return TitleCase(Path);
	}

	FString GetItemName(const FMcItemStack& Stack)
	{
		const FString Custom = Stack.GetCustomName();
		return Custom.IsEmpty() ? DisplayName(Stack.Item) : Custom;
	}

	// The creative list for a search.
	TArray<FName> CreativeList(const FString& Search)
	{
		static TArray<FName> All;
		FMcItemRegistry& Items = FMcItemRegistry::Get();
		if (All.Num() == 0 && Items.EnsureLoaded())
		{
			for (const FMcItemDef& Item : Items.GetAll())
			{
				if (McGui::Creative::IsListed(Item.Name))
				{
					All.Add(Item.Name);
				}
			}
		}
		return McGui::Creative::Filter(All, Search, [](FName Item) { return DisplayName(Item); });
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

void AMcHUD::DrawGuiText(const FString& Text, float X, float Y, uint32 Rgb, bool bShadow, float Alpha)
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
		Draw(1, 1, FromRgb(Rgb) * 0.25f * Alpha);
	}
	Draw(0, 0, FromRgb(Rgb, Alpha));
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
	// Nothing typed before it opened (the E that opened it) goes into its search.
	UMcGameViewportClient::TakeTyped();
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
	State->Enchanting = Window.Enchanting;
	switch (Window.Kind)
	{
	case EMcWindowKind::EnchantingTable:
		State->Layout = McGui::MakeEnchantingLayout();
		break;
	case EMcWindowKind::Stonecutter:
		State->Layout = McGui::MakeStonecutterLayout();
		State->Stonecutter = static_cast<FMcStonecutterMenu*>(Window.Menu.Get());
		break;
	case EMcWindowKind::SmithingTable:
		State->Layout = McGui::MakeSmithingLayout();
		break;
	case EMcWindowKind::Grindstone:
		State->Layout = McGui::MakeGrindstoneLayout();
		break;
	case EMcWindowKind::Anvil:
		State->Layout = McGui::MakeAnvilLayout();
		State->Anvil = static_cast<FMcAnvilMenu*>(Window.Menu.Get());
		break;
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
		if (Data->bCreative)
		{
			// Creative players get the item list in place of their inventory.
			State->Layout = McGui::MakeCreativeLayout();
			State->bCreative = true;
			State->CreativeItems = CreativeList(FString());
#if !UE_BUILD_SHIPPING
			FParse::Value(FCommandLine::Get(), TEXT("-McInventory="), State->Search);
			State->CreativeItems = CreativeList(State->Search);
#endif
		}
		else
		{
			State->Layout = McGui::MakeInventoryLayout();
		}
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
	// E closes the screen, except in creative where it is typed into the search.
	if (Player->WasInputKeyJustPressed(EKeys::E) && !(Screen.IsValid() && Screen->IsTyping()))
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
		if (Player->WasInputKeyJustPressed(EKeys::F7))
		{
			ToggleGraphics();
		}
		const bool bSlash = Player->WasInputKeyJustPressed(EKeys::Slash) || Player->WasInputKeyJustPressed(EKeys::Divide);
		if (bSlash || Player->WasInputKeyJustPressed(EKeys::T))
		{
			// The key's own character is dropped (OpenMenu takes what was typed); / starts a command.
			OpenMenu(EMenu::Chat);
			if (bSlash)
			{
				ChatDraft = TEXT("/");
			}
			return;
		}
#if !UE_BUILD_SHIPPING
		// Development: -McChat="/time set night" sends that line three seconds in, once the player is set up (lines split by |).
		static bool bDevChatDone = false;
		FString DevChat;
		if (!bDevChatDone && GetWorld()->GetTimeSeconds() > 3.0 && FParse::Value(FCommandLine::Get(), TEXT("-McChat="), DevChat, false))
		{
			bDevChatDone = true;
			TArray<FString> Lines;
			DevChat.TrimQuotes().ParseIntoArray(Lines, TEXT("|"));
			for (const FString& Line : Lines)
			{
				Data->SendChat(Line);
			}
		}
		// -McUse=X,Y,Z uses that block (opens its window) five seconds in, after -McChat has built it.
		static bool bDevUseDone = false;
		FString DevUse;
		if (!bDevUseDone && GetWorld()->GetTimeSeconds() > 5.0 && FParse::Value(FCommandLine::Get(), TEXT("-McUse="), DevUse, false))
		{
			bDevUseDone = true;
			TArray<FString> Parts;
			DevUse.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() == 3)
			{
				if (AMcPlayerPawn* User = Cast<AMcPlayerPawn>(GetOwningPawn()))
				{
					User->RequestUseBlock(FIntVector(FCString::Atoi(*Parts[0]), FCString::Atoi(*Parts[1]), FCString::Atoi(*Parts[2])));
				}
			}
		}
		// -McInventory opens the inventory once the world is up (-McInventory=log also searches the creative list).
		static bool bDevInventoryDone = false;
		FString DevSearch;
		if (!bDevInventoryDone && (FParse::Param(FCommandLine::Get(), TEXT("McInventory")) || FParse::Value(FCommandLine::Get(), TEXT("-McInventory="), DevSearch)))
		{
			bDevInventoryDone = true;
			Data->OpenInventoryWindow();
			return;
		}
#endif
		bool bPause = Player->WasInputKeyJustPressed(EKeys::Escape);
#if !UE_BUILD_SHIPPING
		// Development: -McMenu=pause opens the pause menu once the world is up.
		static bool bDevPauseDone = false;
		FString DevMenu;
		if (!bDevPauseDone && FParse::Value(FCommandLine::Get(), TEXT("-McMenu="), DevMenu) && DevMenu == TEXT("pause"))
		{
			bDevPauseDone = true;
			bPause = true;
		}
#endif
		if (bPause)
		{
			OpenMenu(EMenu::Pause);
		}
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
#if !UE_BUILD_SHIPPING
	// Development: -McShiftClick=29,30 shift-clicks those slots of the first window -McUse opens.
	static bool bDevClickDone = false;
	FString DevClicks;
	if (!bDevClickDone && !Screen->bCreative && FParse::Value(FCommandLine::Get(), TEXT("-McShiftClick="), DevClicks, false))
	{
		bDevClickDone = true;
		TArray<FString> Clicks;
		DevClicks.ParseIntoArray(Clicks, TEXT(","));
		for (const FString& Click : Clicks)
		{
			Input.MouseDown(FCString::Atoi(*Click), 0, true, -10.0);
			Input.MouseUp(FCString::Atoi(*Click), 0);
		}
	}
#endif
	// Over the picture but no slot (a label, the gap between slots): clicks do nothing, rather than count as outside.
	const int32 Slot = HoverSlot;
	const bool bShift = Player->IsInputKeyDown(EKeys::LeftShift) || Player->IsInputKeyDown(EKeys::RightShift);
	const double Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	bool bTaken = Screen->bCreative && PollCreative(*Player, Local, *Data);
	Data->PrepareMenuClick();
	if (Screen->Anvil)
	{
		FScreenState& State = *Screen;
		// A new item on the left starts the field at its custom name (empty for none: it keeps its own name).
		const FMcItemStack& Left = State.Anvil->GetStack(FMcAnvilMenu::LeftSlot);
		if (Left.Item != State.AnvilFor.Item || Left.GetCustomName() != State.AnvilFor.GetCustomName())
		{
			State.AnvilFor = Left;
			State.AnvilName = Left.IsEmpty() ? FString() : Left.GetCustomName();
			Data->SetMenuText(State.AnvilName);
		}
		const FString Before = State.AnvilName;
		const FString Typed = UMcGameViewportClient::TakeTyped();
		if (!Left.IsEmpty())
		{
			State.AnvilName.Append(Typed.Left(FMath::Max(0, FMcAnvilMenu::MaxNameLength - State.AnvilName.Len())));
			if (Player->WasInputKeyJustPressed(EKeys::BackSpace) && !State.AnvilName.IsEmpty())
			{
				State.AnvilName.LeftChopInline(1);
			}
		}
		if (State.AnvilName != Before)
		{
			Data->SetMenuText(State.AnvilName);
		}
	}
	if (Screen->Stonecutter && bInside)
	{
		namespace Cutter = McGui::Stonecutter;
		const int32 Count = Screen->Stonecutter->GetRecipes().Num();
		if (Player->WasInputKeyJustPressed(EKeys::MouseScrollDown))
		{
			Screen->RecipeScroll = FMath::Min(Screen->RecipeScroll + 1, Cutter::MaxScroll(Count));
		}
		if (Player->WasInputKeyJustPressed(EKeys::MouseScrollUp))
		{
			Screen->RecipeScroll = FMath::Max(Screen->RecipeScroll - 1, 0);
		}
		Screen->RecipeScroll = FMath::Clamp(Screen->RecipeScroll, 0, Cutter::MaxScroll(Count));
		const int32 Recipe = Cutter::FindRecipe(Local, Screen->RecipeScroll, Count);
		if (Recipe >= 0 && Player->WasInputKeyJustPressed(EKeys::LeftMouseButton))
		{
			Data->ClickMenuButton(Recipe);
			bTaken = true;
		}
	}
	if (Screen->Enchanting && bInside && Player->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		const int32 Option = McGui::Enchanting::FindButton(Local);
		if (Option >= 0)
		{
			if (Screen->Enchanting->CanEnchant(Option, Data->Stats.XpLevel, Data->bCreative))
			{
				Data->Enchant(Option);
			}
			bTaken = true;
		}
	}
	Input.MouseMove(Slot);
	const FKey Buttons[3] = { EKeys::LeftMouseButton, EKeys::RightMouseButton, EKeys::MiddleMouseButton };
	for (int32 Button = 0; Button < 3; ++Button)
	{
		if (Player->WasInputKeyJustPressed(Buttons[Button]) && !(bInside && Slot < 0) && !bTaken)
		{
			Input.MouseDown(Slot, Button, bShift, Time);
		}
		if (Player->WasInputKeyJustReleased(Buttons[Button]))
		{
			Input.MouseUp(Slot, Button);
		}
	}
	if (Screen->IsTyping())
	{
		// The keys are the text field's.
		FlushDrops();
		return;
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

bool AMcHUD::PollCreative(APlayerController& Player, FIntPoint Local, UMcPlayerDataComponent& Data)
{
	namespace Creative = McGui::Creative;
	FScreenState& State = *Screen;
	const FString Before = State.Search;
	State.Search.Append(UMcGameViewportClient::TakeTyped().Left(FMath::Max(0, Creative::MaxSearch - State.Search.Len())));
	if (Player.WasInputKeyJustPressed(EKeys::BackSpace) && !State.Search.IsEmpty())
	{
		State.Search.LeftChopInline(1);
	}
	if (State.Search != Before)
	{
		State.CreativeItems = CreativeList(State.Search);
		State.CreativeScroll = 0;
	}

	// The wheel scrolls a row at a time; the thumb can be dragged (a click on the track jumps it there).
	const int32 MaxScroll = Creative::MaxScroll(State.CreativeItems.Num());
	if (Player.WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		State.CreativeScroll = FMath::Min(MaxScroll, State.CreativeScroll + 1);
	}
	if (Player.WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		State.CreativeScroll = FMath::Max(0, State.CreativeScroll - 1);
	}
	const bool bPressed = Player.WasInputKeyJustPressed(EKeys::LeftMouseButton);
	const bool bOnTrack = Local.X >= Creative::ScrollX && Local.X < Creative::ScrollX + 14 && Local.Y >= Creative::ScrollY && Local.Y < Creative::ScrollY + Creative::ScrollHeight;
	if (bPressed && bOnTrack && MaxScroll > 0)
	{
		State.bScrollDrag = true;
	}
	if (!Player.IsInputKeyDown(EKeys::LeftMouseButton))
	{
		State.bScrollDrag = false;
	}
	if (State.bScrollDrag)
	{
		const float Fraction = (Local.Y - Creative::ScrollY - 7.5f) / (Creative::ScrollHeight - 15.0f);
		State.CreativeScroll = FMath::Clamp(FMath::RoundToInt(Fraction * MaxScroll), 0, MaxScroll);
	}
	State.CreativeScroll = FMath::Clamp(State.CreativeScroll, 0, MaxScroll);

	const int32 Cell = Creative::FindCell(Local);
	const int32 Index = Cell >= 0 ? State.CreativeScroll * Creative::Columns + Cell : -1;
	State.CreativeHover = State.CreativeItems.IsValidIndex(Index) ? Index : -1;
	if (Cell < 0)
	{
		return bPressed && bOnTrack;
	}
	int32 Button = -1;
	const FKey Buttons[3] = { EKeys::LeftMouseButton, EKeys::RightMouseButton, EKeys::MiddleMouseButton };
	for (int32 Each = 0; Each < 3 && Button < 0; ++Each)
	{
		Button = Player.WasInputKeyJustPressed(Buttons[Each]) ? Each : -1;
	}
	if (Button < 0)
	{
		return false;
	}
	if (!Screen->Input->GetMenu().GetCarried().IsEmpty())
	{
		// What is put back on the list is gone.
		Data.CreativeTake(NAME_None, 0, false);
	}
	else if (State.CreativeHover >= 0)
	{
		// One onto the cursor; the middle button takes a full stack, and shift-click puts a full stack in the inventory.
		const FName Item = State.CreativeItems[State.CreativeHover];
		const int32 Full = FMcItemStack(Item, 1).GetMaxStackSize();
		const bool bShift = Player.IsInputKeyDown(EKeys::LeftShift) || Player.IsInputKeyDown(EKeys::RightShift);
		if (bShift && Button == 0)
		{
			Data.CreativeTake(Item, Full, true);
		}
		else
		{
			Data.CreativeTake(Item, Button == 2 ? Full : 1, false);
		}
	}
	return true;
}

void AMcHUD::DrawCreative(FIntPoint Origin)
{
	namespace Creative = McGui::Creative;
	const FScreenState& State = *Screen;
	const bool bCaret = FMath::Fmod(FPlatformTime::Seconds(), 0.6) < 0.3;
	FString Shown = State.Search;
	while (!Shown.IsEmpty() && TextWidth(Shown) > Creative::SearchWidth - 6)
	{
		Shown.RightChopInline(1);
	}
	DrawGuiText(bCaret ? Shown + TEXT("_") : Shown, Origin.X + Creative::SearchX, Origin.Y + Creative::SearchY, 0xFFFFFF, true);
	for (int32 Cell = 0; Cell < Creative::Columns * Creative::Rows; ++Cell)
	{
		const int32 Index = State.CreativeScroll * Creative::Columns + Cell;
		if (!State.CreativeItems.IsValidIndex(Index))
		{
			break;
		}
		const float X = Origin.X + Creative::ListX + Creative::Pitch * (Cell % Creative::Columns);
		const float Y = Origin.Y + Creative::ListY + Creative::Pitch * (Cell / Creative::Columns);
		DrawStack(FMcItemStack(State.CreativeItems[Index], 1), X, Y);
		if (Index == State.CreativeHover)
		{
			DrawFill(X, Y, 16, 16, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
		}
	}
	const int32 MaxScroll = Creative::MaxScroll(State.CreativeItems.Num());
	const float Thumb = MaxScroll > 0 ? (Creative::ScrollHeight - 15.0f) * State.CreativeScroll / MaxScroll : 0.0f;
	DrawSprite(LoadSprite(FString(SpriteRoot) + (MaxScroll > 0 ? TEXT("container/creative_inventory/scroller.png") : TEXT("container/creative_inventory/scroller_disabled.png"))),
		Origin.X + Creative::ScrollX, Origin.Y + Creative::ScrollY + Thumb, 12, 15);
}

void AMcHUD::DrawEnchanting(FIntPoint Origin, FIntPoint Local)
{
	namespace Enchanting = McGui::Enchanting;
	const FMcEnchantMenu& Menu = *Screen->Enchanting;
	const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwningPawn());
	const UMcPlayerDataComponent* Data = Pawn ? Pawn->FindComponentByClass<UMcPlayerDataComponent>() : nullptr;
	const int32 Levels = Data ? Data->Stats.XpLevel : 0;
	const bool bCreative = Data && Data->bCreative;
	const FString Table = FString(SpriteRoot) + TEXT("container/enchanting_table/");
	const int32 Hovered = Enchanting::FindButton(Local);
	for (int32 Option = 0; Option < 3; ++Option)
	{
		const float X = Origin.X + Enchanting::ButtonX;
		const float Y = Origin.Y + Enchanting::ButtonY + Option * Enchanting::ButtonHeight;
		const int32 Cost = Menu.GetOffers().Costs[Option];
		if (Cost <= 0)
		{
			DrawSprite(LoadSprite(Table + TEXT("enchantment_slot_disabled.png")), X, Y, Enchanting::ButtonWidth, Enchanting::ButtonHeight);
			continue;
		}
		// Affordable (the levels, ignoring the lapis, as the game shows it) is lit; else greyed.
		const bool bAffordable = bCreative || Levels >= Cost;
		const bool bUsable = Menu.CanEnchant(Option, Levels, bCreative);
		const TCHAR* Slot = !bUsable ? TEXT("enchantment_slot_disabled.png") : Option == Hovered ? TEXT("enchantment_slot_highlighted.png") : TEXT("enchantment_slot.png");
		DrawSprite(LoadSprite(Table + Slot), X, Y, Enchanting::ButtonWidth, Enchanting::ButtonHeight);
		DrawSprite(LoadSprite(Table + FString::Printf(TEXT("level_%d%s.png"), Option + 1, bUsable ? TEXT("") : TEXT("_disabled"))), X + 1, Y + 1, 16, 16);
		const FString Text = FString::FromInt(Cost);
		DrawGuiText(Text, X + Enchanting::ButtonWidth - 2 - TextWidth(Text), Y + 9, bAffordable ? 0x80FF20 : 0x407F10, true);
	}
}

void AMcHUD::DrawAnvil(FIntPoint Origin)
{
	const FMcAnvilMenu& Menu = *Screen->Anvil;
	const FString Anvil = FString(SpriteRoot) + TEXT("container/anvil/");
	const bool bHasItem = !Menu.GetStack(FMcAnvilMenu::LeftSlot).IsEmpty();
	DrawSprite(LoadSprite(Anvil + (bHasItem ? TEXT("text_field.png") : TEXT("text_field_disabled.png"))), Origin.X + 59, Origin.Y + 20, 110, 16);
	if (bHasItem)
	{
		// The name, with a caret blinking after it.
		const bool bCaret = FMath::Fmod(GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0, 0.6) < 0.3;
		DrawGuiText(Screen->AnvilName + (bCaret ? TEXT("_") : TEXT("")), Origin.X + 62.0f, Origin.Y + 24.0f, 0xFFFFFF, true);
	}
	const bool bResult = !Menu.GetStack(FMcAnvilMenu::ResultSlot).IsEmpty();
	if (bHasItem && !Menu.GetStack(FMcAnvilMenu::RightSlot).IsEmpty() && !bResult)
	{
		DrawSprite(LoadSprite(Anvil + TEXT("error.png")), Origin.X + 99, Origin.Y + 45, 28, 21);
	}
	if (Menu.GetCost() <= 0)
	{
		return;
	}
	const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwningPawn());
	const UMcPlayerDataComponent* Data = Pawn ? Pawn->FindComponentByClass<UMcPlayerDataComponent>() : nullptr;
	const bool bAfford = Data && (Data->bCreative || Data->Stats.XpLevel >= Menu.GetCost());
	FString Text;
	uint32 Color = 0x80FF20;
	if (Menu.IsTooExpensive())
	{
		Text = McLang::Get(TEXT("container.repair.expensive"));
		Color = 0xFF6060;
	}
	else if (bResult)
	{
		Text = McLang::Format(TEXT("container.repair.cost"), { FString::FromInt(Menu.GetCost()) });
		Color = bAfford ? 0x80FF20 : 0xFF6060;
	}
	if (Text.IsEmpty())
	{
		return;
	}
	const float X = Origin.X + 176 - 8 - TextWidth(Text) - 2;
	DrawFill(X - 2, Origin.Y + 67.0f, Origin.X + 168.0f - (X - 2), 12.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.31f));
	DrawGuiText(Text, X, Origin.Y + 69.0f, Color, true);
}

void AMcHUD::DrawStonecutter(FIntPoint Origin, FIntPoint Local)
{
	namespace Layout = McGui::Stonecutter;
	const FMcStonecutterMenu& Menu = *Screen->Stonecutter;
	const TArray<const FMcRecipe*>& Recipes = Menu.GetRecipes();
	const FString Cutter = FString(SpriteRoot) + TEXT("container/stonecutter/");
	const int32 MaxScroll = Layout::MaxScroll(Recipes.Num());
	const float Thumb = MaxScroll > 0 ? FMath::FloorToFloat((Layout::ScrollHeight - 15) * Screen->RecipeScroll / static_cast<float>(MaxScroll)) : 0.0f;
	DrawSprite(LoadSprite(Cutter + (MaxScroll > 0 ? TEXT("scroller.png") : TEXT("scroller_disabled.png"))), Origin.X + Layout::ScrollX, Origin.Y + Layout::ScrollY + Thumb, 12, 15);
	const int32 Hovered = Layout::FindRecipe(Local, Screen->RecipeScroll, Recipes.Num());
	for (int32 Cell = 0; Cell < Layout::Columns * Layout::Rows; ++Cell)
	{
		const int32 Index = Screen->RecipeScroll * Layout::Columns + Cell;
		if (Index >= Recipes.Num())
		{
			break;
		}
		const float X = Origin.X + Layout::RecipesX + (Cell % Layout::Columns) * Layout::CellWidth;
		const float Y = Origin.Y + Layout::RecipesY + (Cell / Layout::Columns) * Layout::CellHeight + 2;
		const TCHAR* Button = Index == Menu.GetSelected() ? TEXT("recipe_selected.png") : Index == Hovered ? TEXT("recipe_highlighted.png") : TEXT("recipe.png");
		DrawSprite(LoadSprite(Cutter + Button), X, Y - 1, 16, 18);
		// The item alone, without its count.
		FMcItemStack Icon = Recipes[Index]->Result;
		Icon.Count = 1;
		DrawStack(Icon, X, Y);
	}
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
		DrawRegion(Background, Origin.X, Origin.Y, Layout.Size.X, Layout.Size.Y, 0, 0, Layout.Size.X, Layout.Size.Y);
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
	case McGui::EScreen::Stonecutter:
		DrawGuiText(Screen->Title, Origin.X + 8.0f, Origin.Y + 4.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		if (Screen->Stonecutter)
		{
			DrawStonecutter(Origin, FIntPoint(FMath::FloorToInt(GuiMouse.X), FMath::FloorToInt(GuiMouse.Y)) - Origin);
		}
		break;
	case McGui::EScreen::Anvil:
		DrawGuiText(Screen->Title, Origin.X + 60.0f, Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		if (Screen->Anvil)
		{
			DrawAnvil(Origin);
		}
		break;
	case McGui::EScreen::Grindstone:
		DrawGuiText(Screen->Title, Origin.X + 8.0f, Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		break;
	case McGui::EScreen::Smithing:
		DrawGuiText(Screen->Title, Origin.X + 44.0f, Origin.Y + 15.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		break;
	case McGui::EScreen::Enchanting:
		DrawGuiText(Screen->Title, Origin.X + 12.0f, Origin.Y + 5.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		DrawEnchanting(Origin, FIntPoint(FMath::FloorToInt(GuiMouse.X), FMath::FloorToInt(GuiMouse.Y)) - Origin);
		break;
	case McGui::EScreen::Chest:
		DrawGuiText(Screen->Title, Origin.X + 8.0f, Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + Layout.Rows * 18.0f + 20.0f, 0x404040, false);
		break;
	case McGui::EScreen::Creative:
		DrawCreative(Origin);
		break;
	}

	for (int32 Slot = 0; Slot < Menu.NumSlots() && Slot < Layout.Slots.Num(); ++Slot)
	{
		if (!Layout.IsShown(Slot))
		{
			continue;
		}
		const FIntPoint At = Origin + Layout.Slots[Slot];
		DrawStack(Menu.GetStack(Slot), static_cast<float>(At.X), static_cast<float>(At.Y));
		if (Slot == HoverSlot)
		{
			DrawFill(static_cast<float>(At.X), static_cast<float>(At.Y), 16, 16, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
		}
	}

	// The stack on the cursor, centred on it; else the name of what is under it, or what an enchanting option gives.
	const int32 Option = Screen->Enchanting ? McGui::Enchanting::FindButton(FIntPoint(FMath::FloorToInt(GuiMouse.X), FMath::FloorToInt(GuiMouse.Y)) - Origin) : -1;
	if (Menu.GetCarried().IsEmpty() && Option >= 0 && Screen->Enchanting->GetOffers().Costs[Option] > 0)
	{
		const McEnchanting::FOffers& Offers = Screen->Enchanting->GetOffers();
		const McEnchanting::FEnchant& Clue = Offers.Clues[Option];
		const FString Name = McLang::Get(TEXT("enchantment.") + Clue.Name.ToString().Replace(TEXT(":"), TEXT("."))) + TEXT(" ") + McLang::Get(FString::Printf(TEXT("enchantment.level.%d"), Clue.Level));
		TArray<TPair<FString, uint32>> Lines;
		Lines.Add({ McLang::Format(TEXT("container.enchant.clue"), { Name }), 0xFFFFFF });
		const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwningPawn());
		const UMcPlayerDataComponent* Viewer = Pawn ? Pawn->FindComponentByClass<UMcPlayerDataComponent>() : nullptr;
		if (Viewer && !Viewer->bCreative)
		{
			const int32 Need = Option + 1;
			const FMcItemStack& Fuel = Menu.GetStack(FMcEnchantMenu::LapisSlot);
			const bool bLevel = Viewer->Stats.XpLevel >= Offers.Costs[Option];
			if (!bLevel)
			{
				Lines.Add({ FString(), 0xFFFFFF });
				Lines.Add({ McLang::Format(TEXT("container.enchant.level.requirement"), { FString::FromInt(Offers.Costs[Option]) }), 0xFF5555 });
			}
			else
			{
				Lines.Add({ FString(), 0xFFFFFF });
				Lines.Add({ Need == 1 ? McLang::Get(TEXT("container.enchant.lapis.one")) : McLang::Format(TEXT("container.enchant.lapis.many"), { FString::FromInt(Need) }), Fuel.Count >= Need ? 0xAAAAAAu : 0xFF5555u });
				Lines.Add({ Need == 1 ? McLang::Get(TEXT("container.enchant.level.one")) : McLang::Format(TEXT("container.enchant.level.many"), { FString::FromInt(Need) }), 0xAAAAAA });
			}
		}
		float Wide = 0.0f;
		for (const TPair<FString, uint32>& Line : Lines)
		{
			Wide = FMath::Max(Wide, TextWidth(Line.Key));
		}
		const float X = static_cast<float>(GuiMouse.X) + 12.0f;
		const float Y = static_cast<float>(GuiMouse.Y) - 12.0f;
		DrawFill(X - 3, Y - 3, Wide + 5, Lines.Num() * 10.0f + 4, FLinearColor(0.06f, 0.0f, 0.06f, 0.9f));
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			DrawGuiText(Lines[Index].Key, X, Y + Index * 10.0f, Lines[Index].Value, true);
		}
	}
	else if (Menu.GetCarried().IsEmpty() && Screen->Stonecutter && HoverSlot < 0)
	{
		const int32 Recipe = McGui::Stonecutter::FindRecipe(FIntPoint(FMath::FloorToInt(GuiMouse.X), FMath::FloorToInt(GuiMouse.Y)) - Origin, Screen->RecipeScroll, Screen->Stonecutter->GetRecipes().Num());
		if (Recipe >= 0)
		{
			const FString Name = GetItemName(Screen->Stonecutter->GetRecipes()[Recipe]->Result);
			const float X = static_cast<float>(GuiMouse.X) + 12.0f;
			const float Y = static_cast<float>(GuiMouse.Y) - 12.0f;
			DrawFill(X - 3, Y - 3, TextWidth(Name) + 5, 14, FLinearColor(0.06f, 0.0f, 0.06f, 0.9f));
			DrawGuiText(Name, X, Y, 0xFFFFFF, true);
		}
	}
	else if (!Menu.GetCarried().IsEmpty())
	{
		DrawStack(Menu.GetCarried(), static_cast<float>(GuiMouse.X) - 8.0f, static_cast<float>(GuiMouse.Y) - 8.0f);
	}
	else if ((HoverSlot >= 0 && HoverSlot < Menu.NumSlots() && !Menu.GetStack(HoverSlot).IsEmpty()) || (Screen->bCreative && Screen->CreativeHover >= 0))
	{
		const FString Name = HoverSlot >= 0 ? GetItemName(Menu.GetStack(HoverSlot)) : DisplayName(Screen->CreativeItems[Screen->CreativeHover]);
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
	// The look chosen last time (options.json), once a run; -McEnhanced picks the enhanced one for development.
	static bool bLookLoaded = false;
	if (!bLookLoaded)
	{
		bLookLoaded = true;
		McRenderMode::SetEnhanced(FMcClientOptions::Load().bEnhancedGraphics || FParse::Param(FCommandLine::Get(), TEXT("McEnhanced")));
	}
	++ShakeCounter;
	Scale = McGui::GetGuiScale(Canvas->SizeX, Canvas->SizeY, GuiScaleSetting);
	const int32 Width = Canvas->SizeX / Scale;
	const int32 Height = Canvas->SizeY / Scale;
	if (McLaunch::IsMenu(GetWorld()))
	{
		DrawMenus(Width, Height, true);
		return;
	}

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
	if (bDead && (ActiveMenu == EMenu::Pause || ActiveMenu == EMenu::Chat))
	{
		// The death screen takes over (and the input with it).
		ActiveMenu = EMenu::None;
		bMenuInput = false;
	}
	SyncScreen(Data);
	if (!bDead && ActiveMenu == EMenu::None)
	{
		PollInput(Width, Height, Data);
	}
	// A click may have closed the window (or the server did).
	SyncScreen(Data);

	const McGui::FHudLayout Layout = McGui::ComputeHud(Width, Height, Data ? Data->Stats : FMcPlayerStats());
	// Minecraft draws the crosshair inverting what is behind it; plain white for now.
	if (!Screen.IsValid() && ActiveMenu == EMenu::None)
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
		// Creative has no health, hunger, armour, air or experience to show (minecraft.wiki: Creative).
		if (!Data->bCreative)
		{
			DrawBars(Width, Height, Data);
		}
	}
	if (!bDead && !Screen.IsValid() && ActiveMenu != EMenu::Pause)
	{
		DrawChat(Width, Height, ActiveMenu == EMenu::Chat);
	}
	if (Screen.IsValid() && !bDead)
	{
		DrawScreen(Width, Height);
	}
	if (bDead)
	{
		ShowDeathScreen(Width, Height, *Player);
	}
	else if (ActiveMenu == EMenu::Pause || ActiveMenu == EMenu::Chat)
	{
		DrawMenus(Width, Height, false);
	}
}

// ---- Chat ----

TArray<FString> AMcHUD::WrapText(const FString& Text, float MaxWidth) const
{
	TArray<FString> Lines;
	FString Line;
	int32 LastSpace = INDEX_NONE;
	for (const TCHAR Char : Text)
	{
		if (Char == TEXT(' '))
		{
			LastSpace = Line.Len();
		}
		Line.AppendChar(Char);
		if (TextWidth(Line) > MaxWidth && Line.Len() > 1)
		{
			// Break at the last space if there is one, else mid-word.
			if (LastSpace > 0)
			{
				Lines.Add(Line.Left(LastSpace));
				Line = Line.Mid(LastSpace + 1);
			}
			else
			{
				Lines.Add(Line.LeftChop(1));
				Line = Line.Right(1);
			}
			LastSpace = INDEX_NONE;
		}
	}
	Lines.Add(Line);
	return Lines;
}

void AMcHUD::DrawChat(int32 Width, int32 Height, bool bOpen)
{
	LoadFont();
	const TArray<UMcPlayerDataComponent::FChatLine>& Log = UMcPlayerDataComponent::GetChatLog();
	// As the game draws it: 320 wide, 9 a line, the newest at the bottom, just above the hotbar.
	const float ChatWidth = FMath::Min(320.0f, Width - 8.0f);
	const double Now = FPlatformTime::Seconds();
	const int32 MaxLines = bOpen ? 20 : 10;
	float Y = Height - 48.0f;
	int32 Shown = 0;
	for (int32 Index = Log.Num() - 1; Index >= 0 && Shown < MaxLines; --Index)
	{
		const UMcPlayerDataComponent::FChatLine& Entry = Log[Index];
		float Alpha = 1.0f;
		if (!bOpen)
		{
			// Ten seconds on screen, the last of them fading.
			const double Age = Now - Entry.Time;
			if (Age >= 10.0)
			{
				break;
			}
			Alpha = static_cast<float>(FMath::Clamp(10.0 - Age, 0.0, 1.0));
		}
		const TArray<FString> Wrapped = WrapText(Entry.Text, ChatWidth - 4.0f);
		for (int32 Part = Wrapped.Num() - 1; Part >= 0 && Shown < MaxLines; --Part, ++Shown)
		{
			Y -= 9.0f;
			DrawFill(2.0f, Y, ChatWidth, 9.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f * Alpha));
			DrawGuiText(Wrapped[Part], 4.0f, Y + 1.0f, Entry.Rgb, true, Alpha);
		}
	}
}

void AMcHUD::DrawChatInput(int32 Width, int32 Height)
{
	LoadFont();
	APlayerController* Player = GetOwningPlayerController();
	UMcPlayerDataComponent* Data = GetOwningPawn() ? GetOwningPawn()->FindComponentByClass<UMcPlayerDataComponent>() : nullptr;
	if (!Data)
	{
		OpenMenu(EMenu::None);
		return;
	}
	ChatDraft.Append(MenuTyped.Left(FMath::Max(0, 256 - ChatDraft.Len())));
	if (bMenuBackspace && !ChatDraft.IsEmpty())
	{
		ChatDraft.LeftChopInline(1);
	}
	if (Player && SentChat.Num() > 0)
	{
		if (Player->WasInputKeyJustPressed(EKeys::Up))
		{
			SentChatIndex = FMath::Max(0, SentChatIndex - 1);
			ChatDraft = SentChat[SentChatIndex];
		}
		else if (Player->WasInputKeyJustPressed(EKeys::Down))
		{
			SentChatIndex = FMath::Min(SentChat.Num(), SentChatIndex + 1);
			ChatDraft = SentChat.IsValidIndex(SentChatIndex) ? SentChat[SentChatIndex] : FString();
		}
	}
	if (bMenuEnter)
	{
		const FString Line = ChatDraft.TrimStartAndEnd();
		if (!Line.IsEmpty())
		{
			if (SentChat.Num() == 0 || SentChat.Last() != Line)
			{
				SentChat.Add(Line);
			}
			Data->SendChat(Line);
		}
		OpenMenu(EMenu::None);
		return;
	}
	// The box along the bottom, showing the end of a long line, with a blinking caret.
	DrawFill(2.0f, Height - 14.0f, Width - 4.0f, 12.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
	FString Shown = ChatDraft;
	while (!Shown.IsEmpty() && TextWidth(Shown) > Width - 14.0f)
	{
		Shown.RightChopInline(1);
	}
	const bool bCaret = FMath::Fmod(FPlatformTime::Seconds(), 0.6) < 0.3;
	DrawGuiText(bCaret ? Shown + TEXT("_") : Shown, 4.0f, Height - 12.0f, 0xFFFFFF, true);
}
