#include "McRenderTextures.h"
#include "McRender.h"
#include "Engine/Texture2DArray.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

bool McRenderTextures::LoadPngPixels(const FString& Path, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
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
	return true;
}

UTexture2DArray* McRenderTextures::MakeArray(UObject* Outer, const TArray<FColor>& Pixels, int32 Width, int32 Height)
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
	Array->Filter = TF_Nearest;
	Array->AddressX = TA_Clamp;
	Array->AddressY = TA_Clamp;
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
	return MakeArray(Outer, Pixels, OutWidth, OutHeight);
}
