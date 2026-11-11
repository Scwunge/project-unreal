#include "McFluidMesher.h"
#include "McBiomeColors.h"
#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McBlockTextures.h"
#include "McChunk.h"
#include "McRenderMode.h"

namespace McFluidMesher
{
namespace
{
	constexpr int32 NumDirections = 6;
	constexpr int32 DownIndex = static_cast<int32>(EMcDirection::Down);
	constexpr int32 UpIndex = static_cast<int32>(EMcDirection::Up);

	// Minecraft (x, y, z) offsets, in EMcDirection order: down, up, north, south, west, east.
	const FIntVector Offsets[NumDirections] = {
		FIntVector(0, -1, 0), FIntVector(0, 1, 0), FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0),
	};
	const float Shades[NumDirections] = { 0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f };

	// The two top corners of each side face as (x, z) corner coordinates, left then right as seen from outside.
	// Index 0 and 1 (down and up) are unused.
	const FIntPoint SideCorners[NumDirections][2] = {
		{ FIntPoint(0, 0), FIntPoint(0, 0) }, { FIntPoint(0, 0), FIntPoint(0, 0) },
		{ FIntPoint(1, 0), FIntPoint(0, 0) },  // north
		{ FIntPoint(0, 1), FIntPoint(1, 1) },  // south
		{ FIntPoint(0, 0), FIntPoint(0, 1) },  // west
		{ FIntPoint(1, 1), FIntPoint(1, 0) },  // east
	};

	const FFluidInfo NoFluid;

	// Blocks that always hold water, whatever their properties say.
	bool IsAlwaysWater(const FString& Name)
	{
		return Name == TEXT("minecraft:bubble_column") || Name == TEXT("minecraft:kelp") || Name == TEXT("minecraft:kelp_plant")
			|| Name == TEXT("minecraft:seagrass") || Name == TEXT("minecraft:tall_seagrass");
	}

	FColor Scale(FColor Color, float Scale)
	{
		return FColor(static_cast<uint8>(Color.R * Scale + 0.5f), static_cast<uint8>(Color.G * Scale + 0.5f), static_cast<uint8>(Color.B * Scale + 0.5f), 255);
	}

	class FBuilder
	{
	public:
		FBuilder(const FMcSectionSnapshot& InSnapshot, const FFluidPalette& InFluids, const FMcRenderPalette& InBlocks, FMcSectionMesh& InMesh)
			: Snapshot(InSnapshot), Fluids(InFluids), Blocks(InBlocks), Mesh(InMesh)
		{
		}

		void BuildCell(int32 X, int32 Y, int32 Z);

	private:
		const FMcSectionSnapshot& Snapshot;
		const FFluidPalette& Fluids;
		const FMcRenderPalette& Blocks;
		FMcSectionMesh& Mesh;

		const FFluidInfo& InfoAt(int32 X, int32 Y, int32 Z) const
		{
			const FMcStateId State = Snapshot.Get(X, Y, Z);
			return Fluids.States.IsValidIndex(State) ? Fluids.States[State] : NoFluid;
		}

		uint8 OpaqueFacesAt(int32 X, int32 Y, int32 Z) const
		{
			const FMcStateId State = Snapshot.Get(X, Y, Z);
			return Blocks.States.IsValidIndex(State) ? Blocks.States[State].OpaqueFaces : 0;
		}

		bool IsSolidAt(int32 X, int32 Y, int32 Z) const
		{
			const FMcStateId State = Snapshot.Get(X, Y, Z);
			return Blocks.States.IsValidIndex(State) && Blocks.States[State].bOccludes;
		}

		FVector2D LightAt(int32 X, int32 Y, int32 Z) const
		{
			const uint8 Packed = Snapshot.Light[FMcSectionSnapshot::Index(X, Y, Z)];
			return FVector2D(Packed >> 4, Packed & 15) / 15.0;
		}

