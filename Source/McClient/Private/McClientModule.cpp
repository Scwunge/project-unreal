#include "McClient.h"
#include "McJavaImport.h"
#include "McLaunch.h"
#include "McPerf.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#if MC_WITH_RENDER
#include "Engine/Engine.h"
#include "Framework/Application/SlateApplication.h"
#include "Async/ParallelFor.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "McCache.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FileHelper.h"
#include "Tasks/Task.h"
#include <atomic>
#include "McWorldActor.h"
#include "MoviePlayer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#endif

DEFINE_LOG_CATEGORY(LogMcClient);

#if MC_WITH_RENDER
namespace
{
	// Opening a world (setting up its generator reads some 2,600 structure and worldgen files: 3 s warm, 28 s once with a
	// cold disk cache) and leaving one (saving it) both happen inside one blocking map load. Without this the window shows
	// the last menu frame, frozen, the whole time. The engine's movie player draws this on its own thread and takes it
	// down when the map has loaded.
	void ShowMapLoadScreen(const FWorldContext& Context, const FString& MapName)
	{
		if (Context.WorldType != EWorldType::Game || !FSlateApplication::IsInitialized() || !GetMoviePlayer()
			|| GetMoviePlayer()->IsMovieCurrentlyPlaying())
		{
			return;
		}
		// The world being left is still up: if it is a game world (it has the world actor), this load saves it.
		bool bLeaving = false;
		if (UWorld* Old = Context.World())
		{
			bLeaving = TActorIterator<AMcWorldActor>(Old) ? true : false;
		}
		const FText Message = bLeaving ? NSLOCTEXT("Mc", "SavingWorld", "Saving world...") : NSLOCTEXT("Mc", "LoadingWorld", "Loading world...");
		FLoadingScreenAttributes Attributes;
		Attributes.bAutoCompleteWhenLoadingCompletes = true;
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
			UE_LOG(LogMcClient, Log, TEXT("Map load screen shown (%s)"), bLeaving ? TEXT("saving") : TEXT("loading"));
		}
	}

	// Reads every structure and worldgen file once in the background as the game starts, so the first world opened
	// finds them in the system's file cache: read cold (after a restart, each file scanned as it is first opened) the
	// generator's setup took 28 s instead of 3.
	void WarmWorldFiles()
	{
		if (!McCache::IsComplete())
		{
			return;
		}
		UE::Tasks::Launch(UE_SOURCE_LOCATION, []()
		{
			const double Start = FPlatformTime::Seconds();
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *McCache::Data(TEXT("structure")), TEXT("*.*"), true, false, true);
			IFileManager::Get().FindFilesRecursive(Files, *McCache::Data(TEXT("worldgen")), TEXT("*.*"), true, false, false);
			std::atomic<int64> Bytes{ 0 };
			ParallelFor(Files.Num(), [&Files, &Bytes](int32 Index)
			{
				TArray<uint8> Contents;
				if (FFileHelper::LoadFileToArray(Contents, *Files[Index], FILEREAD_Silent))
				{
					Bytes += Contents.Num();
				}
			}, EParallelForFlags::BackgroundPriority);
			UE_LOG(LogMcClient, Log, TEXT("Read ahead %d structure and worldgen files (%lld KB) in %.0f ms"), Files.Num(), Bytes.load() / 1024,
				(FPlatformTime::Seconds() - Start) * 1000.0);
		}, LowLevelTasks::ETaskPriority::BackgroundLow);
	}
}
#endif

class FMcClientModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
#if MC_WITH_RENDER
		if (!IsRunningDedicatedServer() && !IsRunningCommandlet() && !GIsEditor)
		{
			FCoreUObjectDelegates::PreLoadMapWithContext.AddStatic(&ShowMapLoadScreen);
			FCoreDelegates::OnPostEngineInit.AddStatic(&WarmWorldFiles);
		}
#endif
		if (McPerf::IsOn())
		{
			FCoreUObjectDelegates::GetPreGarbageCollectDelegate().AddStatic(&McPerf::GcBegin);
			FCoreUObjectDelegates::GetPostGarbageCollect().AddStatic(&McPerf::GcEnd);
		}
		// A dedicated server listens on server.properties' server-port unless the command line gives one (-Port=). The
		// engine reads the port from the command line when it first makes a URL, after the game module has started.
		FString Port;
		if (IsRunningDedicatedServer() && !FParse::Value(FCommandLine::Get(), TEXT("Port="), Port))
		{
			const int32 ServerPort = FMcServerProperties::Get().GetInt(TEXT("server-port"), 25565);
			FCommandLine::Append(*FString::Printf(TEXT(" -Port=%d"), ServerPort));
		}

		// -McImportWorld=<folder of a Minecraft Java world> [-McImportName=<name>]: copies the world into Saved/Worlds
		// (the folder itself is only read) and opens the copy, unless -McWorld names another.
		FString ImportFrom;
		if (FParse::Value(FCommandLine::Get(), TEXT("-McImportWorld="), ImportFrom))
		{
			FString ImportName;
			FParse::Value(FCommandLine::Get(), TEXT("-McImportName="), ImportName);
			// The chunks are converted while the world is played, unless -McImportWhole asks for all of them first.
			McJavaImport::FStats Stats;
			const FString Folder = FParse::Param(FCommandLine::Get(), TEXT("McImportWhole")) ? McJavaImport::ImportWorld(ImportFrom, ImportName, Stats)
				: McJavaImport::BeginImport(ImportFrom, ImportName);
			FString Existing;
			if (!Folder.IsEmpty() && !FParse::Value(FCommandLine::Get(), TEXT("-McWorld="), Existing))
			{
				FCommandLine::Append(*FString::Printf(TEXT(" -McWorld=%s"), *Folder));
			}
		}
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMcClientModule, McClient, "McClient");
