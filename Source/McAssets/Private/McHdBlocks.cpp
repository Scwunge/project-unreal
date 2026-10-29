#include "McHdBlocks.h"
#include "McAssets.h"
#include "McBlockTextures.h"
#include "McCache.h"
#include "Async/ParallelFor.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2DArray.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "TextureResource.h"

struct FMcHdBlocks::FRenderState
{
	// Render thread only: one layer's mip chain, copied into the array (albedo, normal, specular).
	FTextureRHIRef Staging[3];
};

namespace
{
	// A mip chain: every level after the base halves each side, and the whole chain is stored one level after the other.
	// bWeighted: colours are averaged by alpha, so a cutout's edge doesn't darken.
	void MakeMipChain(const FColor* Base, int32 Width, int32 Height, bool bWeighted, TArray<FColor>& Out)
	{
		Out.Reset();
		Out.Append(Base, Width * Height);
		int32 PrevOffset = 0;
		int32 PrevWidth = Width;
		int32 PrevHeight = Height;
		while (PrevWidth > 1 || PrevHeight > 1)
		{
			const int32 NewWidth = FMath::Max(PrevWidth / 2, 1);
			const int32 NewHeight = FMath::Max(PrevHeight / 2, 1);
			const int32 NewOffset = Out.Num();
			Out.AddUninitialized(NewWidth * NewHeight);
			for (int32 Y = 0; Y < NewHeight; ++Y)
			{
				for (int32 X = 0; X < NewWidth; ++X)
				{
					int32 Sum[4] = { 0, 0, 0, 0 };
					int32 Weight = 0;
					int32 Count = 0;
					for (int32 Dy = 0; Dy < 2; ++Dy)
					{
						for (int32 Dx = 0; Dx < 2; ++Dx)
						{
							const int32 Sx = FMath::Min(X * 2 + Dx, PrevWidth - 1);
							const int32 Sy = FMath::Min(Y * 2 + Dy, PrevHeight - 1);
							const FColor& Pixel = Out[PrevOffset + Sy * PrevWidth + Sx];
							const int32 W = bWeighted ? Pixel.A : 1;
							Sum[0] += Pixel.R * W;
							Sum[1] += Pixel.G * W;
							Sum[2] += Pixel.B * W;
							Sum[3] += Pixel.A;
							Weight += W;
							++Count;
						}
					}
					FColor& Pixel = Out[NewOffset + Y * NewWidth + X];
					if (Weight == 0)
					{
						Pixel = FColor(0, 0, 0, 0);
					}
					else
					{
						Pixel = FColor(static_cast<uint8>((Sum[0] + Weight / 2) / Weight), static_cast<uint8>((Sum[1] + Weight / 2) / Weight),
							static_cast<uint8>((Sum[2] + Weight / 2) / Weight), static_cast<uint8>((Sum[3] + Count / 2) / Count));
					}
				}
			}
			PrevOffset = NewOffset;
			PrevWidth = NewWidth;
			PrevHeight = NewHeight;
		}
	}

