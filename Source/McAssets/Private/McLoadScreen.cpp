#include "McLoadScreen.h"
#include "McAssets.h"

#if MC_LOAD_SCREEN
#include "Framework/Application/SlateApplication.h"
#include "MoviePlayer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#endif

namespace
{
	int32 GDepth = 0;
}

FMcLoadScreen::FMcLoadScreen(const FText& Message, bool bShow)
{
#if MC_LOAD_SCREEN
	if (!bShow || GDepth > 0 || !IsInGameThread() || !FSlateApplication::IsInitialized() || !GetMoviePlayer() || GetMoviePlayer()->IsMovieCurrentlyPlaying())
	{
		return;
	}
	FLoadingScreenAttributes Attributes;
	Attributes.bAutoCompleteWhenLoadingCompletes = false;
	Attributes.bWaitForManualStop = true;
	Attributes.bMoviesAreSkippable = false;
	Attributes.MinimumLoadingScreenDisplayTime = 0.0f;
	Attributes.WidgetLoadingScreen = SNew(SBorder)
		.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f))
		[
			SNew(SBox).HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(STextBlock).Text(Message).ColorAndOpacity(FLinearColor::White).Font(FCoreStyle::GetDefaultFontStyle("Bold", 28))
			]
		];
	GetMoviePlayer()->SetupLoadingScreen(Attributes);
	if (GetMoviePlayer()->PlayMovie())
	{
		bOwns = true;
		GDepth = 1;
		UE_LOG(LogMcAssets, Log, TEXT("Loading screen shown"));
	}
#endif
}

FMcLoadScreen::~FMcLoadScreen()
{
#if MC_LOAD_SCREEN
	if (bOwns)
	{
		GetMoviePlayer()->StopMovie();
		GetMoviePlayer()->WaitForMovieToFinish();
		GDepth = 0;
		UE_LOG(LogMcAssets, Log, TEXT("Loading screen closed"));
	}
#endif
}
