// The first-run setup screen: builds the cache from the player's own Minecraft 26.3 install (McCacheSetup.h).

#include "McHUD.h"
#include "McCache.h"
#include "McCacheSetup.h"
#include "McGameViewportClient.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/PlatformApplicationMisc.h"

namespace
{
	const FLinearColor Panel(0.07f, 0.07f, 0.08f, 1.0f);
	const FLinearColor Light(0.92f, 0.92f, 0.92f, 1.0f);
	const FLinearColor Dim(0.62f, 0.62f, 0.64f, 1.0f);
	const FLinearColor Bad(1.0f, 0.4f, 0.4f, 1.0f);
	const FLinearColor Green(0.33f, 0.72f, 0.28f, 1.0f);
}

void AMcHUD::FinishSetup()
{
	// The pictures and font that were looked for before they existed are looked for again.
	Sprites.Reset();
	LayerTextures.Reset();
	GlyphWidths.Reset();
	GalacticWidths.Reset();
	bCacheReady = true;
	CacheBuild.Reset();
	OpenMenu(EMenu::Title);
}

void AMcHUD::DrawSetup(int32 Width, int32 Height)
{
	// Everything here is in screen pixels, not GUI pixels.
	const float W = static_cast<float>(Canvas->SizeX);
	const float H = static_cast<float>(Canvas->SizeY);
	UFont* Font = GEngine->GetMediumFont();
	const float K = FMath::Clamp(H / 720.0f, 0.75f, 2.0f);
	const FVector2D Mouse = GuiMouse * Scale;

	if (CacheBuild && CacheBuild->IsDone())
	{
		FinishSetup();
		return;
	}
	const bool bBuilding = CacheBuild && CacheBuild->IsRunning();
	if (CacheBuild && !bBuilding)
	{
		SetupError = CacheBuild->GetError();
		CacheBuild.Reset();
	}

	DrawRect(Panel, 0.0f, 0.0f, W, H);
	const float Left = W / 2.0f - 340.0f * K;
	const float Right = W / 2.0f + 340.0f * K;
	float Y = H * 0.14f;
	const auto Line = [&](const FString& Text, const FLinearColor& Color, float Size = 1.0f)
	{
		float TextW, TextH;
		GetTextSize(Text, TextW, TextH, Font, K * Size);
		DrawText(Text, Color, Left, Y, Font, K * Size);
		Y += TextH + 6.0f * K;
	};
	const auto Wrapped = [&](const FString& Text, const FLinearColor& Color)
	{
		// Words onto lines as wide as the panel.
		TArray<FString> Words;
		Text.ParseIntoArray(Words, TEXT(" "));
		FString Current;
		for (const FString& Word : Words)
		{
			const FString Next = Current.IsEmpty() ? Word : Current + TEXT(" ") + Word;
			float TextW, TextH;
			GetTextSize(Next, TextW, TextH, Font, K);
			if (TextW > Right - Left && !Current.IsEmpty())
			{
				Line(Current, Color);
				Current = Word;
			}
			else
			{
				Current = Next;
			}
		}
		Line(Current, Color);
	};
	const auto Button = [&](const FString& Label, float X, float ButtonY, float ButtonW, bool bEnabled = true)
	{
		const float ButtonH = 34.0f * K;
		const bool bOver = bEnabled && Mouse.X >= X && Mouse.X < X + ButtonW && Mouse.Y >= ButtonY && Mouse.Y < ButtonY + ButtonH;
		DrawRect(bOver ? FLinearColor(0.34f, 0.34f, 0.38f) : FLinearColor(0.2f, 0.2f, 0.23f), X, ButtonY, ButtonW, ButtonH);
		float TextW, TextH;
		GetTextSize(Label, TextW, TextH, Font, K);
		DrawText(Label, bEnabled ? Light : Dim, X + (ButtonW - TextW) / 2.0f, ButtonY + (ButtonH - TextH) / 2.0f, Font, K);
		return bOver && bMenuClick;
	};

	Line(TEXT("First-time setup"), Light, 1.6f);
	Y += 10.0f * K;
	Wrapped(TEXT("This game uses the pictures, sounds and data of your own copy of Minecraft Java Edition 26.3. It reads them once from your installation and keeps its own copy beside the game. Nothing is downloaded or sent anywhere."), Dim);
	Y += 12.0f * K;

	if (bBuilding)
	{
		Line(CacheBuild->GetStatus(), Light);
		Y += 8.0f * K;
		const float Progress = FMath::Clamp(CacheBuild->GetProgress(), 0.0f, 1.0f);
		DrawRect(FLinearColor(0.2f, 0.2f, 0.23f), Left, Y, Right - Left, 22.0f * K);
		DrawRect(Green, Left, Y, (Right - Left) * Progress, 22.0f * K);
		Y += 30.0f * K;
		Line(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Progress * 100.0f)), Dim);
		Y += 8.0f * K;
		Line(TEXT("This takes 5 to 10 minutes, once. Leave the game open."), Dim);
		return;
	}

	Line(TEXT("Minecraft folder (leave empty to look in the usual places):"), Light);
	const float FieldH = 30.0f * K;
	const bool bOverField = Mouse.X >= Left && Mouse.X < Right && Mouse.Y >= Y && Mouse.Y < Y + FieldH;
	if (bOverField && bMenuClick)
	{
		FocusedField = 0;
	}
	FieldCount = 1;
	if (FocusedField == 0)
	{
		SetupFolder += MenuTyped;
		if (bMenuBackspace && !SetupFolder.IsEmpty())
		{
			SetupFolder.LeftChopInline(1);
		}
		APlayerController* Player = GetOwningPlayerController();
		if (Player && Player->IsInputKeyDown(EKeys::LeftControl) && Player->WasInputKeyJustPressed(EKeys::V))
		{
			FString Pasted;
			FPlatformApplicationMisc::ClipboardPaste(Pasted);
			SetupFolder += Pasted.Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(""));
		}
	}
	DrawRect(FocusedField == 0 ? FLinearColor(0.5f, 0.5f, 0.55f) : FLinearColor(0.3f, 0.3f, 0.34f), Left - 1.0f, Y - 1.0f, Right - Left + 2.0f, FieldH + 2.0f);
	DrawRect(FLinearColor::Black, Left, Y, Right - Left, FieldH);
	FString Shown = SetupFolder;
	float TextW, TextH;
	GetTextSize(Shown, TextW, TextH, Font, K);
	while (TextW > Right - Left - 20.0f * K && !Shown.IsEmpty())
	{
		Shown.RightChopInline(1);
		GetTextSize(Shown, TextW, TextH, Font, K);
	}
	DrawText(Shown + (FMath::FloorToInt(FPlatformTime::Seconds() * 1.6) % 2 == 0 && FocusedField == 0 ? TEXT("_") : TEXT("")), Light, Left + 8.0f * K, Y + (FieldH - TextH) / 2.0f, Font, K);
	Y += FieldH + 6.0f * K;
	const TArray<FString> Found = FMcCacheBuilder::DefaultRoots();
	Line(Found.IsEmpty() ? FString(TEXT("No launcher folder found in the usual places.")) : TEXT("Found: ") + FString::Join(Found, TEXT("   ")), Dim);
	Y += 8.0f * K;

	if (!SetupError.IsEmpty())
	{
		Wrapped(SetupError, Bad);
		Y += 8.0f * K;
	}

	// -McAutoSetup presses Set up once, to test a packaged game without a hand on the mouse.
	static bool bAutoSetup = FParse::Param(FCommandLine::Get(), TEXT("McAutoSetup"));
	const bool bAuto = bAutoSetup && SetupError.IsEmpty();
	bAutoSetup = false;
	if (Button(SetupError.IsEmpty() ? TEXT("Set up") : TEXT("Try again"), Left, Y, 200.0f * K) || bMenuEnter || bAuto)
	{
		TArray<FString> Roots = Found;
		if (!SetupFolder.TrimStartAndEnd().IsEmpty())
		{
			Roots.Insert(SetupFolder, 0);
		}
		FMcCacheBuilder::FSources Sources;
		if (FMcCacheBuilder::Find(Roots, Sources, SetupError))
		{
			SetupError.Reset();
			CacheBuild = MakeShared<FMcCacheBuilder>();
			CacheBuild->Start(Sources);
		}
	}
	if (Button(TEXT("Quit"), Left + 220.0f * K, Y, 120.0f * K))
	{
		if (APlayerController* Player = GetOwningPlayerController())
		{
			Player->ConsoleCommand(TEXT("quit"));
		}
	}
}
