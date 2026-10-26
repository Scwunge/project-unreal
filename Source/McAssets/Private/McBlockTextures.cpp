#include "McBlockTextures.h"
#include "McBlockModels.h"
#include "McCache.h"
#include "Async/ParallelFor.h"
#include "Engine/Texture2DArray.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TextureResource.h"

namespace
{
	constexpr int32 LayerSize = FMcBlockTextures::TextureSize;
	constexpr int32 LayerMips = FMcBlockTextures::NumMips;
	const TCHAR* const MissingTexture = TEXT("minecraft:missingno");

	int32 MipSize(int32 Mip)
	{
		return LayerSize >> Mip;
	}

	// The bytes of a layer's whole mip chain, one mip after the other.
	int32 ChainBytes()
	{
		int32 Total = 0;
		for (int32 Mip = 0; Mip < LayerMips; ++Mip)
		{
			Total += MipSize(Mip) * MipSize(Mip) * 4;
		}
		return Total;
	}

	// Box filter, weighting each texel by its alpha so the colour of transparent texels doesn't leak in and
	// darken the edges of cutouts.
	void BuildMips(const TArray<FColor>& Base, TArray<uint8>& Out)
	{
		Out.SetNumUninitialized(ChainBytes());
		FMemory::Memcpy(Out.GetData(), Base.GetData(), LayerSize * LayerSize * 4);

		int32 PreviousOffset = 0;
		int32 Offset = LayerSize * LayerSize * 4;
		for (int32 Mip = 1; Mip < LayerMips; ++Mip)
		{
			const int32 Dim = MipSize(Mip);
			const FColor* Previous = reinterpret_cast<const FColor*>(Out.GetData() + PreviousOffset);
			FColor* Current = reinterpret_cast<FColor*>(Out.GetData() + Offset);
			for (int32 Y = 0; Y < Dim; ++Y)
			{
				for (int32 X = 0; X < Dim; ++X)
				{
					const FColor Texels[4] = {
						Previous[(Y * 2) * Dim * 2 + X * 2], Previous[(Y * 2) * Dim * 2 + X * 2 + 1],
						Previous[(Y * 2 + 1) * Dim * 2 + X * 2], Previous[(Y * 2 + 1) * Dim * 2 + X * 2 + 1] };
					int32 AlphaSum = 0, B = 0, G = 0, R = 0, PlainB = 0, PlainG = 0, PlainR = 0;
					for (const FColor& Texel : Texels)
					{
						AlphaSum += Texel.A;
						B += Texel.B * Texel.A;
						G += Texel.G * Texel.A;
						R += Texel.R * Texel.A;
						PlainB += Texel.B;
						PlainG += Texel.G;
						PlainR += Texel.R;
					}
					FColor& Result = Current[Y * Dim + X];
					Result.A = static_cast<uint8>((AlphaSum + 2) / 4);
					if (AlphaSum > 0)
					{
						Result.B = static_cast<uint8>((B + AlphaSum / 2) / AlphaSum);
						Result.G = static_cast<uint8>((G + AlphaSum / 2) / AlphaSum);
						Result.R = static_cast<uint8>((R + AlphaSum / 2) / AlphaSum);
					}
					else
					{
						Result.B = static_cast<uint8>((PlainB + 2) / 4);
						Result.G = static_cast<uint8>((PlainG + 2) / 4);
						Result.R = static_cast<uint8>((PlainR + 2) / 4);
					}
				}
			}
			PreviousOffset = Offset;
			Offset += Dim * Dim * 4;
		}
	}

	struct FLoadedImage
	{
		bool bOk = false;
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;
		TSharedPtr<FJsonObject> Meta;  // the .mcmeta, if there is one
	};