		float CornerHeight(EFluid Kind, int32 X, int32 Y, int32 Z, int32 CornerX, int32 CornerZ) const;
		FVector2D FlowDirection(EFluid Kind, int32 X, int32 Y, int32 Z) const;

		// Corners are in Minecraft axes relative to the block's own corner. Anticlockwise seen from outside in
		// those axes, which is clockwise once swapped to Unreal's, as the block mesher does it.
		void Emit(EMcRenderPass Pass, int32 BlockX, int32 BlockY, int32 BlockZ, const FVector (&Corners)[4], const FVector2D (&UVs)[4],
			int32 Direction, bool bBack, FVector2D Light, uint16 Layer, FColor Tint);

		// Unreal Mode only: how close each corner of the next top face is to the water's edge (0 inside, 1 at a shore side),
		// carried as a fraction of the layer number in UV1.x (at most 0.4, so the layer still rounds to itself). Left null,
		// every vertex is exactly what it always was.
		const float* EdgeWeights = nullptr;
	};

	float FBuilder::CornerHeight(EFluid Kind, int32 X, int32 Y, int32 Z, int32 CornerX, int32 CornerZ) const
	{
		// The average of the up to four blocks that meet at the corner. Sources weigh ten times as much as
		// flowing fluid so a surface doesn't sag at its edge; open air pulls the average down, solid blocks don't count.
		float Sum = 0.0f;
		float Weight = 0.0f;
		for (int32 Z2 = CornerZ - 1; Z2 <= CornerZ; ++Z2)
		{
			for (int32 X2 = CornerX - 1; X2 <= CornerX; ++X2)
			{
				const FFluidInfo& Info = InfoAt(X + X2, Y, Z + Z2);
				if (Info.Kind == Kind)
				{
					if (InfoAt(X + X2, Y + 1, Z + Z2).Kind == Kind)
					{
						return 1.0f;
					}
					const float Height = OwnHeight(Info);
					const float Factor = Height >= 0.8f ? 10.0f : 1.0f;
					Sum += Height * Factor;
					Weight += Factor;
				}
				else if (!IsSolidAt(X + X2, Y, Z + Z2))
				{
					Weight += 1.0f;
				}
			}
		}
		return Weight > 0.0f ? Sum / Weight : 0.0f;
	}

	// Points downhill: toward the neighbours with less fluid. Zero on a level surface.
	FVector2D FBuilder::FlowDirection(EFluid Kind, int32 X, int32 Y, int32 Z) const
	{
		const float Own = OwnHeight(InfoAt(X, Y, Z));
		FVector2D Flow = FVector2D::ZeroVector;
		for (int32 Direction = 2; Direction < NumDirections; ++Direction)
		{
			const FIntVector& Offset = Offsets[Direction];
			const FFluidInfo& Info = InfoAt(X + Offset.X, Y, Z + Offset.Z);
			float Other;
			if (Info.Kind == Kind)
			{
				Other = OwnHeight(Info);
			}
			else if (!IsSolidAt(X + Offset.X, Y, Z + Offset.Z))
			{
				Other = 0.0f;
			}
			else
			{
				continue;
			}
			Flow += FVector2D(Offset.X, Offset.Z) * (Own - Other);
		}
		return Flow;
	}

