#include "McClient.h"
#include "McLaunch.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogMcClient);

class FMcClientModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		// A dedicated server listens on server.properties' server-port unless the command line gives one (-Port=). The
		// engine reads the port from the command line when it first makes a URL, after the game module has started.
		FString Port;
		if (IsRunningDedicatedServer() && !FParse::Value(FCommandLine::Get(), TEXT("Port="), Port))
		{
			const int32 ServerPort = FMcServerProperties::Get().GetInt(TEXT("server-port"), 25565);
			FCommandLine::Append(*FString::Printf(TEXT(" -Port=%d"), ServerPort));
		}
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE(FMcClientModule, McClient, "McClient");