	bool LoadHdPng(const FString& Path, McHd::FPicture& Out)
	{
		TArray<uint8> File;
		IImageWrapperModule& ImageModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = ImageModule.CreateImageWrapper(EImageFormat::PNG);
		TArray64<uint8> Raw;
		if (!FFileHelper::LoadFileToArray(File, *Path) || !Wrapper.IsValid() || !Wrapper->SetCompressed(File.GetData(), File.Num())
			|| !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
		{
			return false;
		}
		Out.Width = Wrapper->GetWidth();
		Out.Height = Wrapper->GetHeight();
		if (Out.Width <= 0 || Out.Height <= 0 || Raw.Num() < static_cast<int64>(Out.Width) * Out.Height * 4)
		{
			return false;
		}
		Out.Pixels.SetNumUninitialized(Out.Width * Out.Height);
		FMemory::Memcpy(Out.Pixels.GetData(), Raw.GetData(), static_cast<int64>(Out.Width) * Out.Height * 4);
		return true;
	}

	int32 GetHdSize()
	{
		int32 Size = 64;
		FParse::Value(FCommandLine::Get(), TEXT("-McHdSize="), Size);
		return FMath::RoundUpToPowerOfTwo(FMath::Clamp(Size, 16, 512));
	}

	McHd::FPicture MakeSource(TConstArrayView<FColor> Pixels, int32 Size)
	{
		McHd::FPicture Source;
		Source.Width = Source.Height = Size;
		Source.Pixels = TArray<FColor>(Pixels.GetData(), Pixels.Num());
		return Source;
	}

	constexpr uint32 CacheMagic = 0x4448434d;  // "MCHD"
}

FMcHdBlocks& FMcHdBlocks::Get()
{
	// Never destroyed, like FMcBlockTextures.
	static FMcHdBlocks* Instance = new FMcHdBlocks;
	return *Instance;
}

void FMcHdBlocks::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Albedo);
	Collector.AddReferencedObject(Normal);
	Collector.AddReferencedObject(Specular);
	for (TPair<FString, FMcHdPicture>& Pair : Pictures)
	{
		Collector.AddReferencedObject(Pair.Value.Albedo);
		Collector.AddReferencedObject(Pair.Value.Normal);
		Collector.AddReferencedObject(Pair.Value.Specular);
	}
	for (TPair<FString, TObjectPtr<UTexture2DArray>>& Pair : WhitePictures)
	{
		Collector.AddReferencedObject(Pair.Value);
	}
}

UTexture2DArray* FMcHdBlocks::MakeArray(int32 Width, int32 Height, int32 NumLayers, bool bSrgb, TConstArrayView<FColor> Base)
{
	check(Base.Num() == Width * Height * NumLayers);
	const int32 NumMips = FMath::FloorLog2(FMath::Max(Width, Height)) + 1;
	TArray<TArray<FColor>> Mips;
	Mips.SetNum(NumMips);
	TArray<int32> MipWidths, MipHeights;
	for (int32 Mip = 0; Mip < NumMips; ++Mip)
	{
		MipWidths.Add(FMath::Max(Width >> Mip, 1));
		MipHeights.Add(FMath::Max(Height >> Mip, 1));
		Mips[Mip].SetNumUninitialized(NumLayers * MipWidths[Mip] * MipHeights[Mip]);
	}
	ParallelFor(NumLayers, [&](int32 Layer)
	{
		TArray<FColor> Chain;
		MakeMipChain(Base.GetData() + static_cast<int64>(Layer) * Width * Height, Width, Height, bSrgb, Chain);
		int32 Offset = 0;
		for (int32 Mip = 0; Mip < NumMips; ++Mip)
		{
			const int32 Count = MipWidths[Mip] * MipHeights[Mip];
			FMemory::Memcpy(Mips[Mip].GetData() + static_cast<int64>(Layer) * Count, Chain.GetData() + Offset, Count * sizeof(FColor));
			Offset += Count;
		}
	});

	UTexture2DArray* Array = NewObject<UTexture2DArray>(GetTransientPackage(), NAME_None, RF_Transient);
	Array->SetPlatformData(new FTexturePlatformData());
	FTexturePlatformData* PlatformData = Array->GetPlatformData();
	PlatformData->SizeX = Width;
	PlatformData->SizeY = Height;
	PlatformData->SetNumSlices(NumLayers);
	PlatformData->PixelFormat = PF_B8G8R8A8;
	Array->bNotOfflineProcessed = true;
	for (int32 Mip = 0; Mip < NumMips; ++Mip)
	{
		FTexture2DMipMap* MipMap = new FTexture2DMipMap(MipWidths[Mip], MipHeights[Mip], NumLayers);
		PlatformData->Mips.Add(MipMap);
		MipMap->BulkData.Lock(LOCK_READ_WRITE);
		void* Destination = MipMap->BulkData.Realloc(static_cast<int64>(Mips[Mip].Num()) * sizeof(FColor));
		FMemory::Memcpy(Destination, Mips[Mip].GetData(), static_cast<int64>(Mips[Mip].Num()) * sizeof(FColor));
		MipMap->BulkData.Unlock();
	}
	Array->SRGB = bSrgb;
	// The project's own filter (trilinear, anisotropic by r.MaxAnisotropy), not nearest.
	Array->Filter = TF_Default;
	Array->AddressX = TA_Wrap;
	Array->AddressY = TA_Wrap;
	Array->NeverStream = true;
	Array->LODGroup = TEXTUREGROUP_World;
#if WITH_EDITORONLY_DATA
	Array->MipGenSettings = TMGS_LeaveExistingMips;
#endif
	Array->UpdateResource();
	return Array;
}

