#include "McBlockTextures.h"
#include "McBlockModels.h"
#include "McTextureTiles.h"
#include "McCache.h"
#include "McResourcePacks.h"
#include "Algo/Find.h"
#include "Async/ParallelFor.h"
#include "Engine/Texture2DArray.h"
#include "HAL/FileManager.h"
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
	const TCHAR* const MissingTexture = TEXT("minecraft:missingno");
	const TCHAR* const FlowTextures[] = { TEXT("minecraft:block/water_flow"), TEXT("minecraft:block/lava_flow") };

	int32 NumMipsFor(int32 Size)
	{
		return FMath::FloorLog2(Size) + 1;
	}

	// The bytes of a layer's mip chain, one mip after the other.
	int32 ChainBytes(int32 Size)
	{
		int32 Total = 0;
		for (int32 Side = Size; Side >= 1; Side /= 2)
		{
			Total += Side * Side * 4;
		}
		return Total;
	}

	// Box filter, weighting each texel by its alpha so the colour of transparent texels doesn't leak in and
	// darken the edges of cutouts.
	void BuildMips(const TArray<FColor>& Base, int32 Size, TArray<uint8>& Out)
	{
		Out.SetNumUninitialized(ChainBytes(Size));
		FMemory::Memcpy(Out.GetData(), Base.GetData(), Size * Size * 4);

		int32 PreviousOffset = 0;
		int32 Offset = Size * Size * 4;
		for (int32 Dim = Size / 2; Dim >= 1; Dim /= 2)
		{
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

	// The same chain for a map whose alpha is data (height, emission) and whose colours are never weighted.
	void BuildPlainMips(const TArray<FColor>& Base, int32 Size, TArray<uint8>& Out)
	{
		Out.SetNumUninitialized(ChainBytes(Size));
		FMemory::Memcpy(Out.GetData(), Base.GetData(), Size * Size * 4);
		int32 PreviousOffset = 0;
		int32 Offset = Size * Size * 4;
		for (int32 Dim = Size / 2; Dim >= 1; Dim /= 2)
		{
			const FColor* Previous = reinterpret_cast<const FColor*>(Out.GetData() + PreviousOffset);
			FColor* Current = reinterpret_cast<FColor*>(Out.GetData() + Offset);
			for (int32 Y = 0; Y < Dim; ++Y)
			{
				for (int32 X = 0; X < Dim; ++X)
				{
					const FColor Texels[4] = {
						Previous[(Y * 2) * Dim * 2 + X * 2], Previous[(Y * 2) * Dim * 2 + X * 2 + 1],
						Previous[(Y * 2 + 1) * Dim * 2 + X * 2], Previous[(Y * 2 + 1) * Dim * 2 + X * 2 + 1] };
					Current[Y * Dim + X] = FColor(
						static_cast<uint8>((Texels[0].R + Texels[1].R + Texels[2].R + Texels[3].R + 2) / 4),
						static_cast<uint8>((Texels[0].G + Texels[1].G + Texels[2].G + Texels[3].G + 2) / 4),
						static_cast<uint8>((Texels[0].B + Texels[1].B + Texels[2].B + Texels[3].B + 2) / 4),
						static_cast<uint8>((Texels[0].A + Texels[1].A + Texels[2].A + Texels[3].A + 2) / 4));
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
	};

	// The size from the PNG header alone (the IHDR chunk: width and height, big-endian, at bytes 16 and 20).
	bool ReadPngSize(const FString& PngPath, int32& Width, int32& Height)
	{
		const TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*PngPath));
		uint8 Header[24];
		if (!Reader || Reader->TotalSize() < 24)
		{
			return false;
		}
		Reader->Serialize(Header, sizeof(Header));
		auto Read32 = [&Header](int32 At) { return (Header[At] << 24) | (Header[At + 1] << 16) | (Header[At + 2] << 8) | Header[At + 3]; };
		Width = Read32(16);
		Height = Read32(20);
		return !Reader->IsError() && Width > 0 && Height > 0 && Header[1] == 'P' && Header[2] == 'N' && Header[3] == 'G';
	}

	TSharedPtr<FJsonObject> LoadMeta(const FString& PngPath)
	{
		TSharedPtr<FJsonObject> Meta;
		FString MetaText;
		if (FFileHelper::LoadFileToString(MetaText, *(PngPath + TEXT(".mcmeta"))))
		{
			const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MetaText);
			FJsonSerializer::Deserialize(Reader, Meta);
		}
		return Meta;
	}

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
		return Image;
	}

	// A Width by Height part of an image from (Left, Top), as Size x Size pixels. With Size 16 (vanilla) a part of another
	// size is sampled at its texel centres, as before packs. Larger layers average the texels each pixel covers (shrinking),
	// or repeat a texel (enlarging). bPlain: all four channels averaged as they are (the LabPBR maps, whose alpha is not
	// transparency); else colours are weighted by alpha so transparent texels don't darken the edges.
	void ExtractRegion(const FLoadedImage& Image, int32 Left, int32 Top, int32 Width, int32 Height, int32 Size, bool bPlain, TArray<FColor>& Out)
	{
		Out.SetNumUninitialized(Size * Size);
		if (Size == FMcBlockTextures::TextureSize)
		{
			for (int32 Y = 0; Y < Size; ++Y)
			{
				const int32 SourceY = FMath::Min(Top + (Y * 2 + 1) * Height / (Size * 2), Image.Height - 1);
				for (int32 X = 0; X < Size; ++X)
				{
					const int32 SourceX = FMath::Min(Left + (X * 2 + 1) * Width / (Size * 2), Image.Width - 1);
					Out[Y * Size + X] = Image.Pixels[SourceY * Image.Width + SourceX];
				}
			}
			return;
		}
		for (int32 Y = 0; Y < Size; ++Y)
		{
			const int32 Y0 = FMath::Min(Top + Y * Height / Size, Image.Height - 1);
			const int32 Y1 = FMath::Min(FMath::Max(Top + (Y + 1) * Height / Size, Y0 + 1), Image.Height);
			for (int32 X = 0; X < Size; ++X)
			{
				const int32 X0 = FMath::Min(Left + X * Width / Size, Image.Width - 1);
				const int32 X1 = FMath::Min(FMath::Max(Left + (X + 1) * Width / Size, X0 + 1), Image.Width);
				if (Y1 - Y0 == 1 && X1 - X0 == 1)
				{
					Out[Y * Size + X] = Image.Pixels[Y0 * Image.Width + X0];
					continue;
				}
				int64 R = 0, G = 0, B = 0, A = 0, Weight = 0, Count = 0;
				for (int32 SourceY = Y0; SourceY < Y1; ++SourceY)
				{
					for (int32 SourceX = X0; SourceX < X1; ++SourceX)
					{
						const FColor& Texel = Image.Pixels[SourceY * Image.Width + SourceX];
						const int32 W = bPlain ? 1 : Texel.A;
						R += Texel.R * W;
						G += Texel.G * W;
						B += Texel.B * W;
						A += Texel.A;
						Weight += W;
						++Count;
					}
				}
				FColor& Pixel = Out[Y * Size + X];
				if (Weight == 0)
				{
					Pixel = FColor(0, 0, 0, 0);
				}
				else
				{
					Pixel = FColor(static_cast<uint8>((R + Weight / 2) / Weight), static_cast<uint8>((G + Weight / 2) / Weight),
						static_cast<uint8>((B + Weight / 2) / Weight), static_cast<uint8>((A + Count / 2) / Count));
				}
			}
		}
	}

	// One frame of a (possibly stacked) image.
	void ExtractFrame(const FLoadedImage& Image, int32 FrameWidth, int32 FrameHeight, int32 Frame, int32 Size, bool bPlain, TArray<FColor>& Out)
	{
		ExtractRegion(Image, 0, Frame * FrameHeight, FrameWidth, FrameHeight, Size, bPlain, Out);
	}

	void MeasureAlpha(const TArray<FColor>& Pixels, bool& bTranslucent, bool& bCutout)
	{
		for (const FColor& Pixel : Pixels)
		{
			bCutout |= Pixel.A == 0;
			bTranslucent |= Pixel.A > 0 && Pixel.A < 255;
		}
	}

	TArray<FColor> MakeMissingTexture(int32 Size)
	{
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Size * Size);
		for (int32 Y = 0; Y < Size; ++Y)
		{
			for (int32 X = 0; X < Size; ++X)
			{
				Pixels[Y * Size + X] = ((X * 2 / Size + Y * 2 / Size) & 1) ? FColor(0, 0, 0, 255) : FColor(248, 0, 248, 255);
			}
		}
		return Pixels;
	}

	FColor Blend(const FColor& From, const FColor& To, int32 Part, int32 Whole)
	{
		auto Mix = [Part, Whole](int32 A, int32 B) { return static_cast<uint8>((A * (Whole - Part) + B * Part + Whole / 2) / Whole); };
		return FColor(Mix(From.R, To.R), Mix(From.G, To.G), Mix(From.B, To.B), Mix(From.A, To.A));
	}


	// What the first pass learns about a texture without decoding it.
	struct FProbe
	{
		FString Path;
		bool bFromPack = false;
		bool bFound = false;
		int32 Width = 0;
		int32 Height = 0;
		int32 FrameWidth = 0;  // of an animation's frames (or a tile); the width otherwise
		int32 FrameHeight = 0;
		bool bTile = false;    // a tile of a big picture (McTextureTiles.h): the part at TileLeft, TileTop, FrameWidth by FrameHeight
		int32 TileLeft = 0;
		int32 TileTop = 0;
		TSharedPtr<FJsonObject> Meta;
	};

	// A texture ready to become a layer: every frame at the array's size.
	struct FPrepared
	{
		bool bOk = false;
		bool bAnimated = false;
		bool bFromPack = false;
		int32 SourceWidth = 0;
		int32 SourceHeight = 0;
		int32 FrameWidth = 0;
		int32 FrameHeight = 0;
		int32 FrameTime = 1;
		bool bInterpolate = false;
		TArray<TArray<FColor>> Frames;
		TArray<int32> Order;
		TArray<int32> Times;
		bool bTile = false;
		TArray<FColor> Normal;
		TArray<FColor> Specular;
		TArray<TArray<FColor>> SideFrames;  // a flowing picture's: the top left quarter of each frame
	};
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
	// The fluids' pictures, which no block model names (water's and lava's models give only a particle): the fluid mesher
	// draws with them.
	for (const TCHAR* Fluid : { TEXT("minecraft:block/water_still"), TEXT("minecraft:block/lava_still") })
	{
		Ids.AddUnique(FName(Fluid));
	}
	for (const TCHAR* Flow : FlowTextures)
	{
		Ids.AddUnique(FName(Flow));
	}
	NewStats.Ids = Ids.Num();
	const bool bPbr = McPacks::GetUsePbr() && McPacks::HasPacks();

	// Pass 1, without decoding: where each picture comes from and how big it is. The widest one a resource pack brings
	// decides the size of the array, within the options; vanilla pictures never do (some are 32 wide and are shrunk to 16).
	TArray<FProbe> Probes;
	Probes.SetNum(Ids.Num());
	ParallelFor(Ids.Num(), [&](int32 Index)
	{
		// A tile of a big picture loads the whole picture (see McTextureTiles.h).
		FName Picture = Ids[Index];
		FIntPoint Tile, Grid;
		const bool bTile = McTextureTiles::ParseTileId(Ids[Index], Picture, Tile, Grid);
		const FString Relative = McCache::RelativeOf(Picture.ToString(), TEXT("textures/"), TEXT(".png"));
		FProbe& Probe = Probes[Index];
		Probe.Path = McCache::Asset(Relative);
		Probe.bFromPack = McPacks::IsFromPack(Relative);
		Probe.bFound = ReadPngSize(Probe.Path, Probe.Width, Probe.Height);
		if (!Probe.bFound)
		{
			return;
		}
		Probe.FrameWidth = Probe.Width;
		Probe.FrameHeight = Probe.Height;
		if (bTile)
		{
			Probe.bTile = true;
			Probe.FrameWidth = FMath::Max(Probe.Width / Grid.X, 1);
			Probe.FrameHeight = FMath::Max(Probe.Height / Grid.Y, 1);
			Probe.TileLeft = Tile.X * Probe.FrameWidth;
			Probe.TileTop = Tile.Y * Probe.FrameHeight;
			return;
		}
		Probe.Meta = LoadMeta(Probe.Path);
		const TSharedPtr<FJsonObject>* AnimationObject = nullptr;
		if (Probe.Meta.IsValid() && Probe.Meta->TryGetObjectField(TEXT("animation"), AnimationObject))
		{
			Probe.FrameHeight = 0;
			(*AnimationObject)->TryGetNumberField(TEXT("width"), Probe.FrameWidth);
			(*AnimationObject)->TryGetNumberField(TEXT("height"), Probe.FrameHeight);
			Probe.FrameWidth = FMath::Max(Probe.FrameWidth, 1);
			Probe.FrameHeight = Probe.FrameHeight > 0 ? Probe.FrameHeight : Probe.FrameWidth;  // frames are square unless the file says otherwise
		}
	});

	int32 Wanted = LayerSize;
	for (const FProbe& Probe : Probes)
	{
		if (Probe.bFound && Probe.bFromPack)
		{
			Wanted = FMath::Max(Wanted, FMath::Max(Probe.FrameWidth, Probe.FrameHeight));
		}
	}
	const int32 Size = Wanted > LayerSize ? McPacks::ChooseTextureSize(Wanted, Ids.Num() + 1, McPacks::GetMaxTextureSize(), McPacks::GetBudgetBytes()) : LayerSize;
	NewStats.Size = Size;
	NewStats.WantedSize = Wanted > LayerSize ? static_cast<int32>(FMath::RoundUpToPowerOfTwo(Wanted)) : LayerSize;
	const int32 MipCount = NumMipsFor(Size);

	// Pass 2: decode each picture and bring every frame to the array's size, so only small data is kept.
	TArray<FPrepared> Prepared;
	Prepared.SetNum(Ids.Num());
	ParallelFor(Ids.Num(), [&](int32 Index)
	{
		const FProbe& Probe = Probes[Index];
		FPrepared& Result = Prepared[Index];
		if (!Probe.bFound)
		{
			return;
		}
		const FLoadedImage Image = LoadImage(ImageModule, Probe.Path);
		if (!Image.bOk)
		{
			return;
		}
		Result.bOk = true;
		Result.bFromPack = Probe.bFromPack;
		Result.SourceWidth = Image.Width;
		Result.SourceHeight = Image.Height;
		Result.FrameWidth = Probe.FrameWidth;
		Result.FrameHeight = Probe.FrameHeight;

		const TSharedPtr<FJsonObject>* AnimationObject = nullptr;
		if (Probe.bTile)
		{
			Result.bTile = true;
			Result.SourceWidth = Probe.FrameWidth;
			Result.SourceHeight = Probe.FrameHeight;
			Result.Frames.SetNum(1);
			ExtractRegion(Image, Probe.TileLeft, Probe.TileTop, Probe.FrameWidth, Probe.FrameHeight, Size, false, Result.Frames[0]);
		}
		else if (Probe.Meta.IsValid() && Probe.Meta->TryGetObjectField(TEXT("animation"), AnimationObject))
		{
			Result.bAnimated = true;
			int32 FrameTime = 1;
			(*AnimationObject)->TryGetNumberField(TEXT("frametime"), FrameTime);
			(*AnimationObject)->TryGetBoolField(TEXT("interpolate"), Result.bInterpolate);
			FrameTime = FMath::Max(FrameTime, 1);

			const int32 NumFrames = FMath::Max(Image.Height / Result.FrameHeight, 1);
			Result.Frames.SetNum(NumFrames);
			for (int32 Frame = 0; Frame < NumFrames; ++Frame)
			{
				ExtractFrame(Image, Result.FrameWidth, Result.FrameHeight, Frame, Size, false, Result.Frames[Frame]);
			}
			// A flowing fluid's side shows the top left quarter of its 32-pixel frames, 16 pixels to the block: that
			// quarter, unscaled at vanilla's size, becomes the "<id>#side" layer.
			if (Algo::FindByPredicate(FlowTextures, [&](const TCHAR* Flow) { return Ids[Index] == FName(Flow); }))
			{
				Result.SideFrames.SetNum(NumFrames);
				for (int32 Frame = 0; Frame < NumFrames; ++Frame)
				{
					ExtractRegion(Image, 0, Frame * Result.FrameHeight, FMath::Max(Result.FrameWidth / 2, 1), FMath::Max(Result.FrameHeight / 2, 1),
						Size, false, Result.SideFrames[Frame]);
				}
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
						Result.Order.Add(Frame);
						Result.Times.Add(FMath::Max(Time, 1));
					}
				}
			}
			if (Result.Order.Num() == 0)
			{
				for (int32 Frame = 0; Frame < NumFrames; ++Frame)
				{
					Result.Order.Add(Frame);
					Result.Times.Add(FrameTime);
				}
			}
		}
		else
		{
			Result.Frames.SetNum(1);
			ExtractFrame(Image, Image.Width, Image.Height, 0, Size, false, Result.Frames[0]);
		}

		// LabPBR maps beside a pack's picture: <name>_n.png and <name>_s.png, the first frame of a stacked one. Only packs
		// have them; the files of other packs under this one are not mixed in.
		if (bPbr && Probe.bFromPack && !Probe.bTile)
		{
			const FString Base = McCache::RelativeOf(Ids[Index].ToString(), TEXT("textures/"), TEXT(""));
			for (int32 Kind = 0; Kind < 2; ++Kind)
			{
				const FString Relative = Base + (Kind == 0 ? TEXT("_n.png") : TEXT("_s.png"));
				if (!McPacks::IsFromPack(Relative))
				{
					continue;
				}
				const FLoadedImage Map = LoadImage(ImageModule, McCache::Asset(Relative));
				if (Map.bOk)
				{
					ExtractFrame(Map, Map.Width, FMath::Min(Map.Width, Map.Height), 0, Size, true, Kind == 0 ? Result.Normal : Result.Specular);
				}
			}
		}
	});

	TArray<FLayer> NewLayers;
	TArray<FAnimation> NewAnimations;
	TMap<FName, int32> NewLayersById;

	FLayer& Missing = NewLayers.AddDefaulted_GetRef();
	Missing.Id = MissingTexture;
	Missing.Pixels = MakeMissingTexture(Size);
	NewLayersById.Add(Missing.Id, 0);

	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		FPrepared& Texture = Prepared[Index];
		if (!Texture.bOk)
		{
			NewStats.Fallbacks.Add(Ids[Index]);
			continue;
		}
		++NewStats.Loaded;
		NewStats.FromPacks += Texture.bFromPack;
		NewStats.Tiles += Texture.bTile;

		const int32 LayerIndex = NewLayers.Num();
		FLayer& Layer = NewLayers.AddDefaulted_GetRef();
		Layer.Id = Ids[Index];
		Layer.bFromPack = Texture.bFromPack;
		NewLayersById.Add(Layer.Id, LayerIndex);
		Layer.Normal = MoveTemp(Texture.Normal);
		Layer.Specular = MoveTemp(Texture.Specular);
		NewStats.PbrMaps += (Layer.Normal.Num() > 0 || Layer.Specular.Num() > 0);

		for (const TArray<FColor>& Frame : Texture.Frames)
		{
			MeasureAlpha(Frame, Layer.bTranslucent, Layer.bCutout);
		}
		if (Texture.bAnimated)
		{
			FAnimation Animation;
			Animation.Layer = LayerIndex;
			Animation.bInterpolate = Texture.bInterpolate;
			Animation.Frames = MoveTemp(Texture.Frames);
			Animation.Order = MoveTemp(Texture.Order);
			Animation.Times = MoveTemp(Texture.Times);
			Animation.TotalTime = 0;
			for (const int32 Time : Animation.Times)
			{
				Animation.TotalTime += Time;
			}

			Layer.Pixels = Animation.Frames[Animation.Order[0]];
			Layer.Animation = NewAnimations.Num();
			NewAnimations.Add(MoveTemp(Animation));
			++NewStats.Animated;
			if (Texture.FrameWidth != Size || Texture.FrameHeight != Size)
			{
				NewStats.Resized.Add(FString::Printf(TEXT("%s %dx%d frames"), *Ids[Index].ToString(), Texture.FrameWidth, Texture.FrameHeight));
			}
		}
		else
		{
			Layer.Pixels = MoveTemp(Texture.Frames[0]);
			if (Texture.SourceWidth != Size || Texture.SourceHeight != Size)
			{
				NewStats.Resized.Add(FString::Printf(TEXT("%s %dx%d"), *Ids[Index].ToString(), Texture.SourceWidth, Texture.SourceHeight));
			}
		}
	}

	// The flowing pictures' "#side" layers, after the loaded ones, moving with their frames.
	for (int32 Index = 0; Index < Ids.Num(); ++Index)
	{
		FPrepared& Texture = Prepared[Index];
		const int32* Base = NewLayersById.Find(Ids[Index]);
		if (Texture.SideFrames.IsEmpty() || !Base || NewLayers[*Base].Animation == INDEX_NONE)
		{
			continue;
		}
		FAnimation Animation = NewAnimations[NewLayers[*Base].Animation];
		const int32 LayerIndex = NewLayers.Num();
		Animation.Layer = LayerIndex;
		Animation.Frames = MoveTemp(Texture.SideFrames);
		FLayer& Layer = NewLayers.AddDefaulted_GetRef();
		Layer.Id = FName(Ids[Index].ToString() + TEXT("#side"));
		Layer.bFromPack = Texture.bFromPack;
		for (const TArray<FColor>& Frame : Animation.Frames)
		{
			MeasureAlpha(Frame, Layer.bTranslucent, Layer.bCutout);
		}
		Layer.Pixels = Animation.Frames[Animation.Order[0]];
		Layer.Animation = NewAnimations.Num();
		NewLayersById.Add(Layer.Id, LayerIndex);
		NewAnimations.Add(MoveTemp(Animation));
		++NewStats.SideLayers;
	}

	// The array: mip m holds every layer's m-th mip one after the other. The mips are filled straight into the bulk data
	// (at a pack's size they are hundreds of megabytes).
	const int32 NumLayers = NewLayers.Num();
	UTexture2DArray* Array = NewObject<UTexture2DArray>(GetTransientPackage(), TEXT("McBlockTextureArray"), RF_Transient);
	Array->SetPlatformData(new FTexturePlatformData());
	FTexturePlatformData* PlatformData = Array->GetPlatformData();
	PlatformData->SizeX = Size;
	PlatformData->SizeY = Size;
	PlatformData->SetNumSlices(NumLayers);
	PlatformData->PixelFormat = PF_B8G8R8A8;
	Array->bNotOfflineProcessed = true;
	TArray<uint8*> MipData;
	TArray<int32> MipBytes;
	for (int32 Mip = 0; Mip < MipCount; ++Mip)
	{
		const int32 Dim = Size >> Mip;
		FTexture2DMipMap* MipMap = new FTexture2DMipMap(Dim, Dim, NumLayers);
		PlatformData->Mips.Add(MipMap);
		MipMap->BulkData.Lock(LOCK_READ_WRITE);
		MipBytes.Add(Dim * Dim * 4);
		MipData.Add(static_cast<uint8*>(MipMap->BulkData.Realloc(static_cast<int64>(NumLayers) * Dim * Dim * 4)));
	}
	ParallelFor(NumLayers, [&](int32 LayerIndex)
	{
		TArray<uint8> Chain;
		BuildMips(NewLayers[LayerIndex].Pixels, Size, Chain);
		int32 Offset = 0;
		for (int32 Mip = 0; Mip < MipCount; ++Mip)
		{
			FMemory::Memcpy(MipData[Mip] + static_cast<int64>(LayerIndex) * MipBytes[Mip], Chain.GetData() + Offset, MipBytes[Mip]);
			Offset += MipBytes[Mip];
		}
	});
	for (int32 Mip = 0; Mip < MipCount; ++Mip)
	{
		PlatformData->Mips[Mip].BulkData.Unlock();
	}
	Array->SRGB = true;
	if (Size <= LayerSize)
	{
		Array->Filter = TF_Nearest;
		Array->LODGroup = TEXTUREGROUP_Pixels2D;
	}
	else
	{
		// A pack's pictures are filtered like any other texture of the project (trilinear, anisotropic by r.MaxAnisotropy).
		Array->Filter = TF_Default;
		Array->LODGroup = TEXTUREGROUP_World;
	}
	Array->AddressX = TA_Clamp;
	Array->AddressY = TA_Clamp;
	Array->NeverStream = true;
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
	int64 ArrayBytes = 0;
	for (const int32 Bytes : MipBytes)
	{
		ArrayBytes += static_cast<int64>(Bytes) * NumLayers;
	}
	NewStats.ArrayBytes = ArrayBytes;

	ArraySize = Size;
	Layers = MoveTemp(NewLayers);
	Animations = MoveTemp(NewAnimations);
	LayersById = MoveTemp(NewLayersById);
	RenderState = MakeShared<FRenderState, ESPMode::ThreadSafe>();
	Stats = MoveTemp(NewStats);
	SourceKey = McPacks::GetSignature() + (bPbr ? TEXT("pbr") : TEXT("")) + FString::Printf(TEXT("|%d"), Size);
	TextureArray = Array;

	UE_LOG(LogMcAssets, Log, TEXT("Loaded %d of %d block textures into %d layers of %dx%d (%d animated, %d cutout, %d translucent, %d from resource packs, %d with PBR maps), %.0f MB, in %.0f ms"),
		Stats.Loaded, Stats.Ids, Stats.Layers, Size, Size, Stats.Animated, Stats.Cutout, Stats.Translucent, Stats.FromPacks, Stats.PbrMaps,
		static_cast<double>(Stats.ArrayBytes) / (1024.0 * 1024.0), (FPlatformTime::Seconds() - StartTime) * 1000.0);
	if (Stats.WantedSize > Size)
	{
		UE_LOG(LogMcAssets, Warning, TEXT("  the packs bring %dx%d textures; limited to %dx%d by -McPackMaxSize=%d and the %lld MB budget (-McPackBudgetMB)"),
			Stats.WantedSize, Stats.WantedSize, Size, Size, McPacks::GetMaxTextureSize(), McPacks::GetBudgetBytes() / (1024 * 1024));
	}
	for (const FName Fallback : Stats.Fallbacks)
	{
		UE_LOG(LogMcAssets, Warning, TEXT("  %s can't be loaded; using the missing texture"), *Fallback.ToString());
	}
	for (int32 Index = 0; Index < Stats.Resized.Num() && Index < 20; ++Index)
	{
		UE_LOG(LogMcAssets, Log, TEXT("  %s scaled to %dx%d"), *Stats.Resized[Index], Size, Size);
	}
	if (Stats.Resized.Num() > 20)
	{
		UE_LOG(LogMcAssets, Log, TEXT("  ... and %d more scaled to %dx%d"), Stats.Resized.Num() - 20, Size, Size);
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
		BuildMips(Layer.Pixels, ArraySize, Upload.Chain);
		if (OnLayerAnimated)
		{
			OnLayerAnimated(Animation.Layer);
		}
	}

	if (Uploads.Num() == 0 || !TextureArray->GetResource())
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(McUploadAnimatedTextures)(
		[Resource = TextureArray->GetResource(), State = RenderState, Uploads = MoveTemp(Uploads), Side = ArraySize](FRHICommandListImmediate& RHICmdList)
		{
			if (!Resource->TextureRHI.IsValid())
			{
				return;
			}
			if (!State->Staging.IsValid())
			{
				State->Staging = RHICmdList.CreateTexture(FRHITextureCreateDesc::Create2D(TEXT("McAnimationStaging"), Side, Side, PF_B8G8R8A8)
					.SetNumMips(NumMipsFor(Side))
					.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::SRGB)
					.SetInitialState(ERHIAccess::SRVMask));
			}

			for (const FUpload& Upload : Uploads)
			{
				int32 Offset = 0;
				for (int32 Mip = 0; Mip < NumMipsFor(Side); ++Mip)
				{
					const uint32 Dim = Side >> Mip;
					RHICmdList.UpdateTexture2D(State->Staging, Mip, FUpdateTextureRegion2D(0, 0, 0, 0, Dim, Dim), Dim * 4, Upload.Chain.GetData() + Offset);
					Offset += Dim * Dim * 4;
				}

				FRHICopyTextureInfo Copy;
				Copy.DestSliceIndex = Upload.Layer;
				Copy.NumMips = NumMipsFor(Side);
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

void FMcBlockTextures::Unload()
{
	check(IsInGameThread());
	Shutdown();
	OnLayerAnimated = nullptr;
	TextureArray = nullptr;
	Layers.Empty();
	Animations.Empty();
	LayersById.Empty();
	Stats = FMcTextureStats();
	ArraySize = TextureSize;
	SourceKey.Empty();
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
