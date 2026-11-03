// What an imported world's item frames, glow item frames, paintings and armor stands look like (minecraft.wiki: "Item
// Frame", "Painting", "Armor Stand"). They are static scenery here: nothing ticks, they cannot be hit, and a client draws
// each from one line of text, the way a placed banner is (McBanner), so the line is all an entity has to carry.
//
// The line is "kind;key=value;key=value": every id and number in it is lower case, because the line travels as a name.

#pragma once

#include "CoreMinimal.h"
#include "McCore.h"

namespace McDecor
{
	enum class EKind : uint8
	{
		ItemFrame,
		GlowItemFrame,
		Painting,
		ArmorStand,
	};

	// The armor stand's slots, in the order the line keeps them.
	enum class ESlot : uint8
	{
		Head,
		Chest,
		Legs,
		Feet,
		MainHand,
		OffHand,
		Count,
	};

	enum class EPart : uint8
	{
		Head,
		Body,
		LeftArm,
		RightArm,
		LeftLeg,
		RightLeg,
		Count,
	};

	struct MCITEM_API FDecor
	{
		EKind Kind = EKind::ItemFrame;
		// Which way the thing faces, as the game numbers directions: 0 down, 1 up, 2 north, 3 south, 4 west, 5 east.
		// A frame or painting hangs on the block behind it. Armor stands have none (they turn by Yaw).
		int32 Facing = 3;
		// A painting's picture: "kebab", the file name under textures/painting.
		FString Variant;
		// A frame's item ("minecraft:diamond_sword", empty for none) and how far it is turned, in eighths of a turn.
		FString Item;
		int32 ItemRotation = 0;
		bool bInvisible = false;
		// An armor stand: degrees, minecraft yaw; the pose is the game's Rotation of each part, [pitch, yaw, roll].
		float Yaw = 0.0f;
		bool bSmall = false;
		bool bArms = false;
		bool bBasePlate = true;
		FVector3f Pose[static_cast<int32>(EPart::Count)];
		FString Equipment[static_cast<int32>(ESlot::Count)];
		// A piece's dye, 0xRRGGBB, or -1 for none (leather then wears the game's brown); only leather takes it.
		int32 Dye[static_cast<int32>(ESlot::Count)];

		FDecor();
	};

	MCITEM_API FString Encode(const FDecor& Decor);
	MCITEM_API bool Decode(const FString& Line, FDecor& Out);

	// What the game's default pose of an armor stand is for a part, degrees.
	MCITEM_API FVector3f GetDefaultPose(EPart Part);
}
