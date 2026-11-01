#include "McFontRaster.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "McCache.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

THIRD_PARTY_INCLUDES_START
#include "ft2build.h"
#include FT_FREETYPE_H
#include FT_OUTLINE_H
THIRD_PARTY_INCLUDES_END

// A provider is one of these; Make returns false when the provider has no such character.
struct FMcRasterFonts::FProvider
{
	int32 Priority = 0;
	virtual ~FProvider() = default;
	virtual bool Make(int32 Char, FMcRasterGlyph& Out) = 0;
};

namespace
{
	// A glyph of bits: Rows rows of Width pixels each, Left to Right (inclusive) are drawn. Both Unifont and the legacy pages
	// are 16 pixels tall and are drawn half as large, so a glyph is Width / 2 + 1 pixels of advance, by its columns used.
	void FinishHalfSize(FMcRasterGlyph& Out, int32 Left, int32 Right)
	{
		const int32 Columns = Right - Left + 1;
		Out.DrawWidth = Columns * 0.5f;
		Out.DrawHeight = Out.Height * 0.5f;
		Out.OffsetX = Left * 0.5f;
		Out.OffsetY = 0.0f;
		Out.Advance = static_cast<float>(Columns / 2 + 1);
		Out.ShadowOffset = 0.5f;
	}

	struct FUnihexProvider : FMcRasterFonts::FProvider
	{
		TArray<uint8> Text;
		TArray<FMcRasterFonts::FUnihexOverride> Overrides;
		TMap<int32, int32> Lines;   // where the digits of the character begin in Text
		bool bIndexed = false;

		void Index()
		{
			bIndexed = true;
			const int32 Num = Text.Num();
			for (int32 At = 0; At < Num;)
			{
				int32 Code = 0, Digits = 0;
				while (At < Num && Text[At] != ':' && Text[At] != '\n')
				{
					const uint8 C = Text[At];
					const int32 V = C >= '0' && C <= '9' ? C - '0' : (C | 0x20) >= 'a' && (C | 0x20) <= 'f' ? (C | 0x20) - 'a' + 10 : -1;
					Code = V >= 0 ? Code * 16 + V : Code;
					++At;
					++Digits;
				}
				if (At < Num && Text[At] == ':')
				{
					if (Code > 0 && Code < 0x110000 && Digits <= 6)
					{
						Lines.Add(Code, At + 1);
					}
					while (At < Num && Text[At] != '\n')
					{
						++At;
					}
				}
				++At;
			}
		}

		virtual bool Make(int32 Char, FMcRasterGlyph& Out) override
		{
			if (!bIndexed)
			{
				Index();
			}
			const int32* Found = Lines.Find(Char);
			if (!Found)
			{
				return false;
			}
			int32 Digits = 0;
			while (*Found + Digits < Text.Num() && Text[*Found + Digits] > ' ')
			{
				++Digits;
			}
			if (Digits == 0 || Digits % 16 != 0 || Digits > 128)
			{
				return false;
			}
			const int32 PerRow = Digits / 16;
			const int32 Width = PerRow * 4;
			TArray<uint32> Rows;
			for (int32 Row = 0; Row < 16; ++Row)
			{
				uint32 Bits = 0;
				for (int32 Digit = 0; Digit < PerRow; ++Digit)
				{
					const uint8 C = Text[*Found + Row * PerRow + Digit];
					Bits = (Bits << 4) | static_cast<uint32>(C >= '0' && C <= '9' ? C - '0' : ((C | 0x20) - 'a' + 10) & 15);
				}
				Rows.Add(Bits);
			}
			// The columns that hold anything, unless the definition gives them.
			int32 Left = Width, Right = -1;
			for (const uint32 Bits : Rows)
			{
				for (int32 Column = 0; Column < Width; ++Column)
				{
					if (Bits & (1u << (Width - 1 - Column)))
					{
						Left = FMath::Min(Left, Column);
						Right = FMath::Max(Right, Column);
					}
				}
			}
			for (const FMcRasterFonts::FUnihexOverride& Override : Overrides)
			{
				if (Char >= Override.From && Char <= Override.To)
				{
					Left = Override.Left;
					Right = Override.Right;
				}
			}
			if (Right < Left)
			{
				// Blank: only the room it takes.
				Out = FMcRasterGlyph();
				Out.Advance = static_cast<float>(Width / 4 + 1);
				Out.ShadowOffset = 0.5f;
				return true;
			}
			Out.Width = Right - Left + 1;
			Out.Height = 16;
			Out.Bgra.Init(0, Out.Width * Out.Height * 4);
			for (int32 Row = 0; Row < 16; ++Row)
			{
				for (int32 Column = 0; Column < Out.Width; ++Column)
				{
					if (Rows[Row] & (1u << (Width - 1 - (Left + Column))))
					{
						FMemory::Memset(Out.Bgra.GetData() + (Row * Out.Width + Column) * 4, 0xFF, 4);
					}
				}
			}
			FinishHalfSize(Out, Left, Right);
			// The vanilla client draws the picture cut to its columns from the pen, not from its first column: the ink box it
			// logs for a Hangul syllable (columns 1 to 15) is 0 to 7.5, not 0.5 to 8.
			Out.OffsetX = 0.0f;
			return true;
		}
	};

