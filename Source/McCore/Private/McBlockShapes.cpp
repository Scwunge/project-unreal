#include "McBlockShapes.h"
#include "McBlockRegistry.h"

namespace
{
	// A box in sixteenths of a block, the way the wiki and the model files give sizes.
	FBox3f Px(float X0, float Y0, float Z0, float X1, float Y1, float Z1)
	{
		return FBox3f(FVector3f(X0, Y0, Z0) / 16.0f, FVector3f(X1, Y1, Z1) / 16.0f);
	}

	const FBox3f FullCube = Px(0, 0, 0, 16, 16, 16);

	// Blocks with no collision at all: plants, torches, rails, signs, fluids and the like.
	bool IsPassable(const FString& Name)
	{
		static const TCHAR* const Exact[] = {
			TEXT("air"), TEXT("cave_air"), TEXT("void_air"), TEXT("water"), TEXT("lava"), TEXT("bubble_column"),
			TEXT("short_grass"), TEXT("tall_grass"), TEXT("fern"), TEXT("large_fern"), TEXT("dead_bush"), TEXT("bush"),
			TEXT("firefly_bush"), TEXT("short_dry_grass"), TEXT("tall_dry_grass"), TEXT("seagrass"), TEXT("tall_seagrass"),
			TEXT("kelp"), TEXT("kelp_plant"), TEXT("vine"), TEXT("glow_lichen"), TEXT("sculk_vein"), TEXT("redstone_wire"),
			TEXT("tripwire"), TEXT("lever"), TEXT("wheat"), TEXT("carrots"), TEXT("potatoes"), TEXT("beetroots"),
			TEXT("sugar_cane"), TEXT("cobweb"), TEXT("fire"), TEXT("soul_fire"), TEXT("light"), TEXT("structure_void"),
			TEXT("nether_portal"), TEXT("end_portal"), TEXT("end_gateway"), TEXT("brown_mushroom"), TEXT("red_mushroom"),
			TEXT("crimson_fungus"), TEXT("warped_fungus"), TEXT("crimson_roots"), TEXT("warped_roots"), TEXT("hanging_roots"),
			TEXT("nether_sprouts"), TEXT("spore_blossom"), TEXT("cave_vines"), TEXT("cave_vines_plant"), TEXT("weeping_vines"),
			TEXT("weeping_vines_plant"), TEXT("twisting_vines"), TEXT("twisting_vines_plant"), TEXT("pink_petals"),
			TEXT("wildflowers"), TEXT("leaf_litter"), TEXT("nether_wart"), TEXT("sweet_berry_bush"), TEXT("small_dripleaf"),
			TEXT("big_dripleaf_stem"), TEXT("pitcher_crop"), TEXT("torchflower_crop"), TEXT("melon_stem"), TEXT("pumpkin_stem"),
			TEXT("attached_melon_stem"), TEXT("attached_pumpkin_stem"), TEXT("frogspawn"), TEXT("powder_snow"),
			TEXT("dandelion"), TEXT("poppy"), TEXT("blue_orchid"), TEXT("allium"), TEXT("azure_bluet"), TEXT("oxeye_daisy"),
			TEXT("cornflower"), TEXT("lily_of_the_valley"), TEXT("wither_rose"), TEXT("sunflower"), TEXT("lilac"),
			TEXT("rose_bush"), TEXT("peony"), TEXT("torchflower"), TEXT("pitcher_plant"), TEXT("open_eyeblossom"),
			TEXT("closed_eyeblossom"), TEXT("cactus_flower"), TEXT("tripwire_hook"), TEXT("structure_void"),
		};
		for (const TCHAR* Candidate : Exact)
		{
			if (Name == Candidate)
			{
				return true;
			}
		}
		static const TCHAR* const Parts[] = {
			TEXT("torch"), TEXT("sapling"), TEXT("_button"), TEXT("rail"), TEXT("pressure_plate"), TEXT("_sign"),
			TEXT("banner"), TEXT("_tulip"), TEXT("coral_fan"), TEXT("propagule"),
		};
		for (const TCHAR* Part : Parts)
		{
			if (Name.Contains(Part))
			{
				return true;
			}
		}
		// Coral plants (not coral blocks).
		return Name.EndsWith(TEXT("_coral")) && !Name.EndsWith(TEXT("_coral_block"));
	}

	// Rotates a box given for facing north (thin side towards -z) to the given facing, about the block centre.
	FBox3f Facing(const FBox3f& North, FName Direction)
	{
		auto Rotate = [](const FVector3f& P, int32 Turns)
		{
			FVector3f R = P;
			for (int32 Turn = 0; Turn < Turns; ++Turn)
			{
				// A quarter turn clockwise seen from above: north (-z) to east (+x).
				R = FVector3f(1.0f - R.Z, R.Y, R.X);
			}
			return R;
		};
		const int32 Turns = Direction == TEXT("east") ? 1 : Direction == TEXT("south") ? 2 : Direction == TEXT("west") ? 3 : 0;
		const FVector3f A = Rotate(North.Min, Turns);
		const FVector3f B = Rotate(North.Max, Turns);
		return FBox3f(A.ComponentMin(B), A.ComponentMax(B));
	}
}

