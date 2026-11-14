#include "McResourcePacks.h"
#include "McCache.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Internationalization/Regex.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// What the packs' atlases/*.json add or take away beyond plain textures, which the game finds by their path anyway:
//  - "unstitch" cuts sprites out of a sheet (a region in units of the sheet divided by divisor_x and divisor_y),
//  - "paletted_permutations" recolours textures by swapping the colours of a palette picture for another palette's,
//  - "filter" hides sprites of the game and the packs below.
// The pictures made are written under Saved/McAtlasCache and offered as if a pack had them (McPacks::FindAtlasSprite);
// the filters are asked with McPacks::IsAtlasFiltered.
namespace
{
	struct FPicture
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray<FColor> Pixels;
	};

	struct FFilter
	{
		FString Prefix;  // what the atlas's sprite names are relative to, "textures/" or "textures/gui/sprites/"
		FRegexPattern Namespace;
		FRegexPattern Path;
	};

	FCriticalSection GAtlasLock;
	uint32 GBuiltFor = 0;
	TMap<FString, FString> GSprites;  // "textures/block/zz_cut.png" -> the PNG written for it
	TArray<FFilter> GFilters;

	FString SpritePath(const FString& AtlasName, const FString& Sprite)
	{
		FString Id = Sprite;
		Id.RemoveFromStart(TEXT("minecraft:"));
		return (AtlasName == TEXT("gui") ? TEXT("textures/gui/sprites/") : TEXT("textures/")) + Id + TEXT(".png");
	}

	// A texture by its id ("minecraft:block/foo"), from the packs or the game's own files.
	bool LoadPicture(const FString& Id, FPicture& Out)
	{
		FString Relative = Id;
		Relative.RemoveFromStart(TEXT("minecraft:"));
		Relative = TEXT("textures/") + Relative + TEXT(".png");
		FString Path;
		if (!McPacks::TryResolve(Relative, Path) || !FPaths::FileExists(Path))
		{
			Path = McCache::Root() / TEXT("client/assets/minecraft") / Relative;
		}
		TArray<uint8> File;
		if (!FFileHelper::LoadFileToArray(File, *Path))
		{
			return false;
		}
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		TArray64<uint8> Raw;
		if (!Wrapper.IsValid() || !Wrapper->SetCompressed(File.GetData(), File.Num()) || !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
		{
			return false;
		}
		Out.Width = Wrapper->GetWidth();
		Out.Height = Wrapper->GetHeight();
		if (Raw.Num() != static_cast<int64>(Out.Width) * Out.Height * 4)
		{
			return false;
		}
		Out.Pixels.SetNumUninitialized(Out.Width * Out.Height);
		FMemory::Memcpy(Out.Pixels.GetData(), Raw.GetData(), Raw.Num());
		return true;
	}

	void SavePicture(const FString& Relative, const FString& CacheFolder, const FPicture& Picture)
	{
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!Wrapper.IsValid() || !Wrapper->SetRaw(Picture.Pixels.GetData(), Picture.Pixels.Num() * sizeof(FColor), Picture.Width, Picture.Height, ERGBFormat::BGRA, 8))
		{
			return;
		}
		const FString Out = FPaths::ConvertRelativePathToFull(CacheFolder / Relative);
		const TArray64<uint8>& Png = Wrapper->GetCompressed();
		if (FFileHelper::SaveArrayToFile(TArrayView64<const uint8>(Png.GetData(), Png.Num()), *Out))
		{
			GSprites.Add(Relative, Out);
		}
	}

	// The colours of a palette picture: its opaque pixels in reading order.
	TArray<FColor> PaletteOf(const FPicture& Picture)
	{
		TArray<FColor> Colours;
		for (const FColor& Pixel : Picture.Pixels)
		{
			if (Pixel.A != 0)
			{
				Colours.Add(Pixel);
			}
		}
		return Colours;
	}

	void ReadAtlas(const FString& AtlasName, const TArray<uint8>& File, const FString& CacheFolder)
	{
		FString Text;
		FFileHelper::BufferToString(Text, File.GetData(), File.Num());
		TSharedPtr<FJsonObject> Root;
		const TArray<TSharedPtr<FJsonValue>>* Sources = nullptr;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("sources"), Sources))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& SourceValue : *Sources)
		{
			const TSharedPtr<FJsonObject>* Source;
			FString Type;
			if (!SourceValue->TryGetObject(Source) || !(*Source)->TryGetStringField(TEXT("type"), Type))
			{
				continue;
			}
			Type.RemoveFromStart(TEXT("minecraft:"));
			if (Type == TEXT("filter"))
			{
				FString Namespace = TEXT(".*"), Path = TEXT(".*");
				(*Source)->TryGetStringField(TEXT("namespace"), Namespace);
				(*Source)->TryGetStringField(TEXT("path"), Path);
				GFilters.Add({ AtlasName == TEXT("gui") ? TEXT("textures/gui/sprites/") : TEXT("textures/"), FRegexPattern(TEXT("^(?:") + Namespace + TEXT(")$")), FRegexPattern(TEXT("^(?:") + Path + TEXT(")$")) });
			}
			else if (Type == TEXT("unstitch"))
			{
				FString Resource;
				double DivisorX = 1.0, DivisorY = 1.0;
				const TArray<TSharedPtr<FJsonValue>>* Regions = nullptr;
				FPicture Sheet;
				if (!(*Source)->TryGetStringField(TEXT("resource"), Resource) || !(*Source)->TryGetArrayField(TEXT("regions"), Regions) || !LoadPicture(Resource, Sheet))
				{
					continue;
				}
				(*Source)->TryGetNumberField(TEXT("divisor_x"), DivisorX);
				(*Source)->TryGetNumberField(TEXT("divisor_y"), DivisorY);
				if (DivisorX <= 0.0 || DivisorY <= 0.0)
				{
					continue;
				}
				for (const TSharedPtr<FJsonValue>& RegionValue : *Regions)
				{
					const TSharedPtr<FJsonObject>* Region;
					FString Sprite;
					double X = 0, Y = 0, W = 0, H = 0;
					if (!RegionValue->TryGetObject(Region) || !(*Region)->TryGetStringField(TEXT("sprite"), Sprite))
					{
						continue;
					}
					(*Region)->TryGetNumberField(TEXT("x"), X);
					(*Region)->TryGetNumberField(TEXT("y"), Y);
					(*Region)->TryGetNumberField(TEXT("width"), W);
					(*Region)->TryGetNumberField(TEXT("height"), H);
					const int32 Left = FMath::RoundToInt(X * Sheet.Width / DivisorX), Top = FMath::RoundToInt(Y * Sheet.Height / DivisorY);
					FPicture Cut;
					Cut.Width = FMath::RoundToInt(W * Sheet.Width / DivisorX);
					Cut.Height = FMath::RoundToInt(H * Sheet.Height / DivisorY);
					if (Cut.Width <= 0 || Cut.Height <= 0 || Left < 0 || Top < 0 || Left + Cut.Width > Sheet.Width || Top + Cut.Height > Sheet.Height)
					{
						continue;
					}
					Cut.Pixels.SetNumUninitialized(Cut.Width * Cut.Height);
					for (int32 Row = 0; Row < Cut.Height; ++Row)
					{
						FMemory::Memcpy(&Cut.Pixels[Row * Cut.Width], &Sheet.Pixels[(Top + Row) * Sheet.Width + Left], Cut.Width * sizeof(FColor));
					}
					SavePicture(SpritePath(AtlasName, Sprite), CacheFolder, Cut);
				}
			}
			else if (Type == TEXT("paletted_permutations"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Textures = nullptr;
				const TSharedPtr<FJsonObject>* Permutations;
				FString Key, Separator = TEXT("_");
				if (!(*Source)->TryGetArrayField(TEXT("textures"), Textures) || !(*Source)->TryGetObjectField(TEXT("permutations"), Permutations)
					|| !(*Source)->TryGetStringField(TEXT("palette_key"), Key))
				{
					continue;
				}
				(*Source)->TryGetStringField(TEXT("separator"), Separator);
				FPicture KeyPicture;
				if (!LoadPicture(Key, KeyPicture))
				{
					continue;
				}
				const TArray<FColor> KeyColours = PaletteOf(KeyPicture);
				for (const TPair<FString, TSharedPtr<FJsonValue>> Permutation : (*Permutations)->Values)
				{
					FPicture PalettePicture;
					FString PaletteId;
					if (!Permutation.Value->TryGetString(PaletteId) || !LoadPicture(PaletteId, PalettePicture))
					{
						continue;
					}
					const TArray<FColor> NewColours = PaletteOf(PalettePicture);
					if (NewColours.Num() < KeyColours.Num())
					{
						continue;
					}
					for (const TSharedPtr<FJsonValue>& TextureValue : *Textures)
					{
						FString TextureId;
						FPicture Texture;
						if (!TextureValue->TryGetString(TextureId) || !LoadPicture(TextureId, Texture))
						{
							continue;
						}
						for (FColor& Pixel : Texture.Pixels)
						{
							for (int32 Index = 0; Index < KeyColours.Num(); ++Index)
							{
								if (Pixel.R == KeyColours[Index].R && Pixel.G == KeyColours[Index].G && Pixel.B == KeyColours[Index].B && Pixel.A != 0)
								{
									Pixel = FColor(NewColours[Index].R, NewColours[Index].G, NewColours[Index].B, Pixel.A);
									break;
								}
							}
						}
						SavePicture(SpritePath(AtlasName, TextureId + Separator + Permutation.Key), CacheFolder, Texture);
					}
				}
			}
		}
	}

	// The atlases' names: the game's own, which the packs add to.
	TArray<FString> AtlasNames()
	{
		TArray<FString> Names;
		IFileManager::Get().FindFiles(Names, *(McCache::Root() / TEXT("client/assets/minecraft/atlases/*.json")), true, false);
		for (FString& Name : Names)
		{
			Name = FPaths::GetBaseFilename(Name);
		}
		return Names;
	}

	void EnsureBuilt()
	{
		FScopeLock Lock(&GAtlasLock);
		const uint32 Generation = McPacks::GetGeneration();
		if (GBuiltFor == Generation)
		{
			return;
		}
		GBuiltFor = Generation;
		GSprites.Reset();
		GFilters.Reset();
		if (!McPacks::HasPacks())
		{
			return;
		}
		const FString Folder = FPaths::ProjectSavedDir() / TEXT("McAtlasCache") / FMD5::HashAnsiString(*McPacks::GetSignature()).Left(12);
		for (const FString& Name : AtlasNames())
		{
			TArray<TArray<uint8>> Files;
			McPacks::ReadAll(FString::Printf(TEXT("atlases/%s.json"), *Name), Files);
			for (const TArray<uint8>& File : Files)
			{
				ReadAtlas(Name, File, Folder);
			}
		}
	}
}

namespace McPacks
{
	bool FindAtlasSprite(const FString& Relative, FString& OutPath)
	{
		EnsureBuilt();
		FScopeLock Lock(&GAtlasLock);
		if (const FString* Found = GSprites.Find(Relative))
		{
			OutPath = *Found;
			return true;
		}
		return false;
	}

	bool IsAtlasFiltered(const FString& Relative)
	{
		EnsureBuilt();
		FScopeLock Lock(&GAtlasLock);
		for (const FFilter& Filter : GFilters)
		{
			if (Relative.StartsWith(Filter.Prefix) && Relative.EndsWith(TEXT(".png")))
			{
				const FString Path = Relative.Mid(Filter.Prefix.Len(), Relative.Len() - Filter.Prefix.Len() - 4);
				FRegexMatcher NamespaceMatcher(Filter.Namespace, TEXT("minecraft")), PathMatcher(Filter.Path, Path);
				if (NamespaceMatcher.FindNext() && PathMatcher.FindNext())
				{
					return true;
				}
			}
		}
		return false;
	}
}