	struct FLegacyProvider : FMcRasterFonts::FProvider
	{
		TArray<uint8> Sizes;
		FString Template;
		struct FPage
		{
			int32 Size = 0;
			TArray64<uint8> Raw;
		};
		TMap<int32, FPage> Pages;

		const FPage* GetPage(int32 Index)
		{
			if (const FPage* Found = Pages.Find(Index))
			{
				return Found->Size > 0 ? Found : nullptr;
			}
			FPage& Page = Pages.Add(Index);
			TArray<uint8> File;
			IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
			const FString Name = Template.Replace(TEXT("%s"), *FString::Printf(TEXT("%02x"), Index));
			if (FFileHelper::LoadFileToArray(File, *McCache::Asset(Name)) && Wrapper.IsValid() && Wrapper->SetCompressed(File.GetData(), File.Num())
				&& Wrapper->GetRaw(ERGBFormat::BGRA, 8, Page.Raw) && Wrapper->GetWidth() == Wrapper->GetHeight() && Wrapper->GetWidth() >= 16
				&& Wrapper->GetWidth() % 16 == 0)
			{
				Page.Size = Wrapper->GetWidth();
				return &Page;
			}
			return nullptr;
		}

		virtual bool Make(int32 Char, FMcRasterGlyph& Out) override
		{
			if (Char >= Sizes.Num())
			{
				return false;
			}
			const uint8 Size = Sizes[Char];
			const int32 Left = Size >> 4, Right = Size & 15;
			const FPage* Page = Size == 0 ? nullptr : GetPage(Char >> 8);
			if (!Page || Right < Left)
			{
				return false;
			}
			// The cell is 16 pixels in the game's own pages, more in a pack's larger ones.
			const int32 Cell = Page->Size / 16, Ratio = Cell / 16;
			if (Ratio < 1)
			{
				return false;
			}
			const int32 CellX = ((Char & 255) % 16) * Cell, CellY = ((Char & 255) / 16) * Cell;
			Out.Width = (Right - Left + 1) * Ratio;
			Out.Height = Cell;
			Out.Bgra.SetNumUninitialized(Out.Width * Out.Height * 4);
			for (int32 Row = 0; Row < Cell; ++Row)
			{
				for (int32 Column = 0; Column < Out.Width; ++Column)
				{
					const int64 From = (static_cast<int64>(CellY + Row) * Page->Size + CellX + Left * Ratio + Column) * 4;
					FMemory::Memcpy(Out.Bgra.GetData() + (Row * Out.Width + Column) * 4, Page->Raw.GetData() + From, 4);
				}
			}
			FinishHalfSize(Out, Left, Right);
			return true;
		}
	};

	struct FTrueTypeProvider : FMcRasterFonts::FProvider
	{
		TArray<uint8> Data;
		float Size = 11.0f;
		float Oversample = 1.0f;
		FVector2D Shift = FVector2D::ZeroVector;
		TSet<int32> Skip;
		FT_Library Library = nullptr;
		FT_Face Face = nullptr;

		virtual ~FTrueTypeProvider() override
		{
			if (Face)
			{
				FT_Done_Face(Face);
			}
			if (Library)
			{
				FT_Done_FreeType(Library);
			}
		}

		bool Open()
		{
			if (FT_Init_FreeType(&Library) != 0 || FT_New_Memory_Face(Library, Data.GetData(), Data.Num(), 0, &Face) != 0)
			{
				Face = nullptr;
				return false;
			}
			FT_Select_Charmap(Face, FT_ENCODING_UNICODE);
			// The size is the em in pixels (stb_truetype's ScaleForMappingEmToPixels), times the oversample.
			if (Face->units_per_EM == 0)
			{
				return false;
			}
			const double Em = Size * Oversample;
			return FT_Set_Char_Size(Face, 0, static_cast<FT_F26Dot6>(Em * 64.0 + 0.5), 72, 72) == 0;
		}