	FLoadedImage LoadImage(IImageWrapperModule& Module, const FString& PngPath)
	{
		FLoadedImage Image;
		TArray<uint8> File;
		if (!FFileHelper::LoadFileToArray(File, *PngPath))
		{
			return Image;
		}
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		TArray64<uint8> Raw;
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(File.GetData(), File.Num()) || !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
		{
			return Image;
		}
		Image.Width = Wrapper->GetWidth();
		Image.Height = Wrapper->GetHeight();
		if (Raw.Num() != static_cast<int64>(Image.Width) * Image.Height * 4)
		{
			return Image;
		}
		Image.Pixels.SetNumUninitialized(Image.Width * Image.Height);
		FMemory::Memcpy(Image.Pixels.GetData(), Raw.GetData(), Raw.Num());
		Image.bOk = true;

		FString MetaText;
		if (FFileHelper::LoadFileToString(MetaText, *(PngPath + TEXT(".mcmeta"))))
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MetaText);
			FJsonSerializer::Deserialize(Reader, Image.Meta);
		}
		return Image;
	}

	// One frame of a (possibly stacked) image, as 16x16 pixels. Frames of other sizes are scaled with nearest.
	void ExtractFrame(const FLoadedImage& Image, int32 FrameWidth, int32 FrameHeight, int32 Frame, TArray<FColor>& Out)
	{
		Out.SetNumUninitialized(LayerSize * LayerSize);
		const int32 Top = Frame * FrameHeight;
		for (int32 Y = 0; Y < LayerSize; ++Y)
		{
			const int32 SourceY = FMath::Min(Top + (Y * 2 + 1) * FrameHeight / (LayerSize * 2), Image.Height - 1);
			for (int32 X = 0; X < LayerSize; ++X)
			{
				const int32 SourceX = FMath::Min((X * 2 + 1) * FrameWidth / (LayerSize * 2), Image.Width - 1);
				Out[Y * LayerSize + X] = Image.Pixels[SourceY * Image.Width + SourceX];
			}
		}
	}

	void MeasureAlpha(const TArray<FColor>& Pixels, bool& bTranslucent, bool& bCutout)
	{
		for (const FColor& Pixel : Pixels)
		{
			bCutout |= Pixel.A == 0;
			bTranslucent |= Pixel.A > 0 && Pixel.A < 255;
		}
	}

	TArray<FColor> MakeMissingTexture()
	{
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(LayerSize * LayerSize);
		for (int32 Y = 0; Y < LayerSize; ++Y)
		{
			for (int32 X = 0; X < LayerSize; ++X)
			{
				Pixels[Y * LayerSize + X] = ((X / 8 + Y / 8) & 1) ? FColor(0, 0, 0, 255) : FColor(248, 0, 248, 255);
			}
		}
		return Pixels;
	}

	FColor Blend(const FColor& From, const FColor& To, int32 Part, int32 Whole)
	{
		auto Mix = [Part, Whole](int32 A, int32 B) { return static_cast<uint8>((A * (Whole - Part) + B * Part + Whole / 2) / Whole); };
		return FColor(Mix(From.R, To.R), Mix(From.G, To.G), Mix(From.B, To.B), Mix(From.A, To.A));
	}
}

struct FMcBlockTextures::FRenderState
{
	FTextureRHIRef Staging;  // render thread only: one layer's mip chain, copied into the array
};

FMcBlockTextures& FMcBlockTextures::Get()
{
	// Never destroyed: an FGCObject torn down by static destruction would outlive the garbage collector.
	static FMcBlockTextures* Instance = new FMcBlockTextures;
	return *Instance;
}

void FMcBlockTextures::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(TextureArray);
}

int32 FMcBlockTextures::GetLayer(FName TextureId) const
{
	const int32* Layer = LayersById.Find(TextureId);
	return Layer ? *Layer : 0;
}

int32 FMcBlockTextures::GetAnimationStep(int32 Layer) const
{
	const int32 Animation = Layers[Layer].Animation;
	return Animation == INDEX_NONE ? INDEX_NONE : Animations[Animation].Step;
}

