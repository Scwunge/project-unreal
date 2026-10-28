#include "McGameViewportClient.h"

FString UMcGameViewportClient::Typed;

bool UMcGameViewportClient::InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character)
{
	if (Character >= 32 && Character != 127)
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
