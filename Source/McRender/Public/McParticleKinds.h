// How each kind of the game's particles (the /particle names: heart, smoke, crit, ...) behaves, one table row per kind, fitted to
// vanilla reference runs (PLAN.md "Particles"): how long it lives, how big it is, what it does each tick, which of its pictures it shows.
// A kind with no row still gets a plain default so that /particle shows something for every name the game has.

#pragma once

#include "CoreMinimal.h"
#include "McRender.h"

namespace McParticleKinds
{
	enum class EFrames : uint8
	{
		First,    // always the first picture
		ByAge,    // the pictures in order over the particle's life
		Random,   // one picked when it appears
	};

	struct FKind
	{
		FName Name;                // "minecraft:heart"
		int32 LifeMin = 20;        // ticks
		int32 LifeMax = 20;
		float Size = 0.1f;         // half the width, in blocks
		float Grow = 0.0f;         // size gained over its life, as a fraction of Size (negative shrinks to that much less)
		float Gravity = 0.0f;      // blocks a tick, each tick (positive falls)
		float Friction = 0.98f;    // velocity kept each tick
		float Rise = 0.0f;         // blocks a tick added to the starting upward velocity
		float Spread = 0.0f;       // random sideways and upward starting velocity, blocks a tick
		float Speed = 1.0f;        // how much the /particle speed argument scales the starting velocity (0: ignored)
		EFrames Frames = EFrames::First;
		bool bShrinkAtEnd = false; // shrinks to nothing over its last quarter
		bool bGlow = false;        // drawn at full brightness
		bool bPhysics = false;     // stops on blocks
		FColor Tint = FColor::White;
	};

	// The row for a name ("minecraft:heart" or "heart"); a default row (named as asked) if the table has none.
	MCRENDER_API const FKind& Find(FName Name);
	// Every name the table has a row for.
	MCRENDER_API const TArray<FKind>& GetAll();
}