bool FMcBlockTextures::EnsureLoaded()
{
	check(IsInGameThread());
	if (IsLoaded())
	{
		return true;
	}
	FMcBlockModels& Models = FMcBlockModels::Get();
	if (!Models.EnsureBaked())
	{
		return false;
	}

	const double StartTime = FPlatformTime::Seconds();
	FMcTextureStats NewStats;
	IImageWrapperModule& ImageModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	// Layer 0 is the missing texture; the others follow the models' sorted texture ids.
	TArray<FName> Ids;
	for (const FName Id : Models.GetTextures())
	{
		if (Id != FName(MissingTexture))
		{
			Ids.Add(Id);
		}
	}
	NewStats.Ids = Ids.Num();

	TArray<FLoadedImage> Images;
	Images.SetNum(Ids.Num());
	ParallelFor(Ids.Num(), [&](int32 Index)
	{
		Images[Index] = LoadImage(ImageModule, McCache::Asset(TEXT("textures/") + McCache::PathOf(Ids[Index].ToString()) + TEXT(".png")));
	});

	TArray<FLayer> NewLayers;
	TArray<FAnimation> NewAnimations;
	TMap<FName, int32> NewLayersById;

	FLayer& Missing = NewLayers.AddDefaulted_GetRef();
	Missing.Id = MissingTexture;
	Missing.Pixels = MakeMissingTexture();
	NewLayersById.Add(Missing.Id, 0);

	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		const FLoadedImage& Image = Images[Index];
		if (!Image.bOk)
		{
			NewStats.Fallbacks.Add(Ids[Index]);
			continue;
		}
		++NewStats.Loaded;

		const int32 LayerIndex = NewLayers.Num();
		FLayer& Layer = NewLayers.AddDefaulted_GetRef();
		Layer.Id = Ids[Index];
		NewLayersById.Add(Layer.Id, LayerIndex);

		const TSharedPtr<FJsonObject>* AnimationObject = nullptr;
		if (Image.Meta.IsValid() && Image.Meta->TryGetObjectField(TEXT("animation"), AnimationObject))
		{
			FAnimation Animation;
			Animation.Layer = LayerIndex;

			int32 FrameWidth = Image.Width, FrameHeight = 0, FrameTime = 1;
			(*AnimationObject)->TryGetNumberField(TEXT("width"), FrameWidth);
			(*AnimationObject)->TryGetNumberField(TEXT("height"), FrameHeight);
			(*AnimationObject)->TryGetNumberField(TEXT("frametime"), FrameTime);
			(*AnimationObject)->TryGetBoolField(TEXT("interpolate"), Animation.bInterpolate);
			FrameWidth = FMath::Max(FrameWidth, 1);
			FrameHeight = FrameHeight > 0 ? FrameHeight : FrameWidth;  // frames are square unless the file says otherwise
			FrameTime = FMath::Max(FrameTime, 1);

			const int32 NumFrames = FMath::Max(Image.Height / FrameHeight, 1);
			Animation.Frames.SetNum(NumFrames);
			for (int32 Frame = 0; Frame < NumFrames; ++Frame)
			{
				ExtractFrame(Image, FrameWidth, FrameHeight, Frame, Animation.Frames[Frame]);
				MeasureAlpha(Animation.Frames[Frame], Layer.bTranslucent, Layer.bCutout);
			}

			const TArray<TSharedPtr<FJsonValue>>* Sequence;
			if ((*AnimationObject)->TryGetArrayField(TEXT("frames"), Sequence))
			{
				// A frame is a bare index, or {"index": n, "time": ticks}.
				for (const TSharedPtr<FJsonValue>& Entry : *Sequence)
				{
					int32 Frame = INDEX_NONE, Time = FrameTime;
					double Number;
					const TSharedPtr<FJsonObject>* EntryObject;
					if (Entry->TryGetNumber(Number))
					{
						Frame = static_cast<int32>(Number);
					}
					else if (Entry->TryGetObject(EntryObject))
					{
						(*EntryObject)->TryGetNumberField(TEXT("index"), Frame);
						(*EntryObject)->TryGetNumberField(TEXT("time"), Time);
					}
					if (Frame >= 0 && Frame < NumFrames)
					{
						Animation.Order.Add(Frame);
						Animation.Times.Add(FMath::Max(Time, 1));
					}
				}
			}
			if (Animation.Order.Num() == 0)
			{
				for (int32 Frame = 0; Frame < NumFrames; ++Frame)
				{
					Animation.Order.Add(Frame);
					Animation.Times.Add(FrameTime);
				}
			}
			Animation.TotalTime = 0;
			for (const int32 Time : Animation.Times)
			{
				Animation.TotalTime += Time;
			}

			Layer.Pixels = Animation.Frames[Animation.Order[0]];
			Layer.Animation = NewAnimations.Num();
			NewAnimations.Add(MoveTemp(Animation));
			++NewStats.Animated;
			if (FrameWidth != LayerSize || FrameHeight != LayerSize)
			{
				NewStats.Resized.Add(FString::Printf(TEXT("%s %dx%d frames"), *Ids[Index].ToString(), FrameWidth, FrameHeight));
			}
		}
		else
		{
			ExtractFrame(Image, Image.Width, Image.Height, 0, Layer.Pixels);
			MeasureAlpha(Layer.Pixels, Layer.bTranslucent, Layer.bCutout);
			if (Image.Width != LayerSize || Image.Height != LayerSize)
			{
				NewStats.Resized.Add(FString::Printf(TEXT("%s %dx%d"), *Ids[Index].ToString(), Image.Width, Image.Height));
			}
		}
	}

	// The array: mip m holds every layer's m-th mip one after the other.
	const int32 NumLayers = NewLayers.Num();
	TArray<TArray<uint8>> Mips;
	Mips.SetNum(LayerMips);
	for (int32 Mip = 0; Mip < LayerMips; ++Mip)
	{
		Mips[Mip].SetNumUninitialized(NumLayers * MipSize(Mip) * MipSize(Mip) * 4);
	}
	TArray<uint8> Chain;
	for (int32 LayerIndex = 0; LayerIndex < NumLayers; ++LayerIndex)
	{
		BuildMips(NewLayers[LayerIndex].Pixels, Chain);
		int32 Offset = 0;
		for (int32 Mip = 0; Mip < LayerMips; ++Mip)
		{
			const int32 Bytes = MipSize(Mip) * MipSize(Mip) * 4;
			FMemory::Memcpy(Mips[Mip].GetData() + LayerIndex * Bytes, Chain.GetData() + Offset, Bytes);
			Offset += Bytes;
		}
	}

	UTexture2DArray* Array = NewObject<UTexture2DArray>(GetTransientPackage(), TEXT("McBlockTextureArray"), RF_Transient);
	Array->SetPlatformData(new FTexturePlatformData());
	FTexturePlatformData* PlatformData = Array->GetPlatformData();
	PlatformData->SizeX = LayerSize;
	PlatformData->SizeY = LayerSize;
	PlatformData->SetNumSlices(NumLayers);
	PlatformData->PixelFormat = PF_B8G8R8A8;
	Array->bNotOfflineProcessed = true;
	for (int32 Mip = 0; Mip < LayerMips; ++Mip)
	{
		FTexture2DMipMap* MipMap = new FTexture2DMipMap(MipSize(Mip), MipSize(Mip), NumLayers);
		PlatformData->Mips.Add(MipMap);
		MipMap->BulkData.Lock(LOCK_READ_WRITE);
		void* Destination = MipMap->BulkData.Realloc(Mips[Mip].Num());
		FMemory::Memcpy(Destination, Mips[Mip].GetData(), Mips[Mip].Num());
		MipMap->BulkData.Unlock();
	}
	Array->SRGB = true;
	Array->Filter = TF_Nearest;
	Array->AddressX = TA_Clamp;
	Array->AddressY = TA_Clamp;
	Array->NeverStream = true;
	Array->LODGroup = TEXTUREGROUP_Pixels2D;