	void FBuilder::Emit(EMcRenderPass Pass, int32 BlockX, int32 BlockY, int32 BlockZ, const FVector (&Corners)[4], const FVector2D (&UVs)[4],
		int32 Direction, bool bBack, FVector2D Light, uint16 Layer, FColor Tint)
	{
		FMcMeshBuffers& Buffers = Mesh.Passes[static_cast<int32>(Pass)];
		const FIntVector& Offset = Offsets[Direction];
		const double Sign = bBack ? -1.0 : 1.0;
		const FVector Normal(Offset.X * Sign, Offset.Z * Sign, Offset.Y * Sign);
		const FColor Color = Scale(Tint, Shades[Direction]);
		const int32 First = Buffers.Positions.Num();
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const FVector Block(BlockX + Corners[Corner].X, Snapshot.MinY + BlockY + Corners[Corner].Y, BlockZ + Corners[Corner].Z);
			Buffers.Positions.Add(FVector(Block.X, Block.Z, Block.Y) * McCoords::UnitsPerBlock);
			Buffers.Normals.Add(Normal);
			Buffers.UV0.Add(UVs[Corner]);
			Buffers.UV1.Add(FVector2D(Layer + (EdgeWeights ? 0.4f * EdgeWeights[Corner] : 0.0f), Pass == EMcRenderPass::Water ? McSurface::Water : McSurface::Lava));
			Buffers.UV2.Add(Light);
			Buffers.Colors.Add(Color);
		}
		if (Blocks.bFlipWinding)
		{
			Buffers.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
		}
		else
		{
			Buffers.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
		}
	}

	void FBuilder::BuildCell(int32 X, int32 Y, int32 Z)
	{
		const FFluidInfo& Info = InfoAt(X, Y, Z);
		const EFluid Kind = Info.Kind;
		if (Kind == EFluid::None)
		{
			return;
		}
		const bool bWater = Kind == EFluid::Water;
		const EMcRenderPass Pass = bWater ? EMcRenderPass::Water : EMcRenderPass::Opaque;
		const FColor Tint = bWater ? Snapshot.GetTint(EMcTintSource::Water, X, Z) : FColor::White;
		const uint16 StillLayer = bWater ? Fluids.WaterStill : Fluids.LavaStill;
		const uint16 FlowLayer = bWater ? Fluids.WaterFlow : Fluids.LavaFlow;
		const uint16 SideLayer = bWater ? Fluids.WaterFlowSide : Fluids.LavaFlowSide;
		const uint8 OwnFaces = OpaqueFacesAt(X, Y, Z);
		const FVector2D OwnLight = LightAt(X, Y, Z);

		// Lava glows, so it takes the brighter of its own light and its surroundings'.
		auto LightFor = [&](int32 Direction)
		{
			const FIntVector& Offset = Offsets[Direction];
			FVector2D Light = LightAt(X + Offset.X, Y + Offset.Y, Z + Offset.Z);
			if (!bWater || Direction == UpIndex)
			{
				Light = FVector2D(FMath::Max(Light.X, OwnLight.X), FMath::Max(Light.Y, OwnLight.Y));
			}
			return Light;
		};

		// Heights at the block's four corners, indexed [z][x].
		float Heights[2][2];
		for (int32 CornerZ = 0; CornerZ < 2; ++CornerZ)
		{
			for (int32 CornerX = 0; CornerX < 2; ++CornerX)
			{
				Heights[CornerZ][CornerX] = CornerHeight(Kind, X, Y, Z, CornerX, CornerZ);
			}
		}

		// Sides. They show the top left quarter of the flowing picture, which is a layer of its own, the fluid's height
		// cut from the bottom of it.
		for (int32 Direction = 2; Direction < NumDirections; ++Direction)
		{
			const FIntVector& Offset = Offsets[Direction];
			if (InfoAt(X + Offset.X, Y, Z + Offset.Z).Kind == Kind
				|| ((OwnFaces >> Direction) & 1)
				|| ((OpaqueFacesAt(X + Offset.X, Y, Z + Offset.Z) >> (Direction ^ 1)) & 1))
			{
				continue;
			}
			const FIntPoint Left = SideCorners[Direction][0];
			const FIntPoint Right = SideCorners[Direction][1];
			const float LeftHeight = Heights[Left.Y][Left.X];
			const float RightHeight = Heights[Right.Y][Right.X];
			const FVector Corners[4] = {
				FVector(Left.X, LeftHeight, Left.Y), FVector(Left.X, 0.0, Left.Y), FVector(Right.X, 0.0, Right.Y), FVector(Right.X, RightHeight, Right.Y),
			};
			const FVector2D UVs[4] = {
				FVector2D(0.0, 1.0 - LeftHeight), FVector2D(0.0, 1.0), FVector2D(1.0, 1.0), FVector2D(1.0, 1.0 - RightHeight),
			};
			Emit(Pass, X, Y, Z, Corners, UVs, Direction, false, LightFor(Direction), SideLayer, Tint);
		}

		// Bottom.
		if (InfoAt(X, Y - 1, Z).Kind != Kind && !((OwnFaces >> DownIndex) & 1) && !((OpaqueFacesAt(X, Y - 1, Z) >> UpIndex) & 1))
		{
			const FVector Corners[4] = { FVector(0, 0, 1), FVector(0, 0, 0), FVector(1, 0, 0), FVector(1, 0, 1) };
			const FVector2D UVs[4] = { FVector2D(0, 0), FVector2D(0, 1), FVector2D(1, 1), FVector2D(1, 0) };
			Emit(Pass, X, Y, Z, Corners, UVs, DownIndex, false, LightFor(DownIndex), StillLayer, Tint);
		}

		// Top, and for water the same surface again facing down so it shows from underneath.
		const bool bFullTop = Heights[0][0] > 0.999f && Heights[0][1] > 0.999f && Heights[1][0] > 0.999f && Heights[1][1] > 0.999f;
		const bool bCovered = (bFullTop && ((OpaqueFacesAt(X, Y + 1, Z) >> DownIndex) & 1)) || ((OwnFaces >> UpIndex) & 1);
		if (InfoAt(X, Y + 1, Z).Kind != Kind && !bCovered)
		{
			// Corners north-west, south-west, south-east, north-east.
			const FVector Corners[4] = {
				FVector(0, Heights[0][0], 0), FVector(0, Heights[1][0], 1), FVector(1, Heights[1][1], 1), FVector(1, Heights[0][1], 0),
			};
			const FVector2D Flow = FlowDirection(Kind, X, Y, Z);
			FVector2D UVs[4];
			uint16 Layer = StillLayer;
			if (Flow.SizeSquared() > 1e-6)
			{
				// Turn the half-size piece of the flowing sprite so its v axis, the way the animation runs, follows the flow.
				const FVector2D Unit = Flow.GetSafeNormal();
				static const FVector2D Around[4] = { FVector2D(-0.5, -0.5), FVector2D(-0.5, 0.5), FVector2D(0.5, 0.5), FVector2D(0.5, -0.5) };
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const FVector2D& P = Around[Corner];
					UVs[Corner] = FVector2D(0.5, 0.5) + 0.5 * FVector2D(Unit.Y * P.X - Unit.X * P.Y, Unit.X * P.X + Unit.Y * P.Y);
				}
				Layer = FlowLayer;
			}
			else
			{
				UVs[0] = FVector2D(0, 0);
				UVs[1] = FVector2D(0, 1);
				UVs[2] = FVector2D(1, 1);
				UVs[3] = FVector2D(1, 0);
			}
			const FVector2D Light = LightFor(UpIndex);
			// A water corner touching a side with no water beside it is on the shore (north-west, south-west, south-east, north-east).
			float Edge[4] = { 0, 0, 0, 0 };
			if (bShoreFoam && bWater && McRenderMode::IsEnhanced())
			{
				const bool West = InfoAt(X - 1, Y, Z).Kind != Kind, East = InfoAt(X + 1, Y, Z).Kind != Kind;
				const bool North = InfoAt(X, Y, Z - 1).Kind != Kind, South = InfoAt(X, Y, Z + 1).Kind != Kind;
				Edge[0] = (West || North) ? 1.0f : 0.0f;
				Edge[1] = (West || South) ? 1.0f : 0.0f;
				Edge[2] = (East || South) ? 1.0f : 0.0f;
				Edge[3] = (East || North) ? 1.0f : 0.0f;
				EdgeWeights = Edge;
			}
			Emit(Pass, X, Y, Z, Corners, UVs, UpIndex, false, Light, Layer, Tint);
			EdgeWeights = nullptr;
			if (bWater)
			{
				const FVector BackCorners[4] = { Corners[0], Corners[3], Corners[2], Corners[1] };
				const FVector2D BackUVs[4] = { UVs[0], UVs[3], UVs[2], UVs[1] };
				Emit(Pass, X, Y, Z, BackCorners, BackUVs, UpIndex, true, Light, Layer, Tint);
			}
		}
	}
}

