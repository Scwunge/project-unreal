#pragma once

#include "CoreMinimal.h"
#include "McCore.h"
#include "Misc/CoreMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Whether this process is a dedicated server. Unreal's IsRunningDedicatedServer() is fixed false in a Game target
// (only a Server target, which needs an engine built from source, or the editor can be one), so the packaged game
// becomes one itself: with -server (or -McServer) McCore's module start sets GIsClient false and GIsServer true,
// before the engine makes its viewport and local player, and the engine then loads the map as a listening server with
// no one playing on it, which is what it does for a Server target. McIsServerProcess() is true for that and for the
// real thing.
namespace McProcess
{
	// The command line asks for server mode.
	inline bool WantsServer()
	{
		return FParse::Param(FCommandLine::Get(), TEXT("server")) || FParse::Param(FCommandLine::Get(), TEXT("McServer"));
	}

	// One line of the server's own output, in every build (Shipping compiles UE_LOG out): to stdout, flushed, and
	// appended to Saved/Logs/McServer.log. Start-up stages and the console's replies go through it.
	MCCORE_API void Print(const FString& Line);

	// True in a dedicated server process, however it was started. Read the flags, not the command line, so it also
	// holds in the editor's -server and a Server target.
	inline bool IsServer()
	{
		return IsRunningDedicatedServer() || (GIsServer && !GIsClient && !GIsEditor && !IsRunningCommandlet());
	}
}
