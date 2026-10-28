// The player at this machine. Their name is the one chosen on the join screen (Saved/options.json): the engine sends
// it as ?Name= when opening a world or joining a server, and the server files the player's save under it. Without
// one (a development run) it is the platform's, as before.

#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "McLocalPlayer.generated.h"

UCLASS()
class MCUI_API UMcLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
	virtual FString GetNickname() const override;
};

// What the client remembers between runs, in Saved/options.json.
struct MCUI_API FMcClientOptions
{
	FString PlayerName;
	FString LastServer;

	static FMcClientOptions Load();
	void Save() const;
	// The names the game takes: 3 to 16 letters, digits and underscores.
	static bool IsValidPlayerName(const FString& Name);
};
