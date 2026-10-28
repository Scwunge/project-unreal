#include "McBlockPlacement.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"

namespace
{
	const TCHAR* const FaceNames[6] = { TEXT("down"), TEXT("up"), TEXT("north"), TEXT("south"), TEXT("west"), TEXT("east") };

	int32 OppositeFace(int32 Face)
	{
		return Face ^ 1;
	}

	// The horizontal direction the player looks in: yaw 0 is south, 90 west, 180 north, 270 east.
	int32 HorizontalFacing(float Yaw)
	{
		const int32 Quarter = FMath::RoundToInt32(FMath::Fmod(FMath::Fmod(Yaw, 360.0f) + 360.0f, 360.0f) / 90.0f) & 3;
		static const int32 Faces[4] = { 3, 4, 2, 5 };
		return Faces[Quarter];
	}

	// The direction the player looks in most, counting up and down.
	int32 NearestFacing(float Yaw, float Pitch)
	{
		if (Pitch > 45.0f)
		{
			return 0;
		}
		if (Pitch < -45.0f)
		{
			return 1;
		}
		return HorizontalFacing(Yaw);
	}

	bool HasValue(const FMcBlock& Block, FName Property, FName Value)
	{
		const int32 Index = Block.FindProperty(Property);
		return Index != INDEX_NONE && Block.Properties[Index].Values.Contains(Value);
	}

	// Whether the hit was on the upper half of its block: for slabs, stairs and trapdoors.
	bool IsUpperHalf(const FMcBlockHit& Hit)
	{
		if (Hit.Face == 0)
		{
			return true;
		}
		if (Hit.Face == 1)
		{
			return false;
		}
		return Hit.Point.Y - Hit.Block.Y > 0.5;
	}

	// Blocks that face the player rather than away: they show their front to whoever places them.
	bool FacesPlayer(const FString& Name)
	{
		return !(Name.EndsWith(TEXT("_stairs")) || Name.EndsWith(TEXT("_door")) || Name.EndsWith(TEXT("_fence_gate"))
			|| Name.EndsWith(TEXT("_bed")) || Name.EndsWith(TEXT("anvil")) || Name == TEXT("minecraft:lever")
			|| Name.EndsWith(TEXT("_button")));
	}
}

