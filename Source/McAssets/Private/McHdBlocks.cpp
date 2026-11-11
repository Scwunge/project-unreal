#include "McHdBlocks.h"
#include "McAssets.h"
#include "McBlockTextures.h"
#include "McCache.h"
#include "McLoadScreen.h"
#include "McResourcePacks.h"
#include "Async/Async.h"
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

	// The three maps of one block layer at Size pixels on a side.
	// - A layer from the vanilla assets is 16 pixels (repeated, in an array made wider by a resource pack): the generator
	//   works from those 16 as always.
	// - A layer from a resource pack is used as the picture it is, shrunk to Size if needed; its normal and specular maps
	//   are the pack's own when it has them (and -McPackPbr is on), else made from the picture without added detail.
	// What one layer's maps are made from. The views point at the layer's pixels, or at copies when the maps are made on
	// another thread while the layer goes on animating.
	struct FLayerInput
	{
		FString Id;
		int32 Source = 0;
		bool bFromPack = false;
		McHd::EMaterialClass Class = McHd::EMaterialClass::Generic;
		bool bEmissive = false;
		TConstArrayView<FColor> Pixels, Normal, Specular;
	};

	FLayerInput MakeLayerInput(const FMcBlockTextures& Blocks, int32 Layer, const McHd::FClassTags& Tags)
	{
		FLayerInput Input;
		Input.Source = Blocks.GetSize();
		Input.Id = Blocks.GetLayerId(Layer) == NAME_None ? FString(TEXT("minecraft:block/missing")) : Blocks.GetLayerId(Layer).ToString();
		Input.bFromPack = Blocks.IsFromPack(Layer);
		Input.Class = McHd::PickClass(Input.Id, &Tags);
		Input.bEmissive = McHd::IsEmissiveTexture(Input.Id);
		Input.Pixels = Blocks.GetPixels(Layer);
		Input.Normal = Blocks.GetNormalPixels(Layer);
		Input.Specular = Blocks.GetSpecularPixels(Layer);
		return Input;
	}

	McHd::FMaps MakeLayerMaps(const FLayerInput& In, int32 Size)
	{
		const int32 Source = In.Source;
		const FString& Id = In.Id;
		const McHd::EMaterialClass Class = In.Class;
		const bool bEmissive = In.bEmissive;
		const TConstArrayView<FColor> Pixels = In.Pixels;

		if (Source == FMcBlockTextures::TextureSize || !In.bFromPack)
		{
			McHd::FPicture Plain;
			Plain.Width = Plain.Height = FMcBlockTextures::TextureSize;
			Plain.Pixels.SetNumUninitialized(Plain.Width * Plain.Height);
			const int32 Step = Source / FMcBlockTextures::TextureSize;
			for (int32 Y = 0; Y < Plain.Height; ++Y)
			{
				for (int32 X = 0; X < Plain.Width; ++X)
				{
					Plain.Pixels[Y * Plain.Width + X] = Pixels[Y * Step * Source + X * Step];
				}
			}
			McHd::FMaps Generated = McHd::Generate(Id, Plain, Size / FMcBlockTextures::TextureSize, Class, bEmissive, true);
			// A pack's picture is cut into 16 pixel tiles (one layer each) whatever its size, so a pack layer is 16 wide here
			// too: its own normal and specular maps, when it brought them, replace the generated ones (enlarged without
			// smoothing, which would bend the normals).
			if (In.bFromPack)
			{
				if (In.Normal.Num() > 0)
				{
					Generated.Normal = McHd::ScaleNearest(MakeSource(In.Normal, Source), Size, Size);
				}
				if (In.Specular.Num() > 0)
				{
					Generated.Specular = McHd::ScaleNearest(MakeSource(In.Specular, Source), Size, Size);
				}
			}
			return Generated;
		}

		auto ToSize = [Source, Size](TConstArrayView<FColor> From, bool bPlain)
		{
			McHd::FPicture Picture = MakeSource(From, Source);
			return Size == Source ? Picture : McHd::Shrink(Picture, Size, Size, bPlain);
		};
		McHd::FMaps Made = McHd::GenerateFromAlbedo(Id, ToSize(Pixels, false), Class, bEmissive, true);
		if (In.Normal.Num() > 0)
		{
			Made.Normal = ToSize(In.Normal, true);
		}
		if (In.Specular.Num() > 0)
		{
			Made.Specular = ToSize(In.Specular, true);
		}
		return Made;
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
	// Nearest, like the game's own pictures: trilinear softened every 16 pixel texel edge into a blur (the generated detail
	// sits inside each texel, so nearest keeps the edges crisp and the detail). -McHdSmooth brings the project's filter back.
	Array->Filter = FParse::Param(FCommandLine::Get(), TEXT("McHdSmooth")) ? TF_Default : TF_Nearest;
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
	const FMcLoadScreen LoadScreen(NSLOCTEXT("Mc", "LoadingHd", "Loading resource packs..."), McPacks::HasPacks());
	FMcBlockTextures& Blocks = FMcBlockTextures::Get();
	if (!Blocks.EnsureLoaded())
	{
		return false;
	}
	const double Start = FPlatformTime::Seconds();
	const int32 NumLayers = Blocks.NumLayers();
	int32 Size = GetHdSize();
	if (Blocks.GetSize() > FMcBlockTextures::TextureSize)
	{
		// A resource pack's pictures are kept as they are; the three arrays together stay within the pack memory budget.
		Size = McPacks::ChooseTextureSize(Blocks.GetSize(), NumLayers, Blocks.GetSize(), McPacks::GetBudgetBytes() / 3);
	}
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
		if (!Blocks.IsAnimated(Layer) && Blocks.GetSize() == FMcBlockTextures::TextureSize)
		{
			Hash.Update(reinterpret_cast<const uint8*>(Blocks.GetPixels(Layer).GetData()), Blocks.GetPixels(Layer).Num() * sizeof(FColor));
		}
	}
	// With a resource pack the picture is too big to hash; the packs' files and the options stand for it.
	if (Blocks.GetSize() > FMcBlockTextures::TextureSize)
	{
		const FString Source = Blocks.GetSourceKey();
		Hash.Update(reinterpret_cast<const uint8*>(*Source), Source.Len() * sizeof(TCHAR));
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
			const McHd::FMaps Made = MakeLayerMaps(MakeLayerInput(Blocks, Layer, Tags), Size);
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
	check(IsInGameThread());
	if (!IsBuilt() || !RenderState.IsValid() || !Albedo->GetResource() || !LayerIds.IsValidIndex(Layer))
	{
		return;
	}
	FTextureResource* Resources[3] = { Albedo->GetResource(), Normal->GetResource(), Specular->GetResource() };
	if (!Resources[1] || !Resources[2])
	{
		return;
	}
	// The maps are made on a worker thread: at 128 pixels and up the game thread spent 45 to 220 ms a frame on the animated
	// layers (4.5 fps). One job per layer at a time; a step that comes while it runs is made when it is done, from the newest.
	if (RefreshBusy.Contains(Layer))
	{
		RefreshAgain.Add(Layer);
		return;
	}
	RefreshBusy.Add(Layer);
	struct FCopy
	{
		FLayerInput Input;
		TArray<FColor> Pixels, Normal, Specular;
	};
	TSharedRef<FCopy> Copy = MakeShared<FCopy>();
	Copy->Input = MakeLayerInput(FMcBlockTextures::Get(), Layer, Tags);
	Copy->Pixels = TArray<FColor>(Copy->Input.Pixels.GetData(), Copy->Input.Pixels.Num());
	Copy->Normal = TArray<FColor>(Copy->Input.Normal.GetData(), Copy->Input.Normal.Num());
	Copy->Specular = TArray<FColor>(Copy->Input.Specular.GetData(), Copy->Input.Specular.Num());
	Copy->Input.Pixels = Copy->Pixels;
	Copy->Input.Normal = Copy->Normal;
	Copy->Input.Specular = Copy->Specular;
	const int32 Size = Summary.Size;
	TSharedPtr<FRenderState, ESPMode::ThreadSafe> State = RenderState;
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, Copy, Layer, Size, State, R0 = Resources[0], R1 = Resources[1], R2 = Resources[2]]()
	{
		const McHd::FMaps Made = MakeLayerMaps(Copy->Input, Size);
		struct FUpload
		{
			TArray<FColor> Chain[3];
		};
		FUpload Upload;
		MakeMipChain(Made.Albedo.Pixels.GetData(), Size, Size, true, Upload.Chain[0]);
		MakeMipChain(Made.Normal.Pixels.GetData(), Size, Size, false, Upload.Chain[1]);
		MakeMipChain(Made.Specular.Pixels.GetData(), Size, Size, false, Upload.Chain[2]);
		const int32 NumMips = FMath::FloorLog2(Size) + 1;
		ENQUEUE_RENDER_COMMAND(McUploadHdLayer)(
			[Resources = TStaticArray<FTextureResource*, 3>{ R0, R1, R2 }, State, Upload = MoveTemp(Upload), Layer, Size, NumMips](FRHICommandListImmediate& RHICmdList)
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
					FRHICopyTextureInfo CopyInfo;
					CopyInfo.DestSliceIndex = Layer;
					CopyInfo.NumMips = NumMips;
					RHICmdList.Transition({
						FRHITransitionInfo(State->Staging[Kind], ERHIAccess::Unknown, ERHIAccess::CopySrc),
						FRHITransitionInfo(Resources[Kind]->TextureRHI, ERHIAccess::Unknown, ERHIAccess::CopyDest) });
					RHICmdList.CopyTexture(State->Staging[Kind], Resources[Kind]->TextureRHI, CopyInfo);
					RHICmdList.Transition({
						FRHITransitionInfo(State->Staging[Kind], ERHIAccess::CopySrc, ERHIAccess::SRVMask),
						FRHITransitionInfo(Resources[Kind]->TextureRHI, ERHIAccess::CopyDest, ERHIAccess::SRVMask) });
				}
			});
		AsyncTask(ENamedThreads::GameThread, [this, Layer]()
		{
			RefreshBusy.Remove(Layer);
			if (RefreshAgain.Remove(Layer) > 0)
			{
				RefreshLayer(Layer);
			}
		});
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

void FMcHdBlocks::Unload()
{
	check(IsInGameThread());
	Shutdown();
	Albedo = nullptr;
	Normal = nullptr;
	Specular = nullptr;
	Summary = FMcHdSummary();
	LayerIds.Empty();
	Pictures.Empty();
	WhitePictures.Empty();
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