void FMcHdBlocks::ReadTags()
{
	if (Tags.Num() > 0)
	{
		return;
	}
	// The tags that name a class, in the order that wins (the first to name a block keeps it).
	static const struct { const TCHAR* Tag; McHd::EMaterialClass Class; } Lists[] = {
		{ TEXT("coal_ores"), McHd::EMaterialClass::Ore }, { TEXT("iron_ores"), McHd::EMaterialClass::Ore },
		{ TEXT("gold_ores"), McHd::EMaterialClass::Ore }, { TEXT("copper_ores"), McHd::EMaterialClass::Ore },
		{ TEXT("diamond_ores"), McHd::EMaterialClass::Ore }, { TEXT("emerald_ores"), McHd::EMaterialClass::Ore },
		{ TEXT("lapis_ores"), McHd::EMaterialClass::Ore }, { TEXT("redstone_ores"), McHd::EMaterialClass::Ore },
		{ TEXT("leaves"), McHd::EMaterialClass::Leaves }, { TEXT("wool"), McHd::EMaterialClass::Wool },
		{ TEXT("wool_carpets"), McHd::EMaterialClass::Wool }, { TEXT("logs"), McHd::EMaterialClass::Wood },
		{ TEXT("planks"), McHd::EMaterialClass::Wood }, { TEXT("wooden_slabs"), McHd::EMaterialClass::Wood },
		{ TEXT("wooden_stairs"), McHd::EMaterialClass::Wood }, { TEXT("wooden_doors"), McHd::EMaterialClass::Wood },
		{ TEXT("wooden_fences"), McHd::EMaterialClass::Wood }, { TEXT("bamboo_blocks"), McHd::EMaterialClass::Wood },
		{ TEXT("dirt"), McHd::EMaterialClass::Soil }, { TEXT("sand"), McHd::EMaterialClass::Soil },
		{ TEXT("flowers"), McHd::EMaterialClass::Plant }, { TEXT("crops"), McHd::EMaterialClass::Plant },
		{ TEXT("saplings"), McHd::EMaterialClass::Plant }, { TEXT("impermeable"), McHd::EMaterialClass::Glass },
		{ TEXT("base_stone_overworld"), McHd::EMaterialClass::Stone }, { TEXT("base_stone_nether"), McHd::EMaterialClass::Stone },
		{ TEXT("stone_bricks"), McHd::EMaterialClass::Stone }, { TEXT("terracotta"), McHd::EMaterialClass::Stone },
	};
	for (const auto& List : Lists)
	{
		TArray<FString> Pending = { FString(List.Tag) };
		TSet<FString> Seen;
		while (Pending.Num() > 0)
		{
			const FString Tag = Pending.Pop();
			if (Seen.Contains(Tag))
			{
				continue;
			}
			Seen.Add(Tag);
			const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(McCache::Data(FString::Printf(TEXT("tags/block/%s.json"), *Tag)));
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			if (!Json || !Json->TryGetArrayField(TEXT("values"), Values))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FString Entry;
				if (!Value->TryGetString(Entry))
				{
					continue;
				}
				if (Entry.StartsWith(TEXT("#")))
				{
					Pending.Add(McCache::PathOf(Entry.Mid(1)));
				}
				else if (!Tags.Contains(FName(*McCache::PathOf(Entry))))
				{
					Tags.Add(FName(*McCache::PathOf(Entry)), List.Class);
				}
			}
		}
	}
	UE_LOG(LogMcAssets, Log, TEXT("HD Textures: %d blocks classed by tag"), Tags.Num());
}