#if WITH_EDITORONLY_DATA
	Array->MipGenSettings = TMGS_LeaveExistingMips;
#endif
	Array->UpdateResource();

	for (const FLayer& Layer : NewLayers)
	{
		NewStats.Translucent += Layer.bTranslucent;
		NewStats.Cutout += Layer.bCutout;
	}
	NewStats.Layers = NumLayers;

	Layers = MoveTemp(NewLayers);
	Animations = MoveTemp(NewAnimations);
	LayersById = MoveTemp(NewLayersById);
	RenderState = MakeShared<FRenderState, ESPMode::ThreadSafe>();
	Stats = MoveTemp(NewStats);
	TextureArray = Array;

	UE_LOG(LogMcAssets, Log, TEXT("Loaded %d of %d block textures into %d layers (%d animated, %d cutout, %d translucent) in %.0f ms"),
		Stats.Loaded, Stats.Ids, Stats.Layers, Stats.Animated, Stats.Cutout, Stats.Translucent, (FPlatformTime::Seconds() - StartTime) * 1000.0);
	for (const FName Fallback : Stats.Fallbacks)
	{
		UE_LOG(LogMcAssets, Warning, TEXT("  %s can't be loaded; using the missing texture"), *Fallback.ToString());
	}
	for (const FString& Resized : Stats.Resized)
	{
		UE_LOG(LogMcAssets, Log, TEXT("  %s scaled to %dx%d"), *Resized, LayerSize, LayerSize);
	}
	return true;
}