FMcBlockShapes& FMcBlockShapes::Get()
{
	static FMcBlockShapes Instance;
	return Instance;
}

TConstArrayView<FBox3f> FMcBlockShapes::GetCollision(FMcStateId State) const
{
	const FState& Info = States[State];
	return TConstArrayView<FBox3f>(Boxes.GetData() + Info.FirstBox, Info.NumBoxes);
}

bool FMcBlockShapes::EnsureLoaded()
{
	if (IsLoaded())
	{
		return true;
	}
	FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!Registry.EnsureLoaded())
	{
		return false;
	}

	States.SetNum(Registry.NumStates());
	TArray<FBox3f, TInlineAllocator<8>> Shape;
	for (int32 BlockIndex = 0; BlockIndex < Registry.NumBlocks(); ++BlockIndex)
	{
		const FMcBlock& Block = Registry.GetBlock(BlockIndex);
		FString Name = Block.Name.ToString();
		Name.RemoveFromStart(TEXT("minecraft:"));

		float Slipperiness = 0.6f;
		if (Name == TEXT("ice") || Name == TEXT("packed_ice") || Name == TEXT("frosted_ice"))
		{
			Slipperiness = 0.98f;
		}
		else if (Name == TEXT("blue_ice"))
		{
			Slipperiness = 0.989f;
		}
		else if (Name == TEXT("slime_block"))
		{
			Slipperiness = 0.8f;
		}
		const float SpeedFactor = (Name == TEXT("soul_sand") || Name == TEXT("honey_block")) ? 0.4f : 1.0f;
		const float JumpFactor = Name == TEXT("honey_block") ? 0.5f : 1.0f;
		const bool bPassable = IsPassable(Name);

		for (const FMcStateId Id : Block.States)
		{
			auto Value = [&](const TCHAR* Property) { return Registry.GetPropertyValue(Id, Property); };
			auto IsTrue = [&](const TCHAR* Property) { return Value(Property) == TEXT("true"); };
			Shape.Reset();

			if (bPassable)
			{
			}
			else if (Name.EndsWith(TEXT("_slab")))
			{
				const FName Type = Value(TEXT("type"));
				Shape.Add(Type == TEXT("bottom") ? Px(0, 0, 0, 16, 8, 16) : Type == TEXT("top") ? Px(0, 8, 0, 16, 16, 16) : FullCube);
			}
			else if (Name.EndsWith(TEXT("_stairs")))
			{
				// The half slab, plus the step on the facing side. Corner shapes are drawn straight for now.
				const bool bTop = Value(TEXT("half")) == TEXT("top");
				Shape.Add(bTop ? Px(0, 8, 0, 16, 16, 16) : Px(0, 0, 0, 16, 8, 16));
				Shape.Add(Facing(bTop ? Px(0, 0, 0, 16, 8, 8) : Px(0, 8, 0, 16, 16, 8), Value(TEXT("facing"))));
			}
			else if (Name.EndsWith(TEXT("carpet")))
			{
				Shape.Add(Px(0, 0, 0, 16, 1, 16));
			}
			else if (Name == TEXT("snow"))
			{
				const int32 Layers = FCString::Atoi(*Value(TEXT("layers")).ToString());
				if (Layers > 1)
				{
					Shape.Add(Px(0, 0, 0, 16, 2.0f * (Layers - 1), 16));
				}
			}
			else if (Name == TEXT("farmland") || Name == TEXT("dirt_path"))
			{
				Shape.Add(Px(0, 0, 0, 16, 15, 16));
			}
			else if (Name == TEXT("soul_sand") || Name == TEXT("mud"))
			{
				Shape.Add(Px(0, 0, 0, 16, 14, 16));
			}
			else if (Name == TEXT("honey_block"))
			{
				Shape.Add(Px(1, 0, 1, 15, 15, 15));
			}
			else if (Name == TEXT("cactus"))
			{
				Shape.Add(Px(1, 0, 1, 15, 15, 15));
			}
			else if (Name == TEXT("lily_pad"))
			{
				Shape.Add(Px(1, 0, 1, 15, 1.5f, 15));
			}
			else if (Name.EndsWith(TEXT("_bed")))
			{
				Shape.Add(Px(0, 0, 0, 16, 9, 16));
			}
			else if (Name == TEXT("chest") || Name == TEXT("trapped_chest") || Name == TEXT("ender_chest"))
			{
				Shape.Add(Px(1, 0, 1, 15, 14, 15));
			}
			else if (Name == TEXT("enchanting_table"))
			{
				Shape.Add(Px(0, 0, 0, 16, 12, 16));
			}
			else if (Name == TEXT("end_portal_frame"))
			{
				Shape.Add(Px(0, 0, 0, 16, 13, 16));
			}
			else if (Name == TEXT("daylight_detector"))
			{
				Shape.Add(Px(0, 0, 0, 16, 6, 16));
			}
			else if (Name == TEXT("flower_pot") || Name.StartsWith(TEXT("potted_")))
			{
				Shape.Add(Px(5, 0, 5, 11, 6, 11));
			}
			else if (Name == TEXT("ladder"))
			{
				// Against the wall behind it: facing north means it hangs on the south side of its block.
				Shape.Add(Facing(Px(0, 0, 13, 16, 16, 16), Value(TEXT("facing"))));
			}
			else if (Name.EndsWith(TEXT("_trapdoor")))
			{
				if (IsTrue(TEXT("open")))
				{
					Shape.Add(Facing(Px(0, 0, 13, 16, 16, 16), Value(TEXT("facing"))));
				}
				else
				{
					Shape.Add(Value(TEXT("half")) == TEXT("top") ? Px(0, 13, 0, 16, 16, 16) : Px(0, 0, 0, 16, 3, 16));
				}
			}
			else if (Name.EndsWith(TEXT("_door")))
			{
				// Closed, a door is a 3-pixel slab on the side it faces away from; open, it swings to the hinge side.
				FBox3f Closed = Px(0, 0, 13, 16, 16, 16);
				if (IsTrue(TEXT("open")))
				{
					Closed = Value(TEXT("hinge")) == TEXT("right") ? Px(0, 0, 0, 3, 16, 16) : Px(13, 0, 0, 16, 16, 16);
				}
				Shape.Add(Facing(Closed, Value(TEXT("facing"))));
			}
			else if (Name.EndsWith(TEXT("_fence_gate")))
			{
				if (!IsTrue(TEXT("open")))
				{
					Shape.Add(Facing(Px(0, 0, 6, 16, 24, 10), Value(TEXT("facing"))));
				}
			}
			else if (Name.EndsWith(TEXT("_fence")))
			{
				// A post, and an arm towards each connected side; 1.5 blocks high so nothing jumps over.
				Shape.Add(Px(6, 0, 6, 10, 24, 10));
				if (IsTrue(TEXT("north"))) Shape.Add(Px(6, 0, 0, 10, 24, 6));
				if (IsTrue(TEXT("south"))) Shape.Add(Px(6, 0, 10, 10, 24, 16));
				if (IsTrue(TEXT("west"))) Shape.Add(Px(0, 0, 6, 6, 24, 10));
				if (IsTrue(TEXT("east"))) Shape.Add(Px(10, 0, 6, 16, 24, 10));
			}
			else if (Name.EndsWith(TEXT("_wall")))
			{
				// Wall sides are none, low or tall; all of them block up to 1.5.
				auto Connected = [&](const TCHAR* Side) { const FName Side_ = Value(Side); return !Side_.IsNone() && Side_ != TEXT("none"); };
				Shape.Add(Px(4, 0, 4, 12, 24, 12));
				if (Connected(TEXT("north"))) Shape.Add(Px(5, 0, 0, 11, 24, 4));
				if (Connected(TEXT("south"))) Shape.Add(Px(5, 0, 12, 11, 24, 16));
				if (Connected(TEXT("west"))) Shape.Add(Px(0, 0, 5, 4, 24, 11));
				if (Connected(TEXT("east"))) Shape.Add(Px(12, 0, 5, 16, 24, 11));
			}
			else if (Name.EndsWith(TEXT("_pane")) || Name == TEXT("iron_bars") || Name.EndsWith(TEXT("_bars")))
			{
				Shape.Add(Px(7, 0, 7, 9, 16, 9));
				if (IsTrue(TEXT("north"))) Shape.Add(Px(7, 0, 0, 9, 16, 7));
				if (IsTrue(TEXT("south"))) Shape.Add(Px(7, 0, 9, 9, 16, 16));
				if (IsTrue(TEXT("west"))) Shape.Add(Px(0, 0, 7, 7, 16, 9));
				if (IsTrue(TEXT("east"))) Shape.Add(Px(9, 0, 7, 16, 16, 9));
			}
			else
			{
				Shape.Add(FullCube);
			}

			FState& Info = States[Id];
			Info.FirstBox = Boxes.Num();
			Info.NumBoxes = Shape.Num();
			Info.Slipperiness = Slipperiness;
			Info.SpeedFactor = SpeedFactor;
			Info.JumpFactor = JumpFactor;
			Boxes.Append(Shape);
		}
	}
	return true;
}
