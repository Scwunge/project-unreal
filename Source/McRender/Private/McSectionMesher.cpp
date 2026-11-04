#include "McSectionMesher.h"
#include "McBiomeColors.h"
#include "McBiomeRegistry.h"
#include "McBlockLight.h"
#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McBlockTextures.h"
#include "McBlockWorld.h"
#include "McLightEngine.h"

FMcSectionSnapshot FMcSectionSnapshot::Capture(const FMcBlockWorld& World, FIntPoint ChunkPos, int32 SectionIndex)
{
	FMcSectionSnapshot Snapshot;
	Snapshot.ChunkPos = ChunkPos;
	Snapshot.SectionIndex = SectionIndex;
	Snapshot.MinY = World.GetDimension().MinY + SectionIndex * McCoords::SectionSize;
	Snapshot.Blocks.SetNumZeroed(Padded * Padded * Padded);
	Snapshot.Light.Init(0xF0, Padded * Padded * Padded);

	// The 3x3 chunk columns the border can reach into.
	const FMcChunk* Columns[3][3];
	for (int32 DZ = -1; DZ <= 1; ++DZ)
	{
		for (int32 DX = -1; DX <= 1; ++DX)
		{
			Columns[DZ + 1][DX + 1] = World.FindChunk(ChunkPos + FIntPoint(DX, DZ));
		}
	}

	for (int32 Y = -1; Y <= McCoords::SectionSize; ++Y)
	{
		const int32 WorldY = Snapshot.MinY + Y;
		for (int32 Z = -1; Z <= McCoords::SectionSize; ++Z)
		{
			const int32 ColumnZ = Z < 0 ? 0 : (Z >= McCoords::SectionSize ? 2 : 1);
			for (int32 X = -1; X <= McCoords::SectionSize; ++X)
			{
				const int32 ColumnX = X < 0 ? 0 : (X >= McCoords::SectionSize ? 2 : 1);
				if (const FMcChunk* Chunk = Columns[ColumnZ][ColumnX])
				{
					Snapshot.Blocks[Index(X, Y, Z)] = Chunk->GetBlock(McCoords::BlockToLocal(X), WorldY, McCoords::BlockToLocal(Z));
				}
			}
		}
	}
	return Snapshot;
}

namespace
{
	// Plains (minecraft.wiki, "Color"): grass #91BD59, foliage #77AB2F, water #3F76E4.
	const FColor PlainsGrass(0x91, 0xBD, 0x59);
	const FColor PlainsFoliage(0x77, 0xAB, 0x2F);
	const FColor PlainsWater(0x3F, 0x76, 0xE4);

	// The game's default Biome Blend setting, 5x5: two columns each way.
	constexpr int32 BlendRadius = 2;
}

FColor FMcSectionSnapshot::GetTint(EMcTintSource Source, int32 X, int32 Z) const
{
	if (Source == EMcTintSource::Fixed)
	{
		return FColor::White;
	}
	const int32 Kind = static_cast<int32>(Source) - static_cast<int32>(EMcTintSource::Grass);
	if (Tints.Num() != 3 * Padded * Padded)
	{
		return Kind == 0 ? PlainsGrass : Kind == 1 ? PlainsFoliage : PlainsWater;
	}
	return Tints[Kind * Padded * Padded + ColumnIndex(X, Z)];
}