bool FMcHdBlocks::EnsureBuilt()
{
	check(IsInGameThread());
	if (IsBuilt())
	{
		return true;
	}
	FMcBlockTextures& Blocks = FMcBlockTextures::Get();
	if (!Blocks.EnsureLoaded())
	{
		return false;
	}
	const double Start = FPlatformTime::Seconds();
	const int32 Size = GetHdSize();
	const int32 NumLayers = Blocks.NumLayers();
	const int32 Scale = Size / FMcBlockTextures::TextureSize;
	ReadTags();

	LayerIds.Reset();
	for (int32 Layer = 0; Layer < NumLayers; ++Layer)
	{
		LayerIds.Add(Blocks.GetLayerId(Layer));
	}

	// What the pictures depend on: the ids and the pictures of the layers that stand still (an animated layer's picture
	// depends on when it was asked).
	FMD5 Hash;
	const int32 Header[3] = { McHd::GeneratorVersion, Size, NumLayers };
	Hash.Update(reinterpret_cast<const uint8*>(Header), sizeof(Header));
	for (int32 Layer = 0; Layer < NumLayers; ++Layer)
	{
		const FString Id = LayerIds[Layer].ToString();
		Hash.Update(reinterpret_cast<const uint8*>(*Id), Id.Len() * sizeof(TCHAR));
		if (!Blocks.IsAnimated(Layer))
		{
			Hash.Update(reinterpret_cast<const uint8*>(Blocks.GetPixels(Layer).GetData()), Blocks.GetPixels(Layer).Num() * sizeof(FColor));
		}
	}
	uint8 Key[16];
	Hash.Final(Key);

	const int64 LayerPixels = static_cast<int64>(Size) * Size;
	TArray<FColor> Maps[3];
	for (TArray<FColor>& Map : Maps)
	{
		Map.SetNumUninitialized(LayerPixels * NumLayers);
	}

	const FString CachePath = FPaths::ProjectSavedDir() / TEXT("HdCache/Blocks.bin");
	bool bLoaded = false;
	if (TUniquePtr<FArchive> Reader = TUniquePtr<FArchive>(IFileManager::Get().CreateFileReader(*CachePath)))
	{
		uint32 Magic = 0;
		int32 CachedSize = 0, CachedLayers = 0;
		uint8 CachedKey[16] = {};
		*Reader << Magic;
		*Reader << CachedSize;
		*Reader << CachedLayers;
		Reader->Serialize(CachedKey, sizeof(CachedKey));
		if (!Reader->IsError() && Magic == CacheMagic && CachedSize == Size && CachedLayers == NumLayers && FMemory::Memcmp(CachedKey, Key, sizeof(Key)) == 0)
		{
			for (TArray<FColor>& Map : Maps)
			{
				Reader->Serialize(Map.GetData(), static_cast<int64>(Map.Num()) * sizeof(FColor));
			}
			bLoaded = !Reader->IsError();
		}
	}

	if (!bLoaded)
	{
		ParallelFor(NumLayers, [&](int32 Layer)
		{
			const FString Id = LayerIds[Layer] == NAME_None ? FString(TEXT("minecraft:block/missing")) : LayerIds[Layer].ToString();
			const McHd::FMaps Made = McHd::Generate(Id, MakeSource(Blocks.GetPixels(Layer), FMcBlockTextures::TextureSize), Scale,
				McHd::PickClass(Id, &Tags), McHd::IsEmissiveTexture(Id), true);
			const McHd::FPicture* Pictures[3] = { &Made.Albedo, &Made.Normal, &Made.Specular };
			for (int32 Kind = 0; Kind < 3; ++Kind)
			{
				FMemory::Memcpy(Maps[Kind].GetData() + Layer * LayerPixels, Pictures[Kind]->Pixels.GetData(), LayerPixels * sizeof(FColor));
			}
		});
		// Over about 600 MB the file costs more to read than the pictures cost to make.
		if (static_cast<int64>(Maps[0].Num()) * 3 * sizeof(FColor) < 600ll * 1024 * 1024)
		{
			if (TUniquePtr<FArchive> Writer = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*CachePath)))
			{
				uint32 Magic = CacheMagic;
				int32 CachedSize = Size, CachedLayers = NumLayers;
				*Writer << Magic;
				*Writer << CachedSize;
				*Writer << CachedLayers;
				Writer->Serialize(Key, sizeof(Key));
				for (TArray<FColor>& Map : Maps)
				{
					Writer->Serialize(Map.GetData(), static_cast<int64>(Map.Num()) * sizeof(FColor));
				}
			}
		}
	}

	Albedo = MakeArray(Size, Size, NumLayers, true, Maps[0]);
	Normal = MakeArray(Size, Size, NumLayers, false, Maps[1]);
	Specular = MakeArray(Size, Size, NumLayers, false, Maps[2]);
	RenderState = MakeShared<FRenderState, ESPMode::ThreadSafe>();
	Summary.Size = Size;
	Summary.Layers = NumLayers;
	Summary.bFromCache = bLoaded;
	Summary.Seconds = FPlatformTime::Seconds() - Start;
	Blocks.OnLayerAnimated = [](int32 Layer)
	{
		FMcHdBlocks::Get().RefreshLayer(Layer);
	};
	UE_LOG(LogMcAssets, Log, TEXT("HD Textures: %d layers at %dx%d %s in %.0f ms (%.0f MB of arrays)"), NumLayers, Size, Size,
		bLoaded ? TEXT("loaded from Saved/HdCache") : TEXT("generated"), Summary.Seconds * 1000.0,
		static_cast<double>(LayerPixels) * NumLayers * 3 * 4 * 4 / 3 / (1024.0 * 1024.0));
	return true;
}

