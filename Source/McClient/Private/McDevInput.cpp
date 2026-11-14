#include "McDevInput.h"

#include "McClient.h"
#include "McPerf.h"
#include "McPlayerData.h"
#include "McResourcePacks.h"
#if MC_WITH_RENDER
#include "McHUD.h"
#endif
#include "McPlayerController.h"
#include "McPlayerPawn.h"
#include "Engine/GameViewportClient.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "UnrealClient.h"

bool FMcDevInput::Wanted()
{
	static const bool bWanted = FParse::Param(FCommandLine::Get(), TEXT("McInput")) || FCString::Strifind(FCommandLine::Get(), TEXT("-McInput=")) != nullptr
		|| FCString::Strifind(FCommandLine::Get(), TEXT("-McInputFile=")) != nullptr;
	return bWanted;
}

FMcDevInput& FMcDevInput::Get()
{
	static TUniquePtr<FMcDevInput> Instance = []
	{
		FString Script, Path;
		if (FParse::Value(FCommandLine::Get(), TEXT("-McInputFile="), Path) && FFileHelper::LoadFileToString(Script, *Path))
		{
			TArray<FString> Lines;
			Script.ParseIntoArrayLines(Lines);
			Script.Reset();
			for (const FString& Line : Lines)
			{
				// One to a line, so a step may hold ';'; the constructor splits on '\n' when it sees one.
				if (!Line.TrimStart().StartsWith(TEXT("#")))
				{
					Script += Line + TEXT("\n");
				}
			}
		}
		else
		{
			FParse::Value(FCommandLine::Get(), TEXT("-McInput="), Script, false);
			Script = Script.TrimQuotes();
		}
		return MakeUnique<FMcDevInput>(Script);
	}();
	return *Instance;
}

FKey FMcDevInput::ParseKey(const FString& Name)
{
	static const TMap<FString, FName> Aliases = {
		{ TEXT("L"), TEXT("LeftMouseButton") }, { TEXT("R"), TEXT("RightMouseButton") }, { TEXT("M"), TEXT("MiddleMouseButton") },
		{ TEXT("Shift"), TEXT("LeftShift") }, { TEXT("Ctrl"), TEXT("LeftControl") }, { TEXT("Space"), TEXT("SpaceBar") },
		{ TEXT("Esc"), TEXT("Escape") },
	};
	if (const FName* Alias = Aliases.Find(Name))
	{
		return FKey(*Alias);
	}
	return FKey(*Name);
}

bool FMcDevInput::ParseStep(const FString& Text, FStep& Out)
{
	TArray<FString> Fields;
	Text.TrimStartAndEnd().ParseIntoArray(Fields, TEXT(":"));
	if (Fields.Num() == 0)
	{
		return false;
	}
	const FString Verb = Fields[0].ToLower();
	Out = FStep();
	// The rest of the step after the verb, colons and all.
	const FString Rest = Text.TrimStartAndEnd().Mid(Fields[0].Len() + 1);
	static const TMap<FString, FStep::EKind> TextSteps = {
		{ TEXT("mark"), FStep::EKind::Mark }, { TEXT("note"), FStep::EKind::Note }, { TEXT("chat"), FStep::EKind::Chat }, { TEXT("press"), FStep::EKind::MenuPress }, { TEXT("pack"), FStep::EKind::Pack },
	};
	if (const FStep::EKind* Kind = TextSteps.Find(Verb))
	{
		Out.Kind = *Kind;
		Out.Name = Rest;
		return !Rest.IsEmpty();
	}
	if (Verb == TEXT("fly") && Fields.Num() >= 2)
	{
		Out.Kind = FStep::EKind::Fly;
		Out.A = FCString::Atof(*Fields[1]);
		return true;
	}
	if (Verb == TEXT("settle") && Fields.Num() >= 2)
	{
		Out.Kind = FStep::EKind::Settle;
		Out.Seconds = FCString::Atof(*Fields[1]);
		return true;
	}
	auto ReadKeys = [&Out](const FString& List)
	{
		TArray<FString> Names;
		List.ParseIntoArray(Names, TEXT("+"));
		for (const FString& Name : Names)
		{
			Out.Keys.Add(ParseKey(Name));
		}
	};
	if ((Verb == TEXT("hold") || Verb == TEXT("click")) && Fields.Num() >= 3)
	{
		Out.Kind = FStep::EKind::Hold;
		ReadKeys(Fields[1]);
		Out.Seconds = FCString::Atof(*Fields[2]);
		// A fourth field is the name of a shot taken halfway through the hold.
		if (Fields.Num() >= 4)
		{
			Out.Name = Fields[3];
		}
		return Out.Keys.Num() > 0;
	}
	if (Verb == TEXT("key") && Fields.Num() >= 2)
	{
		Out.Kind = FStep::EKind::Key;
		ReadKeys(Fields[1]);
		return Out.Keys.Num() > 0;
	}
	if (Verb == TEXT("look") && Fields.Num() >= 2)
	{
		FString Yaw, Pitch;
		if (!Fields[1].Split(TEXT(","), &Yaw, &Pitch))
		{
			Yaw = Fields[1];
			Pitch = TEXT("0");
		}
		Out.Kind = FStep::EKind::Look;
		Out.A = FCString::Atof(*Yaw);
		Out.B = FCString::Atof(*Pitch);
		return true;
	}
	if (Verb == TEXT("wait") && Fields.Num() >= 2)
	{
		Out.Kind = FStep::EKind::Wait;
		Out.Seconds = FCString::Atof(*Fields[1]);
		return true;
	}
	if (Verb == TEXT("shot") && Fields.Num() >= 2)
	{
		Out.Kind = FStep::EKind::Shot;
		Out.Name = Fields[1];
		return true;
	}
	if (Verb == TEXT("end"))
	{
		Out.Kind = FStep::EKind::End;
		return true;
	}
	return false;
}