FMcSectionSnapshot FMcSectionSnapshot::FromRegion(const FMcLightRegion& Region, int32 SectionIndex, const FMcRenderPalette& Palette)
{
	FMcSectionSnapshot Snapshot;
	Snapshot.ChunkPos = Region.CenterChunk;
	Snapshot.SectionIndex = SectionIndex;
	Snapshot.MinY = Region.MinY + SectionIndex * McCoords::SectionSize;
	Snapshot.Blocks.SetNumUninitialized(Padded * Padded * Padded);
	Snapshot.Light.SetNumUninitialized(Padded * Padded * Padded);

	for (int32 Y = -1; Y <= McCoords::SectionSize; ++Y)
	{
		const int32 WorldY = Snapshot.MinY + Y;
		for (int32 Z = -1; Z <= McCoords::SectionSize; ++Z)
		{
			for (int32 X = -1; X <= McCoords::SectionSize; ++X)
			{
				const int32 RegionX = FMcLightRegion::Border + X;
				const int32 RegionZ = FMcLightRegion::Border + Z;
				const int32 Cell = Index(X, Y, Z);
				Snapshot.Blocks[Cell] = Region.GetBlock(RegionX, WorldY, RegionZ);
				Snapshot.Light[Cell] = static_cast<uint8>((Region.GetSkyLight(RegionX, WorldY, RegionZ) << 4) | Region.GetBlockLight(RegionX, WorldY, RegionZ));
			}
		}
	}

	// Biome colours, averaged over the columns around each one, at the middle of the section.
	const int32 SampleY = Snapshot.MinY + McCoords::SectionSize / 2;
	constexpr int32 Columns = Padded * Padded;
	Snapshot.Tints.SetNumUninitialized(3 * Columns);
	for (int32 Z = -1; Z <= McCoords::SectionSize; ++Z)
	{
		for (int32 X = -1; X <= McCoords::SectionSize; ++X)
		{
			int32 Sum[3][3] = {};
			int32 Count = 0;
			for (int32 DZ = -BlendRadius; DZ <= BlendRadius; ++DZ)
			{
				for (int32 DX = -BlendRadius; DX <= BlendRadius; ++DX)
				{
					const FMcBiomeId Biome = Region.GetBiome(FMcLightRegion::Border + X + DX, SampleY, FMcLightRegion::Border + Z + DZ);
					if (!Palette.BiomeTints.IsValidIndex(Biome))
					{
						continue;
					}
					const FMcRenderPalette::FBiomeTint& Tint = Palette.BiomeTints[Biome];
					const FColor Colors[3] = { Tint.Grass, Tint.Foliage, Tint.Water };
					for (int32 Kind = 0; Kind < 3; ++Kind)
					{
						Sum[Kind][0] += Colors[Kind].R;
						Sum[Kind][1] += Colors[Kind].G;
						Sum[Kind][2] += Colors[Kind].B;
					}
					++Count;
				}
			}
			const FColor Plains[3] = { PlainsGrass, PlainsFoliage, PlainsWater };
			for (int32 Kind = 0; Kind < 3; ++Kind)
			{
				Snapshot.Tints[Kind * Columns + ColumnIndex(X, Z)] = Count == 0 ? Plains[Kind]
					: FColor(static_cast<uint8>(Sum[Kind][0] / Count), static_cast<uint8>(Sum[Kind][1] / Count), static_cast<uint8>(Sum[Kind][2] / Count), 255);
			}
		}
	}
	return Snapshot;
}

bool FMcSectionMesh::IsEmpty() const
{
	for (const FMcMeshBuffers& Pass : Passes)
	{
		if (!Pass.IsEmpty())
		{
			return false;
		}
	}
	return true;
}

namespace
{
	constexpr int32 NumDirections = 6;

	// Minecraft (x, y, z) offsets, in EMcDirection order: down, up, north, south, west, east.
	const FIntVector DirectionOffsets[NumDirections] = {
		FIntVector(0, -1, 0), FIntVector(0, 1, 0), FIntVector(0, 0, -1), FIntVector(0, 0, 1), FIntVector(-1, 0, 0), FIntVector(1, 0, 0),
	};

	// The flat shading Minecraft draws each face direction with.
	const float DirectionShades[NumDirections] = { 0.5f, 1.0f, 0.8f, 0.8f, 0.6f, 0.6f };

	FORCEINLINE int32 DirectionIndex(EMcDirection Direction) { return static_cast<int32>(Direction); }

	FORCEINLINE EMcDirection Opposite(EMcDirection Direction)
	{
		// Pairs sit next to each other in the enum: down/up, north/south, west/east.
		return static_cast<EMcDirection>(DirectionIndex(Direction) ^ 1);
	}