void FMcHdBlocks::RefreshLayer(int32 Layer)
{
	if (!IsBuilt() || !RenderState.IsValid() || !Albedo->GetResource() || !LayerIds.IsValidIndex(Layer))
	{
		return;
	}
	FMcBlockTextures& Blocks = FMcBlockTextures::Get();
	const int32 Size = Summary.Size;
	const FString Id = LayerIds[Layer].ToString();
	const McHd::FMaps Made = McHd::Generate(Id, MakeSource(Blocks.GetPixels(Layer), FMcBlockTextures::TextureSize), Size / FMcBlockTextures::TextureSize,
		McHd::PickClass(Id, &Tags), McHd::IsEmissiveTexture(Id), true);

	struct FUpload
	{
		TArray<FColor> Chain[3];
	};
	FUpload Upload;
	MakeMipChain(Made.Albedo.Pixels.GetData(), Size, Size, true, Upload.Chain[0]);
	MakeMipChain(Made.Normal.Pixels.GetData(), Size, Size, false, Upload.Chain[1]);
	MakeMipChain(Made.Specular.Pixels.GetData(), Size, Size, false, Upload.Chain[2]);

	FTextureResource* Resources[3] = { Albedo->GetResource(), Normal->GetResource(), Specular->GetResource() };
	if (!Resources[1] || !Resources[2])
	{
		return;
	}
	const int32 NumMips = FMath::FloorLog2(Size) + 1;
	ENQUEUE_RENDER_COMMAND(McUploadHdLayer)(
		[Resources, State = RenderState, Upload = MoveTemp(Upload), Layer, Size, NumMips](FRHICommandListImmediate& RHICmdList)
		{
			for (int32 Kind = 0; Kind < 3; ++Kind)
			{
				if (!Resources[Kind]->TextureRHI.IsValid())
				{
					return;
				}
				if (!State->Staging[Kind].IsValid())
				{
					State->Staging[Kind] = RHICmdList.CreateTexture(FRHITextureCreateDesc::Create2D(TEXT("McHdStaging"), Size, Size, PF_B8G8R8A8)
						.SetNumMips(NumMips)
						.SetFlags(Kind == 0 ? (ETextureCreateFlags::ShaderResource | ETextureCreateFlags::SRGB) : ETextureCreateFlags::ShaderResource)
						.SetInitialState(ERHIAccess::SRVMask));
				}
				int32 Offset = 0;
				for (int32 Mip = 0; Mip < NumMips; ++Mip)
				{
					const uint32 Dim = FMath::Max(Size >> Mip, 1);
					RHICmdList.UpdateTexture2D(State->Staging[Kind], Mip, FUpdateTextureRegion2D(0, 0, 0, 0, Dim, Dim), Dim * 4,
						reinterpret_cast<const uint8*>(Upload.Chain[Kind].GetData() + Offset));
					Offset += Dim * Dim;
				}
				FRHICopyTextureInfo Copy;
				Copy.DestSliceIndex = Layer;
				Copy.NumMips = NumMips;
				RHICmdList.Transition({
					FRHITransitionInfo(State->Staging[Kind], ERHIAccess::Unknown, ERHIAccess::CopySrc),
					FRHITransitionInfo(Resources[Kind]->TextureRHI, ERHIAccess::Unknown, ERHIAccess::CopyDest) });
				RHICmdList.CopyTexture(State->Staging[Kind], Resources[Kind]->TextureRHI, Copy);
				RHICmdList.Transition({
					FRHITransitionInfo(State->Staging[Kind], ERHIAccess::CopySrc, ERHIAccess::SRVMask),
					FRHITransitionInfo(Resources[Kind]->TextureRHI, ERHIAccess::CopyDest, ERHIAccess::SRVMask) });
			}
		});
}

