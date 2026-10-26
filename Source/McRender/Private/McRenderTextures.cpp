#include "McRenderTextures.h"
#include "McRender.h"
#include "Engine/Texture2DArray.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

UTexture2DArray* McRenderTextures::LoadPngAsArray(UObject* Outer, const FString& Path, int32& OutWidth, int32& OutHeight)
{
	TArray<uint8> File;
	IImageWrapperModule& ImageModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = ImageModule.CreateImageWrapper(EImageFormat::PNG);
	TArray64<uint8> Raw;
	if (!FFileHelper::LoadFileToArray(File, *Path) || !Wrapper.IsValid() || !Wrapper->SetCompressed(File.GetData(), File.Num())
		|| !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		UE_LOG(LogMcRender, Error, TEXT("Can't load the picture %s"), *Path);
		return nullptr;
	}
	OutWidth = Wrapper->GetWidth();
	OutHeight = Wrapper->GetHeight();

	UTexture2DArray* Array = NewObject<UTexture2DArray>(Outer, NAME_None, RF_Transient);
	Array->SetPlatformData(new FTexturePlatformData());
	FTexturePlatformData* PlatformData = Array->GetPlatformData();
	PlatformData->SizeX = OutWidth;
	PlatformData->SizeY = OutHeight;
	PlatformData->SetNumSlices(1);
	PlatformData->PixelFormat = PF_B8G8R8A8;
	Array->bNotOfflineProcessed = true;
	FTexture2DMipMap* MipMap = new FTexture2DMipMap(OutWidth, OutHeight, 1);
	PlatformData->Mips.Add(MipMap);
	MipMap->BulkData.Lock(LOCK_READ_WRITE);
	void* Destination = MipMap->BulkData.Realloc(Raw.Num());
	FMemory::Memcpy(Destination, Raw.GetData(), Raw.Num());
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