	// The direction a normal (model axes) points most along, for faces whose shading direction isn't given.
	EMcDirection NearestDirection(const FVector3f& Normal)
	{
		const FVector3f Abs = Normal.GetAbs();
		if (Abs.Y >= Abs.X && Abs.Y >= Abs.Z)
		{
			return Normal.Y < 0.0f ? EMcDirection::Down : EMcDirection::Up;
		}
		if (Abs.Z >= Abs.X)
		{
			return Normal.Z < 0.0f ? EMcDirection::North : EMcDirection::South;
		}
		return Normal.X < 0.0f ? EMcDirection::West : EMcDirection::East;
	}

	FORCEINLINE int32 DirectionAxis(EMcDirection Direction)
	{
		// Model axes: x is 0, y is 1, z is 2.
		return (Direction == EMcDirection::Down || Direction == EMcDirection::Up) ? 1 : (Direction == EMcDirection::North || Direction == EMcDirection::South) ? 2 : 0;
	}

	// Whether every corner of the quad lies on that side of the block.
	bool IsOnSide(const FMcBakedQuad& Quad, EMcDirection Side)
	{
		const int32 Axis = DirectionAxis(Side);
		const float Plane = (DirectionIndex(Side) & 1) ? 1.0f : 0.0f;
		for (const FVector3f& Position : Quad.Positions)
		{
			if (!FMath::IsNearlyEqual(Position[Axis], Plane, 1e-4f))
			{
				return false;
			}
		}
		return true;
	}

	// How much of a block side (0..1) a quad lying flat on it covers; 0 if it isn't on that side.
	float AreaOnSide(const FMcBakedQuad& Quad, EMcDirection Side)
	{
		const int32 Axis = (Side == EMcDirection::Down || Side == EMcDirection::Up) ? 1 : (Side == EMcDirection::North || Side == EMcDirection::South) ? 2 : 0;
		const float Plane = (DirectionIndex(Side) & 1) ? 1.0f : 0.0f;
		const int32 AxisU = Axis == 0 ? 1 : 0;
		const int32 AxisV = Axis == 2 ? 1 : 2;

		float Twice = 0.0f;
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const FVector3f& A = Quad.Positions[Corner];
			const FVector3f& B = Quad.Positions[(Corner + 1) % 4];
			if (!FMath::IsNearlyEqual(A[Axis], Plane, 1e-4f))
			{
				return 0.0f;
			}
			Twice += A[AxisU] * B[AxisV] - B[AxisU] * A[AxisV];
		}
		return FMath::Abs(Twice) * 0.5f;
	}

	FColor ScaleColor(FColor Color, float Scale)
	{
		return FColor(static_cast<uint8>(Color.R * Scale + 0.5f), static_cast<uint8>(Color.G * Scale + 0.5f), static_cast<uint8>(Color.B * Scale + 0.5f), 255);
	}

	FColor ColorFromUnit(float R, float G, float B)
	{
		auto Byte = [](float Value) { return static_cast<uint8>(FMath::Clamp(Value, 0.0f, 1.0f) * 255.0f + 0.5f); };
		return FColor(Byte(R), Byte(G), Byte(B), 255);
	}

	int32 IntProperty(const FMcBlockRegistry& Registry, FMcStateId State, const TCHAR* Name)
	{
		const FName Value = Registry.GetPropertyValue(State, Name);
		return Value.IsNone() ? 0 : FCString::Atoi(*Value.ToString());
	}
}

