#include "McSectionMesher.h"
#include "McBlockModels.h"
#include "McBlockRegistry.h"
#include "McBlockTextures.h"
#include "McBlockWorld.h"

FMcSectionSnapshot FMcSectionSnapshot::Capture(const FMcBlockWorld& World, FIntPoint ChunkPos, int32 SectionIndex)
{
	FMcSectionSnapshot Snapshot;
	Snapshot.ChunkPos = ChunkPos;
	Snapshot.SectionIndex = SectionIndex;
	Snapshot.MinY = World.GetDimension().MinY + SectionIndex * McCoords::SectionSize;
	Snapshot.Blocks.SetNumZeroed(Padded * Padded * Padded);

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
	// Plains colours (minecraft.wiki, "Color"): grass #91BD59, foliage #77AB2F, water #3F76E4. Some blocks
	// ignore the biome and always use their own.
	static const FColor Grass(0x91, 0xBD, 0x59);
	static const FColor Foliage(0x77, 0xAB, 0x2F);
	static const FColor Water(0x3F, 0x76, 0xE4);

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
		Info.bHidesOwnBlock = bFullCube && Info.OpaqueFaces != (1 << NumDirections) - 1 && !Name.EndsWith(TEXT("leaves"));

		if (bAnyTinted)
		{
			Info.Tint = McBlockMesher::GetBlockTint(Registry, static_cast<FMcStateId>(Id));
		}
	}
	return Palette;
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
					const FColor Tint = Quad.TintIndex >= 0 ? Info.Tint : FColor::White;
					// The material multiplies in linear space; Minecraft multiplies the sRGB values.
					const FColor Color = FLinearColor(ScaleColor(Tint, Shade)).ToFColor(false);

					// Model axes (x east, y up, z south) to Unreal's (X east, Y south, Z up).
					const FVector Normal(ModelNormal.X, ModelNormal.Z, ModelNormal.Y);
					const int32 First = Buffers.Positions.Num();
					for (int32 Corner = 0; Corner < 4; ++Corner)
					{
						const FVector3f Position = Block + Quad.Positions[Corner];
						Buffers.Positions.Add(FVector(Position.X, Position.Z, Position.Y) * McCoords::UnitsPerBlock);
						Buffers.Normals.Add(Normal);
						Buffers.UV0.Add(FVector2D(Quad.UVs[Corner]));
						Buffers.UV1.Add(FVector2D(QuadInfo.Layer, 0.0));
						Buffers.Colors.Add(Color);
					}

					// The corners go anticlockwise seen from outside in the model's right-handed axes. Swapping
					// y and z for Unreal mirrors them to clockwise, which is the order Unreal draws as front
					// faces. mc.Render.FlipWinding reverses it, for checking.
					if (Palette.bFlipWinding)
					{
						Buffers.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
					}
					else
					{
						Buffers.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
					}
				}
			}
		}
	}
	return Mesh;
}
