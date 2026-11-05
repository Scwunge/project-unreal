#include "McClient.h"
#include "McJavaImport.h"
#include "McLaunch.h"
#include "McPerf.h"
#include "UObject/UObjectGlobals.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogMcClient);

class FMcClientModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
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