FColor McBlockMesher::GetBlockTint(const FMcBlockRegistry& Registry, FMcStateId State)
{
	// Some blocks ignore the biome and always use their own colour.
	const FColor& Grass = PlainsGrass;
	const FColor& Foliage = PlainsFoliage;
	const FColor& Water = PlainsWater;

	const FString Name = Registry.GetBlockOf(State).Name.ToString();
	if (Name == TEXT("minecraft:birch_leaves"))
	{
		return FColor(0x80, 0xA7, 0x55);
	}
	if (Name == TEXT("minecraft:spruce_leaves"))
	{
		return FColor(0x61, 0x99, 0x61);
	}
	if (Name == TEXT("minecraft:lily_pad"))
	{
		return FColor(0x20, 0x80, 0x30);
	}
	if (Name == TEXT("minecraft:redstone_wire"))
	{
		// Brighter and yellower with power; dark red when unpowered.
		const float Power = IntProperty(Registry, State, TEXT("power")) / 15.0f;
		const float R = Power * 0.6f + (Power > 0.0f ? 0.4f : 0.3f);
		return ColorFromUnit(R, Power * Power * 0.7f - 0.5f, Power * Power * 0.6f - 0.7f);
	}
	if (Name == TEXT("minecraft:pumpkin_stem") || Name == TEXT("minecraft:melon_stem"))
	{
		// Green when young, ripening towards yellow.
		const int32 Age = IntProperty(Registry, State, TEXT("age"));
		return FColor(static_cast<uint8>(Age * 32), static_cast<uint8>(255 - Age * 8), static_cast<uint8>(Age * 4), 255);
	}
	if (Name == TEXT("minecraft:attached_pumpkin_stem") || Name == TEXT("minecraft:attached_melon_stem"))
	{
		return FColor(0xE0, 0xC7, 0x1C);
	}
	if (Name.Contains(TEXT("water")) || Name == TEXT("minecraft:bubble_column"))
	{
		return Water;
	}
	if (Name.EndsWith(TEXT("leaves")) || Name == TEXT("minecraft:vine"))
	{
		return Foliage;
	}
	return Grass;
}

EMcTintSource McBlockMesher::GetTintSource(const FMcBlockRegistry& Registry, FMcStateId State)
{
	const FString Name = Registry.GetBlockOf(State).Name.ToString();
	if (Name == TEXT("minecraft:birch_leaves") || Name == TEXT("minecraft:spruce_leaves") || Name == TEXT("minecraft:lily_pad")
		|| Name == TEXT("minecraft:redstone_wire") || Name.EndsWith(TEXT("_stem")))
	{
		return EMcTintSource::Fixed;
	}
	if (Name.Contains(TEXT("water")) || Name == TEXT("minecraft:bubble_column"))
	{
		return EMcTintSource::Water;
	}
	if (Name.EndsWith(TEXT("leaves")) || Name == TEXT("minecraft:vine"))
	{
		return EMcTintSource::Foliage;
	}
	return EMcTintSource::Grass;
}

uint8 McSurface::ForBlock(FName Block, uint8 Emission)
{
	FMcBlockTags& Tags = FMcBlockTags::Get();
	Tags.EnsureLoaded();
	static const int32 Leaves = Tags.Find(TEXT("leaves"));
	static const int32 Plants = Tags.Find(TEXT("replaceable_by_trees"));
	static const int32 Flowers = Tags.Find(TEXT("flowers"));
	static const int32 Saplings = Tags.Find(TEXT("saplings"));
	static const int32 Crops = Tags.Find(TEXT("crops"));
	// Lying flat or hanging, these would ripple rather than sway.
	static const TSet<FName> Flat = { TEXT("minecraft:pale_moss_carpet"), TEXT("minecraft:leaf_litter"), TEXT("minecraft:glow_lichen"),
		TEXT("minecraft:pink_petals"), TEXT("minecraft:wildflowers"), TEXT("minecraft:shelf_mushroom"), TEXT("minecraft:chorus_flower"),
		TEXT("minecraft:cactus_flower"), TEXT("minecraft:spore_blossom"), TEXT("minecraft:hanging_roots"), TEXT("minecraft:water") };
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	const FMcBlock* Found = Registry.FindBlock(Block);
	if (!Found)
	{
		return Plain;
	}
	const int32 Index = Found->Index;
	if (Tags.HasBlock(Leaves, Index) || Block == TEXT("minecraft:vine"))
	{
		return McSurface::Leaves;
	}
	if (!Flat.Contains(Block) && (Tags.HasBlock(Plants, Index) || Tags.HasBlock(Flowers, Index) || Tags.HasBlock(Saplings, Index)
		|| Tags.HasBlock(Crops, Index) || Block == TEXT("minecraft:sugar_cane") || Block == TEXT("minecraft:sweet_berry_bush")
		|| Block == TEXT("minecraft:kelp_plant") || Block == TEXT("minecraft:kelp")))
	{
		return Plant;
	}
	// A lit furnace gives light from its front only; glowing all over would look wrong.
	const FString Name = Block.ToString();
	if (Emission >= 3 && !Name.Contains(TEXT("furnace")) && !Name.Contains(TEXT("smoker")))
	{
		return Glow;
	}
	return Plain;
}

