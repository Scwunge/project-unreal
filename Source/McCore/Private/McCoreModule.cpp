#include "McCore.h"
#include "McPerf.h"
#include "McProcess.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY(LogMcCore);

class FMcCoreModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override
	{
		// Server mode: -server or -McServer on the packaged game (see McProcess.h). First of the game's modules, so
		// every later one, and the engine's own start, sees the process as a server.
		if (McProcess::WantsServer() && !IsRunningDedicatedServer() && !GIsEditor && !IsRunningCommandlet())
		{
			GIsClient = false;
			GIsServer = true;
			// Nothing to draw, so nothing to pace the loop: without a cap it spins a core.
			if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
			{
				MaxFps->Set(60.f, ECVF_SetByCommandline);
			}
			McProcess::Print(FString::Printf(TEXT("Server mode: no window, no menu, no local player. Arguments: %s"), FCommandLine::Get()));
		}
		McPerf::Start();
	}
};

IMPLEMENT_MODULE(FMcCoreModule, McCore);