void FMcBlockTextures::Tick(double GameTicks)
{
	check(IsInGameThread());
	if (!IsLoaded() || Animations.Num() == 0)
	{
		return;
	}

	struct FUpload
	{
		int32 Layer;
		TArray<uint8> Chain;
	};
	TArray<FUpload> Uploads;

	const int64 Ticks = static_cast<int64>(FMath::FloorToDouble(GameTicks));
	for (FAnimation& Animation : Animations)
	{
		int32 Time = static_cast<int32>(((Ticks % Animation.TotalTime) + Animation.TotalTime) % Animation.TotalTime);
		int32 Step = 0;
		while (Time >= Animation.Times[Step])
		{
			Time -= Animation.Times[Step];
			++Step;
		}

		// Interpolated animations change every tick; the others only when the step does.
		const int32 TicksIntoStep = Animation.bInterpolate ? Time : 0;
		if (Step == Animation.Step && TicksIntoStep == Animation.TicksIntoStep)
		{
			continue;
		}
		Animation.Step = Step;
		Animation.TicksIntoStep = TicksIntoStep;

		FLayer& Layer = Layers[Animation.Layer];
		const TArray<FColor>& Current = Animation.Frames[Animation.Order[Step]];
		if (Animation.bInterpolate && TicksIntoStep > 0)
		{
			const TArray<FColor>& Next = Animation.Frames[Animation.Order[(Step + 1) % Animation.Order.Num()]];
			for (int32 Index = 0; Index < Current.Num(); ++Index)
			{
				Layer.Pixels[Index] = Blend(Current[Index], Next[Index], TicksIntoStep, Animation.Times[Step]);
			}
		}
		else
		{
			Layer.Pixels = Current;
		}

		FUpload& Upload = Uploads.AddDefaulted_GetRef();
		Upload.Layer = Animation.Layer;
		BuildMips(Layer.Pixels, Upload.Chain);
	}

	if (Uploads.Num() == 0 || !TextureArray->GetResource())
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(McUploadAnimatedTextures)(
		[Resource = TextureArray->GetResource(), State = RenderState, Uploads = MoveTemp(Uploads)](FRHICommandListImmediate& RHICmdList)
		{
			if (!Resource->TextureRHI.IsValid())
			{
				return;
			}
			if (!State->Staging.IsValid())
			{
				State->Staging = RHICmdList.CreateTexture(FRHITextureCreateDesc::Create2D(TEXT("McAnimationStaging"), LayerSize, LayerSize, PF_B8G8R8A8)
					.SetNumMips(LayerMips)
					.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::SRGB)
					.SetInitialState(ERHIAccess::SRVMask));
			}

			for (const FUpload& Upload : Uploads)
			{
				int32 Offset = 0;
				for (int32 Mip = 0; Mip < LayerMips; ++Mip)
				{
					const uint32 Dim = MipSize(Mip);
					RHICmdList.UpdateTexture2D(State->Staging, Mip, FUpdateTextureRegion2D(0, 0, 0, 0, Dim, Dim), Dim * 4, Upload.Chain.GetData() + Offset);
					Offset += Dim * Dim * 4;
				}

				FRHICopyTextureInfo Copy;
				Copy.DestSliceIndex = Upload.Layer;
				Copy.NumMips = LayerMips;
				RHICmdList.Transition({
					FRHITransitionInfo(State->Staging, ERHIAccess::Unknown, ERHIAccess::CopySrc),
					FRHITransitionInfo(Resource->TextureRHI, ERHIAccess::Unknown, ERHIAccess::CopyDest) });
				RHICmdList.CopyTexture(State->Staging, Resource->TextureRHI, Copy);
				RHICmdList.Transition({
					FRHITransitionInfo(State->Staging, ERHIAccess::CopySrc, ERHIAccess::SRVMask),
					FRHITransitionInfo(Resource->TextureRHI, ERHIAccess::CopyDest, ERHIAccess::SRVMask) });
			}
		});
}

void FMcBlockTextures::Shutdown()
{
	if (RenderState.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(McReleaseAnimationStaging)([State = RenderState](FRHICommandListImmediate&)
		{
			State->Staging.SafeRelease();
		});
		RenderState.Reset();
	}
}
