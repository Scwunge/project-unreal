#include "McRenderTextures.h"
#include "McRender.h"
#include "McCache.h"
#include "Engine/Texture2DArray.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Tasks/Task.h"

namespace
{
	uint32 ReadBigEndian(const uint8* Data)
	{
		return (static_cast<uint32>(Data[0]) << 24) | (static_cast<uint32>(Data[1]) << 16) | (static_cast<uint32>(Data[2]) << 8) | Data[3];
	}

	// The engine's PNG reader turns a 1, 2 or 4 bit palette picture (a few villager clothes) into opaque black, so those
	// are read here: the palette, its transparency and the filtered rows. False when the file is anything else.
	bool LoadLowDepthPalettePng(const TArray<uint8>& File, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		static const uint8 Signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
		if (File.Num() < 33 || FMemory::Memcmp(File.GetData(), Signature, 8) != 0)
		{
			return false;
		}
		int32 Width = 0, Height = 0, Depth = 0;
		TArray<FColor> Palette;
		TArray<uint8> Alpha, Packed;
		bool bHeader = false;
		for (int32 At = 8; At + 12 <= File.Num();)
		{
			const int32 Length = static_cast<int32>(ReadBigEndian(&File[At]));
			const uint8* Type = &File[At + 4];
			if (Length < 0 || At + 12 + Length > File.Num())
			{
				return false;
			}
			const uint8* Data = &File[At + 8];
			if (FMemory::Memcmp(Type, "IHDR", 4) == 0 && Length == 13)
			{
				Width = static_cast<int32>(ReadBigEndian(Data));
				Height = static_cast<int32>(ReadBigEndian(Data + 4));
				Depth = Data[8];
				// Only a plain (not interlaced) palette picture below 8 bits.
				if (Depth >= 8 || Data[9] != 3 || Data[12] != 0 || Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384)
				{
					return false;
				}
				bHeader = true;
			}
			else if (FMemory::Memcmp(Type, "PLTE", 4) == 0)
			{
				for (int32 Index = 0; Index + 2 < Length; Index += 3)
				{
					Palette.Add(FColor(Data[Index], Data[Index + 1], Data[Index + 2], 255));
				}
			}
			else if (FMemory::Memcmp(Type, "tRNS", 4) == 0)
			{
				Alpha.Append(Data, Length);
			}
			else if (FMemory::Memcmp(Type, "IDAT", 4) == 0)
			{
				Packed.Append(Data, Length);
			}
			At += 12 + Length;
		}
		if (!bHeader || Palette.IsEmpty() || Packed.IsEmpty())
		{
			return false;
		}
		for (int32 Index = 0; Index < Palette.Num() && Index < Alpha.Num(); ++Index)
		{
			Palette[Index].A = Alpha[Index];
		}
		const int32 RowBytes = (Width * Depth + 7) / 8;
		TArray<uint8> Rows;
		Rows.SetNumUninitialized((RowBytes + 1) * Height);
		if (!FCompression::UncompressMemory(NAME_Zlib, Rows.GetData(), Rows.Num(), Packed.GetData(), Packed.Num()))
		{
			return false;
		}
		// Undo each row's filter; a row is a byte per sample of its own length, so the left neighbour is one byte back.
		TArray<uint8> Raw;
		Raw.SetNumZeroed(RowBytes * Height);
		for (int32 Row = 0; Row < Height; ++Row)
		{
			const uint8 Filter = Rows[Row * (RowBytes + 1)];
			const uint8* In = &Rows[Row * (RowBytes + 1) + 1];
			uint8* Out = &Raw[Row * RowBytes];
			const uint8* Above = Row > 0 ? Out - RowBytes : nullptr;
			for (int32 Byte = 0; Byte < RowBytes; ++Byte)
			{
				const int32 Left = Byte > 0 ? Out[Byte - 1] : 0;
				const int32 Up = Above ? Above[Byte] : 0;
				const int32 UpLeft = Above && Byte > 0 ? Above[Byte - 1] : 0;
				int32 Predictor = 0;
				switch (Filter)
				{
				case 1: Predictor = Left; break;
				case 2: Predictor = Up; break;
				case 3: Predictor = (Left + Up) / 2; break;
				case 4:
				{
					const int32 Estimate = Left + Up - UpLeft;
					const int32 DistLeft = FMath::Abs(Estimate - Left), DistUp = FMath::Abs(Estimate - Up), DistUpLeft = FMath::Abs(Estimate - UpLeft);
					Predictor = DistLeft <= DistUp && DistLeft <= DistUpLeft ? Left : DistUp <= DistUpLeft ? Up : UpLeft;
					break;
				}
				default: break;
				}
				Out[Byte] = static_cast<uint8>(In[Byte] + Predictor);
			}
		}
		OutWidth = Width;
		OutHeight = Height;
		OutPixels.SetNumUninitialized(Width * Height);
		const int32 Mask = (1 << Depth) - 1;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Bit = X * Depth;
				const int32 Index = (Raw[Y * RowBytes + Bit / 8] >> (8 - Depth - Bit % 8)) & Mask;
				OutPixels[Y * Width + X] = Palette.IsValidIndex(Index) ? Palette[Index] : FColor(0, 0, 0, 0);
			}
		}
		return true;
	}
}