TSharedRef<const FMcRenderPalette> FMcRenderPalette::Build(const FMcBlockRegistry& Registry, const FMcBlockModels& Models, const FMcBlockTextures& Textures, bool bFlipWinding)
{
	TSharedRef<FMcRenderPalette> Palette = MakeShared<FMcRenderPalette>();
	Palette->Models = &Models;
	Palette->bFlipWinding = bFlipWinding;
	Palette->States.SetNum(Models.NumStates());

	for (int32 Id = 0; Id < Models.NumStates(); ++Id)
	{
		const FMcBakedModel& Model = Models.GetModel(static_cast<FMcStateId>(Id));
		FStateInfo& Info = Palette->States[Id];
		Info.FirstQuad = Palette->Quads.Num();
		Info.Block = Registry.GetBlockOf(static_cast<FMcStateId>(Id)).Index;

		float OpaqueArea[NumDirections] = {};
		float AnyArea[NumDirections] = {};
		bool bAnyTinted = false;
		for (const FMcBakedQuad& Quad : Model.Quads)
		{
			FQuadInfo& QuadInfo = Palette->Quads.AddDefaulted_GetRef();
			const int32 Layer = Textures.GetLayer(Quad.Texture);
			QuadInfo.Layer = static_cast<uint16>(Layer);
			QuadInfo.Pass = (Quad.bForceTranslucent || Textures.HasTranslucentPixels(Layer)) ? EMcRenderPass::Translucent
				: Textures.HasCutoutPixels(Layer) ? EMcRenderPass::Cutout
				: EMcRenderPass::Opaque;
			bAnyTinted |= Quad.TintIndex >= 0;

			if (Quad.CullFace != EMcDirection::None)
			{
				const float Area = AreaOnSide(Quad, Quad.CullFace);
				AnyArea[DirectionIndex(Quad.CullFace)] += Area;
				if (QuadInfo.Pass == EMcRenderPass::Opaque)
				{
					OpaqueArea[DirectionIndex(Quad.CullFace)] += Area;
				}
			}
		}

		bool bFullCube = Model.Status == EMcModelStatus::Baked;
		for (int32 Direction = 0; Direction < NumDirections; ++Direction)
		{
			// Assumes a side's quads don't overlap, which holds for the vanilla models.
			if (OpaqueArea[Direction] >= 0.999f)
			{
				Info.OpaqueFaces |= 1 << Direction;
			}
			bFullCube &= AnyArea[Direction] >= 0.999f;
		}

		// Glass, stained glass, ice and the like hide the faces between two of the same block. Leaves don't:
		// with fancy graphics you see the leaves behind.
		const FString Name = Registry.GetBlockOf(static_cast<FMcStateId>(Id)).Name.ToString();
		Info.bOccludes = Info.OpaqueFaces == (1 << NumDirections) - 1;
		Info.bHidesOwnBlock = bFullCube && Info.OpaqueFaces != (1 << NumDirections) - 1 && !Name.EndsWith(TEXT("leaves"));

		if (bAnyTinted)
		{
			Info.Tint = McBlockMesher::GetBlockTint(Registry, static_cast<FMcStateId>(Id));
			Info.TintSource = McBlockMesher::GetTintSource(Registry, static_cast<FMcStateId>(Id));
		}
		const FMcBlockLight& BlockLight = FMcBlockLight::Get();
		Info.bOpaqueToLight = BlockLight.IsLoaded() && BlockLight.GetDampening(static_cast<FMcStateId>(Id)) >= 15;
		Info.Surface = McSurface::ForBlock(Registry.GetBlockOf(static_cast<FMcStateId>(Id)).Name,
			BlockLight.IsLoaded() ? BlockLight.GetEmission(static_cast<FMcStateId>(Id)) : 0);
	}

	FMcBiomeRegistry& BiomeIds = FMcBiomeRegistry::Get();
	FMcBiomeColorTable& BiomeColors = FMcBiomeColorTable::Get();
	if (BiomeIds.EnsureLoaded() && BiomeColors.EnsureLoaded())
	{
		for (int32 Biome = 0; Biome < BiomeIds.NumBiomes(); ++Biome)
		{
			const FMcBiomeColors Colors = BiomeColors.Get(BiomeIds.GetName(static_cast<FMcBiomeId>(Biome)));
			Palette->BiomeTints.Add({ Colors.Grass, Colors.Foliage, Colors.Water });
		}
	}
	return Palette;
}

