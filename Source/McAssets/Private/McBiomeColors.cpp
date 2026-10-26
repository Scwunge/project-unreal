#include "McBiomeColors.h"
#include "McCache.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

namespace
{
	// A colour in biome JSON: "#rrggbb", or a plain integer.
	bool ReadColor(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, FColor& Out)
	{
		FString Text;
		double Number;
		uint32 Rgb;
		if (Object->TryGetStringField(Field, Text))
		{
			Text.RemoveFromStart(TEXT("#"));
			Rgb = FParse::HexNumber(*Text);
		}
		else if (Object->TryGetNumberField(Field, Number))
		{
			Rgb = static_cast<uint32>(static_cast<int64>(Number));
		}
		else
		{
			return false;
		}
		Out = FColor((Rgb >> 16) & 0xFF, (Rgb >> 8) & 0xFF, Rgb & 0xFF, 255);
		return true;
	}

	FColor DarkForestGrass(const FColor& Base)
	{
		// Halfway between the colormap colour and a dark green, done channel by channel.
		auto Mix = [](uint32 Channel, uint32 Dark) { return static_cast<uint8>(((Channel & 0xFE) + Dark) >> 1); };
		return FColor(Mix(Base.R, 0x28), Mix(Base.G, 0x34), Mix(Base.B, 0x0A), 255);
	}
}

bool FMcColormap::Load(const FString& PngPath)
{
	TArray<uint8> File;
	if (!FFileHelper::LoadFileToArray(File, *PngPath))
	{
		UE_LOG(LogMcAssets, Error, TEXT("Can't read %s. Run tools/fetch_cache.py to fill cache/."), *PngPath);
		return false;
	}
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	TArray64<uint8> Raw;
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(File.GetData(), File.Num()) || !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw)
		|| Raw.Num() != static_cast<int64>(Wrapper->GetWidth()) * Wrapper->GetHeight() * 4)
	{
		UE_LOG(LogMcAssets, Error, TEXT("%s is not a readable PNG"), *PngPath);
		return false;
	}
	Width = Wrapper->GetWidth();
	Height = Wrapper->GetHeight();
	Pixels.SetNumUninitialized(Width * Height);
	FMemory::Memcpy(Pixels.GetData(), Raw.GetData(), Raw.Num());
	return true;
}

FColor FMcColormap::Sample(float Temperature, float Downfall) const
{
	if (!IsLoaded())
	{
		return FColor::White;
	}
	Temperature = FMath::Clamp(Temperature, 0.f, 1.f);
	Downfall = FMath::Clamp(Downfall, 0.f, 1.f) * Temperature;
	const int32 X = FMath::Clamp(static_cast<int32>((1.f - Temperature) * (Width - 1)), 0, Width - 1);
	const int32 Y = FMath::Clamp(static_cast<int32>((1.f - Downfall) * (Height - 1)), 0, Height - 1);
	FColor Color = Pixels[Y * Width + X];
	Color.A = 255;
	return Color;
}

FMcBiomeColorTable& FMcBiomeColorTable::Get()
{
	static FMcBiomeColorTable Instance;
	return Instance;
}

bool FMcBiomeColorTable::EnsureLoaded()
{
	check(IsInGameThread());
	if (IsLoaded())
	{
		return true;
	}

	const double StartTime = FPlatformTime::Seconds();
	FMcColormap NewGrass, NewFoliage, NewDryFoliage;
	if (!NewGrass.Load(McCache::Asset(TEXT("textures/colormap/grass.png")))
		|| !NewFoliage.Load(McCache::Asset(TEXT("textures/colormap/foliage.png"))))
	{
		return false;
	}
	NewDryFoliage.Load(McCache::Asset(TEXT("textures/colormap/dry_foliage.png")));  // newer than the other two; optional

	TArray<FString> Files;
	const FString Folder = McCache::Data(TEXT("worldgen/biome"));
	IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.json")), true, false);
	if (Files.Num() == 0)
	{
		UE_LOG(LogMcAssets, Error, TEXT("No biomes in %s. Run tools/fetch_cache.py to fill cache/."), *Folder);
		return false;
	}

	TMap<FName, FMcBiomeColors> NewBiomes;
	for (const FString& File : Files)
	{
		const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(Folder / File);
		if (!Json.IsValid())
		{
			continue;
		}

		FMcBiomeColors Colors;
		double Number;
		if (Json->TryGetNumberField(TEXT("temperature"), Number))
		{
			Colors.Temperature = static_cast<float>(Number);
		}
		if (Json->TryGetNumberField(TEXT("downfall"), Number))
		{
			Colors.Downfall = static_cast<float>(Number);
		}

		Colors.Grass = NewGrass.Sample(Colors.Temperature, Colors.Downfall);
		Colors.Foliage = NewFoliage.Sample(Colors.Temperature, Colors.Downfall);
		Colors.DryFoliage = NewDryFoliage.IsLoaded() ? NewDryFoliage.Sample(Colors.Temperature, Colors.Downfall) : Colors.Foliage;
		Colors.Water = FColor(0x3F, 0x76, 0xE4, 255);

		const TSharedPtr<FJsonObject>* Effects;
		if (Json->TryGetObjectField(TEXT("effects"), Effects))
		{
			FString Modifier;
			if ((*Effects)->TryGetStringField(TEXT("grass_color_modifier"), Modifier))
			{
				Colors.GrassModifier = Modifier == TEXT("dark_forest") ? EMcGrassModifier::DarkForest
					: Modifier == TEXT("swamp") ? EMcGrassModifier::Swamp : EMcGrassModifier::None;
			}

			// An explicit grass_color replaces the colormap; the modifier then works on the colormap colour only
			// when there is none (dark forest), and swamp fixes one of its two colours.
			if (!ReadColor(*Effects, TEXT("grass_color"), Colors.Grass))
			{
				if (Colors.GrassModifier == EMcGrassModifier::DarkForest)
				{
					Colors.Grass = DarkForestGrass(Colors.Grass);
				}
				else if (Colors.GrassModifier == EMcGrassModifier::Swamp)
				{
					Colors.Grass = FColor(0x6A, 0x70, 0x39, 255);
				}
			}
			ReadColor(*Effects, TEXT("foliage_color"), Colors.Foliage);
			ReadColor(*Effects, TEXT("dry_foliage_color"), Colors.DryFoliage);
			ReadColor(*Effects, TEXT("water_color"), Colors.Water);
		}

		NewBiomes.Add(FName(*McCache::Namespaced(FPaths::GetBaseFilename(File))), Colors);
	}

	GrassMap = MoveTemp(NewGrass);
	FoliageMap = MoveTemp(NewFoliage);
	DryFoliageMap = MoveTemp(NewDryFoliage);
	Biomes = MoveTemp(NewBiomes);
	UE_LOG(LogMcAssets, Log, TEXT("Loaded the colours of %d biomes in %.0f ms"), Biomes.Num(), (FPlatformTime::Seconds() - StartTime) * 1000.0);
	return true;
}

const FMcBiomeColors* FMcBiomeColorTable::Find(FName Biome) const
{
	return Biomes.Find(FName(*McCache::Namespaced(Biome.ToString())));
}

FMcBiomeColors FMcBiomeColorTable::Get(FName Biome) const
{
	if (const FMcBiomeColors* Colors = Find(Biome))
	{
		return *Colors;
	}
	const FMcBiomeColors* Plains = Biomes.Find(FName(TEXT("minecraft:plains")));
	return Plains ? *Plains : FMcBiomeColors();
}