FMcHdPicture FMcHdBlocks::GetPicture(const FString& TextureId, const FString& PngPath)
{
	if (const FMcHdPicture* Found = Pictures.Find(PngPath))
	{
		return *Found;
	}
	FMcHdPicture Result;
	McHd::FPicture Source;
	if (LoadHdPng(PngPath, Source))
	{
		// 64 -> 256 by default; nothing past 1024 on a side.
		int32 Scale = FMath::Clamp(GetHdSize() / 16, 1, 8);
		while (Scale > 1 && Source.Width * Scale > 1024)
		{
			Scale /= 2;
		}
		const McHd::EMaterialClass Class = TextureId.Contains(TEXT("entity/")) ? McHd::EMaterialClass::Fabric : McHd::EMaterialClass::Generic;
		const McHd::FMaps Made = McHd::Generate(TextureId, Source, Scale, Class, false, false);
		Result.Albedo = MakeArray(Made.Albedo.Width, Made.Albedo.Height, 1, true, Made.Albedo.Pixels);
		Result.Normal = MakeArray(Made.Normal.Width, Made.Normal.Height, 1, false, Made.Normal.Pixels);
		Result.Specular = MakeArray(Made.Specular.Width, Made.Specular.Height, 1, false, Made.Specular.Pixels);
	}
	else
	{
		UE_LOG(LogMcAssets, Warning, TEXT("HD Textures: can't read %s"), *PngPath);
	}
	Pictures.Add(PngPath, Result);
	return Result;
}

UTexture2DArray* FMcHdBlocks::GetWhitePicture(const FString& TextureId, const FString& PngPath)
{
	if (const TObjectPtr<UTexture2DArray>* Found = WhitePictures.Find(PngPath))
	{
		return *Found;
	}
	McHd::FPicture Source;
	UTexture2DArray* Result = nullptr;
	if (LoadHdPng(PngPath, Source))
	{
		int32 Scale = FMath::Clamp(GetHdSize() / 16, 1, 8);
		while (Scale > 1 && Source.Width * Scale > 1024)
		{
			Scale /= 2;
		}
		McHd::FPicture Big = McHd::ScaleNearest(Source, Source.Width * Scale, Source.Height * Scale);
		for (FColor& Pixel : Big.Pixels)
		{
			Pixel = FColor(255, 255, 255, Pixel.A);
		}
		Result = MakeArray(Big.Width, Big.Height, 1, true, Big.Pixels);
	}
	WhitePictures.Add(PngPath, Result);
	return Result;
}

void FMcHdBlocks::Shutdown()
{
	FMcBlockTextures::Get().OnLayerAnimated = nullptr;
	if (RenderState.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(McReleaseHdStaging)([State = RenderState](FRHICommandListImmediate&)
		{
			for (FTextureRHIRef& Staging : State->Staging)
			{
				Staging.SafeRelease();
			}
		});
		RenderState.Reset();
	}
}
