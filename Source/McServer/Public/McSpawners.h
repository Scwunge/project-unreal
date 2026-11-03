// Monster spawners (minecraft.wiki, "Monster Spawner"), fitted to a vanilla spawner's own numbers and the mobs it put
// out, logged each tick (mc-reference `log spawner`, `log mobs`, script spawner_log.txt):
//  - a new spawner waits 20 ticks, then each cycle waits a random 200 to 799 ticks (its fields: delay 20, min 200,
//    max 800); the wait only counts down while a player stands within 16 blocks of its middle (the timer stood
//    still while the player was 17 off);
//  - when the wait is over it makes 4 tries; each puts the mob at a random spot within 4 blocks of its middle across,
//    its feet on the spawner's layer or the one above or below (whole layers, as logged), and is lost if the mob
//    doesn't fit there or it is too bright for a monster (the mob's own rule, minecraft.wiki); 2 to 4 of 4 came out;
//  - before each try, 6 or more of that mob within the spawner's block grown by 4 stop the cycle;
//  - a cycle that put out at least one (or was stopped by the crowd) starts the next wait; one that put out none
//    tries again the next tick (minecraft.wiki). On peaceful, monsters don't come but the cycles go on.
// The kind comes from the spawner's block entity. Server only; game thread.

#pragma once

#include "CoreMinimal.h"

class FMcBlockWorld;
class FMcEntityHost;

class MCSERVER_API FMcSpawners
{
public:
	explicit FMcSpawners(FMcBlockWorld& InWorld) : World(InWorld) {}

	static constexpr int32 FirstDelay = 20;
	static constexpr int32 MinDelay = 200;
	static constexpr int32 MaxDelay = 800;
	static constexpr int32 Tries = 4;
	static constexpr int32 MaxNearby = 6;
	static constexpr double PlayerRange = 16.0;
	static constexpr double SpawnRange = 4.0;

	// The spawners in a chunk that has come in, or the one at Pos whose block entity changed.
	void OnChunkAdded(FIntPoint ChunkPos);
	void OnBlockEntityChanged(const FIntVector& Pos);
	// One game tick; Players are the positions of their feet.
	void Tick(FMcEntityHost& Host, const TArray<FVector>& Players, FRandomStream& Random);

	int32 Num() const { return Spawners.Num(); }
	// Ticks left before the spawner at Pos next tries, or -1 if there is none there.
	int32 GetDelay(const FIntVector& Pos) const { const int32* Delay = Spawners.Find(Pos); return Delay ? *Delay : -1; }

	// A spawner finished a cycle and started its next wait (the clients' spinning mob slows down again).
	TFunction<void(const FIntVector& Pos)> OnCycle;

private:
	// Tries one cycle of the spawner at Pos; true if the next wait starts.
	bool Spawn(FMcEntityHost& Host, const FIntVector& Pos, FName Kind, FRandomStream& Random);

	FMcBlockWorld& World;
	// Each spawner's ticks to wait.
	TMap<FIntVector, int32> Spawners;
};
