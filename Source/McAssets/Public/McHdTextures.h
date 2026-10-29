// HD Textures, the pure part: the functions that turn a 16 pixel texture into a high-resolution albedo, a normal map and
// a specular map (LabPBR layout) without a UObject in sight, so they can be tested. The engine side (texture arrays,
// the cache, resource packs) is FMcHdTextures.
//
// - Upscale2x is an edge-aware pixel-art scaler in the style of Scale2x/hqx/xBR: corners that two similar neighbours
//   enclose are rounded off, and every sub-pixel then blends only with the neighbours its colour is close to, so a hard
//   edge stays hard and a gradient or grain turns smooth.
// - PickClass names the kind of material a texture is from the block tags and the texture's name.
// - Generate adds the class's detail (stone cracks and pits, wood grain, grains of soil, brushed metal, gems in ore,
//   veins in leaves, wool fibre) and makes the maps.
//
// Texture layouts (LabPBR 1.3, which the lit HD materials read):
//   normal   R,G = tangent-space normal x,y (0.5 is flat; +y is towards the top of the picture), B = ambient occlusion
//            (255 = open), A = height (255 = highest).
//   specular R = perceptual smoothness, G = F0 (0..229 dielectric, 230..254 metal, 255 a white metal), B = porosity
//            (unused, 0), A = emissive (255 = none, else 0..254 scaled to 0..1).

#pragma once

#include "CoreMinimal.h"

namespace McHd
{
	// Bump this when the generated pictures change, so cached ones are rebuilt.
	constexpr int32 GeneratorVersion = 5;

	enum class EMaterialClass : uint8
	{
		Generic,
		Stone,
		Wood,
		Soil,
		Metal,
		Ore,
		Gem,
		Leaves,
		Plant,
		Wool,
		Glass,
		// Skins and mob textures: a fine weave, no block detail.
		Fabric,
	};

	// The result of scaling: Width x Height pixels, rows from the top.
	struct FPicture
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;
	};

	// Doubles a picture. With bWrap the edges meet the opposite ones (block textures tile), else they stop.
	MCASSETS_API FPicture Upscale2x(const FPicture& Source, bool bWrap);
	// Scales by a power of two with Upscale2x, or to any other size with edge-aware sampling (bilinear that does not
	// cross a hard edge). A smaller target shrinks with a box filter.
	// The nearest texel, every texel a hard square (for the textures themselves; ScaleTo smooths).
	MCASSETS_API FPicture ScaleNearest(const FPicture& Source, int32 Width, int32 Height);
	MCASSETS_API FPicture ScaleTo(const FPicture& Source, int32 Width, int32 Height, bool bWrap);
	// A box filter down to the size: each pixel the average (alpha weighted) of the pixels it covers. bPlain: all four
	// channels averaged as they are, for the maps (whose alpha is height or emission, not transparency).
	MCASSETS_API FPicture Shrink(const FPicture& Source, int32 Width, int32 Height, bool bPlain = false);

	// How alike two colours are: the largest channel difference over all four channels (0 to 255).
	MCASSETS_API int32 GetColourDistance(const FColor& A, const FColor& B);

	// Which blocks the tags put in which class: block name without namespace ("oak_log") to class. Filled from the data
	// pack's tags (BuildTagClasses in the engine part), or by hand in a test.
	using FClassTags = TMap<FName, EMaterialClass>;

	// The class of a texture id such as "minecraft:block/oak_log_top" or "block/stone": the tags first, by the block the
	// texture belongs to (the id without a face suffix), then keywords in the name. Ids under "entity/" are Fabric.
	MCASSETS_API EMaterialClass PickClass(const FString& TextureId, const FClassTags* Tags = nullptr);
	// True if the texture belongs to a block that gives light (glowstone, lanterns, lava, magma and the like).
	MCASSETS_API bool IsEmissiveTexture(const FString& TextureId);

	// A normal map (RGB, with the occlusion and height in B and A as the layout says) from a height field of 0..1
	// values (Width x Height, rows from the top). Strength scales the slopes. The edges wrap.
	MCASSETS_API void NormalFromHeight(TConstArrayView<float> Height, int32 Width, int32 Height_, float Strength, TArray<FColor>& OutNormal);

	struct FMaps
	{
		FPicture Albedo;
		FPicture Normal;
		FPicture Specular;
	};

	// The three pictures for a source of any size (block textures are square; skins are 64x64 or 64x32), at Scale times
	// its size (a power of two). The detail is the same every time for a texture id. bEmissive gives the specular map
	// an emissive mask from the picture's bright pixels. bWrap: the picture tiles.
	MCASSETS_API FMaps Generate(const FString& TextureId, const FPicture& Source, int32 Scale, EMaterialClass Class, bool bEmissive, bool bWrap);

	// Maps for a source that already has albedo only at the target size: the normal and specular maps are generated
	// from the picture as it is (no detail added). For resource packs that have a texture but no _n or _s file.
	MCASSETS_API FMaps GenerateFromAlbedo(const FString& TextureId, const FPicture& Albedo, EMaterialClass Class, bool bEmissive, bool bWrap);

	// A stable seed for a texture name.
	MCASSETS_API uint32 GetSeed(const FString& TextureId);
}