namespace
{
	// Reads and decodes one png (any thread, once the ImageWrapper module is loaded). The file is read once.
	bool DecodePngFile(const FString& Path, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		TArray<uint8> File;
		const bool bRead = FFileHelper::LoadFileToArray(File, *Path);
		if (!bRead || !LoadLowDepthPalettePng(File, OutPixels, OutWidth, OutHeight))
		{
			// Off the game thread the module can only be looked up (PrefetchEntityPictures loads it first).
			IImageWrapperModule* ImageModule = IsInGameThread() ? &FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"))
				: FModuleManager::GetModulePtr<IImageWrapperModule>(TEXT("ImageWrapper"));
			const TSharedPtr<IImageWrapper> Wrapper = ImageModule ? ImageModule->CreateImageWrapper(EImageFormat::PNG) : TSharedPtr<IImageWrapper>();
			TArray64<uint8> Raw;
			if (!bRead || !Wrapper.IsValid() || !Wrapper->SetCompressed(File.GetData(), File.Num())
				|| !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
			{
				UE_LOG(LogMcRender, Error, TEXT("Can't load the picture %s"), *Path);
				return false;
			}
			OutWidth = Wrapper->GetWidth();
			OutHeight = Wrapper->GetHeight();
			// FColor is BGRA in memory, as the raw data is.
			OutPixels.SetNumUninitialized(OutWidth * OutHeight);
			FMemory::Memcpy(OutPixels.GetData(), Raw.GetData(), FMath::Min<int64>(Raw.Num(), OutPixels.Num() * sizeof(FColor)));
		}
		return true;
	}

	// Entity pictures decoded ahead of use on background threads (PrefetchEntityPictures), by full path. Taken out on use.
	struct FPrefetchedPicture
	{
		TArray<FColor> Pixels;
		int32 Width = 0;
		int32 Height = 0;
	};
	FCriticalSection PrefetchLock;
	TMap<FString, FPrefetchedPicture> Prefetched;
	bool bPrefetchStarted = false;

	bool TakePrefetched(const FString& Path, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
	{
		FScopeLock Lock(&PrefetchLock);
		FPrefetchedPicture Picture;
		if (!Prefetched.RemoveAndCopyValue(Path, Picture))
		{
			return false;
		}
		OutPixels = MoveTemp(Picture.Pixels);
		OutWidth = Picture.Width;
		OutHeight = Picture.Height;
		return true;
	}
}

void McRenderTextures::PrefetchEntityPictures()
{
	if (bPrefetchStarted)
	{
		return;
	}
	bPrefetchStarted = true;
	// The engine's png reader is a module: loaded here, on the game thread, before any worker wants it.
	FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const FString Folder = McCache::Asset(TEXT("textures/entity"));
	UE::Tasks::Launch(UE_SOURCE_LOCATION, [Folder]()
	{
		const double Began = FPlatformTime::Seconds();
		TArray<FString> Files;
		IFileManager::Get().FindFilesRecursive(Files, *Folder, TEXT("*.png"), true, false);
		// Two readers: on a slow disk most of a picture's first use is waiting for the file, not decoding it.
		const int32 Readers = 2;
		TArray<UE::Tasks::FTask> Tasks;
		for (int32 Reader = 0; Reader < Readers; ++Reader)
		{
			Tasks.Add(UE::Tasks::Launch(UE_SOURCE_LOCATION, [&Files, Reader, Readers]()
			{
				for (int32 Index = Reader; Index < Files.Num(); Index += Readers)
				{
					FPrefetchedPicture Picture;
					FString Path = Files[Index];
					FPaths::NormalizeFilename(Path);
					if (DecodePngFile(Path, Picture.Pixels, Picture.Width, Picture.Height))
					{
						FScopeLock Lock(&PrefetchLock);
						Prefetched.Add(MoveTemp(Path), MoveTemp(Picture));
					}
				}
			}, LowLevelTasks::ETaskPriority::BackgroundNormal));
		}
		UE::Tasks::Wait(Tasks);
		UE_LOG(LogMcRender, Display, TEXT("Entity pictures read ahead: %d in %.1f s"), Files.Num(), FPlatformTime::Seconds() - Began);
	}, LowLevelTasks::ETaskPriority::BackgroundNormal);
}

bool McRenderTextures::LoadPngPixels(const FString& Path, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight, int32 MaxSide)
{
	FString Normal = Path;
	FPaths::NormalizeFilename(Normal);
	if (!TakePrefetched(Normal, OutPixels, OutWidth, OutHeight) && !DecodePngFile(Path, OutPixels, OutWidth, OutHeight))
	{
		return false;
	}

	// A resource pack's picture can be far wider than the 16 pixels it replaces. Whole blocks of Factor x Factor pixels
	// become one, so a thin line stays (its alpha is the largest of the block's) and the colours are averaged.
	const int32 Factor = MaxSide > 0 ? OutWidth / MaxSide : 1;
	if (Factor > 1 && OutWidth % Factor == 0 && OutHeight % Factor == 0)
	{
		const int32 NewWidth = OutWidth / Factor;
		const int32 NewHeight = OutHeight / Factor;
		TArray<FColor> Small;
		Small.SetNumUninitialized(NewWidth * NewHeight);
		for (int32 Y = 0; Y < NewHeight; ++Y)
		{
			for (int32 X = 0; X < NewWidth; ++X)
			{
				int32 R = 0, G = 0, B = 0, A = 0;
				for (int32 Dy = 0; Dy < Factor; ++Dy)
				{
					for (int32 Dx = 0; Dx < Factor; ++Dx)
					{
						const FColor& Pixel = OutPixels[(Y * Factor + Dy) * OutWidth + X * Factor + Dx];
						R += Pixel.R;
						G += Pixel.G;
						B += Pixel.B;
						A = FMath::Max<int32>(A, Pixel.A);
					}
				}
				const int32 Count = Factor * Factor;
				Small[Y * NewWidth + X] = FColor(static_cast<uint8>(R / Count), static_cast<uint8>(G / Count), static_cast<uint8>(B / Count), static_cast<uint8>(A));
			}
		}
		OutPixels = MoveTemp(Small);
		OutWidth = NewWidth;
		OutHeight = NewHeight;
	}
	return true;
}

UTexture2DArray* McRenderTextures::MakeArray(UObject* Outer, const TArray<FColor>& Pixels, int32 Width, int32 Height, bool bBlur, bool bClamp)
{
	UTexture2DArray* Array = NewObject<UTexture2DArray>(Outer, NAME_None, RF_Transient);
	Array->SetPlatformData(new FTexturePlatformData());
	FTexturePlatformData* PlatformData = Array->GetPlatformData();
	PlatformData->SizeX = Width;
	PlatformData->SizeY = Height;
	PlatformData->SetNumSlices(1);
	PlatformData->PixelFormat = PF_B8G8R8A8;
	Array->bNotOfflineProcessed = true;
	FTexture2DMipMap* MipMap = new FTexture2DMipMap(Width, Height, 1);
	PlatformData->Mips.Add(MipMap);
	MipMap->BulkData.Lock(LOCK_READ_WRITE);
	void* Destination = MipMap->BulkData.Realloc(Pixels.Num() * sizeof(FColor));
	FMemory::Memcpy(Destination, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	MipMap->BulkData.Unlock();
	Array->SRGB = true;
	Array->Filter = bBlur ? TF_Bilinear : TF_Nearest;
	Array->AddressX = bClamp ? TA_Clamp : TA_Wrap;
	Array->AddressY = bClamp ? TA_Clamp : TA_Wrap;
	Array->NeverStream = true;
	Array->LODGroup = TEXTUREGROUP_Pixels2D;
#if WITH_EDITORONLY_DATA
	Array->MipGenSettings = TMGS_LeaveExistingMips;
#endif
	Array->UpdateResource();
	return Array;
}

void McRenderTextures::CompositeOver(TArray<FColor>& Under, const TArray<FColor>& Over)
{
	const int32 Count = FMath::Min(Under.Num(), Over.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FColor Top = Over[Index];
		if (Top.A == 0)
		{
			continue;
		}
		FColor& Bottom = Under[Index];
		if (Top.A == 255 || Bottom.A == 0)
		{
			Bottom = Top;
			continue;
		}
		const float TopAlpha = Top.A / 255.0f;
		const float BottomAlpha = Bottom.A / 255.0f * (1.0f - TopAlpha);
		const float Alpha = TopAlpha + BottomAlpha;
		const auto Mix = [&](uint8 Above, uint8 Below) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32((Above * TopAlpha + Below * BottomAlpha) / Alpha), 0, 255)); };
		Bottom = FColor(Mix(Top.R, Bottom.R), Mix(Top.G, Bottom.G), Mix(Top.B, Bottom.B), static_cast<uint8>(FMath::Clamp(FMath::RoundToInt32(Alpha * 255.0f), 0, 255)));
	}
}

