#include "McRender.h"
#include "McPerf.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "RenderTimer.h"
#include "RHIGlobals.h"

DEFINE_LOG_CATEGORY(LogMcRender);

class FMcRenderModule : public FDefaultModuleImpl
{
public:
	virtual void StartupModule() override
	{
		if (!McPerf::IsOn())
		{
			return;
		}
		FCoreDelegates::OnEndFrame.AddLambda([]
		{
			static bool bFirst = true;
			if (bFirst && GEngine && GDynamicRHI)
			{
				bFirst = false;
				// The harness's -McPerfCvars=a=1,b=2 (no console in Shipping), and -McPerfUncap to see what the frame could be.
				FString Cvars;
				if (FParse::Value(FCommandLine::Get(), TEXT("-McPerfCvars="), Cvars, /*bShouldStopOnSeparator=*/false))
				{
					TArray<FString> Pairs;
					Cvars.TrimQuotes().ParseIntoArray(Pairs, TEXT(","));
					for (const FString& Pair : Pairs)
					{
						FString Name, Value;
						if (Pair.Split(TEXT("="), &Name, &Value))
						{
							if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name))
							{
								Var->Set(*Value, ECVF_SetByConsole);
							}
							else
							{
								McPerf::Event(FString::Printf(TEXT("NO_CVAR %s"), *Name));
							}
						}
					}
				}
				if (FParse::Param(FCommandLine::Get(), TEXT("McPerfUncap")))
				{
					GEngine->bSmoothFrameRate = false;
				}
				const IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"));
				const IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
				McPerf::Event(FString::Printf(TEXT("RHI %s %s vsync=%d maxfps=%.0f smooth=%d"), GDynamicRHI->GetName(), *GRHIAdapterName,
					VSync ? VSync->GetInt() : -1, MaxFps ? MaxFps->GetFloat() : -1.0f, GEngine->bSmoothFrameRate ? 1 : 0));
			}
			McPerf::SetFrameTimes(FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
				FPlatformTime::ToMilliseconds(GRHIThreadTime), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
		});
	}
};

IMPLEMENT_MODULE(FMcRenderModule, McRender);
