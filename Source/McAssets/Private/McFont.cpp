#include "McFont.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "McAssets.h"
#include "McCache.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

void McFont::ReadGlyphWidths(const TCHAR* Asset, TArray<uint8>& OutWidths)
{
	OutWidths.Init(0, 256);
	TArray<uint8> File;
	IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
	TArray64<uint8> Raw;
	if (!FFileHelper::LoadFileToArray(File, *McCache::Asset(Asset)) || !Wrapper.IsValid() || !Wrapper->SetCompressed(File.GetData(), File.Num())
		|| !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw) || Wrapper->GetWidth() < 128 || Wrapper->GetWidth() % 128 != 0 || Wrapper->GetHeight() != Wrapper->GetWidth())
	{
		UE_LOG(LogMcAssets, Warning, TEXT("Can't read the font %s"), Asset);
		OutWidths.Init(5, 256);
		return;
	}
	// A pack's sheet can be a whole number of times larger: widths are given in vanilla pixels.
	const int32 Size = Wrapper->GetWidth();
	const int32 Cell = Size / 16;
	const int32 Scale = Size / 128;
	for (int32 Code = 0; Code < 256; ++Code)
	{
		int32 Widest = 0;
		for (int32 Column = 0; Column < Cell; ++Column)
		{
			for (int32 Row = 0; Row < Cell; ++Row)
			{
				const int64 Pixel = (static_cast<int64>((Code / 16) * Cell + Row) * Size + (Code % 16) * Cell + Column) * 4;
				if (Raw[Pixel + 3] != 0)
				{
					Widest = Column + 1;
				}
			}
		}
		OutWidths[Code] = static_cast<uint8>(FMath::DivideAndRoundUp(Widest, Scale));
	}
	OutWidths[' '] = 3;
}

namespace
{
	TArray<uint8> AsciiWidths;
}

const TArray<uint8>& McFont::GetGlyphWidths()
{
	TArray<uint8>& Widths = AsciiWidths;
	if (Widths.IsEmpty())
	{
		ReadGlyphWidths(TEXT("textures/font/ascii.png"), Widths);
	}
	return Widths;
}

void McFont::ClearCache()
{
	AsciiWidths.Empty();
}

int32 McFont::GetWidth(const FString& Text)
{
	const TArray<uint8>& Widths = GetGlyphWidths();
	int32 Width = 0;
	for (const TCHAR Char : Text)
	{
		Width += (Widths.IsValidIndex(Char) ? Widths[Char] : 5) + 1;
	}
	return Width;
}

FString McFont::FitToWidth(const FString& Text, int32 MaxWidth)
{
	FString Out = Text;
	while (!Out.IsEmpty() && GetWidth(Out) > MaxWidth)
	{
		Out.LeftChopInline(1);
	}
	return Out;
}
