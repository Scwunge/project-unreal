// The playability harness's recorder, in every build including the Shipping one players get, and off unless the command
// line says -McPerf=<file>. Shipping has no logs and no stats, so this writes its own file, a line at a time with no
// buffering so a crash loses nothing:
//   E  t=<s> <text>                  something happened (world opened, player died, ...)
//   S  t=<s> phase=... n= avg= ...   one line a second: frames, thread times, memory, chunk counts, per-frame scope times
//   H  t=<s> ms=... <scopes>         one frame over 33 ms and what its time went on
//   P  phase=... fps= p99= low1= ... the summary of a phase, when the next starts or the game ends
//   X  ...                           an exception or hang, with its stack as module+rva (tools/perf_symbols.ps1 names them)
//   E  t=<s> CLEAN_EXIT              the last line of a run that ended on purpose; a run without it crashed or was killed
// When off, a scope is one branch on a bool, and nothing else runs.

#pragma once

#include "CoreMinimal.h"

enum class EMcPerfScope : uint8
{
	Stream,      // choosing, generating and dropping chunks on the game thread, all of it; the next three are parts of it
	StreamAdd,   // taking finished chunks in
	StreamDecorate, // features and structures
	StreamUnload,// saving and dropping far chunks
	Sim,         // block ticks and the rest of a world tick except entities
	Entities,    // mobs and items
	MeshLaunch,  // choosing chunks to mesh and starting their tasks
	MeshUpload,  // handing finished meshes to the engine
	Resort,      // putting glass and ice back in order for the camera
	TexAnim,     // texture animation
	Hud,         // menus and the HUD
	Pawn,        // the player's own tick
	EntityRender,// entity actors' per-frame pose and mesh work
	Sky,         // sky, clouds and weather components
	World,       // the world actor's whole tick; stream, sim and ents are parts of it
	Render,      // the chunk renderer's whole tick; launch, upload, resort and texanim are parts of it
	Models,      // entity, player and hand model components
	BlockEnts,   // chest and sign components
	Particles,   // particles
	BlockLight,  // block light components
	Ctrl,        // the player controller's tick (the harness script, music, view sync)
	Audio,       // the audio subsystem
	Misc,        // chunk stream component, block use and travel subsystems
	ModelsEntity,// the entity model components, part of models
	ModelsPlayer,// the player model components, part of models
	ModelsHand,  // the first-person hand, part of models
	Count
};

enum class EMcPerfCounter : uint8
{
	Chunks,      // chunks the renderer knows of
	Dirty,       // of those, waiting to be meshed
	InFlight,    // mesh tasks running or waiting for upload
	Meshes,      // chunk block meshes
	WaterMeshes, // chunk water meshes
	MeshTris,    // triangles in the meshes uploaded so far (thousands), a running total of what is on the card
	Entities,    // entities in the world
	X, Y, Z,     // the local player, in blocks
	LiveKVerts,  // vertices (thousands) in the chunk meshes now, each kept in memory by the mesh component as well as on the card
	LiveKTris,   // triangles (thousands) in the chunk meshes now
	KeptKVerts,  // vertices (thousands) kept again for re-sorting glass and ice
	UObjects,    // live UObjects, which a leak of actors or components would show in
	Streaming,   // 0 once the world has every chunk these players need
	ModelsPosed, // mob models built again on the last frame
	ModelsKept,  // mob models left as they were on the last frame (not drawn lately)
	Count
};

namespace McPerf
{
	// True when -McPerf=<file> is on the command line. Worth checking only around work done just for the recorder.
	MCCORE_API bool IsOn();
	// Reads the command line and installs everything; called once as McCore loads.
	MCCORE_API void Start();
	// Ends the phase running now, writes its summary and starts this one.
	MCCORE_API void Mark(const FString& Phase);
	MCCORE_API void Event(const FString& Text);
	MCCORE_API void SetCounter(EMcPerfCounter Counter, int64 Value);
	MCCORE_API int64 GetCounter(EMcPerfCounter Counter);
	// Thread and card times of the last frame, milliseconds, from the render module (McCore doesn't link the renderer).
	MCCORE_API void SetFrameTimes(float GameMs, float RenderMs, float RhiMs, float GpuMs);
	MCCORE_API void AddScopeCycles(EMcPerfScope Scope, uint64 Cycles);
	// A garbage collection's time, from the module that links CoreUObject.
	MCCORE_API void GcBegin();
	MCCORE_API void GcEnd();

	extern MCCORE_API bool bOn;

	struct FScope
	{
		EMcPerfScope Which;
		uint64 Begin;
		explicit FScope(EMcPerfScope InWhich) : Which(InWhich), Begin(bOn ? FPlatformTime::Cycles64() : 0) {}
		~FScope()
		{
			if (bOn)
			{
				AddScopeCycles(Which, FPlatformTime::Cycles64() - Begin);
			}
		}
	};
}

#define MC_PERF_SCOPE(Name) McPerf::FScope UE_JOIN(McPerfScope, __LINE__)(EMcPerfScope::Name)
