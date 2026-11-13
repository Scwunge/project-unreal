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
	// The look: Unreal's lighting (true) or the game's own.
	bool bEnhancedGraphics = false;
	// HD Textures, on top of Unreal Mode.
	bool bHdTextures = false;
	// Unreal Mode's quality, 0 Low to 3 Ultra, and the frames-per-second readout.
	int32 Quality = 2;
	bool bShowFps = false;
	// Closed captions for sounds (off, as in the game, until chosen).
	bool bShowSubtitles = false;
	// Hardware ray tracing, on top of Unreal Mode, where the card can.
	bool bRayTracing = false;
	// The video, mouse and sound settings of the Options screen. Field of View is in degrees (30 to 110), render
	// distance in chunks (2 to 32), sensitivity a fraction of the default (0 to 2) and volume 0 to 1.
	float FieldOfView = 70.0f;
	int32 RenderDistance = 12;
	float Sensitivity = 1.0f;
	float MasterVolume = 1.0f;
	// Max Framerate, 10 to 260 frames per second in steps of 10, where 260 is Unlimited, and VSync; the game's own
	// defaults, 120 and on. Uncapped frames also take the cores the world's workers need to load chunks.
	int32 MaxFps = 120;
	bool bVSync = true;
	static constexpr int32 UnlimitedFps = 260;

	// Sets the engine's frame cap and VSync from MaxFps and bVSync. Left alone in -McPerf runs, which choose their own
	// with -McPerfCvars, so their numbers stay comparable.
	void ApplyFrameRate() const;

	static FMcClientOptions Load();
	void Save() const;
	// The names the game takes: 3 to 16 letters, digits and underscores.
	static bool IsValidPlayerName(const FString& Name);
};
