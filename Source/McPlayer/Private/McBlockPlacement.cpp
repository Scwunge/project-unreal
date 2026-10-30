#include "McBlockPlacement.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McChestLink.h"

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
	else if (Name.EndsWith(TEXT("_hanging_sign")) && !Name.Contains(TEXT("_wall_")))
	{
		// Under a block it hangs from chains, facing the player square on (minecraft.wiki, "Hanging Sign"); on a side
		// it hangs from a bar out of the wall, its board across the wall, facing the side the player looks from.
		if (Face >= 2)
		{
			FString Wall = Name;
			Wall.ReplaceInline(TEXT("_hanging_sign"), TEXT("_wall_hanging_sign"));
			if (!Registry.FindBlock(FName(*Wall)))
			{
				return {};
			}
			PlacedName = FName(*Wall);
			const double Radians = FMath::DegreesToRadians(static_cast<double>(Yaw));
			const FVector2D Look(-FMath::Sin(Radians), FMath::Cos(Radians));
			// Across the clicked face's axis: z for an east or west face, x for a north or south one.
			const int32 Across = Face >= 4 ? (Look.Y >= 0.0 ? 3 : 2) : (Look.X >= 0.0 ? 5 : 4);
			Properties.Add(TEXT("facing"), FaceNames[OppositeFace(Across)]);
		}
		else if (Face == 0 || !McRaycast::IsReplaceable(World.GetBlock(Pos + FIntVector(0, 1, 0))))
		{
			const int32 Quarter = FMath::RoundToInt32(FMath::Fmod(FMath::Fmod(Yaw + 180.0f, 360.0f) + 360.0f, 360.0f) / 90.0f) & 3;
			Properties.Add(TEXT("rotation"), *FString::FromInt(Quarter * 4));
			Properties.Add(TEXT("attached"), TEXT("false"));
		}
		else
		{
			return {};
		}
	}
	else if (Name.EndsWith(TEXT("_sign")) && !Name.Contains(TEXT("_wall_")))
	{
		// On top of a block it stands, facing the player in sixteen steps; on a side it goes on the wall.
		if (Face == 0)
		{
			return {};
		}
		if (Face >= 2)
		{
			FString Wall = Name;
			Wall.ReplaceInline(TEXT("_sign"), TEXT("_wall_sign"));
			if (!Registry.FindBlock(FName(*Wall)))
			{
				return {};
			}
			PlacedName = FName(*Wall);
			Properties.Add(TEXT("facing"), FaceNames[Face]);
		}
		else
		{
			const int32 Rotation = FMath::RoundToInt32(FMath::Fmod(FMath::Fmod(Yaw + 180.0f, 360.0f) + 360.0f, 360.0f) / 22.5f) & 15;
			Properties.Add(TEXT("rotation"), *FString::FromInt(Rotation));
		}
	}
	else if (Name.EndsWith(TEXT("shulker_box")))
	{
		// Its lid faces out of the face clicked (minecraft.wiki, "Shulker Box").
		Properties.Add(TEXT("facing"), FaceNames[Face]);
	}
	else if ((Name.EndsWith(TEXT("_skull")) || Name.EndsWith(TEXT("_head"))) && Name != TEXT("minecraft:piston_head") && !Name.Contains(TEXT("_wall_")))
	{
		// On a floor a head faces the player in sixteen steps; on a side it goes on the wall, facing out (minecraft.wiki,
		// "Head"). Not under a block.
		if (Face == 0)
		{
			return {};
		}
		if (Face >= 2)
		{
			const int32 Last = Name.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			const FString Wall = Name.Left(Last) + TEXT("_wall") + Name.Mid(Last);
			if (!Registry.FindBlock(FName(*Wall)))
			{
				return {};
			}
			PlacedName = FName(*Wall);
			Properties.Add(TEXT("facing"), FaceNames[Face]);
		}
		else
		{
			// A head's rotation 0 faces north (a sign's south), so one facing the player takes the player's own yaw.
			const int32 Rotation = FMath::RoundToInt32(FMath::Fmod(FMath::Fmod(Yaw, 360.0f) + 360.0f, 360.0f) / 22.5f) & 15;
			Properties.Add(TEXT("rotation"), *FString::FromInt(Rotation));
		}
	}
	else if (Name == TEXT("minecraft:bell"))
	{
		// On a floor it stands in its frame, under a block it hangs, both turned to the player; on a side it hangs from a
		// bar out of the wall (facing the wall), or across to a block on the far side too (minecraft.wiki, "Bell").
		if (Face >= 2)
		{
			const bool bBetween = !McRaycast::IsReplaceable(World.GetBlock(Pos + McRaycast::FaceDirection(Face)));
			Properties.Add(TEXT("attachment"), bBetween ? TEXT("double_wall") : TEXT("single_wall"));
			Properties.Add(TEXT("facing"), FaceNames[OppositeFace(Face)]);
		}
		else
		{
			Properties.Add(TEXT("attachment"), Face == 1 ? TEXT("floor") : TEXT("ceiling"));
			Properties.Add(TEXT("facing"), FaceNames[OppositeFace(HorizontalFacing(Yaw))]);
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

	// A chest beside a single chest of the same kind and facing joins it as a double chest.
	if (McChestLink::IsLinkable(PlacedName) && Properties.Contains(TEXT("facing")))
	{
		if (const TOptional<TPair<FName, FIntVector>> Join = McChestLink::ChooseJoin(World, Pos, PlacedName, Properties[TEXT("facing")], Hit.Block))
		{
			Properties.Add(TEXT("type"), Join->Key);
		}
	}

	const TOptional<FMcStateId> State = Registry.FindState(PlacedName, Properties);
	Placed.Add({ Pos, State.Get(Registry.FindDefaultState(PlacedName).Get(BlockState)) });
	return Placed;
}
