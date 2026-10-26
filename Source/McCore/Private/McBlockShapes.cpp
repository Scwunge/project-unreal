#include "McBlockShapes.h"
#include "McBlockRegistry.h"
#include "McCache.h"
#include "Dom/JsonObject.h"

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
			TEXT("closed_eyeblossom"), TEXT("cactus_flower"), TEXT("tripwire_hook"),
			TEXT("pale_hanging_moss"), TEXT("resin_clump"), TEXT("red_shrub"), TEXT("golden_dandelion"), TEXT("shelf_mushroom"),
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
		// Coral wall fans, and coral plants (not coral blocks).
		return Name.EndsWith(TEXT("_coral_wall_fan")) || (Name.EndsWith(TEXT("_coral")) && !Name.EndsWith(TEXT("_coral_block")));
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

	// A box given for a part whose tip points up, turned so the tip points in Direction (up, down or a side).
	FBox3f Oriented(const FBox3f& Up, FName Direction)
	{
		if (Direction == TEXT("down"))
		{
			return FBox3f(FVector3f(Up.Min.X, 1.0f - Up.Max.Y, Up.Min.Z), FVector3f(Up.Max.X, 1.0f - Up.Min.Y, Up.Max.Z));
		}
		if (Direction == TEXT("up"))
		{
			return Up;
		}
		// Facing north the tip's y becomes -z and the old z becomes y.
		const FBox3f North(FVector3f(Up.Min.X, Up.Min.Z, 1.0f - Up.Max.Y), FVector3f(Up.Max.X, Up.Max.Z, 1.0f - Up.Min.Y));
		return Facing(North, Direction);
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

FVector3f FMcBlockShapes::GetStuckMultiplier(FMcStateId State) const
{
	switch (States[State].Stuck)
	{
	case EStuck::Cobweb: return FVector3f(0.25f, 0.05f, 0.25f);
	case EStuck::SweetBerryBush: return FVector3f(0.8f, 0.75f, 0.8f);
	case EStuck::PowderSnow: return FVector3f(0.9f, 1.5f, 0.9f);
	default: return FVector3f(1.0f, 1.0f, 1.0f);
	}
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

	// The climbable tag, or the blocks the wiki lists if the data isn't there.
	TSet<FString> Climbable;
	{
		TArray<FString> Values;
		if (const TSharedPtr<FJsonObject> Tag = McCache::LoadJsonObject(McCache::Data(TEXT("tags/block/climbable.json"))))
		{
			Tag->TryGetStringArrayField(TEXT("values"), Values);
		}
		if (Values.Num() == 0)
		{
			Values = { TEXT("ladder"), TEXT("vine"), TEXT("scaffolding"), TEXT("weeping_vines"), TEXT("weeping_vines_plant"),
				TEXT("twisting_vines"), TEXT("twisting_vines_plant"), TEXT("cave_vines"), TEXT("cave_vines_plant") };
		}
		for (FString Value : Values)
		{
			Value.RemoveFromStart(TEXT("minecraft:"));
			Climbable.Add(Value);
		}
	}

	States.SetNum(Registry.NumStates());
	TArray<FBox3f, TInlineAllocator<16>> Shape;
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
		const EStuck Stuck = Name == TEXT("cobweb") ? EStuck::Cobweb : Name == TEXT("sweet_berry_bush") ? EStuck::SweetBerryBush
			: Name == TEXT("powder_snow") ? EStuck::PowderSnow : EStuck::None;
		const bool bPassable = IsPassable(Name);

		for (const FMcStateId Id : Block.States)
		{
			auto Value = [&](const TCHAR* Property) { return Registry.GetPropertyValue(Id, Property); };
			auto IsTrue = [&](const TCHAR* Property) { return Value(Property) == TEXT("true"); };
			auto Number = [&](const TCHAR* Property) { return FCString::Atoi(*Value(Property).ToString()); };
			auto Add = [&](const FBox3f& Box) { Shape.Add(Box); };
			auto AddFacing = [&](const FBox3f& NorthBox) { Shape.Add(Facing(NorthBox, Value(TEXT("facing")))); };
			Shape.Reset();

			if (bPassable)
			{
			}
			else if (Name.EndsWith(TEXT("_slab")))
			{
				const FName Type = Value(TEXT("type"));
				Add(Type == TEXT("bottom") ? Px(0, 0, 0, 16, 8, 16) : Type == TEXT("top") ? Px(0, 8, 0, 16, 16, 16) : FullCube);
			}
			else if (Name.EndsWith(TEXT("_stairs")))
			{
				// The half slab, plus the upper half's quarters the shape keeps. Written for facing north, where
				// outer_left is the north-west quarter, and the inner shapes add the quarter on the open side.
				const bool bTop = Value(TEXT("half")) == TEXT("top");
				Add(bTop ? Px(0, 8, 0, 16, 16, 16) : Px(0, 0, 0, 16, 8, 16));
				const float Y0 = bTop ? 0.0f : 8.0f;
				const float Y1 = bTop ? 8.0f : 16.0f;
				const FName Shape_ = Value(TEXT("shape"));
				if (Shape_ == TEXT("outer_left"))
				{
					AddFacing(Px(0, Y0, 0, 8, Y1, 8));
				}
				else if (Shape_ == TEXT("outer_right"))
				{
					AddFacing(Px(8, Y0, 0, 16, Y1, 8));
				}
				else
				{
					AddFacing(Px(0, Y0, 0, 16, Y1, 8));
					if (Shape_ == TEXT("inner_left"))
					{
						AddFacing(Px(0, Y0, 8, 8, Y1, 16));
					}
					else if (Shape_ == TEXT("inner_right"))
					{
						AddFacing(Px(8, Y0, 8, 16, Y1, 16));
					}
				}
			}
			else if (Name.EndsWith(TEXT("carpet")))
			{
				Add(Px(0, 0, 0, 16, 1, 16));
			}
			else if (Name == TEXT("snow"))
			{
				const int32 Layers = Number(TEXT("layers"));
				if (Layers > 1)
				{
					Add(Px(0, 0, 0, 16, 2.0f * (Layers - 1), 16));
				}
			}
			else if (Name == TEXT("farmland") || Name == TEXT("dirt_path"))
			{
				Add(Px(0, 0, 0, 16, 15, 16));
			}
			else if (Name == TEXT("soul_sand") || Name == TEXT("mud"))
			{
				Add(Px(0, 0, 0, 16, 14, 16));
			}
			else if (Name == TEXT("honey_block") || Name == TEXT("cactus"))
			{
				Add(Px(1, 0, 1, 15, 15, 15));
			}
			else if (Name == TEXT("lily_pad"))
			{
				Add(Px(1, 0, 1, 15, 1.5f, 15));
			}
			else if (Name.EndsWith(TEXT("_bed")))
			{
				// The legs add nothing to the collision, so both halves are the same 9-pixel slab.
				Add(Px(0, 0, 0, 16, 9, 16));
			}
			else if (Name == TEXT("chest") || Name == TEXT("trapped_chest") || Name == TEXT("ender_chest") || Name.EndsWith(TEXT("copper_chest")))
			{
				Add(Px(1, 0, 1, 15, 14, 15));
			}
			else if (Name == TEXT("enchanting_table"))
			{
				Add(Px(0, 0, 0, 16, 12, 16));
			}
			else if (Name == TEXT("end_portal_frame"))
			{
				Add(Px(0, 0, 0, 16, 13, 16));
				if (IsTrue(TEXT("eye")))
				{
					Add(Px(4, 13, 4, 12, 16, 12));
				}
			}
			else if (Name == TEXT("daylight_detector"))
			{
				Add(Px(0, 0, 0, 16, 6, 16));
			}
			else if (Name == TEXT("repeater") || Name == TEXT("comparator"))
			{
				Add(Px(0, 0, 0, 16, 2, 16));
			}
			else if (Name == TEXT("sculk_sensor") || Name == TEXT("calibrated_sculk_sensor") || Name == TEXT("sculk_shrieker"))
			{
				Add(Px(0, 0, 0, 16, 8, 16));
			}
			else if (Name == TEXT("flower_pot") || Name.StartsWith(TEXT("potted_")))
			{
				Add(Px(5, 0, 5, 11, 6, 11));
			}
			else if (Name == TEXT("ladder"))
			{
				// Against the wall behind it: facing north means it hangs on the south side of its block.
				AddFacing(Px(0, 0, 13, 16, 16, 16));
			}
			else if (Name.EndsWith(TEXT("_trapdoor")))
			{
				if (IsTrue(TEXT("open")))
				{
					AddFacing(Px(0, 0, 13, 16, 16, 16));
				}
				else
				{
					Add(Value(TEXT("half")) == TEXT("top") ? Px(0, 13, 0, 16, 16, 16) : Px(0, 0, 0, 16, 3, 16));
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
				AddFacing(Closed);
			}
			else if (Name.EndsWith(TEXT("_fence_gate")))
			{
				if (!IsTrue(TEXT("open")))
				{
					AddFacing(Px(0, 0, 6, 16, 24, 10));
				}
			}
			else if (Name.EndsWith(TEXT("_fence")))
			{
				// A post, and an arm towards each connected side; 1.5 blocks high so nothing jumps over.
				Add(Px(6, 0, 6, 10, 24, 10));
				if (IsTrue(TEXT("north"))) Add(Px(6, 0, 0, 10, 24, 6));
				if (IsTrue(TEXT("south"))) Add(Px(6, 0, 10, 10, 24, 16));
				if (IsTrue(TEXT("west"))) Add(Px(0, 0, 6, 6, 24, 10));
				if (IsTrue(TEXT("east"))) Add(Px(10, 0, 6, 16, 24, 10));
			}
			else if (Name.EndsWith(TEXT("_wall")))
			{
				// The post only where the wall turns or ends (up); each arm runs to the middle. Low and tall
				// sides both block up to 1.5.
				auto Connected = [&](const TCHAR* Side) { const FName Side_ = Value(Side); return !Side_.IsNone() && Side_ != TEXT("none"); };
				if (IsTrue(TEXT("up")))
				{
					Add(Px(4, 0, 4, 12, 24, 12));
				}
				if (Connected(TEXT("north"))) Add(Px(5, 0, 0, 11, 24, 8));
				if (Connected(TEXT("south"))) Add(Px(5, 0, 8, 11, 24, 16));
				if (Connected(TEXT("west"))) Add(Px(0, 0, 5, 8, 24, 11));
				if (Connected(TEXT("east"))) Add(Px(8, 0, 5, 16, 24, 11));
			}
			else if (Name.EndsWith(TEXT("_pane")) || Name.EndsWith(TEXT("_bars")) || Name == TEXT("iron_bars"))
			{
				Add(Px(7, 0, 7, 9, 16, 9));
				if (IsTrue(TEXT("north"))) Add(Px(7, 0, 0, 9, 16, 7));
				if (IsTrue(TEXT("south"))) Add(Px(7, 0, 9, 9, 16, 16));
				if (IsTrue(TEXT("west"))) Add(Px(0, 0, 7, 7, 16, 9));
				if (IsTrue(TEXT("east"))) Add(Px(9, 0, 7, 16, 16, 9));
			}
			else if (Name == TEXT("lantern") || Name == TEXT("soul_lantern") || Name.EndsWith(TEXT("copper_lantern")))
			{
				if (IsTrue(TEXT("hanging")))
				{
					Add(Px(5, 1, 5, 11, 8, 11));
					Add(Px(6, 8, 6, 10, 10, 10));
				}
				else
				{
					Add(Px(5, 0, 5, 11, 7, 11));
					Add(Px(6, 7, 6, 10, 9, 10));
				}
			}
			else if (Name.EndsWith(TEXT("_chain")))
			{
				const FName Axis = Value(TEXT("axis"));
				Add(Axis == TEXT("x") ? Px(0, 6.5f, 6.5f, 16, 9.5f, 9.5f) : Axis == TEXT("z") ? Px(6.5f, 6.5f, 0, 9.5f, 9.5f, 16) : Px(6.5f, 0, 6.5f, 9.5f, 16, 9.5f));
			}
			else if (Name.EndsWith(TEXT("anvil")))
			{
				// Facing north the top bar runs north to south.
				AddFacing(Px(2, 0, 2, 14, 4, 14));
				AddFacing(Px(4, 4, 3, 12, 5, 13));
				AddFacing(Px(6, 5, 4, 10, 10, 12));
				AddFacing(Px(3, 10, 0, 13, 16, 16));
			}
			else if (Name.EndsWith(TEXT("cauldron")))
			{
				// A full block with the bowl and the gaps between the legs cut out.
				Add(Px(0, 3, 0, 16, 4, 16));
				Add(Px(0, 4, 0, 16, 16, 2));
				Add(Px(0, 4, 14, 16, 16, 16));
				Add(Px(0, 4, 2, 2, 16, 14));
				Add(Px(14, 4, 2, 16, 16, 14));
				// Each leg is an L: four pixels along one wall, two along the other.
				Add(Px(0, 0, 0, 4, 3, 2));
				Add(Px(0, 0, 2, 2, 3, 4));
				Add(Px(12, 0, 0, 16, 3, 2));
				Add(Px(14, 0, 2, 16, 3, 4));
				Add(Px(0, 0, 14, 4, 3, 16));
				Add(Px(0, 0, 12, 2, 3, 14));
				Add(Px(12, 0, 14, 16, 3, 16));
				Add(Px(14, 0, 12, 16, 3, 14));
			}
			else if (Name == TEXT("hopper"))
			{
				Add(Px(0, 10, 0, 16, 11, 16));
				Add(Px(0, 11, 0, 2, 16, 16));
				Add(Px(14, 11, 0, 16, 16, 16));
				Add(Px(2, 11, 0, 14, 16, 2));
				Add(Px(2, 11, 14, 14, 16, 16));
				Add(Px(4, 4, 4, 12, 10, 12));
				if (Value(TEXT("facing")) == TEXT("down"))
				{
					Add(Px(6, 0, 6, 10, 4, 10));
				}
				else
				{
					AddFacing(Px(6, 4, 0, 10, 8, 4));
				}
			}
			else if (Name == TEXT("brewing_stand"))
			{
				Add(Px(1, 0, 1, 15, 2, 15));
				Add(Px(7, 0, 7, 9, 14, 9));
			}
			else if (Name == TEXT("composter"))
			{
				Add(Px(0, 0, 0, 16, 2, 16));
				Add(Px(0, 2, 0, 2, 16, 16));
				Add(Px(14, 2, 0, 16, 16, 16));
				Add(Px(2, 2, 0, 14, 16, 2));
				Add(Px(2, 2, 14, 14, 16, 16));
			}
			else if (Name == TEXT("campfire") || Name == TEXT("soul_campfire"))
			{
				Add(Px(0, 0, 0, 16, 7, 16));
			}
			else if (Name == TEXT("stonecutter"))
			{
				Add(Px(0, 0, 0, 16, 9, 16));
			}
			else if (Name == TEXT("lectern"))
			{
				Add(Px(0, 0, 0, 16, 2, 16));
				Add(Px(4, 2, 4, 12, 14, 12));
				Add(Px(0, 12, 0, 16, 14, 16));
			}
			else if (Name == TEXT("grindstone"))
			{
				// Written for facing north; the wheel's axle runs east to west.
				const FName Face = Value(TEXT("face"));
				if (Face == TEXT("wall"))
				{
					AddFacing(Px(12, 6, 0, 14, 10, 7));
					AddFacing(Px(2, 6, 0, 4, 10, 7));
					AddFacing(Px(12, 5, 7, 14, 11, 13));
					AddFacing(Px(2, 5, 7, 4, 11, 13));
					AddFacing(Px(4, 2, 4, 12, 14, 16));
				}
				else if (Face == TEXT("ceiling"))
				{
					AddFacing(Px(2, 9, 6, 4, 16, 10));
					AddFacing(Px(12, 9, 6, 14, 16, 10));
					AddFacing(Px(2, 3, 5, 4, 9, 11));
					AddFacing(Px(12, 3, 5, 14, 9, 11));
					AddFacing(Px(4, 0, 2, 12, 12, 14));
				}
				else
				{
					AddFacing(Px(12, 0, 6, 14, 7, 10));
					AddFacing(Px(2, 0, 6, 4, 7, 10));
					AddFacing(Px(12, 7, 5, 14, 13, 11));
					AddFacing(Px(2, 7, 5, 4, 13, 11));
					AddFacing(Px(4, 4, 2, 12, 16, 14));
				}
			}
			else if (Name == TEXT("bell"))
			{
				const FName Attachment = Value(TEXT("attachment"));
				const FName Direction = Value(TEXT("facing"));
				if (Attachment == TEXT("floor"))
				{
					// The frame's two posts: facing north or south the bell swings north to south.
					const bool bNorthSouth = Direction == TEXT("north") || Direction == TEXT("south");
					Add(bNorthSouth ? Px(0, 0, 4, 16, 16, 12) : Px(4, 0, 0, 12, 16, 16));
				}
				else
				{
					Add(Px(5, 6, 5, 11, 13, 11));
					Add(Px(4, 4, 4, 12, 6, 12));
					if (Attachment == TEXT("ceiling"))
					{
						Add(Px(7, 13, 7, 9, 16, 9));
					}
					else if (Attachment == TEXT("double_wall"))
					{
						AddFacing(Px(7, 13, 0, 9, 15, 16));
					}
					else
					{
						AddFacing(Px(7, 13, 0, 9, 15, 13));
					}
				}
			}
			else if (Name == TEXT("scaffolding"))
			{
				// The shape you stand on from above: a plank on top and a post in each corner.
				Add(Px(0, 14, 0, 16, 16, 16));
				Add(Px(0, 0, 0, 2, 14, 2));
				Add(Px(14, 0, 0, 16, 14, 2));
				Add(Px(0, 0, 14, 2, 14, 16));
				Add(Px(14, 0, 14, 16, 14, 16));
			}
			else if (Name == TEXT("azalea") || Name == TEXT("flowering_azalea"))
			{
				Add(Px(0, 8, 0, 16, 16, 16));
				Add(Px(6, 0, 6, 10, 8, 10));
			}
			else if (Name == TEXT("end_rod"))
			{
				const FName Direction = Value(TEXT("facing"));
				Add(Oriented(Px(6, 0, 6, 10, 1, 10), Direction));
				Add(Oriented(Px(7, 1, 7, 9, 16, 9), Direction));
			}
			else if (Name.EndsWith(TEXT("lightning_rod")))
			{
				const FName Direction = Value(TEXT("facing"));
				Add(Oriented(Px(6, 12, 6, 10, 16, 10), Direction));
				Add(Oriented(Px(7, 0, 7, 9, 12, 9), Direction));
			}
			else if (Name == TEXT("conduit"))
			{
				Add(Px(5, 5, 5, 11, 11, 11));
			}
			else if (Name == TEXT("decorated_pot"))
			{
				Add(Px(1, 0, 1, 15, 16, 15));
			}
			else if (Name == TEXT("sniffer_egg"))
			{
				Add(Px(1, 0, 2, 15, 16, 14));
			}
			else if (Name == TEXT("dragon_egg"))
			{
				Add(Px(1, 0, 1, 15, 16, 15));
			}
			else if (Name == TEXT("turtle_egg"))
			{
				Add(Number(TEXT("eggs")) == 1 ? Px(3, 0, 3, 12, 7, 12) : Px(1, 0, 1, 15, 7, 15));
			}
			else if (Name == TEXT("sea_pickle"))
			{
				switch (Number(TEXT("pickles")))
				{
				case 1: Add(Px(6, 0, 6, 10, 6, 10)); break;
				case 2: Add(Px(3, 0, 3, 13, 6, 13)); break;
				case 3: Add(Px(2, 0, 2, 14, 6, 14)); break;
				default: Add(Px(2, 0, 2, 14, 7, 14)); break;
				}
			}
			else if (Name == TEXT("candle") || Name.EndsWith(TEXT("_candle")))
			{
				switch (Number(TEXT("candles")))
				{
				case 1: Add(Px(7, 0, 7, 9, 6, 9)); break;
				case 2: Add(Px(5, 0, 6, 11, 6, 9)); break;
				case 3: Add(Px(5, 0, 6, 10, 6, 11)); break;
				default: Add(Px(5, 0, 5, 11, 6, 10)); break;
				}
			}
			else if (Name == TEXT("cake"))
			{
				Add(Px(1.0f + 2.0f * Number(TEXT("bites")), 0, 1, 15, 8, 15));
			}
			else if (Name == TEXT("candle_cake") || Name.EndsWith(TEXT("_candle_cake")))
			{
				Add(Px(1, 0, 1, 15, 8, 15));
				Add(Px(7, 8, 7, 9, 14, 9));
			}
			else if (Name == TEXT("small_amethyst_bud") || Name == TEXT("medium_amethyst_bud") || Name == TEXT("large_amethyst_bud") || Name == TEXT("amethyst_cluster"))
			{
				// Height and inset from the sides, written for a bud growing up.
				const float Height = Name == TEXT("small_amethyst_bud") ? 3.0f : Name == TEXT("medium_amethyst_bud") ? 4.0f : Name == TEXT("large_amethyst_bud") ? 5.0f : 7.0f;
				const float Inset = Name == TEXT("small_amethyst_bud") ? 4.0f : 3.0f;
				Add(Oriented(Px(Inset, 0, Inset, 16 - Inset, Height, 16 - Inset), Value(TEXT("facing"))));
			}
			else if (Name == TEXT("pointed_dripstone") || Name == TEXT("sulfur_spike"))
			{
				// Points up or down; the thicker parts are wider and longer.
				const FName Thickness = Value(TEXT("thickness"));
				FBox3f Up;
				if (Thickness == TEXT("tip"))
				{
					Up = Px(5, 0, 5, 11, 11, 11);
				}
				else if (Thickness == TEXT("tip_merge"))
				{
					Up = Px(5, 0, 5, 11, 16, 11);
				}
				else if (Thickness == TEXT("frustum"))
				{
					Up = Px(4, 0, 4, 12, 16, 12);
				}
				else if (Thickness == TEXT("middle"))
				{
					Up = Px(3, 0, 3, 13, 16, 13);
				}
				else
				{
					Up = Px(2, 0, 2, 14, 16, 14);
				}
				Add(Oriented(Up, Value(TEXT("vertical_direction"))));
			}
			else if (Name.EndsWith(TEXT("_wall_skull")) || Name.EndsWith(TEXT("_wall_head")))
			{
				const bool bWide = Name.StartsWith(TEXT("dragon_")) || Name.StartsWith(TEXT("piglin_"));
				AddFacing(bWide ? Px(3, 4, 8, 13, 12, 16) : Px(4, 4, 8, 12, 12, 16));
			}
			else if ((Name.EndsWith(TEXT("_skull")) || Name.EndsWith(TEXT("_head"))) && Name != TEXT("piston_head"))
			{
				const bool bWide = Name.StartsWith(TEXT("dragon_")) || Name.StartsWith(TEXT("piglin_"));
				Add(bWide ? Px(3, 0, 3, 13, 8, 13) : Px(4, 0, 4, 12, 8, 12));
			}
			else if (Name == TEXT("piston") || Name == TEXT("sticky_piston"))
			{
				// Extended, the body is 12 pixels deep and the head block carries the rest.
				if (IsTrue(TEXT("extended")))
				{
					Add(Oriented(Px(0, 0, 0, 16, 12, 16), Value(TEXT("facing"))));
				}
				else
				{
					Add(FullCube);
				}
			}
			else if (Name == TEXT("piston_head"))
			{
				const FName Direction = Value(TEXT("facing"));
				Add(Oriented(Px(0, 12, 0, 16, 16, 16), Direction));
				Add(Oriented(Px(6, 0, 6, 10, 12, 10), Direction));
			}
			else if (Name == TEXT("bamboo"))
			{
				Add(Px(6.5f, 0, 6.5f, 9.5f, 16, 9.5f));
			}
			else if (Name == TEXT("chorus_flower"))
			{
				Add(Px(2, 14, 2, 14, 16, 14));
				Add(Px(0, 2, 2, 2, 14, 14));
				Add(Px(14, 2, 2, 16, 14, 14));
				Add(Px(2, 2, 0, 14, 14, 2));
				Add(Px(2, 2, 14, 14, 14, 16));
				Add(Px(2, 0, 2, 14, 14, 14));
			}
			else if (Name == TEXT("chorus_plant"))
			{
				Add(Px(4, 4, 4, 12, 12, 12));
				if (IsTrue(TEXT("north"))) Add(Px(4, 4, 0, 12, 12, 4));
				if (IsTrue(TEXT("south"))) Add(Px(4, 4, 12, 12, 12, 16));
				if (IsTrue(TEXT("west"))) Add(Px(0, 4, 4, 4, 12, 12));
				if (IsTrue(TEXT("east"))) Add(Px(12, 4, 4, 16, 12, 12));
				if (IsTrue(TEXT("up"))) Add(Px(4, 12, 4, 12, 16, 12));
				if (IsTrue(TEXT("down"))) Add(Px(4, 0, 4, 12, 4, 12));
			}
			else if (Name == TEXT("cocoa"))
			{
				const int32 Age = Number(TEXT("age"));
				AddFacing(Age == 0 ? Px(6, 7, 1, 10, 12, 5) : Age == 1 ? Px(5, 5, 1, 11, 12, 7) : Px(4, 3, 1, 12, 12, 9));
			}
			else if (Name == TEXT("big_dripleaf"))
			{
				// The leaf droops as it tilts, then lets go.
				const FName Tilt = Value(TEXT("tilt"));
				if (Tilt != TEXT("full"))
				{
					Add(Tilt == TEXT("partial") ? Px(0, 11, 0, 16, 13, 16) : Px(0, 11, 0, 16, 15, 16));
				}
			}
			else if (Name == TEXT("heavy_core"))
			{
				Add(Px(4, 0, 4, 12, 8, 12));
			}
			else if (Name == TEXT("dried_ghast"))
			{
				Add(Px(3, 0, 3, 13, 10, 13));
			}
			else if (Name.EndsWith(TEXT("copper_golem_statue")))
			{
				Add(Px(3, 0, 3, 13, 14, 13));
			}
			else if (Name.EndsWith(TEXT("_shelf")))
			{
				// The back panel and the lips of the shelf in front of it.
				AddFacing(Px(0, 0, 13, 16, 16, 16));
				AddFacing(Px(0, 0, 11, 16, 4, 13));
				AddFacing(Px(0, 12, 11, 16, 16, 13));
			}
			else
			{
				Add(FullCube);
			}

			FState& Info = States[Id];
			Info.FirstBox = Boxes.Num();
			Info.NumBoxes = Shape.Num();
			Info.Slipperiness = Slipperiness;
			Info.SpeedFactor = SpeedFactor;
			Info.JumpFactor = JumpFactor;
			Info.Stuck = Stuck;
			Info.bClimbable = Climbable.Contains(Name);
			if (Name == TEXT("water") || Name == TEXT("lava"))
			{
				const int32 Level = Number(TEXT("level"));
				Info.Fluid = Name == TEXT("water") ? EMcFluid::Water : EMcFluid::Lava;
				Info.FluidLevel = static_cast<uint8>(FMath::Clamp(Level, 0, 15));
			}
			else if (Name == TEXT("bubble_column") || Name == TEXT("kelp") || Name == TEXT("kelp_plant") || Name == TEXT("seagrass")
				|| Name == TEXT("tall_seagrass") || IsTrue(TEXT("waterlogged")))
			{
				Info.Fluid = EMcFluid::Water;
			}
			Boxes.Append(Shape);
		}
	}
	return true;
}
