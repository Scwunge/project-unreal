// How entities move through blocks: Minecraft's tick-based movement with axis-by-axis collision. Pure functions
// of the block world, run at 20 ticks a second, so the server and client get the same answer.
//
// Units are blocks and blocks per tick, Minecraft axes (x east, y up, z south). Numbers are the game's own
// constants as documented on minecraft.wiki (Entity, Movement and Sprinting pages).

#pragma once

#include "CoreMinimal.h"

class FMcBlockWorld;

struct FMcMoveInput
{
	// -1..1 each: forward is W minus S, strafe is A minus D (positive to the left, as in Minecraft).
	float Forward = 0.0f;
	float Strafe = 0.0f;
	bool bJump = false;
	bool bSprint = false;
	bool bSneak = false;
	// Flying: Jump goes up and Sneak goes down.
	bool bFlying = false;
	// Minecraft yaw in degrees: 0 faces south (+z), 90 faces west.
	float Yaw = 0.0f;
	// The movement speed attribute: 0.1 for a player walking, other values for mobs.
	float Speed = 0.1f;
	// Added to the jump's upward speed (jump boost).
	float JumpBonus = 0.0f;
};

struct MCENTITY_API FMcBody
{
	// Middle of the bottom of the hitbox.
	FVector Position = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	float Width = 0.6f;
	float Height = 1.8f;
	float StepHeight = 0.6f;
	bool bOnGround = false;
	bool bHorizontalCollision = false;
	// Set by TickPlayer from the blocks the body is in, for sounds, effects and the UI.
	bool bInWater = false;
	bool bInLava = false;
	bool bClimbing = false;  // in a ladder, vine or scaffolding
	bool bStuck = false;     // in a cobweb, sweet berry bush or powder snow

	FBox GetBox() const;
};

namespace McPhysics
{
	// Collision boxes of the blocks touching Area, in world block coordinates.
	MCENTITY_API void GatherBoxes(const FMcBlockWorld& World, const FBox& Area, TArray<FBox>& Out);

	// How far Box can move along Motion before hitting a block, trying y, then the larger of x and z, then
	// the other. Bodies on the ground climb up to StepHeight when that gets them further.
	MCENTITY_API FVector Collide(const FMcBlockWorld& World, const FBox& Box, const FVector& Motion, float StepHeight, bool bOnGround);

	// Moves the body by Motion, updating its ground and wall flags and zeroing velocity into what it hit.
	MCENTITY_API void Move(const FMcBlockWorld& World, FMcBody& Body, FVector Motion, bool bSneakAtEdges);

	// One tick of a player's movement on foot, swimming, climbing or flying: input, collision, then gravity
	// and friction. In water and lava the drag is 0.8 and 0.5, Jump swims up and Sneak sinks, and flowing water
	// pushes; ladders and vines cap the fall and sideways speed at 0.15 and climb at 0.2 when you press against
	// the wall or jump; cobwebs scale the motion down.
	MCENTITY_API void TickPlayer(const FMcBlockWorld& World, FMcBody& Body, const FMcMoveInput& Input);

	// Whether the box overlaps any block's collision.
	MCENTITY_API bool Collides(const FMcBlockWorld& World, const FBox& Box);
}
