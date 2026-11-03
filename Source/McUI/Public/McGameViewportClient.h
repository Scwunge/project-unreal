// The game's viewport, which hands typed characters to the menus' text fields (the HUD polls keys, which can't tell
// what a key types on the player's keyboard layout).

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameViewportClient.h"
#include "McGameViewportClient.generated.h"

UCLASS()
class MCUI_API UMcGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

public:
	virtual bool InputChar(FViewport* InViewport, int32 ControllerId, TCHAR Character) override;

	// What has been typed since the last call (printable characters only; Backspace, Enter and the like are keys).
	static FString TakeTyped();
	// How many times backspace went since last asked, the key's repeats while held included.
	static int32 TakeBackspaces();

private:
	static FString Typed;
	static int32 Backspaces;
};