		virtual bool Make(int32 Char, FMcRasterGlyph& Out) override
		{
			if (!Face || Skip.Contains(Char))
			{
				return false;
			}
			const FT_UInt Index = FT_Get_Char_Index(Face, Char);
			if (Index == 0 || FT_Load_Glyph(Face, Index, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0)
			{
				return false;
			}
			FT_GlyphSlot Slot = Face->glyph;
			// The advance is whole pixels of the oversampled glyph, as the game measures it.
			Out.Advance = static_cast<float>(FMath::RoundToDouble(Slot->linearHoriAdvance / 65536.0) / Oversample);
			if (Slot->format != FT_GLYPH_FORMAT_OUTLINE)
			{
				return true;
			}
			// The shift moves the glyph right and down, in pixels of the game.
			FT_Outline_Translate(&Slot->outline, static_cast<FT_Pos>(Shift.X * Oversample * 64.0), static_cast<FT_Pos>(-Shift.Y * Oversample * 64.0));
			if (FT_Render_Glyph(Slot, FT_RENDER_MODE_NORMAL) != 0 || Slot->bitmap.width == 0 || Slot->bitmap.rows == 0)
			{
				return true;
			}
			const FT_Bitmap& Bitmap = Slot->bitmap;
			Out.Width = Bitmap.width;
			Out.Height = Bitmap.rows;
			Out.Bgra.SetNumUninitialized(Out.Width * Out.Height * 4);
			for (int32 Row = 0; Row < Out.Height; ++Row)
			{
				for (int32 Column = 0; Column < Out.Width; ++Column)
				{
					uint8 Coverage = 0;
					if (Bitmap.pixel_mode == FT_PIXEL_MODE_MONO)
					{
						Coverage = (Bitmap.buffer[Row * Bitmap.pitch + Column / 8] & (0x80 >> (Column % 8))) ? 255 : 0;
					}
					else
					{
						Coverage = Bitmap.buffer[Row * Bitmap.pitch + Column];
					}
					uint8* Pixel = Out.Bgra.GetData() + (Row * Out.Width + Column) * 4;
					Pixel[0] = Pixel[1] = Pixel[2] = 255;
					Pixel[3] = Coverage;
				}
			}
			Out.DrawWidth = Out.Width / Oversample;
			Out.DrawHeight = Out.Height / Oversample;
			Out.OffsetX = Slot->bitmap_left / Oversample;
			Out.OffsetY = 7.0f - Slot->bitmap_top / Oversample;
			return true;
		}
	};
}

FMcRasterFonts::FMcRasterFonts() = default;
FMcRasterFonts::~FMcRasterFonts() = default;

void FMcRasterFonts::AddUnihex(int32 Priority, TArray<uint8> HexText, TArray<FUnihexOverride> Overrides)
{
	TSharedRef<FUnihexProvider> Provider = MakeShared<FUnihexProvider>();
	Provider->Priority = Priority;
	Provider->Text = MoveTemp(HexText);
	Provider->Overrides = MoveTemp(Overrides);
	Providers.Add(Provider);
}

void FMcRasterFonts::AddLegacyUnicode(int32 Priority, TArray<uint8> Sizes, const FString& PageTemplate)
{
	TSharedRef<FLegacyProvider> Provider = MakeShared<FLegacyProvider>();
	Provider->Priority = Priority;
	Provider->Sizes = MoveTemp(Sizes);
	Provider->Template = PageTemplate;
	Providers.Add(Provider);
}

void FMcRasterFonts::AddTrueType(int32 Priority, TArray<uint8> File, float Size, float Oversample, FVector2D Shift, const FString& Skip)
{
	TSharedRef<FTrueTypeProvider> Provider = MakeShared<FTrueTypeProvider>();
	Provider->Priority = Priority;
	Provider->Data = MoveTemp(File);
	Provider->Size = Size;
	Provider->Oversample = FMath::Max(Oversample, 1.0f);
	Provider->Shift = Shift;
	for (int32 At = 0; At < Skip.Len();)
	{
		const int32 Char = Skip[At];
		const bool bPair = Char >= 0xd800 && Char < 0xdc00 && At + 1 < Skip.Len() && Skip[At + 1] >= 0xdc00 && Skip[At + 1] < 0xe000;
		Provider->Skip.Add(bPair ? 0x10000 + ((Char - 0xd800) << 10) + (Skip[At + 1] - 0xdc00) : Char);
		At += bPair ? 2 : 1;
	}
	if (Provider->Open())
	{
		Providers.Add(Provider);
	}
}

bool FMcRasterFonts::Make(int32 Char, int32 BeforePriority, FMcRasterGlyph& Out)
{
	for (const TSharedPtr<FProvider>& Provider : Providers)
	{
		if (Provider->Priority >= BeforePriority)
		{
			break;
		}
		Out = FMcRasterGlyph();
		if (Provider->Make(Char, Out))
		{
			return true;
		}
	}
	return false;
}