TArray<FMcPlacedBlock> McBlockPlacement::GetPlacement(const FMcBlockWorld& World, FMcStateId BlockState, const FMcBlockHit& Hit, float Yaw, float Pitch)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcBlock& Block = Registry.GetBlockOf(BlockState);
	const FString Name = Block.Name.ToString();
	TMap<FName, FName> Properties;
	FName PlacedName = Block.Name;

	// Clicking a slab's open half with the same slab makes a double slab in place.
	if (Name.EndsWith(TEXT("_slab")) && Registry.GetBlockOf(Hit.State).Name == Block.Name)
	{
		const FName Type = Registry.GetPropertyValue(Hit.State, TEXT("type"));
		if ((Type == TEXT("bottom") && Hit.Face == 1) || (Type == TEXT("top") && Hit.Face == 0))
		{
			Properties.Add(TEXT("type"), TEXT("double"));
			return { { Hit.Block, Registry.FindState(Block.Name, Properties).Get(BlockState) } };
		}
	}

	// Into the clicked block itself when that can be replaced (grass, snow), otherwise beside it.
	const FIntVector Pos = McRaycast::IsReplaceable(Hit.State) ? Hit.Block : Hit.GetAdjacent();
	const int32 Face = McRaycast::IsReplaceable(Hit.State) ? 1 : Hit.Face;
	if (!McRaycast::IsReplaceable(World.GetBlock(Pos)))
	{
		return {};
	}

	// Torches, and the like with a wall variant, hang on the wall when placed on a side.
	if (Name.EndsWith(TEXT("torch")) && !Name.Contains(TEXT("wall")))
	{
		if (Face == 0)
		{
			return {};
		}
		if (Face >= 2)
		{
			FString Wall = Name;
			Wall.ReplaceInline(TEXT("torch"), TEXT("wall_torch"));
			if (Registry.FindBlock(FName(*Wall)))
			{
				PlacedName = FName(*Wall);
				Properties.Add(TEXT("facing"), FaceNames[Face]);
			}
		}
	}
	else if (Name == TEXT("minecraft:hopper"))
	{
		// The spout points into the block clicked on, or down when placed on a top or bottom.
		Properties.Add(TEXT("facing"), Face < 2 ? FaceNames[0] : FaceNames[OppositeFace(Face)]);
	}
	else if (Name == TEXT("minecraft:ladder"))
	{
		if (Face < 2)
		{
			return {};
		}
		Properties.Add(TEXT("facing"), FaceNames[Face]);
	}
	else
	{
		if (Block.FindProperty(TEXT("axis")) != INDEX_NONE)
		{
			Properties.Add(TEXT("axis"), Face < 2 ? TEXT("y") : Face < 4 ? TEXT("z") : TEXT("x"));
		}
		if (Name.EndsWith(TEXT("_slab")))
		{
			Properties.Add(TEXT("type"), IsUpperHalf(Hit) ? TEXT("top") : TEXT("bottom"));
		}
		if (Name.EndsWith(TEXT("_trapdoor")))
		{
			Properties.Add(TEXT("half"), IsUpperHalf(Hit) ? TEXT("top") : TEXT("bottom"));
			Properties.Add(TEXT("facing"), FaceNames[Face >= 2 ? Face : OppositeFace(HorizontalFacing(Yaw))]);
		}
		else if (Block.FindProperty(TEXT("facing")) != INDEX_NONE)
		{
			int32 Facing;
			if (HasValue(Block, TEXT("facing"), TEXT("up")))
			{
				// Pistons, dispensers, observers: face the player, up and down included.
				Facing = OppositeFace(NearestFacing(Yaw, Pitch));
			}
			else
			{
				Facing = FacesPlayer(Name) ? OppositeFace(HorizontalFacing(Yaw)) : HorizontalFacing(Yaw);
			}
			Properties.Add(TEXT("facing"), FaceNames[Facing]);
		}
		if (Name.EndsWith(TEXT("_stairs")))
		{
			Properties.Add(TEXT("half"), IsUpperHalf(Hit) ? TEXT("top") : TEXT("bottom"));
		}
	}

	// Two-block-tall things: doors and tall plants get their upper half too; beds their head.
	TArray<FMcPlacedBlock> Placed;
	const bool bDoubleHalf = HasValue(Block, TEXT("half"), TEXT("upper"));
	const bool bBed = HasValue(Block, TEXT("part"), TEXT("head"));
	if (bDoubleHalf)
	{
		const FIntVector Above = Pos + FIntVector(0, 1, 0);
		if (!McRaycast::IsReplaceable(World.GetBlock(Above)))
		{
			return {};
		}
		Properties.Add(TEXT("half"), TEXT("lower"));
		Placed.Add({ Pos, Registry.FindState(PlacedName, Properties).Get(BlockState) });
		Properties.Add(TEXT("half"), TEXT("upper"));
		Placed.Add({ Above, Registry.FindState(PlacedName, Properties).Get(BlockState) });
		return Placed;
	}
	if (bBed)
	{
		const int32 Facing = HorizontalFacing(Yaw);
		const FIntVector Head = Pos + McRaycast::FaceDirection(Facing);
		if (!McRaycast::IsReplaceable(World.GetBlock(Head)))
		{
			return {};
		}
		Properties.Add(TEXT("part"), TEXT("foot"));
		Placed.Add({ Pos, Registry.FindState(PlacedName, Properties).Get(BlockState) });
		Properties.Add(TEXT("part"), TEXT("head"));
		Placed.Add({ Head, Registry.FindState(PlacedName, Properties).Get(BlockState) });
		return Placed;
	}

	const TOptional<FMcStateId> State = Registry.FindState(PlacedName, Properties);
	Placed.Add({ Pos, State.Get(Registry.FindDefaultState(PlacedName).Get(BlockState)) });
	return Placed;
}
