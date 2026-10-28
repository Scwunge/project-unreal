// The pure rules of Unreal Mode's immersive audio: how enclosed a listener is and what reverb that gives, how much solid
// stone between a sound and the listener muffles it, how dark a place is (for the cave's mood sound). Nothing here
// touches the engine or the cache, so it can be tested without a world: the blocks are asked through a callback.

#pragma once

#include "CoreMinimal.h"

namespace McAcoustics
{
	// Whether the block at a position stops sound (a full opaque block; glass, leaves, fluids and air let it through).
	using FSolidQuery = TFunctionRef<bool(const FIntVector&)>;

	constexpr int32 NumRays = 12;
	// Blocks a ray goes before it counts as having escaped.
	constexpr float MaxRayDistance = 24.0f;

	// What the rays around the listener found.
	struct FEnclosure
	{
		// The fraction of all rays that escaped, 0 in a sealed room and 1 in the open.
		float Openness = 1.0f;
		// The fraction of the rays that look up that escaped: sky overhead (0 under a roof, 1 in the open).
		float SkyOpenness = 1.0f;
		// How far the rays went on average before hitting something, in blocks (the size of the space).
		float MeanDistance = MaxRayDistance;
	};

	// The directions the rays go, unit vectors (the corners of an icosahedron). Y is up.
	MCAUDIO_API const TArray<FVector>& GetRayDirections();

	// From how far each of the NumRays rays went (MaxRayDistance or more for one that escaped), in the order of
	// GetRayDirections.
	MCAUDIO_API FEnclosure Summarise(TConstArrayView<float> RayDistances);

	// Casts the rays from a point (block coordinates, Y up).
	MCAUDIO_API FEnclosure EstimateEnclosure(const FVector& Origin, FSolidQuery IsSolid);

	// What the reverb should be, in the units of the engine's reverb effect.
	struct FReverb
	{
		// How much of the world's sound goes through it, 0 (none) to 1.
		float Volume = 0.0f;
		// Seconds the tail lasts.
		float DecayTime = 0.5f;
		float Density = 1.0f;
		float Diffusion = 1.0f;
		// How much of the high end survives the walls, 0..1.
		float GainHF = 0.8f;
		float ReflectionsGain = 0.1f;
		float ReflectionsDelay = 0.01f;
		float LateGain = 1.0f;
		float LateDelay = 0.02f;
	};

	// A small room: a short, dry reverb. A big cave: a long echo. Open sky: almost none. In the Nether (bBigDark): the
	// same enclosed or not, large and dark.
	MCAUDIO_API FReverb ToReverb(const FEnclosure& Enclosure, bool bBigDark);

	// What a sound has to go through to reach the listener.
	struct FOcclusion
	{
		// Multiplies the sound's volume.
		float Volume = 1.0f;
		// The low-pass cutoff in hertz; 20000 and over is none.
		float LowpassHz = 20000.0f;
	};
	MCAUDIO_API FOcclusion GetOcclusion(int32 SolidBlocks);

	// The solid blocks on the straight line from one point to another, not counting the blocks the ends are in. A run
	// of solid blocks counts each one.
	MCAUDIO_API int32 CountSolidBetween(const FVector& From, const FVector& To, FSolidQuery IsSolid);

	// Everything is dull under water.
	constexpr float UnderwaterLowpassHz = 650.0f;
	constexpr float UnderwaterVolume = 0.85f;

	// The light at a position as far as the sound needs it (0..15): sky light (15 with open sky overhead, else 0) and
	// the strongest nearby light source, each lamp's level less the distance to it.
	MCAUDIO_API bool IsSkyOpen(const FIntVector& Cell, FSolidQuery IsSolid, int32 Up = 48);
	MCAUDIO_API int32 EstimateBlockLight(const FIntVector& Cell, TFunctionRef<int32(const FIntVector&)> EmissionAt, int32 Radius = 7);

	// The cave mood's meter: it fills with the dark and empties in the light, and the sound plays when it is full.
	// Light is 0..15. Returns true (and resets) when it is time. DelayTicks is how long total darkness takes.
	MCAUDIO_API bool TickMood(float& Meter, int32 Light, int32 DelayTicks);

	// The tail of an echo that comes back from a far sound in a cave: how long after, and how loud.
	struct FEcho
	{
		float DelaySeconds = 0.0f;
		float Volume = 0.0f;
	};
	// No echo for a near sound or in the open. A far one in an enclosed space gets one, later the further it is and
	// the bigger the space.
	MCAUDIO_API FEcho GetEcho(float DistanceBlocks, const FEnclosure& Enclosure);
}
