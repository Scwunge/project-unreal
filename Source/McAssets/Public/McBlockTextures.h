// Every block texture packed into one texture array, with the animations from the .mcmeta files.

#pragma once

#include "CoreMinimal.h"
#include "McAssets.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectPtr.h"

class UTexture2DArray;

struct FMcTextureStats
{
	int32 Ids = 0;            // texture ids the models refer to
	int32 Loaded = 0;
	int32 Layers = 0;         // including layer 0, the missing texture
	int32 Animated = 0;
	int32 Translucent = 0;    // layers with partial-alpha pixels
	int32 Cutout = 0;         // layers with fully transparent pixels
	TArray<FName> Fallbacks;  // ids that couldn't be loaded and use layer 0
	TArray<FString> Resized;  // "id WxH": not 16 wide (or a 16x16 frame), scaled down with nearest
};

// Loaded once on the game thread. After that the layer lookups and flags may be read from any thread; Tick
// and the pixel readers belong to the game thread.
class MCASSETS_API FMcBlockTextures : public FGCObject
{
public:
	static constexpr int32 TextureSize = 16;
	static constexpr int32 NumMips = 5;  // 16, 8, 4, 2, 1

	static FMcBlockTextures& Get();

	// Bakes the block models if needed, then loads every texture they use. Does nothing if already loaded.
	bool EnsureLoaded();
	bool IsLoaded() const { return TextureArray != nullptr; }

	// Releases the render-side helper resources; the module calls this on shutdown.
	void Shutdown();

	// The layer for an id like "minecraft:block/stone". 0, the missing-texture layer, for unknown ids.
	int32 GetLayer(FName TextureId) const;
	int32 NumLayers() const { return Layers.Num(); }
	UTexture2DArray* GetTextureArray() const { return TextureArray; }

	// Over every frame of an animation. Translucent: some pixel has alpha strictly between 0 and 255.
	// Cutout: some pixel is fully transparent.
	bool HasTranslucentPixels(int32 Layer) const { return Layers[Layer].bTranslucent; }
	bool HasCutoutPixels(int32 Layer) const { return Layers[Layer].bCutout; }

	bool IsAnimated(int32 Layer) const { return Layers[Layer].Animation != INDEX_NONE; }
	// The step of the animation's frame order now showing, or INDEX_NONE if the layer isn't animated.
	int32 GetAnimationStep(int32 Layer) const;
	// The 16x16 pixels now in the layer's top mip (the blend, for interpolated animations).
	TConstArrayView<FColor> GetPixels(int32 Layer) const { return Layers[Layer].Pixels; }

	// The id the layer was loaded for (NAME_None for the missing-texture layer).
	FName GetLayerId(int32 Layer) const { return Layers[Layer].Id; }
	// Called (game thread, from Tick) for each layer whose picture just changed, after GetPixels has the new one.
	TFunction<void(int32 Layer)> OnLayerAnimated;

	// Moves the animations to this game time (20 ticks a second) and uploads the layers whose picture changed.
	void Tick(double GameTicks);

	const FMcTextureStats& GetStats() const { return Stats; }

	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FMcBlockTextures"); }

private:
	struct FLayer
	{
		FName Id;
		bool bTranslucent = false;
		bool bCutout = false;
		int32 Animation = INDEX_NONE;
		TArray<FColor> Pixels;  // the current top mip, TextureSize squared
	};

	struct FAnimation
	{
		int32 Layer = INDEX_NONE;
		TArray<TArray<FColor>> Frames;  // each TextureSize squared
		TArray<int32> Order;            // the frame shown at each step
		TArray<int32> Times;            // ticks each step lasts
		int32 TotalTime = 1;
		bool bInterpolate = false;
		int32 Step = INDEX_NONE;
		int32 TicksIntoStep = INDEX_NONE;
	};

	struct FRenderState;

	TObjectPtr<UTexture2DArray> TextureArray;
	TArray<FLayer> Layers;
	TArray<FAnimation> Animations;
	TMap<FName, int32> LayersById;
	TSharedPtr<FRenderState, ESPMode::ThreadSafe> RenderState;
	FMcTextureStats Stats;
};