UTexture2DArray* McRenderTextures::LoadPngLayersAsArray(UObject* Outer, const TArray<FString>& Paths, int32& OutWidth, int32& OutHeight)
{
	TArray<FColor> Pixels;
	bool bFirst = true;
	for (const FString& Path : Paths)
	{
		if (Path.IsEmpty())
		{
			continue;
		}
		TArray<FColor> Layer;
		int32 Width = 0, Height = 0;
		if (!LoadPngPixels(Path, Layer, Width, Height))
		{
			if (bFirst)
			{
				return nullptr;
			}
			continue;
		}
		if (bFirst)
		{
			Pixels = MoveTemp(Layer);
			OutWidth = Width;
			OutHeight = Height;
			bFirst = false;
		}
		else if (Width == OutWidth && Height == OutHeight)
		{
			CompositeOver(Pixels, Layer);
		}
	}
	return bFirst ? nullptr : MakeArray(Outer, Pixels, OutWidth, OutHeight);
}

UTexture2DArray* McRenderTextures::LoadPngAsArray(UObject* Outer, const FString& Path, int32& OutWidth, int32& OutHeight, bool bWhite)
{
#if !UE_BUILD_SHIPPING
	const double Began = FPlatformTime::Seconds();
#endif
	TArray<FColor> Pixels;
	if (!LoadPngPixels(Path, Pixels, OutWidth, OutHeight))
	{
		return nullptr;
	}
#if !UE_BUILD_SHIPPING
	const double Decoded = FPlatformTime::Seconds();
#endif
	if (bWhite)
	{
		// The colour channels go to white, the alpha stays.
		for (FColor& Pixel : Pixels)
		{
			Pixel = FColor(255, 255, 255, Pixel.A);
		}
	}
	const McCache::FTextureMeta Meta = McCache::ReadTextureMeta(Path);
#if !UE_BUILD_SHIPPING
	const double Meta_ = FPlatformTime::Seconds();
#endif
	UTexture2DArray* Array = MakeArray(Outer, Pixels, OutWidth, OutHeight, Meta.bBlur, !Meta.bHasClamp || Meta.bClamp);
#if !UE_BUILD_SHIPPING
	const double Done = FPlatformTime::Seconds();
	if (Done - Began > 0.005)
	{
		// Development: where a slow picture load went (mc-playable's travel runs, first mob of a kind).
		UE_LOG(LogMcRender, Display, TEXT("McSlowTexture: %s %dx%d: decode %.1f ms, meta %.1f ms, array %.1f ms"), *Path, OutWidth, OutHeight,
			(Decoded - Began) * 1000.0, (Meta_ - Decoded) * 1000.0, (Done - Meta_) * 1000.0);
	}
#endif
	return Array;
}