namespace McTranslucent
{
	void SortQuadsBackToFront(FMcMeshBuffers& Buffers, const FVector& View)
	{
		const int32 NumQuads = Buffers.Triangles.Num() / 6;
		if (NumQuads < 2)
		{
			return;
		}
		struct FKey
		{
			double Distance;
			int32 Quad;
		};
		TArray<FKey> Keys;
		Keys.Reserve(NumQuads);
		for (int32 Quad = 0; Quad < NumQuads; ++Quad)
		{
			// The quad's vertices are the four from the lowest index its triangles use.
			int32 First = MAX_int32;
			for (int32 Index = 0; Index < 6; ++Index)
			{
				First = FMath::Min(First, Buffers.Triangles[Quad * 6 + Index]);
			}
			const FVector Middle = (Buffers.Positions[First] + Buffers.Positions[First + 1] + Buffers.Positions[First + 2] + Buffers.Positions[First + 3]) * 0.25;
			Keys.Add({ FVector::DistSquared(Middle, View), Quad });
		}
		Keys.StableSort([](const FKey& A, const FKey& B) { return A.Distance > B.Distance; });
		TArray<int32> Sorted;
		Sorted.SetNumUninitialized(Buffers.Triangles.Num());
		for (int32 Slot = 0; Slot < NumQuads; ++Slot)
		{
			FMemory::Memcpy(&Sorted[Slot * 6], &Buffers.Triangles[Keys[Slot].Quad * 6], sizeof(int32) * 6);
		}
		for (int32 Index = NumQuads * 6; Index < Buffers.Triangles.Num(); ++Index)
		{
			Sorted[Index] = Buffers.Triangles[Index];
		}
		Buffers.Triangles = MoveTemp(Sorted);
	}
}