float OwnHeight(const FFluidInfo& Info)
{
	// Falling fluid still has the height of a source; only its neighbours see it as full.
	return Info.bFalling ? 8.0f / 9.0f : (8 - Info.Level) / 9.0f;
}

TSharedRef<const FFluidPalette> BuildPalette(const FMcBlockRegistry& Registry, const FMcBlockTextures& Textures)
{
	TSharedRef<FFluidPalette> Palette = MakeShared<FFluidPalette>();
	Palette->States.SetNum(Registry.NumStates());
	const FName LevelProperty(TEXT("level"));
	const FName WaterloggedProperty(TEXT("waterlogged"));
	const FName True(TEXT("true"));

	for (int32 Id = 0; Id < Registry.NumStates(); ++Id)
	{
		const FMcStateId State = static_cast<FMcStateId>(Id);
		if (!Registry.IsValidState(State))
		{
			continue;
		}
		FFluidInfo& Info = Palette->States[Id];
		const FString Name = Registry.GetBlockOf(State).Name.ToString();
		if (Name == TEXT("minecraft:water") || Name == TEXT("minecraft:lava"))
		{
			Info.Kind = Name == TEXT("minecraft:water") ? EFluid::Water : EFluid::Lava;
			const int32 Level = FCString::Atoi(*Registry.GetPropertyValue(State, LevelProperty).ToString());
			Info.bFalling = Level >= 8;
			Info.Level = static_cast<uint8>(Level & 7);
		}
		else if (IsAlwaysWater(Name) || Registry.GetPropertyValue(State, WaterloggedProperty) == True)
		{
			Info.Kind = EFluid::Water;
		}
	}

	Palette->WaterStill = static_cast<uint16>(Textures.GetLayer(TEXT("minecraft:block/water_still")));
	Palette->WaterFlow = static_cast<uint16>(Textures.GetLayer(TEXT("minecraft:block/water_flow")));
	Palette->LavaStill = static_cast<uint16>(Textures.GetLayer(TEXT("minecraft:block/lava_still")));
	Palette->LavaFlow = static_cast<uint16>(Textures.GetLayer(TEXT("minecraft:block/lava_flow")));
	Palette->WaterFlowSide = static_cast<uint16>(Textures.GetLayer(TEXT("minecraft:block/water_flow#side")));
	Palette->LavaFlowSide = static_cast<uint16>(Textures.GetLayer(TEXT("minecraft:block/lava_flow#side")));

	FMcBiomeColorTable& Biomes = FMcBiomeColorTable::Get();
	Biomes.EnsureLoaded();
	Palette->WaterTint = Biomes.Get(TEXT("minecraft:plains")).Water;
	return Palette;
}

void Build(const FMcSectionSnapshot& Snapshot, const FFluidPalette& Fluids, const FMcRenderPalette& Blocks, FMcSectionMesh& InOut)
{
	FBuilder Builder(Snapshot, Fluids, Blocks, InOut);
	for (int32 Y = 0; Y < McCoords::SectionSize; ++Y)
	{
		for (int32 Z = 0; Z < McCoords::SectionSize; ++Z)
		{
			for (int32 X = 0; X < McCoords::SectionSize; ++X)
			{
				Builder.BuildCell(X, Y, Z);
			}
		}
	}
}
}
