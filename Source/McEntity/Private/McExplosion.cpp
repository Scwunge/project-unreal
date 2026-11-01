#include "McExplosion.h"
#include "McBlockRegistry.h"
#include "McBlockShapes.h"
#include "McBlockWorld.h"
#include "McDimensionType.h"

namespace
{
	constexpr float Unbreakable = 3600000.0f;

	float ResistanceOfName(const FString& Name)
	{
		static const TMap<FString, float> Known = {
			{ TEXT("air"), 0.0f }, { TEXT("cave_air"), 0.0f }, { TEXT("void_air"), 0.0f }, { TEXT("tnt"), 0.0f },
			{ TEXT("dirt"), 0.5f }, { TEXT("coarse_dirt"), 0.5f }, { TEXT("rooted_dirt"), 0.5f }, { TEXT("podzol"), 0.5f }, { TEXT("mycelium"), 0.5f },
			{ TEXT("grass_block"), 0.6f }, { TEXT("dirt_path"), 0.65f }, { TEXT("farmland"), 0.6f }, { TEXT("gravel"), 0.6f }, { TEXT("clay"), 0.6f },
			{ TEXT("sand"), 0.5f }, { TEXT("red_sand"), 0.5f }, { TEXT("soul_sand"), 0.5f }, { TEXT("soul_soil"), 0.5f }, { TEXT("mud"), 0.5f },
			{ TEXT("snow"), 0.1f }, { TEXT("snow_block"), 0.2f }, { TEXT("ice"), 0.5f }, { TEXT("packed_ice"), 0.5f }, { TEXT("blue_ice"), 2.8f },
			{ TEXT("stone"), 6.0f }, { TEXT("cobblestone"), 6.0f }, { TEXT("mossy_cobblestone"), 6.0f }, { TEXT("smooth_stone"), 6.0f },
			{ TEXT("andesite"), 6.0f }, { TEXT("diorite"), 6.0f }, { TEXT("granite"), 6.0f }, { TEXT("deepslate"), 6.0f }, { TEXT("cobbled_deepslate"), 6.0f },
			{ TEXT("tuff"), 6.0f }, { TEXT("calcite"), 0.75f }, { TEXT("dripstone_block"), 1.0f }, { TEXT("blackstone"), 6.0f }, { TEXT("basalt"), 4.2f },
			{ TEXT("netherrack"), 0.4f }, { TEXT("nether_bricks"), 6.0f }, { TEXT("red_nether_bricks"), 6.0f }, { TEXT("magma_block"), 0.5f },
			{ TEXT("end_stone"), 9.0f }, { TEXT("end_stone_bricks"), 9.0f }, { TEXT("sandstone"), 0.8f }, { TEXT("red_sandstone"), 0.8f },
			{ TEXT("bricks"), 6.0f }, { TEXT("stone_bricks"), 6.0f }, { TEXT("mossy_stone_bricks"), 6.0f }, { TEXT("cracked_stone_bricks"), 6.0f },
			{ TEXT("iron_block"), 6.0f }, { TEXT("gold_block"), 6.0f }, { TEXT("diamond_block"), 6.0f }, { TEXT("emerald_block"), 6.0f },
			{ TEXT("copper_block"), 6.0f }, { TEXT("coal_block"), 6.0f }, { TEXT("lapis_block"), 3.0f }, { TEXT("redstone_block"), 6.0f },
			{ TEXT("obsidian"), 1200.0f }, { TEXT("crying_obsidian"), 1200.0f }, { TEXT("respawn_anchor"), 1200.0f }, { TEXT("ancient_debris"), 1200.0f },
			{ TEXT("netherite_block"), 1200.0f }, { TEXT("enchanting_table"), 1200.0f }, { TEXT("anvil"), 1200.0f }, { TEXT("ender_chest"), 600.0f },
			{ TEXT("bedrock"), Unbreakable }, { TEXT("barrier"), Unbreakable }, { TEXT("light"), Unbreakable }, { TEXT("command_block"), Unbreakable },
			{ TEXT("chain_command_block"), Unbreakable }, { TEXT("repeating_command_block"), Unbreakable }, { TEXT("structure_block"), Unbreakable },
			{ TEXT("jigsaw"), Unbreakable }, { TEXT("end_portal"), Unbreakable }, { TEXT("end_portal_frame"), Unbreakable }, { TEXT("end_gateway"), Unbreakable },
			{ TEXT("nether_portal"), Unbreakable }, { TEXT("reinforced_deepslate"), Unbreakable },
			{ TEXT("cobweb"), 4.0f }, { TEXT("glass"), 0.3f }, { TEXT("glowstone"), 0.3f }, { TEXT("sea_lantern"), 0.3f },
			{ TEXT("bookshelf"), 1.5f }, { TEXT("chest"), 2.5f }, { TEXT("trapped_chest"), 2.5f }, { TEXT("crafting_table"), 2.5f },
			{ TEXT("furnace"), 3.5f }, { TEXT("blast_furnace"), 3.5f }, { TEXT("smoker"), 3.5f }, { TEXT("dispenser"), 3.5f }, { TEXT("dropper"), 3.5f },
			{ TEXT("hay_block"), 0.5f }, { TEXT("sponge"), 0.6f }, { TEXT("bone_block"), 2.0f }, { TEXT("pumpkin"), 1.0f }, { TEXT("melon"), 1.0f },
			{ TEXT("cactus"), 0.4f }, { TEXT("bamboo"), 1.0f }, { TEXT("torch"), 0.0f }, { TEXT("ladder"), 0.4f },
		};
		if (const float* Value = Known.Find(Name))
		{
			return *Value;
		}
		// Families, by what the name ends with or contains.
		if (Name.EndsWith(TEXT("_ore")) || Name == TEXT("nether_gold_ore") || Name == TEXT("gilded_blackstone"))
		{
			return 3.0f;
		}
		if (Name.EndsWith(TEXT("_leaves")))
		{
			return 0.2f;
		}
		if (Name.EndsWith(TEXT("_wool")) || Name.EndsWith(TEXT("_carpet")))
		{
			return Name.EndsWith(TEXT("_wool")) ? 0.8f : 0.1f;
		}
		if (Name.EndsWith(TEXT("glass")) || Name.EndsWith(TEXT("glass_pane")))
		{
			return 0.3f;
		}
		if (Name.EndsWith(TEXT("_concrete")))
		{
			return 1.8f;
		}
		if (Name.EndsWith(TEXT("_concrete_powder")))
		{
			return 0.5f;
		}
		if (Name.EndsWith(TEXT("terracotta")))
		{
			return 4.2f;
		}
		if (Name.EndsWith(TEXT("_log")) || Name.EndsWith(TEXT("_wood")) || Name.EndsWith(TEXT("_stem")) || Name.EndsWith(TEXT("_hyphae")))
		{
			return 2.0f;
		}
		if (Name.EndsWith(TEXT("_planks")) || Name.EndsWith(TEXT("_fence")) || Name.EndsWith(TEXT("_fence_gate")) || Name.EndsWith(TEXT("_door")))
		{
			// Wooden stuff is 3; iron doors are as hard as iron.
			return Name.StartsWith(TEXT("iron_")) ? 5.0f : 3.0f;
		}
		if (Name.EndsWith(TEXT("_stairs")) || Name.EndsWith(TEXT("_slab")) || Name.EndsWith(TEXT("_wall")))
		{
			// Same as the block they're cut from: wood is 3, stone and brick 6, sandstone and the like 0.8.
			static const TCHAR* Wooden[] = { TEXT("oak"), TEXT("spruce"), TEXT("birch"), TEXT("jungle"), TEXT("acacia"), TEXT("dark_oak"),
				TEXT("mangrove"), TEXT("cherry"), TEXT("bamboo"), TEXT("crimson"), TEXT("warped") };
			for (const TCHAR* Prefix : Wooden)
			{
				if (Name.StartsWith(Prefix))
				{
					return 3.0f;
				}
			}
			return Name.Contains(TEXT("sandstone")) ? 0.8f : 6.0f;
		}
		return -1.0f;
	}
}

