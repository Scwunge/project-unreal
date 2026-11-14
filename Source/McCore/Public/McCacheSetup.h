// The game's own version of tools/fetch_cache.py, for a packaged game whose player has no Python: builds the cache
// (see McCache.h) from their own Minecraft 26.3 install, with progress for the first-run setup screen.
//
//   - the client jar's assets/ and data/ are unpacked to client/
//   - the assets index's objects (sounds, sounds.json, the title panorama) are copied to assets/
//   - the reports (every block state, the registries, item components) come from running Mojang's data generator,
//     which is in the client jar, with the Java runtime the launcher keeps for the game: nothing is downloaded
//
// The install is looked for in the official launcher's folder (%APPDATA%\.minecraft) and Prism's
// (%APPDATA%\PrismLauncher), and any folder the player points at; a file needed may be in any of them.

#pragma once

#include "CoreMinimal.h"
#include <atomic>

class MCCORE_API FMcCacheBuilder
{
public:
	// What a build reads, found by Find.
	struct FSources
	{
		TArray<FString> Roots;
		FString Profile;     // versions/26.3/26.3.json
		FString ClientJar;
		FString AssetIndex;  // assets/indexes/<id>.json
		FString AssetIndexId;
		FString Java;
		FString ClientSha1;
	};

	static const TCHAR* Version() { return TEXT("26.3"); }

	// The launcher folders that exist on this PC.
	static TArray<FString> DefaultRoots();

	// Looks for 26.3 in Roots. On failure Error says what is missing and what the player can do about it.
	static bool Find(const TArray<FString>& Roots, FSources& Out, FString& Error);

	// Builds the cache at McCache::Root() on a worker thread. Not to be started twice.
	void Start(const FSources& Sources);

	bool IsRunning() const { return bRunning.load(); }
	// True once the cache is finished, or Error says why not.
	bool IsDone() const { return bDone.load(); }
	FString GetError() const;
	// What it is doing, and how far along the whole is (0 to 1).
	FString GetStatus() const;
	float GetProgress() const { return Progress.load(); }

private:
	bool Run(const FSources& Sources, FString& Error);
	bool UnpackJar(const FSources& Sources, const FString& Out, FString& Error);
	bool CopyAssets(const FSources& Sources, const FString& Out, FString& Error);
	bool RunGenerator(const FSources& Sources, const FString& Out, FString& Error);
	void SetStatus(const FString& Text);

	mutable FCriticalSection Lock;
	FString Status;
	FString Error;
	std::atomic<float> Progress{ 0.0f };
	std::atomic<bool> bRunning{ false };
	std::atomic<bool> bDone{ false };
};
