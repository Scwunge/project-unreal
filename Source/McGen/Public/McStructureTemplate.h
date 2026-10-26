// Structure templates (minecraft.wiki, "Structure file"): the .nbt files under data/minecraft/structure/ that
// hold buildings, wells, ship wrecks and fossils as a palette of block states and a list of blocks. A template
// is placed into a generation region at a position, turned and mirrored, through a processor list that may
// change or drop blocks on the way (minecraft.wiki, "Processor list").
//
// Axes are the game's: x east, y up, z south. A rotation turns clockwise as seen from above.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"
#include "McNbt.h"

class FJsonObject;
class FJsonValue;
class FMcGenRegion;
class FMcRandom;
class FMcRuleTest;

enum class EMcRotation : uint8
{
	None,
	Clockwise90,
	Clockwise180,
	CounterClockwise90,
};

// Left-right flips the z axis (north and south swap), front-back flips x (east and west swap).
enum class EMcMirror : uint8
{
	None,
	LeftRight,
	FrontBack,
};

namespace McTemplate
{
	// A state a template or processor leaves out.
	constexpr FMcStateId SkipState = 0xFFFF;

	// Mirrors, then turns, an offset from a pivot (the pivot itself stays put).
	MCGEN_API FIntVector TransformPos(const FIntVector& Pos, EMcRotation Rotation, EMcMirror Mirror, const FIntVector& Pivot = FIntVector::ZeroValue);
	// The state with facing, axis, rotation, connections, rail shape, stair shape, hinge and chest side changed
	// to match.
	MCGEN_API FMcStateId TransformState(FMcStateId State, EMcRotation Rotation, EMcMirror Mirror);
	MCGEN_API EMcRotation Compose(EMcRotation First, EMcRotation Second);
	MCGEN_API EMcRotation RandomRotation(FMcRandom& Random);
	MCGEN_API const TCHAR* Name(EMcRotation Rotation);
}

// A list of processors run on every block a template places. A processor may change the state or drop the block
// (SkipState); gravity also moves it.
class MCGEN_API FMcProcessorList
{
public:
	// From a processor list file's JSON ({ "processors": [...] }) or a bare array of processors.
	static TSharedPtr<const FMcProcessorList> Parse(const TSharedPtr<FJsonValue>& Json);
	// From a processor list id ("minecraft:fossil_rot") or inline.
	static TSharedPtr<const FMcProcessorList> ParseRef(const TSharedPtr<FJsonValue>& Json);

	// The state to place at Pos (which gravity may change), or SkipState. LocalY is the block's height inside the
	// template, Existing what is in the world there.
	FMcStateId Process(FMcGenRegion& Region, FIntVector& Pos, int32 LocalY, FMcStateId State, FMcRandom& Random) const;

	struct FProcessor
	{
		virtual ~FProcessor() = default;
		virtual FMcStateId Process(FMcGenRegion& Region, FIntVector& Pos, int32 LocalY, FMcStateId State, FMcStateId Existing, FMcRandom& Random) const = 0;
	};

	void Add(TSharedPtr<const FProcessor> Processor) { Processors.Add(MoveTemp(Processor)); }

private:
	TArray<TSharedPtr<const FProcessor>> Processors;
};

struct FMcTemplatePlaceSettings
{
	EMcRotation Rotation = EMcRotation::None;
	EMcMirror Mirror = EMcMirror::None;
	// Turned around this offset from the template's corner.
	FIntVector Pivot = FIntVector::ZeroValue;
	const FMcProcessorList* Processors = nullptr;
	// Only blocks in this column rectangle (inclusive) are placed.
	bool bClip = false;
	FIntPoint ClipMin = FIntPoint::ZeroValue;
	FIntPoint ClipMax = FIntPoint::ZeroValue;
	// Which of several palettes to use; negative picks one at random.
	int32 Palette = -1;
	// Leave air out of the template alone (don't carve with it).
	bool bSkipAir = false;
};

class MCGEN_API FMcStructureTemplate
{
public:
	struct FBlock
	{
		FIntVector Pos;
		int32 Palette = 0;  // index into each palette's state list
		// A jigsaw block's final_state; SkipState if it has none.
		FMcStateId Final = McTemplate::SkipState;
		bool bJigsaw = false;
		TSharedPtr<const FMcNbt> Nbt;
	};
	struct FEntity
	{
		FVector Pos = FVector::ZeroVector;
		FIntVector BlockPos;
		TSharedPtr<const FMcNbt> Nbt;
	};

	FIntVector Size = FIntVector::ZeroValue;
	// States per palette; a block's Palette indexes into each. SkipState for structure voids and unknown blocks.
	TArray<TArray<FMcStateId>> Palettes;
	TArray<FBlock> Blocks;
	TArray<FEntity> Entities;

	// Loads cache/.../structure/<Id>.nbt; Id like "minecraft:igloo/top" or "igloo/top". Needs the block registry.
	static bool Load(const FString& Id, FMcStructureTemplate& Out);
	// A loaded template, kept for good; null if there is none. Safe from any thread.
	static TSharedPtr<const FMcStructureTemplate> Find(const FString& Id);
	// Ids of every template file in the jar.
	static void ListIds(TArray<FString>& Out);

	// Places the template with its corner (before turning) at Position. Returns how many blocks were set.
	int32 Place(FMcGenRegion& Region, const FIntVector& Position, const FMcTemplatePlaceSettings& Settings, FMcRandom& Random) const;
	// The box (inclusive corners) the template covers when placed so.
	void GetBounds(const FIntVector& Position, const FMcTemplatePlaceSettings& Settings, FIntVector& OutMin, FIntVector& OutMax) const;
	// Where the first block of a block (by its original position) ends up.
	FIntVector ToWorld(const FIntVector& Local, const FIntVector& Position, const FMcTemplatePlaceSettings& Settings) const;
};