float McExplosion::GetBlastResistance(FMcStateId State)
{
	static TMap<FMcStateId, float> Cache;
	if (const float* Known = Cache.Find(State))
	{
		return *Known;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	float Resistance = 0.0f;
	if (Shapes.GetFluid(State) != EMcFluid::None)
	{
		Resistance = 100.0f;
	}
	else if (Registry.IsValidState(State) && !Registry.IsAir(State))
	{
		const FString Name = Registry.GetBlockOf(State).Name.ToString().RightChop(10);
		Resistance = ResistanceOfName(Name);
		if (Resistance < 0.0f)
		{
			Resistance = Shapes.HasCollision(State) ? 2.0f : 0.0f;
		}
	}
	Cache.Add(State, Resistance);
	return Resistance;
}

void McExplosion::Collect(const FMcBlockWorld& World, const FVector& Center, float Power, FRandomStream& Random, TArray<FIntVector>& Out)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcDimensionType& Dimension = World.GetDimension();
	TSet<FIntVector> Hit;
	constexpr int32 Grid = 16;
	for (int32 J = 0; J < Grid; ++J)
	{
		for (int32 K = 0; K < Grid; ++K)
		{
			for (int32 L = 0; L < Grid; ++L)
			{
				if (J != 0 && J != Grid - 1 && K != 0 && K != Grid - 1 && L != 0 && L != Grid - 1)
				{
					continue;
				}
				FVector Direction(J / (Grid - 1.0) * 2.0 - 1.0, K / (Grid - 1.0) * 2.0 - 1.0, L / (Grid - 1.0) * 2.0 - 1.0);
				Direction = Direction.GetSafeNormal();
				float Strength = Power * (0.7f + Random.FRand() * 0.6f);
				FVector At = Center;
				for (; Strength > 0.0f; Strength -= 0.225f)
				{
					const FIntVector Cell(FMath::FloorToInt32(At.X), FMath::FloorToInt32(At.Y), FMath::FloorToInt32(At.Z));
					if (Cell.Y < Dimension.MinY || Cell.Y > Dimension.MaxY() || !World.HasChunk(FIntPoint(Cell.X >> 4, Cell.Z >> 4)))
					{
						break;
					}
					const FMcStateId State = World.GetBlock(Cell);
					if (!Registry.IsAir(State))
					{
						Strength -= (GetBlastResistance(State) + 0.3f) * 0.3f;
						if (Strength > 0.0f)
						{
							Hit.Add(Cell);
						}
					}
					At += Direction * 0.3;
				}
			}
		}
	}
	Out.Append(Hit.Array());
}

