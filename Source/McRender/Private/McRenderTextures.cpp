#include "McRenderTextures.h"
#include "McRender.h"
#include "McCache.h"
#include "Engine/Texture2DArray.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

bool McRenderTextures::LoadPngPixels(const FString& Path, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight, int32 MaxSide)
{
	TArray<uint8> File;
	IImageWrapperModule& ImageModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = ImageModule.CreateImageWrapper(EImageFormat::PNG);
	TArray64<uint8> Raw;
	if (!FFileHelper::LoadFileToArray(File, *Path) || !Wrapper.IsValid() || !Wrapper->SetCompressed(File.GetData(), File.Num())
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

UTexture2DArray* McRenderTextures::LoadPngAsArray(UObject* Outer, const FString& Path, int32& OutWidth, int32& OutHeight, bool bWhite)
{
	TArray<FColor> Pixels;
	if (!LoadPngPixels(Path, Pixels, OutWidth, OutHeight))
	{
		return nullptr;
	}
	if (bWhite)
	{
		// The colour channels go to white, the alpha stays.
		for (FColor& Pixel : Pixels)
		{
			Pixel = FColor(255, 255, 255, Pixel.A);
		}
	}
	const McCache::FTextureMeta Meta = McCache::ReadTextureMeta(Path);
	return MakeArray(Outer, Pixels, OutWidth, OutHeight, Meta.bBlur, !Meta.bHasClamp || Meta.bClamp);
}