FMcDevInput::FMcDevInput(const FString& Script)
{
	TArray<FString> Texts;
	Script.ParseIntoArray(Texts, Script.Contains(TEXT("\n")) ? TEXT("\n") : TEXT(";"));
	for (const FString& Text : Texts)
	{
		FStep Step;
		if (ParseStep(Text, Step))
		{
			Steps.Add(MoveTemp(Step));
		}
		else
		{
			UE_LOG(LogMcClient, Error, TEXT("-McInput: cannot read the step \"%s\""), *Text);
		}
	}
	FParse::Value(FCommandLine::Get(), TEXT("-McInputDelay="), StartDelay);
	Waited = -StartDelay;
}

void FMcDevInput::TakeShot(const FString& Name)
{
	const FString Path = FPaths::ProjectDir() / TEXT("Saved/Shots") / (Name + TEXT(".png"));
	UE_LOG(LogMcClient, Display, TEXT("McInput: saving %s"), *Path);
	FScreenshotRequest::RequestScreenshot(Path, false, false);
}

void FMcDevInput::Press(AMcPlayerController& Controller, const FKey& Key, bool bDown)
{
	Controller.InputKey(FInputKeyEventArgs(nullptr, FInputDeviceId::CreateFromInternalId(0), Key, bDown ? IE_Pressed : IE_Released, 1.0f, false, 0));
}