float McExplosion::GetExposure(const FMcBlockWorld& World, const FVector& Center, const FBox& Box)
{
	const FMcBlockShapes& Shapes = FMcBlockShapes::Get();
	const FVector Size = Box.GetSize();
	const double StepX = 1.0 / (Size.X * 2.0 + 1.0);
	const double StepY = 1.0 / (Size.Y * 2.0 + 1.0);
	const double StepZ = 1.0 / (Size.Z * 2.0 + 1.0);
	// Centres the grid in the box.
	const double OffsetX = (1.0 - FMath::FloorToDouble(1.0 / StepX) * StepX) * 0.5;
	const double OffsetZ = (1.0 - FMath::FloorToDouble(1.0 / StepZ) * StepZ) * 0.5;
	const FIntVector CentreCell(FMath::FloorToInt32(Center.X), FMath::FloorToInt32(Center.Y), FMath::FloorToInt32(Center.Z));
	int32 Seen = 0;
	int32 Total = 0;
	for (double FX = 0.0; FX <= 1.0; FX += StepX)
	{
		for (double FY = 0.0; FY <= 1.0; FY += StepY)
		{
			for (double FZ = 0.0; FZ <= 1.0; FZ += StepZ)
			{
				const FVector Point(FMath::Lerp(Box.Min.X, Box.Max.X, FX) + OffsetX, FMath::Lerp(Box.Min.Y, Box.Max.Y, FY),
					FMath::Lerp(Box.Min.Z, Box.Max.Z, FZ) + OffsetZ);
				const FIntVector PointCell(FMath::FloorToInt32(Point.X), FMath::FloorToInt32(Point.Y), FMath::FloorToInt32(Point.Z));
				// March from the point to the centre; a block with collision (other than the ones either end is in) hides it.
				const FVector Way = Center - Point;
				const int32 Steps = FMath::Max(1, FMath::CeilToInt32(Way.Size() / 0.25));
				bool bBlocked = false;
				for (int32 Step = 1; Step < Steps && !bBlocked; ++Step)
				{
					const FVector At = Point + Way * (static_cast<double>(Step) / Steps);
					const FIntVector Cell(FMath::FloorToInt32(At.X), FMath::FloorToInt32(At.Y), FMath::FloorToInt32(At.Z));
					bBlocked = Cell != PointCell && Cell != CentreCell && World.HasChunk(FIntPoint(Cell.X >> 4, Cell.Z >> 4))
						&& Shapes.HasCollision(World.GetBlock(Cell));
				}
				Seen += bBlocked ? 0 : 1;
				++Total;
			}
		}
	}
	return Total > 0 ? static_cast<float>(Seen) / Total : 0.0f;
}

float McExplosion::GetDamage(float Power, float Distance, float Exposure)
{
	const float Radius = Power * 2.0f;
	if (Radius <= 0.0f || Distance >= Radius || Exposure <= 0.0f)
	{
		return 0.0f;
	}
	const float Impact = (1.0f - Distance / Radius) * Exposure;
	return (Impact * Impact + Impact) * 0.5f * 7.0f * Radius + 1.0f;
}

float McExplosion::GetBlastPitch(float Roll)
{
	return 0.56f + 0.24f * Roll;
}

FVector McExplosion::GetKnockback(float Power, const FVector& Center, const FVector& Position, const FVector& Eye, float Exposure)
{
	const float Radius = Power * 2.0f;
	const float Distance = static_cast<float>(FVector::Dist(Position, Center));
	if (Radius <= 0.0f || Distance >= Radius || Exposure <= 0.0f)
	{
		return FVector::ZeroVector;
	}
	return (Eye - Center).GetSafeNormal() * ((1.0f - Distance / Radius) * Exposure);
}