FMcSectionMesh McBlockMesher::Build(const FMcSectionSnapshot& Snapshot, const FMcRenderPalette& Palette)
{
	FMcSectionMesh Mesh;
	Mesh.SectionIndex = Snapshot.SectionIndex;
	const FMcBlockModels& Models = *Palette.Models;

	for (int32 Y = 0; Y < McCoords::SectionSize; ++Y)
	{
		for (int32 Z = 0; Z < McCoords::SectionSize; ++Z)
		{
			for (int32 X = 0; X < McCoords::SectionSize; ++X)
			{
				const FMcStateId State = Snapshot.Get(X, Y, Z);
				if (!Palette.States.IsValidIndex(State))
				{
					continue;
				}
				const FMcBakedModel& Model = Models.GetModel(State);
				if (Model.Quads.Num() == 0)
				{
					continue;
				}
				const FMcRenderPalette::FStateInfo& Info = Palette.States[State];

				// Which sides are hidden by the neighbour there.
				uint8 HiddenSides = 0;
				for (int32 Direction = 0; Direction < NumDirections; ++Direction)
				{
					const FIntVector& Offset = DirectionOffsets[Direction];
					const FMcStateId Neighbour = Snapshot.Get(X + Offset.X, Y + Offset.Y, Z + Offset.Z);
					if (!Palette.States.IsValidIndex(Neighbour))
					{
						continue;
					}
					const FMcRenderPalette::FStateInfo& NeighbourInfo = Palette.States[Neighbour];
					const bool bOpaqueAgainst = (NeighbourInfo.OpaqueFaces >> DirectionIndex(Opposite(static_cast<EMcDirection>(Direction)))) & 1;
					if (bOpaqueAgainst || (Info.bHidesOwnBlock && NeighbourInfo.Block == Info.Block))
					{
						HiddenSides |= 1 << Direction;
					}
				}

				// The block's minimum corner, Minecraft axes, relative to the chunk column.
				const FVector3f Block(static_cast<float>(X), static_cast<float>(Snapshot.MinY + Y), static_cast<float>(Z));
				for (int32 QuadIndex = 0; QuadIndex < Model.Quads.Num(); ++QuadIndex)
				{
					const FMcBakedQuad& Quad = Model.Quads[QuadIndex];
					if (Quad.CullFace != EMcDirection::None && ((HiddenSides >> DirectionIndex(Quad.CullFace)) & 1))
					{
						continue;
					}
					const FMcRenderPalette::FQuadInfo& QuadInfo = Palette.Quads[Info.FirstQuad + QuadIndex];
					FMcMeshBuffers& Buffers = Mesh.Passes[static_cast<int32>(QuadInfo.Pass)];

					FVector3f ModelNormal = ((Quad.Positions[1] - Quad.Positions[0]) ^ (Quad.Positions[2] - Quad.Positions[0])).GetSafeNormal();
					const EMcDirection ShadeFrom = Quad.ShadeFace != EMcDirection::None ? Quad.ShadeFace
						: Quad.Face != EMcDirection::None ? Quad.Face
						: NearestDirection(ModelNormal);
					const float Shade = Quad.bShade ? DirectionShades[DirectionIndex(ShadeFrom)] : 1.0f;
					const FColor Tint = Quad.TintIndex < 0 ? FColor::White
						: Info.TintSource == EMcTintSource::Fixed ? Info.Tint
						: Snapshot.GetTint(Info.TintSource, X, Z);

					// Light comes from the block the face looks into: the neighbour for faces on the block's
					// side, the block's own cell for faces inside it (a slab's top, a torch).
					const EMcDirection LightFrom = Quad.Face != EMcDirection::None ? Quad.Face : NearestDirection(ModelNormal);
					FIntVector Front(X, Y, Z);
					// An opaque block holds no light in its own cell, so a pack model's quad inside one takes the light of the
					// cell it faces (the vanilla client shows such a quad lit; the shot zz_test_model).
					if (IsOnSide(Quad, LightFrom) || Info.bOpaqueToLight)
					{
						Front += DirectionOffsets[DirectionIndex(LightFrom)];
					}
					const uint8 FrontLight = Snapshot.Light[FMcSectionSnapshot::Index(Front.X, Front.Y, Front.Z)];

					FVector2D CornerLight[4];
					float CornerOcclusion[4];
					const bool bSmooth = Palette.bSmoothLighting && Quad.bAmbientOcclusion;
					const int32 Axis = DirectionAxis(LightFrom);
					const int32 AxisU = Axis == 0 ? 1 : 0;
					const int32 AxisV = Axis == 2 ? 1 : 2;
					for (int32 Corner = 0; Corner < 4; ++Corner)
					{
						if (!bSmooth)
						{
							CornerLight[Corner] = FVector2D(FrontLight >> 4, FrontLight & 15) / 15.0;
							CornerOcclusion[Corner] = 1.0f;
							continue;
						}

						// The front block and the three beside it that touch this corner. A block that occludes
						// darkens the corner and lends it the front's light in place of its own (none).
						FIntVector StepU(0, 0, 0);
						FIntVector StepV(0, 0, 0);
						StepU[AxisU] = Quad.Positions[Corner][AxisU] < 0.5f ? -1 : 1;
						StepV[AxisV] = Quad.Positions[Corner][AxisV] < 0.5f ? -1 : 1;
						const FIntVector Cells[4] = { Front, Front + StepU, Front + StepV, Front + StepU + StepV };

						bool bOccluded[4];
						for (int32 Index = 0; Index < 4; ++Index)
						{
							const FMcStateId Beside = Snapshot.Get(Cells[Index].X, Cells[Index].Y, Cells[Index].Z);
							bOccluded[Index] = Palette.States.IsValidIndex(Beside) && Palette.States[Beside].bOccludes;
						}
						// With both sides blocked, the diagonal block can't be seen from the corner.
						bOccluded[3] |= bOccluded[1] && bOccluded[2];

						FVector2D Sum = FVector2D::ZeroVector;
						float Occlusion = 0.0f;
						for (int32 Index = 0; Index < 4; ++Index)
						{
							// A cell the light engine holds opaque has no light of its own, even when a pack's model for it is not a full cube.
							const FMcStateId BesideState = Snapshot.Get(Cells[Index].X, Cells[Index].Y, Cells[Index].Z);
							const bool bNoLight = bOccluded[Index] || (Palette.States.IsValidIndex(BesideState) && Palette.States[BesideState].bOpaqueToLight);
							const uint8 Packed = bNoLight ? FrontLight : Snapshot.Light[FMcSectionSnapshot::Index(Cells[Index].X, Cells[Index].Y, Cells[Index].Z)];
							Sum += FVector2D(Packed >> 4, Packed & 15);
							Occlusion += bOccluded[Index] ? 0.2f : 1.0f;
						}
						CornerLight[Corner] = Sum / 60.0;
						CornerOcclusion[Corner] = Occlusion / 4.0f;
					}

					// Model axes (x east, y up, z south) to Unreal's (X east, Y south, Z up).
					const FVector Normal(ModelNormal.X, ModelNormal.Z, ModelNormal.Y);
					const int32 First = Buffers.Positions.Num();
					for (int32 Corner = 0; Corner < 4; ++Corner)
					{
						const FVector3f Position = Block + Quad.Positions[Corner];
						Buffers.Positions.Add(FVector(Position.X, Position.Z, Position.Y) * McCoords::UnitsPerBlock);
						Buffers.Normals.Add(Normal);
						Buffers.UV0.Add(FVector2D(Quad.UVs[Corner]));
						Buffers.UV1.Add(FVector2D(QuadInfo.Layer, Info.Surface));
						Buffers.UV2.Add(CornerLight[Corner]);
						// sRGB: Minecraft multiplies these in display space, and so does the material.
						Buffers.Colors.Add(ScaleColor(Tint, Shade * CornerOcclusion[Corner]));
					}

					// Split the quad along the diagonal whose ends are more alike, so one dark corner doesn't
					// smear across the whole face.
					auto Brightness = [&](int32 Corner) { return CornerOcclusion[Corner] * FMath::Max(CornerLight[Corner].X, CornerLight[Corner].Y); };
					const int32 Start = First + (Brightness(0) + Brightness(2) > Brightness(1) + Brightness(3) ? 1 : 0);
					const int32 A = Start;
					const int32 B = First + (Start - First + 1) % 4;
					const int32 C = First + (Start - First + 2) % 4;
					const int32 D = First + (Start - First + 3) % 4;

					// The corners go anticlockwise seen from outside in the model's right-handed axes. Swapping
					// y and z for Unreal mirrors them to clockwise, which is the order Unreal draws as front
					// faces. mc.Render.FlipWinding reverses it, for checking.
					if (Palette.bFlipWinding)
					{
						Buffers.Triangles.Append({ A, C, B, A, D, C });
					}
					else
					{
						Buffers.Triangles.Append({ A, B, C, A, C, D });
					}
				}
			}
		}
	}
	return Mesh;
}