void FMcDevInput::Tick(AMcPlayerController& Controller, float DeltaSeconds)
{
	for (const FKey& Key : ToRelease)
	{
		Press(Controller, Key, false);
	}
	ToRelease.Reset();

	if (bEnding)
	{
#if CSV_PROFILER
		if (FCsvProfiler::IsCapturing() || FCsvProfiler::Get()->IsEndCapturePending())
		{
			return;
		}
#endif
		bEnding = false;
		FPlatformMisc::RequestExit(false, TEXT("McInput"));
		return;
	}

	AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(Controller.GetPawn());
	if (!Pawn)
	{
		// Between worlds, or at the title. The script takes up again a while after a player is back.
		if (bHadPawn)
		{
			bHadPawn = false;
			Waited = -StartDelay;
		}
		return;
	}
	if (!bHadPawn)
	{
		bHadPawn = true;
		McPerf::Event(TEXT("PLAYER_IN_WORLD"));
	}
	if (Pawn->IsDead())
	{
		DeadFor += DeltaSeconds;
		if (DeadFor >= 2.0f)
		{
			DeadFor = 0.0f;
			McPerf::Event(FString::Printf(TEXT("RESPAWN at %s"), *Pawn->GetActorLocation().ToString()));
			Pawn->RequestRespawn();
		}
		return;
	}
	DeadFor = 0.0f;
	if (Waited < 0.0f)
	{
		Waited += DeltaSeconds;
		return;
	}

	if (bRunning)
	{
		Left -= DeltaSeconds;
		// Losing the window's focus lets go of every key; a held step keeps them down.
		if (Current.Kind == FStep::EKind::Hold && Left > 0.0f)
		{
			for (const FKey& Key : Current.Keys)
			{
				if (!Controller.IsInputKeyDown(Key))
				{
					Press(Controller, Key, true);
				}
			}
		}
		if (Current.Kind == FStep::EKind::Hold && !Current.Name.IsEmpty() && !bHoldShot && Left <= Current.Seconds * 0.5f)
		{
			bHoldShot = true;
			TakeShot(Current.Name);
		}
		if (Current.Kind == FStep::EKind::Settle && Left > 0.0f && McPerf::GetCounter(EMcPerfCounter::Chunks) > 0
			&& McPerf::GetCounter(EMcPerfCounter::Dirty) == 0 && McPerf::GetCounter(EMcPerfCounter::InFlight) == 0
			&& McPerf::GetCounter(EMcPerfCounter::Streaming) == 0)
		{
			McPerf::Event(FString::Printf(TEXT("SETTLED after %.1f s, %lld chunks, %lld meshes"), Current.Seconds - Left, McPerf::GetCounter(EMcPerfCounter::Chunks), McPerf::GetCounter(EMcPerfCounter::Meshes)));
			Left = 0.0f;
		}
		if (Left > 0.0f)
		{
			return;
		}
		if (Current.Kind == FStep::EKind::Settle && McPerf::GetCounter(EMcPerfCounter::Dirty) + McPerf::GetCounter(EMcPerfCounter::InFlight) + McPerf::GetCounter(EMcPerfCounter::Streaming) > 0)
		{
			McPerf::Event(FString::Printf(TEXT("NOT_SETTLED after %.1f s, %lld dirty, %lld in flight, %lld streaming"), Current.Seconds, McPerf::GetCounter(EMcPerfCounter::Dirty), McPerf::GetCounter(EMcPerfCounter::InFlight), McPerf::GetCounter(EMcPerfCounter::Streaming)));
		}
		if (Current.Kind == FStep::EKind::Hold)
		{
			for (const FKey& Key : Current.Keys)
			{
				Press(Controller, Key, false);
			}
		}
		bRunning = false;
	}

	// One step a frame; steps that take no time run through to the next that does.
	while (!bRunning && Steps.IsValidIndex(Next))
	{
		Current = Steps[Next++];
		switch (Current.Kind)
		{
		case FStep::EKind::Hold:
			for (const FKey& Key : Current.Keys)
			{
				Press(Controller, Key, true);
			}
			Left = Current.Seconds;
			bHoldShot = false;
			bRunning = true;
			break;
		case FStep::EKind::Wait:
			Left = Current.Seconds;
			bRunning = true;
			break;
		case FStep::EKind::Key:
			for (const FKey& Key : Current.Keys)
			{
				Press(Controller, Key, true);
				ToRelease.Add(Key);
			}
			// Held for a frame so the press is seen.
			Left = 0.05f;
			bRunning = true;
			break;
		case FStep::EKind::Look:
			Pawn->DevTurn(Current.A, Current.B);
			break;
		case FStep::EKind::Shot:
			TakeShot(Current.Name);
			// Give the frame time to be captured.
			Left = 0.5f;
			bRunning = true;
			break;
		case FStep::EKind::End:
#if CSV_PROFILER
			if (FCsvProfiler::IsCapturing())
			{
				FCsvProfiler::Get()->EndCapture();
			}
#endif
			bEnding = true;
			Next = Steps.Num();
			break;
		case FStep::EKind::Mark:
			McPerf::Mark(Current.Name);
			break;
		case FStep::EKind::Note:
			McPerf::Event(Current.Name);
			break;
		case FStep::EKind::Chat:
			McPerf::Event(FString::Printf(TEXT("CHAT %s"), *Current.Name));
			if (Pawn->Data)
			{
				Pawn->Data->SendChat(Current.Name);
			}
			break;
		case FStep::EKind::MenuPress:
#if MC_WITH_RENDER
			AMcHUD::QueueMenuPresses(Current.Name);
#endif
			break;
		case FStep::EKind::Fly:
			Pawn->SetFlying(Current.A != 0.0f);
			break;
		case FStep::EKind::Pack:
		{
			TArray<FString> Names;
			if (Current.Name != TEXT("off"))
			{
				Current.Name.ParseIntoArray(Names, TEXT(","));
			}
			McPerf::Event(FString::Printf(TEXT("PACKS %s"), *Current.Name));
			McPacks::ApplySelection(Names);
			McPacks::RequestReload();
			break;
		}
		case FStep::EKind::Settle:
			Left = Current.Seconds;
			bRunning = true;
			break;
		}
	}
	if (!bRunning && !Steps.IsValidIndex(Next) && FParse::Param(FCommandLine::Get(), TEXT("McInputQuit")))
	{
		FPlatformMisc::RequestExit(false, TEXT("McInput"));
	}
}
