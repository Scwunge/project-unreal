#include "McGameViewportClient.h"

FString UMcGameViewportClient::Typed;
int32 UMcGameViewportClient::Backspaces = 0;

bool UMcGameViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
	if (Character == TEXT('\b'))
	{
		Backspaces = FMath::Min(Backspaces + 1, 256);
	}
	else if (Character >= 32 && Character != 127)
	{
		// Bounded, in case nothing reads it (no text field is focused).
		if (Typed.Len() < 256)
		{
			Typed.AppendChar(Character);
		}
	}
	return Super::InputChar(InViewport, ControllerId, Character);
}

FString UMcGameViewportClient::TakeTyped()
{
	FString Out = MoveTemp(Typed);
	Typed.Reset();
	return Out;
}

int32 UMcGameViewportClient::TakeBackspaces()
{
	const int32 Out = Backspaces;
	Backspaces = 0;
	return Out;
}
