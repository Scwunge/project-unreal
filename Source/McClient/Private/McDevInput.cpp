#include "McDevInput.h"

#if !UE_BUILD_SHIPPING

#include "McClient.h"
#include "McPlayerController.h"
#include "McPlayerPawn.h"
#include "Engine/GameViewportClient.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "UnrealClient.h"

bool FMcDevInput::Wanted()
{
	return FParse::Param(FCommandLine::Get(), TEXT("McInput")) || FCString::Strifind(FCommandLine::Get(), TEXT("-McInput=")) != nullptr;
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
	Script.ParseIntoArray(Texts, TEXT(";"));
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
	float Delay = 5.0f;
	FParse::Value(FCommandLine::Get(), TEXT("-McInputDelay="), Delay);
	Waited = -Delay;
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

	AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(Controller.GetPawn());
	if (!Pawn)
	{
		return;
	}
	if (Waited < 0.0f)
	{
		Waited += DeltaSeconds;
		return;
	}

	if (bRunning)
	{
		Left -= DeltaSeconds;
		if (Current.Kind == FStep::EKind::Hold && !Current.Name.IsEmpty() && !bHoldShot && Left <= Current.Seconds * 0.5f)
		{
			bHoldShot = true;
			TakeShot(Current.Name);
		}
		if (Left > 0.0f)
		{
			return;
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
			FPlatformMisc::RequestExit(false, TEXT("McInput"));
			Next = Steps.Num();
			break;
		}
	}
	if (!bRunning && !Steps.IsValidIndex(Next) && FParse::Param(FCommandLine::Get(), TEXT("McInputQuit")))
	{
		FPlatformMisc::RequestExit(false, TEXT("McInput"));
	}
}

#endif
