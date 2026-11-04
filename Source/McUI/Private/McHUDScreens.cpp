// The parts of the HUD that use the player's data: the bars, the hotbar with its items, and the container screens
// with their mouse and key handling. The sprites are the jar's own (textures/gui/sprites, textures/gui/container).

#include "Kismet/GameplayStatics.h"
#include "McHUD.h"
#include "McFontRaster.h"
#include "McDropTarget.h"
#include "Engine/GameViewportClient.h"
#include "McEffects.h"
#include "McRenderMode.h"
#include "McSkyComponent.h"
#include "McLocalPlayer.h"
#include "McLaunch.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "InputCoreTypes.h"
#include "Algo/Reverse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "McResourcePacks.h"
#include "McCache.h"
#include "McFont.h"
#include "McGameViewportClient.h"
#include "McItemRegistry.h"
#include "McLang.h"
#include "McSound.h"
#include "McBiomeRegistry.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McLightEngine.h"
#include "McRaycast.h"
#include "McContainer.h"
#include "McCraftingMenu.h"
#include "McEnchantMenu.h"
#include "McWorkstationMenus.h"
#include "McAnvilMenu.h"
#include "McSignMenu.h"
#include "McLecternMenu.h"
#include "McFurnace.h"
#include "McBrewingStand.h"
#include "McMenuInput.h"
#include "McPlayerData.h"
#include "McPlayerPawn.h"
#include "McTradeMenu.h"
#include "McScreenLayout.h"
#include "McTotem.h"
#include "McUI.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"

struct AMcHUD::FScreenState
{
	int32 WindowId = -1;
	TSharedPtr<FMcMenuInput> Input;
	McGui::FScreenLayout Layout;
	TSharedPtr<FMcFurnace> Furnace;
	TSharedPtr<FMcBrewingStand> Brewing;
	// The enchanting table's menu (Input holds it).
	FMcEnchantMenu* Enchanting = nullptr;
	// The stonecutter's menu and how far down its recipes are scrolled.
	FMcStonecutterMenu* Stonecutter = nullptr;
	int32 RecipeScroll = 0;
	// The loom's menu (Input holds it), its patterns scrolled the same way.
	FMcLoomMenu* Loom = nullptr;
	// The trading menu; its offers are scrolled by RecipeScroll too.
	FMcTradeMenu* Trading = nullptr;
	// The anvil's menu, the name typed, and the item the name was set for.
	FMcAnvilMenu* Anvil = nullptr;
	FString AnvilName;
	FMcItemStack AnvilFor;
	// The sign editor's menu, the line being written (0 to 3) and where in it the cursor is.
	FMcSignMenu* Sign = nullptr;
	int32 SignLine = 0;
	int32 SignCursor = 0;
	// A lectern's book being read.
	FMcLecternMenu* Lectern = nullptr;
	// Whether keys go to a text field (the creative search, the anvil's name, a sign) rather than the slots; a book
	// takes its keys too (E doesn't close it).
	bool IsTyping() const { return bCreative || Anvil != nullptr || Sign != nullptr || Lectern != nullptr; }
	FString Title;
	// The creative screen: what the search finds, how far down the list is scrolled, the item under the mouse (an
	// index into CreativeItems) and whether the scroll bar is being dragged.
	bool bCreative = false;
	FString Search;
	TArray<FName> CreativeItems;
	int32 CreativeScroll = 0;
	int32 CreativeHover = -1;
	bool bScrollDrag = false;
};

namespace
{
	const TCHAR* SpriteRoot = TEXT("textures/gui/sprites/");

	// "oak_log" -> "Oak Log".
	FString TitleCase(const FString& Path)
	{
		FString Out;
		bool bStart = true;
		for (const TCHAR Char : Path)
		{
			Out.AppendChar(Char == TEXT('_') ? TEXT(' ') : bStart ? FChar::ToUpper(Char) : Char);
			bStart = Char == TEXT('_');
		}
		return Out;
	}

	// The name the jar gives an item (as an item, else as a block), else made from its id.
	FString DisplayName(FName Item)
	{
		const FString Path = McCache::PathOf(Item.ToString());
		for (const TCHAR* Prefix : { TEXT("item.minecraft."), TEXT("block.minecraft.") })
		{
			const FString Key = Prefix + Path;
			FString Name = McLang::Get(Key);
			if (Name != Key)
			{
				return Name;
			}
		}
		return TitleCase(Path);
	}

	FString GetItemName(const FMcItemStack& Stack)
	{
		const FString Custom = Stack.GetCustomName();
		return Custom.IsEmpty() ? DisplayName(Stack.Item) : Custom;
	}

	// The creative list for a search.
	TArray<FName> CreativeList(const FString& Search)
	{
		static TArray<FName> All;
		FMcItemRegistry& Items = FMcItemRegistry::Get();
		if (All.Num() == 0 && Items.EnsureLoaded())
		{
			for (const FMcItemDef& Item : Items.GetAll())
			{
				if (McGui::Creative::IsListed(Item.Name))
				{
					All.Add(Item.Name);
				}
			}
		}
		return McGui::Creative::Filter(All, Search, [](FName Item) { return DisplayName(Item); });
	}

	FLinearColor FromRgb(uint32 Rgb, float Alpha = 1.0f)
	{
		FLinearColor Color(FColor((Rgb >> 16) & 0xFF, (Rgb >> 8) & 0xFF, Rgb & 0xFF));
		Color.A = Alpha;
		return Color;
	}
}

AMcHUD::AMcHUD() = default;
AMcHUD::~AMcHUD()
{
	if (DropTarget.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(DropTarget.ToSharedRef());
	}
}

void AMcHUD::DrawRegion(UTexture2D* Texture, float X, float Y, float Width, float Height, float U, float V, float UW, float VH, const FLinearColor& Tint)
{
	if (!Texture || !Texture->GetResource())
	{
		return;
	}
	const float TexW = static_cast<float>(Texture->GetSizeX());
	const float TexH = static_cast<float>(Texture->GetSizeY());
	FCanvasTileItem Tile(FVector2D(X, Y) * Scale, Texture->GetResource(), FVector2D(Width, Height) * Scale, FVector2D(U / TexW, V / TexH), FVector2D((U + UW) / TexW, (V + VH) / TexH), Tint);
	Tile.BlendMode = SE_BLEND_Translucent;
	Canvas->DrawItem(Tile);
}

void AMcHUD::DrawFill(float X, float Y, float Width, float Height, const FLinearColor& Color)
{
	DrawRect(Color, X * Scale, Y * Scale, Width * Scale, Height * Scale);
}

// ---- Text ----

namespace
{
	// The pictures a font definition names, by sheet file; read once per load.
	struct FSheet
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray64<uint8> Raw;
	};

	// The providers of a font definition in priority order: a pack's copy of the file first (the topmost pack first),
	// then the game's. A "reference" provider is replaced by the providers of the file it names.
	void CollectProviders(const FString& Relative, int32 Depth, TArray<TSharedPtr<FJsonObject>>& Out)
	{
		if (Depth > 4)
		{
			return;
		}
		TArray<TArray<uint8>> Files;
		McPacks::ReadAll(Relative, Files);
		Algo::Reverse(Files);
		TArray<uint8> Vanilla;
		const FString InJar = FPaths::Combine(McCache::Root(), TEXT("client/assets/minecraft"), Relative);
		FFileHelper::LoadFileToArray(Vanilla, *InJar);
		// The jar's include/unifont.json has no providers (the launcher builds it, with the size overrides of the CJK and Hangul
		// blocks); the launcher's own copy, with the asset files, has them.
		TArray<uint8> FromAssets;
		if (FString(reinterpret_cast<const char*>(Vanilla.GetData()), Vanilla.Num()).Contains(TEXT("\"type\"")) == false
			&& FFileHelper::LoadFileToArray(FromAssets, *FPaths::Combine(McCache::Root(), TEXT("assets/minecraft"), Relative)))
		{
			Vanilla = MoveTemp(FromAssets);
		}
		if (Vanilla.Num() > 0)
		{
			Files.Add(MoveTemp(Vanilla));
		}
		for (const TArray<uint8>& File : Files)
		{
			FString Text;
			FFileHelper::BufferToString(Text, File.GetData(), File.Num());
			// The empty cells of a sheet are NUL characters in the lines of characters, which would end the string early.
			Text.ReplaceInline(TEXT("\\u0000"), TEXT("\\uffff"), ESearchCase::IgnoreCase);
			TSharedPtr<FJsonObject> Root;
			const TArray<TSharedPtr<FJsonValue>>* Providers = nullptr;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid() || !Root->TryGetArrayField(TEXT("providers"), Providers))
			{
				continue;
			}
			for (const TSharedPtr<FJsonValue>& Value : *Providers)
			{
				const TSharedPtr<FJsonObject>* Provider;
				if (!Value->TryGetObject(Provider))
				{
					continue;
				}
				// A filter names options the provider needs switched on; none of them is on here.
				const TSharedPtr<FJsonObject>* Filter;
				bool bSkip = false;
				if ((*Provider)->TryGetObjectField(TEXT("filter"), Filter))
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Option : (*Filter)->Values)
					{
						bool bOn = false;
						bSkip |= Option.Value->TryGetBool(bOn) && bOn;
					}
				}
				if (bSkip)
				{
					continue;
				}
				FString Type;
				(*Provider)->TryGetStringField(TEXT("type"), Type);
				if (Type == TEXT("reference"))
				{
					FString Id;
					if ((*Provider)->TryGetStringField(TEXT("id"), Id))
					{
						Id.RemoveFromStart(TEXT("minecraft:"));
						CollectProviders(TEXT("font/") + Id + TEXT(".json"), Depth + 1, Out);
					}
				}
				else
				{
					Out.Add(*Provider);
				}
			}
		}
	}

	// The .hex file inside a Unifont zip of the packs, or of the game's asset files (the launcher's, next to the jar's).
	bool ReadHexZip(const FString& Relative, TArray<uint8>& Out)
	{
		FString Path;
		if (!McPacks::TryResolve(Relative, Path))
		{
			Path = FPaths::Combine(McCache::Root(), TEXT("assets/minecraft"), Relative);
		}
		return FMcResourcePack::ReadZipEntry(Path, TEXT(".hex"), Out);
	}

	// The code point at Text[At] (a surrogate pair is one), and At moved past it.
	int32 NextCodePoint(const FString& Text, int32& At)
	{
		const int32 High = Text[At++];
		if (High >= 0xd800 && High < 0xdc00 && At < Text.Len() && Text[At] >= 0xdc00 && Text[At] < 0xe000)
		{
			return 0x10000 + ((High - 0xd800) << 10) + (Text[At++] - 0xdc00);
		}
		return High;
	}

	bool ReadSheet(const FString& Path, FSheet& Sheet)
	{
		TArray<uint8> File;
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		if (!FFileHelper::LoadFileToArray(File, *Path) || !Wrapper.IsValid() || !Wrapper->SetCompressed(File.GetData(), File.Num())
			|| !Wrapper->GetRaw(ERGBFormat::BGRA, 8, Sheet.Raw))
		{
			return false;
		}
		Sheet.Width = Wrapper->GetWidth();
		Sheet.Height = Wrapper->GetHeight();
		return Sheet.Raw.Num() == static_cast<int64>(Sheet.Width) * Sheet.Height * 4;
	}
}

// The glyph table from the font definition font/default.json, as the game reads it: the first provider that has a character
// wins. "bitmap" providers cut a sheet into a grid of the characters of their lines (a glyph is as wide as its rightmost
// opaque pixel, in the pixels of a glyph "height" tall, plus a gap of 1), "space" providers give plain advances. The
// other kinds ("ttf", "unihex", "legacy_unicode") draw a character when it is first needed (FindGlyph); where the
// definition of the game has none for Unifont, its own .hex files stand in at the end.
void AMcHUD::LoadFont()
{
	if (Glyphs.Num() > 0)
	{
		return;
	}
	McFont::ReadGlyphWidths(TEXT("textures/font/ascii_sga.png"), GalacticWidths);

	TArray<TSharedPtr<FJsonObject>> Providers;
	CollectProviders(TEXT("font/default.json"), 0, Providers);
	RasterFonts = MakeShared<FMcRasterFonts>();
	RasterAsked.Reset();
	bool bHasUnihex = false;
	for (int32 Priority = 0; Priority < Providers.Num(); ++Priority)
	{
		const TSharedPtr<FJsonObject>& Provider = Providers[Priority];
		FString Type;
		Provider->TryGetStringField(TEXT("type"), Type);
		if (Type == TEXT("unihex"))
		{
			FString HexFile;
			TArray<uint8> Hex;
			if (Provider->TryGetStringField(TEXT("hex_file"), HexFile))
			{
				HexFile.RemoveFromStart(TEXT("minecraft:"));
				if (ReadHexZip(HexFile, Hex))
				{
					TArray<FMcRasterFonts::FUnihexOverride> Overrides;
					const TArray<TSharedPtr<FJsonValue>>* Entries;
					if (Provider->TryGetArrayField(TEXT("size_overrides"), Entries))
					{
						for (const TSharedPtr<FJsonValue>& Entry : *Entries)
						{
							const TSharedPtr<FJsonObject>* Object;
							FString From, To;
							double Left = 0, Right = 0;
							if (Entry->TryGetObject(Object) && (*Object)->TryGetStringField(TEXT("from"), From) && (*Object)->TryGetStringField(TEXT("to"), To)
								&& From.Len() == 1 && To.Len() == 1)
							{
								(*Object)->TryGetNumberField(TEXT("left"), Left);
								(*Object)->TryGetNumberField(TEXT("right"), Right);
								Overrides.Add({ From[0], To[0], static_cast<int32>(Left), static_cast<int32>(Right) });
							}
						}
					}
					RasterFonts->AddUnihex(Priority, MoveTemp(Hex), MoveTemp(Overrides));
					bHasUnihex = true;
				}
			}
		}
		else if (Type == TEXT("legacy_unicode"))
		{
			FString SizesFile, Template;
			TArray<uint8> Sizes;
			if (Provider->TryGetStringField(TEXT("sizes"), SizesFile) && Provider->TryGetStringField(TEXT("template"), Template))
			{
				SizesFile.RemoveFromStart(TEXT("minecraft:"));
				Template.RemoveFromStart(TEXT("minecraft:"));
				// The pages are pictures, so the template is relative to textures/.
				Template = TEXT("textures/") + Template;
				if (FFileHelper::LoadFileToArray(Sizes, *McCache::Asset(SizesFile)))
				{
					RasterFonts->AddLegacyUnicode(Priority, MoveTemp(Sizes), Template);
				}
			}
		}
		else if (Type == TEXT("ttf"))
		{
			FString File, Skip;
			double Size = 11.0, Oversample = 1.0;
			FVector2D Shift = FVector2D::ZeroVector;
			const TArray<TSharedPtr<FJsonValue>>* ShiftArray;
			if (Provider->TryGetArrayField(TEXT("shift"), ShiftArray) && ShiftArray->Num() >= 2)
			{
				Shift = FVector2D((*ShiftArray)[0]->AsNumber(), (*ShiftArray)[1]->AsNumber());
			}
			Provider->TryGetNumberField(TEXT("size"), Size);
			Provider->TryGetNumberField(TEXT("oversample"), Oversample);
			// "skip" is one string or a list of them.
			const TArray<TSharedPtr<FJsonValue>>* SkipArray;
			if (Provider->TryGetArrayField(TEXT("skip"), SkipArray))
			{
				for (const TSharedPtr<FJsonValue>& Value : *SkipArray)
				{
					Skip += Value->AsString();
				}
			}
			else
			{
				Provider->TryGetStringField(TEXT("skip"), Skip);
			}
			TArray<uint8> Data;
			if (Provider->TryGetStringField(TEXT("file"), File))
			{
				File.RemoveFromStart(TEXT("minecraft:"));
				// The font file is relative to font/ ("minecraft:mojangles.ttf" is assets/minecraft/font/mojangles.ttf).
				if (FFileHelper::LoadFileToArray(Data, *McCache::Asset(TEXT("font/") + File)) || FFileHelper::LoadFileToArray(Data, *McCache::Asset(File)))
				{
					RasterFonts->AddTrueType(Priority, MoveTemp(Data), static_cast<float>(Size), static_cast<float>(Oversample), Shift, Skip);
				}
			}
		}
		else if (Type == TEXT("space"))
		{
			const TSharedPtr<FJsonObject>* Advances;
			if (Provider->TryGetObjectField(TEXT("advances"), Advances))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Advances)->Values)
				{
					double Advance = 0.0;
					int32 KeyAt = 0;
					const int32 KeyChar = Entry.Key.IsEmpty() ? 0 : NextCodePoint(Entry.Key, KeyAt);
					if (KeyChar != 0 && KeyAt == Entry.Key.Len() && Entry.Value->TryGetNumber(Advance) && !Glyphs.Contains(KeyChar))
					{
						FGlyph& Glyph = Glyphs.Add(KeyChar);
						Glyph.Priority = Priority;
						Glyph.Advance = static_cast<float>(Advance);
					}
				}
			}
		}
		else if (Type == TEXT("bitmap"))
		{
			FString File;
			const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
			double Height = 8.0, Ascent = 0.0;
			if (!Provider->TryGetStringField(TEXT("file"), File) || !Provider->TryGetArrayField(TEXT("chars"), Lines) || Lines->Num() == 0)
			{
				continue;
			}
			Provider->TryGetNumberField(TEXT("height"), Height);
			Provider->TryGetNumberField(TEXT("ascent"), Ascent);
			File.RemoveFromStart(TEXT("minecraft:"));
			const FString Sprite = TEXT("textures/") + File;
			FSheet Sheet;
			if (!ReadSheet(McCache::Asset(Sprite), Sheet))
			{
				UE_LOG(LogMcUI, Warning, TEXT("Can't read the font %s"), *Sprite);
				continue;
			}
			// A cell is a code point: one beyond the basic plane is two UTF-16 units here but one cell.
			TArray<TArray<int32>> Rows;
			int32 Columns = 0;
			for (const TSharedPtr<FJsonValue>& Line : *Lines)
			{
				const FString Text = Line->AsString();
				TArray<int32>& Row = Rows.AddDefaulted_GetRef();
				for (int32 At = 0; At < Text.Len();)
				{
					Row.Add(NextCodePoint(Text, At));
				}
				Columns = FMath::Max(Columns, Row.Num());
			}
			const int32 CellWidth = Sheet.Width / FMath::Max(Columns, 1), CellHeight = Sheet.Height / Rows.Num();
			if (CellWidth < 1 || CellHeight < 1 || Height <= 0.0)
			{
				continue;
			}
			const float GlyphScale = static_cast<float>(Height) / CellHeight;
			for (int32 Row = 0; Row < Rows.Num(); ++Row)
			{
				for (int32 Column = 0; Column < Rows[Row].Num(); ++Column)
				{
					const int32 Char = Rows[Row][Column];
					if (Char == 0 || Char == 0xffff || Glyphs.Contains(Char))
					{
						continue;
					}
					int32 Widest = 0;
					for (int32 X = 0; X < CellWidth; ++X)
					{
						for (int32 Y = 0; Y < CellHeight; ++Y)
						{
							const int64 Pixel = (static_cast<int64>(Row * CellHeight + Y) * Sheet.Width + Column * CellWidth + X) * 4;
							if (Sheet.Raw[Pixel + 3] != 0)
							{
								Widest = X + 1;
							}
						}
					}
					FGlyph& Glyph = Glyphs.Add(Char);
					Glyph.Priority = Priority;
					Glyph.Sprite = Sprite;
					Glyph.U = static_cast<float>(Column * CellWidth);
					Glyph.V = static_cast<float>(Row * CellHeight);
					Glyph.CellWidth = static_cast<float>(CellWidth);
					Glyph.CellHeight = static_cast<float>(CellHeight);
					Glyph.DrawWidth = CellWidth * GlyphScale;
					Glyph.DrawHeight = static_cast<float>(Height);
					Glyph.OffsetY = static_cast<float>(7.0 - Ascent);
					Glyph.Advance = FMath::CeilToFloat(Widest * GlyphScale) + 1.0f;
				}
			}
		}
	}
	if (!bHasUnihex)
	{
		// The jar's include/unifont.json lists no providers (the launcher builds it); the data is in the asset files.
		for (const TCHAR* Zip : { TEXT("font/unifont.zip"), TEXT("font/unifont_pua.zip") })
		{
			TArray<uint8> Hex;
			if (ReadHexZip(Zip, Hex))
			{
				RasterFonts->AddUnihex(Providers.Num(), MoveTemp(Hex), {});
			}
		}
	}
	if (!Glyphs.Contains(TEXT(' ')))
	{
		Glyphs.Add(TEXT(' ')).Advance = 4.0f;
	}
}

// The character's glyph: from the sheet providers read at once, unless a provider before that one draws it itself, which
// happens the first time it is asked for. A character nothing has shows as "?".
const AMcHUD::FGlyph* AMcHUD::FindGlyph(int32 Char) const
{
	const FGlyph* Found = Glyphs.Find(Char);
	if (RasterFonts.IsValid() && !RasterAsked.Contains(Char))
	{
		AMcHUD* Self = const_cast<AMcHUD*>(this);
		Self->RasterAsked.Add(Char);
		FMcRasterGlyph Raster;
		if (RasterFonts->Make(Char, Found ? Found->Priority : MAX_int32, Raster))
		{
			FGlyph Glyph;
			Glyph.Texture = Raster.Width > 0 ? Self->MakeGlyphTexture(Raster) : nullptr;
			Glyph.CellWidth = static_cast<float>(Raster.Width);
			Glyph.CellHeight = static_cast<float>(Raster.Height);
			Glyph.DrawWidth = Raster.DrawWidth;
			Glyph.DrawHeight = Raster.DrawHeight;
			Glyph.OffsetX = Raster.OffsetX;
			Glyph.OffsetY = Raster.OffsetY;
			Glyph.Advance = Raster.Advance;
			Glyph.ShadowOffset = Raster.ShadowOffset;
			Glyph.Priority = -1;
			Found = &Self->Glyphs.Add(Char, Glyph);
		}
	}
	if (Found)
	{
		return Found;
	}
	return Char == TEXT('?') ? nullptr : FindGlyph(TEXT('?'));
}

float AMcHUD::GetGlyphAdvance(int32 Char)
{
	LoadFont();
	const FGlyph* Glyph = FindGlyph(Char);
	return Glyph ? Glyph->Advance : 6.0f;
}

float AMcHUD::GetGlyphShadowOffset(int32 Char)
{
	LoadFont();
	const FGlyph* Glyph = FindGlyph(Char);
	return Glyph ? Glyph->ShadowOffset : 1.0f;
}

bool AMcHUD::GetGlyphInk(int32 Char, float& Left, float& Right, float& Up, float& Down)
{
	LoadFont();
	const FGlyph* Glyph = FindGlyph(Char);
	if (!Glyph || Glyph->DrawWidth <= 0.0f)
	{
		return false;
	}
	Left = Glyph->OffsetX;
	Right = Glyph->OffsetX + Glyph->DrawWidth;
	Up = Glyph->OffsetY;
	Down = Glyph->OffsetY + Glyph->DrawHeight;
	return true;
}

float AMcHUD::GetTextWidth(const FString& Text)
{
	LoadFont();
	return TextWidth(Text);
}

float AMcHUD::TextWidth(const FString& Text) const
{
	float Width = 0.0f;
	for (int32 At = 0; At < Text.Len();)
	{
		const FGlyph* Glyph = FindGlyph(NextCodePoint(Text, At));
		Width += Glyph ? Glyph->Advance : 6.0f;
	}
	return Width;
}

void AMcHUD::DrawGuiText(const FString& Text, float X, float Y, uint32 Rgb, bool bShadow, float Alpha, float TextScale)
{
	LoadFont();
	// The shadow is the colour at a quarter of its brightness, moved down and right by a pixel (half a pixel for Unifont).
	const auto Draw = [&](bool bShadowPass, const FLinearColor& Tint)
	{
		float At = X;
		for (int32 CharAt = 0; CharAt < Text.Len();)
		{
			const FGlyph* Glyph = FindGlyph(NextCodePoint(Text, CharAt));
			if (!Glyph)
			{
				At += 6.0f * TextScale;
				continue;
			}
			UTexture2D* Picture = Glyph->Texture ? Glyph->Texture : Glyph->Sprite.IsEmpty() ? nullptr : LoadSprite(Glyph->Sprite);
			if (Picture)
			{
				const float Shadow = bShadowPass ? Glyph->ShadowOffset * TextScale : 0.0f;
				DrawRegion(Picture, At + Glyph->OffsetX * TextScale + Shadow, Y + Glyph->OffsetY * TextScale + Shadow, Glyph->DrawWidth * TextScale,
					Glyph->DrawHeight * TextScale, Glyph->U, Glyph->V, Glyph->CellWidth, Glyph->CellHeight, Tint);
			}
			At += Glyph->Advance * TextScale;
		}
	};
	if (bShadow)
	{
		Draw(true, FromRgb(Rgb) * 0.25f * Alpha);
	}
	Draw(false, FromRgb(Rgb, Alpha));
}

// ---- Items ----

void AMcHUD::DrawStack(const FMcItemStack& Stack, float X, float Y, FName Picture)
{
	if (Stack.IsEmpty())
	{
		return;
	}
	DrawIcon(!Picture.IsNone() ? GetItemIcon(Picture) : Stack.Item.ToString().EndsWith(TEXT("_banner")) ? GetBannerIcon(Stack)
		: Stack.Item == TEXT("minecraft:decorated_pot") ? GetPotIcon(Stack) : GetItemIcon(Stack.Item), X, Y);
	const McGui::FDurabilityBar Bar = McGui::GetDurabilityBar(Stack.GetDamage(), Stack.GetMaxDamage());
	if (Bar.bShow)
	{
		DrawFill(X + 2, Y + 13, 13, 2, FLinearColor::Black);
		DrawFill(X + 2, Y + 13, static_cast<float>(Bar.Width), 1, FromRgb(Bar.Color));
	}
	if (McGui::ShowsCount(Stack.Count))
	{
		const FString Count = FString::FromInt(Stack.Count);
		DrawGuiText(Count, X + 17 - TextWidth(Count) + 1, Y + 9, 0xFFFFFF, true);
	}
}

// ---- The bars ----

void AMcHUD::DrawBars(int32 Width, int32 Height, UMcPlayerDataComponent* Data)
{
	const FMcPlayerStats& Stats = Data->Stats;
	const McGui::FHudLayout Layout = McGui::ComputeHud(Width, Height, Stats, ShakeCounter / 3);
	const FString Hud = FString(SpriteRoot) + TEXT("hud/");
	const FString Hardcore = Stats.bHardcore ? TEXT("hardcore_") : TEXT("");
	// Poison turns the hearts green and wither (which wins) black.
	const auto HasEffect = [&](const TCHAR* Id) { return Stats.Effects.ContainsByPredicate([Id](const FMcStatusEffect& Effect) { return Effect.Id == Id; }); };
	const FString Style = HasEffect(TEXT("wither")) ? TEXT("withered_") : HasEffect(TEXT("poison")) ? TEXT("poisoned_") : TEXT("");

	for (const McGui::FBarIcon& Heart : Layout.Hearts)
	{
		const FString Blink = Heart.bBlink ? TEXT("_blinking") : TEXT("");
		DrawSprite(LoadSprite(Hud + TEXT("heart/container") + (Stats.bHardcore ? TEXT("_hardcore") : TEXT("")) + Blink + TEXT(".png")), Heart.Pos.X, Heart.Pos.Y, 9, 9);
		if (Heart.Fill != McGui::EIcon::Empty)
		{
			DrawSprite(LoadSprite(Hud + TEXT("heart/") + Style + Hardcore + (Heart.Fill == McGui::EIcon::Full ? TEXT("full") : TEXT("half")) + Blink + TEXT(".png")), Heart.Pos.X, Heart.Pos.Y, 9, 9);
		}
	}
	for (const McGui::FBarIcon& Heart : Layout.Absorption)
	{
		if (Heart.Fill != McGui::EIcon::Empty)
		{
			DrawSprite(LoadSprite(Hud + TEXT("heart/absorbing_") + Hardcore + (Heart.Fill == McGui::EIcon::Full ? TEXT("full") : TEXT("half")) + TEXT(".png")), Heart.Pos.X, Heart.Pos.Y, 9, 9);
		}
	}
	if (Layout.bShowArmor)
	{
		for (const McGui::FBarIcon& Icon : Layout.Armor)
		{
			const TCHAR* Name = Icon.Fill == McGui::EIcon::Full ? TEXT("armor_full") : Icon.Fill == McGui::EIcon::Half ? TEXT("armor_half") : TEXT("armor_empty");
			DrawSprite(LoadSprite(Hud + Name + TEXT(".png")), Icon.Pos.X, Icon.Pos.Y, 9, 9);
		}
	}
	for (const McGui::FBarIcon& Icon : Layout.Food)
	{
		DrawSprite(LoadSprite(Hud + TEXT("food_empty.png")), Icon.Pos.X, Icon.Pos.Y, 9, 9);
		if (Icon.Fill != McGui::EIcon::Empty)
		{
			DrawSprite(LoadSprite(Hud + (Icon.Fill == McGui::EIcon::Full ? TEXT("food_full.png") : TEXT("food_half.png"))), Icon.Pos.X, Icon.Pos.Y, 9, 9);
		}
	}
	for (const McGui::FBarIcon& Icon : Layout.Mount)
	{
		DrawSprite(LoadSprite(Hud + TEXT("heart/vehicle_container.png")), Icon.Pos.X, Icon.Pos.Y, 9, 9);
		if (Icon.Fill != McGui::EIcon::Empty)
		{
			DrawSprite(LoadSprite(Hud + (Icon.Fill == McGui::EIcon::Full ? TEXT("heart/vehicle_full.png") : TEXT("heart/vehicle_half.png"))), Icon.Pos.X, Icon.Pos.Y, 9, 9);
		}
	}
	if (Layout.bShowAir)
	{
		for (const McGui::FBarIcon& Icon : Layout.Air)
		{
			DrawSprite(LoadSprite(Hud + (Icon.Fill == McGui::EIcon::Full ? TEXT("air.png") : TEXT("air_bursting.png"))), Icon.Pos.X, Icon.Pos.Y, 9, 9);
		}
	}
	if (Layout.bShowJump)
	{
		DrawSprite(LoadSprite(Hud + TEXT("jump_bar_background.png")), Layout.XpBar.X, Layout.XpBar.Y, 182, 5);
		if (Layout.JumpFill > 0)
		{
			DrawRegion(LoadSprite(Hud + TEXT("jump_bar_progress.png")), Layout.XpBar.X, Layout.XpBar.Y, static_cast<float>(Layout.JumpFill), 5, 0, 0, static_cast<float>(Layout.JumpFill), 5);
		}
	}
	else
	{
		DrawSprite(LoadSprite(Hud + TEXT("experience_bar_background.png")), Layout.XpBar.X, Layout.XpBar.Y, 182, 5);
	}
	if (!Layout.bShowJump && Layout.XpFill > 0)
	{
		DrawRegion(LoadSprite(Hud + TEXT("experience_bar_progress.png")), Layout.XpBar.X, Layout.XpBar.Y, static_cast<float>(Layout.XpFill), 5, 0, 0, static_cast<float>(Layout.XpFill), 5);
	}
	if (Layout.bShowLevel)
	{
		LoadFont();
		const FString Level = FString::FromInt(Stats.XpLevel);
		const float X = Layout.LevelText.X - TextWidth(Level) / 2.0f;
		const float Y = static_cast<float>(Layout.LevelText.Y);
		for (const FVector2D& Offset : { FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1) })
		{
			DrawGuiText(Level, X + Offset.X, Y + Offset.Y, 0x000000, false);
		}
		DrawGuiText(Level, X, Y, 0x80FF20, false);
	}
}

void AMcHUD::DrawRaidBar(int32 Width, UMcPlayerDataComponent* Data)
{
	if (Data->RaidBarFraction < 0.0f)
	{
		return;
	}
	const float X = (static_cast<float>(Width) - 182.0f) * 0.5f;
	DrawGuiText(Data->RaidBarTitle, (static_cast<float>(Width) - TextWidth(Data->RaidBarTitle)) * 0.5f, 3.0f, 0xFFFFFF, true);
	DrawSprite(LoadSprite(FString(SpriteRoot) + TEXT("boss_bar/red_background.png")), X, 12.0f, 182, 5);
	DrawSprite(LoadSprite(FString(SpriteRoot) + TEXT("boss_bar/notched_10_background.png")), X, 12.0f, 182, 5);
	const float Filled = FMath::Clamp(Data->RaidBarFraction, 0.0f, 1.0f);
	if (Filled > 0.0f)
	{
		// The progress pictures are 182 wide; only the filled part of them is shown.
		const float Part = 182.0f * Filled;
		DrawRegion(LoadSprite(FString(SpriteRoot) + TEXT("boss_bar/red_progress.png")), X, 12.0f, Part, 5.0f, 0.0f, 0.0f, Part, 5.0f);
		DrawRegion(LoadSprite(FString(SpriteRoot) + TEXT("boss_bar/notched_10_progress.png")), X, 12.0f, Part, 5.0f, 0.0f, 0.0f, Part, 5.0f);
	}
}

void AMcHUD::DrawEffects(int32 Width, int32 Height, UMcPlayerDataComponent* Data)
{
	const float ScreenWidth = static_cast<float>(Width);
	int32 Good = 0, Bad = 0;
	for (const FMcStatusEffect& Effect : Data->Stats.Effects)
	{
		// The last ten seconds, the icon blinks.
		if (Effect.Duration <= 200 && (Effect.Duration / 5) % 2 == 0)
		{
			continue;
		}
		const bool bHarmful = McEffects::IsHarmful(Effect.Id);
		const float X = ScreenWidth - 25.0f * ((bHarmful ? Bad++ : Good++) + 1);
		const float Y = bHarmful ? 26.0f : 1.0f;
		DrawSprite(LoadSprite(FString(SpriteRoot) + TEXT("hud/effect_background.png")), X, Y, 24, 24);
		DrawSprite(LoadSprite(TEXT("textures/mob_effect/") + Effect.Id.ToString() + TEXT(".png")), X + 3, Y + 3, 18, 18);
	}
}

// ---- Screens ----

void AMcHUD::ShowScreen(TSharedRef<FScreenState> State)
{
	Screen = State;
	// Nothing typed before it opened (the E that opened it) goes into its search.
	UMcGameViewportClient::TakeTyped();
	if (APlayerController* Player = GetOwningPlayerController())
	{
		Player->bShowMouseCursor = true;
		Player->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
		Player->SetIgnoreLookInput(true);
		Player->SetIgnoreMoveInput(true);
	}
}

void AMcHUD::HideScreen()
{
	if (!Screen.IsValid())
	{
		return;
	}
	Screen.Reset();
	HoverSlot = INDEX_NONE;
	if (APlayerController* Player = GetOwningPlayerController())
	{
		Player->bShowMouseCursor = false;
		Player->SetInputMode(FInputModeGameOnly());
		Player->SetIgnoreLookInput(false);
		Player->SetIgnoreMoveInput(false);
	}
}

void AMcHUD::SyncScreen(UMcPlayerDataComponent* Data)
{
	// The window is the data component's: the player opens their own (E) or the server opens one on a block, and it goes
	// when either side closes it. The screen is only how it looks and where the mouse goes.
	const FMcWindow& Window = Data ? Data->GetWindow() : FMcWindow();
	if (!Window.IsOpen())
	{
		HideScreen();
		return;
	}
	if (Screen.IsValid() && Screen->WindowId == Window.Id)
	{
		return;
	}
	HideScreen();
	TSharedRef<FScreenState> State = MakeShared<FScreenState>();
	State->WindowId = Window.Id;
	State->Input = MakeShared<FMcMenuInput>(Window.Menu, Data->bCreative);
	State->Input->OnClick = [WeakData = TWeakObjectPtr<UMcPlayerDataComponent>(Data)](int32 Slot, int32 Button, EMcClickType Type)
	{
		if (UMcPlayerDataComponent* Owner = WeakData.Get())
		{
			Owner->NotifyClick(Slot, Button, Type);
		}
	};
	State->Input->OnDrag = [WeakData = TWeakObjectPtr<UMcPlayerDataComponent>(Data)](int32 Button, const TArray<int32>& Slots)
	{
		if (UMcPlayerDataComponent* Owner = WeakData.Get())
		{
			Owner->NotifyDrag(Button, Slots);
		}
	};
	State->Title = Window.Title;
	State->Furnace = Window.Furnace;
	State->Brewing = Window.Brewing;
	State->Enchanting = Window.Enchanting;
	switch (Window.Kind)
	{
	case EMcWindowKind::EnchantingTable:
		State->Layout = McGui::MakeEnchantingLayout();
		break;
	case EMcWindowKind::Stonecutter:
		State->Layout = McGui::MakeStonecutterLayout();
		State->Stonecutter = static_cast<FMcStonecutterMenu*>(Window.Menu.Get());
		break;
	case EMcWindowKind::Trading:
		State->Layout = McGui::MakeTradingLayout();
		State->Trading = static_cast<FMcTradeMenu*>(Window.Menu.Get());
		break;
	case EMcWindowKind::Loom:
		State->Layout = McGui::MakeLoomLayout();
		State->Loom = static_cast<FMcLoomMenu*>(Window.Menu.Get());
		break;
	case EMcWindowKind::SmithingTable:
		State->Layout = McGui::MakeSmithingLayout();
		break;
	case EMcWindowKind::Grindstone:
		State->Layout = McGui::MakeGrindstoneLayout();
		break;
	case EMcWindowKind::Anvil:
		State->Layout = McGui::MakeAnvilLayout();
		State->Anvil = static_cast<FMcAnvilMenu*>(Window.Menu.Get());
		break;
	case EMcWindowKind::CraftingTable:
		State->Layout = McGui::MakeCraftingTableLayout();
		break;
	case EMcWindowKind::Chest:
		State->Layout = McGui::MakeChestLayout(FMath::Clamp(Window.Menu.IsValid() ? (Window.Menu->NumSlots() - 36) / 9 : 3, 1, 6));
		break;
	case EMcWindowKind::BrewingStand:
		State->Layout = McGui::MakeBrewingStandLayout();
		break;
	case EMcWindowKind::Hopper:
		State->Layout = McGui::MakeHopperLayout();
		break;
	case EMcWindowKind::Horse:
		State->Layout = McGui::MakeHorseLayout(Window.Menu.IsValid() ? Window.Menu->NumSlots() - 36 : 2, Window.Title == TEXT("Horse"));
		break;
	case EMcWindowKind::Dispenser:
		State->Layout = McGui::MakeDispenserLayout();
		break;
	case EMcWindowKind::Furnace:
		State->Layout = McGui::MakeFurnaceLayout();
		break;
	case EMcWindowKind::Sign:
		// It opens on the first line, the cursor at its end; backspaces from before it opened are not its.
		State->Sign = static_cast<FMcSignMenu*>(Window.Menu.Get());
		State->SignCursor = State->Sign->Lines.IsEmpty() ? 0 : State->Sign->Lines[0].Len();
		UMcGameViewportClient::TakeTyped();
		UMcGameViewportClient::TakeBackspaces();
		break;
	case EMcWindowKind::Lectern:
		State->Lectern = static_cast<FMcLecternMenu*>(Window.Menu.Get());
		break;
	default:
		if (Data->bCreative)
		{
			// Creative players get the item list in place of their inventory.
			State->Layout = McGui::MakeCreativeLayout();
			State->bCreative = true;
			State->CreativeItems = CreativeList(FString());
#if !UE_BUILD_SHIPPING
			FParse::Value(FCommandLine::Get(), TEXT("-McInventory="), State->Search);
			State->CreativeItems = CreativeList(State->Search);
#endif
		}
		else
		{
			State->Layout = McGui::MakeInventoryLayout();
		}
		break;
	}
	ShowScreen(State);
}

void AMcHUD::OpenInventory()
{
	const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwningPawn());
	if (UMcPlayerDataComponent* Data = Pawn ? Pawn->FindComponentByClass<UMcPlayerDataComponent>() : nullptr)
	{
		Data->OpenInventoryWindow();
	}
}

void AMcHUD::FlushDrops()
{
	if (!Screen.IsValid())
	{
		return;
	}
	const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwningPawn());
	UMcPlayerDataComponent* Data = Pawn ? Pawn->FindComponentByClass<UMcPlayerDataComponent>() : nullptr;
	for (const FMcItemStack& Stack : Screen->Input->TakeDropped())
	{
		// Only the server makes items; on a client the server's copy of the same click throws the same stack.
		if (Data && !Stack.IsEmpty())
		{
			Data->DropFromWindow(Stack);
		}
	}
}

void AMcHUD::CloseScreen()
{
	if (!Screen.IsValid())
	{
		return;
	}
	FlushDrops();
	const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwningPawn());
	if (UMcPlayerDataComponent* Data = Pawn ? Pawn->FindComponentByClass<UMcPlayerDataComponent>() : nullptr)
	{
		Data->CloseWindow();
	}
	HideScreen();
}

void AMcHUD::PollInput(int32 Width, int32 Height, UMcPlayerDataComponent* Data)
{
	APlayerController* Player = GetOwningPlayerController();
	if (!Player || !Data)
	{
		return;
	}
	// E closes the screen, except in creative where it is typed into the search.
	if (Player->WasInputKeyJustPressed(EKeys::E) && !(Screen.IsValid() && Screen->IsTyping()))
	{
		if (Screen.IsValid())
		{
			CloseScreen();
		}
		else
		{
			Data->OpenInventoryWindow();
		}
		return;
	}
	if (!Screen.IsValid())
	{
		if (Player->WasInputKeyJustPressed(EKeys::F7))
		{
			ToggleGraphics();
		}
		const bool bSlash = Player->WasInputKeyJustPressed(EKeys::Slash) || Player->WasInputKeyJustPressed(EKeys::Divide);
		if (bSlash || Player->WasInputKeyJustPressed(EKeys::T))
		{
			// The key's own character is dropped (OpenMenu takes what was typed); / starts a command.
			OpenMenu(EMenu::Chat);
			if (bSlash)
			{
				ChatDraft = TEXT("/");
			}
			return;
		}
#if !UE_BUILD_SHIPPING
		// Development: -McChat="/time set night" sends that line three seconds in, once the player is set up (lines split by |).
		static bool bDevChatDone = false;
		FString DevChat;
		if (!bDevChatDone && GetWorld()->GetTimeSeconds() > 3.0 && FParse::Value(FCommandLine::Get(), TEXT("-McChat="), DevChat, false))
		{
			bDevChatDone = true;
			TArray<FString> Lines;
			DevChat.TrimQuotes().ParseIntoArray(Lines, TEXT("|"));
			for (FString& Line : Lines)
			{
				// \uXXXX stands for that character, for text a command line can't carry.
				for (int32 At = 0; (At = Line.Find(TEXT("\\u"), ESearchCase::CaseSensitive, ESearchDir::FromStart, At)) != INDEX_NONE && At + 6 <= Line.Len(); ++At)
				{
					const TCHAR Char = static_cast<TCHAR>(FParse::HexNumber(*Line.Mid(At + 2, 4)));
					Line = Line.Left(At) + FString(1, &Char) + Line.Mid(At + 6);
				}
				Data->SendChat(Line);
			}
		}
		// -McUse=X,Y,Z uses that block (opens its window) five seconds in, after -McChat has built it.
		static bool bDevUseDone = false;
		FString DevUse;
		if (!bDevUseDone && GetWorld()->GetTimeSeconds() > 5.0 && FParse::Value(FCommandLine::Get(), TEXT("-McUse="), DevUse, false))
		{
			bDevUseDone = true;
			TArray<FString> Parts;
			DevUse.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() == 3)
			{
				if (AMcPlayerPawn* User = Cast<AMcPlayerPawn>(GetOwningPawn()))
				{
					User->RequestUseBlock(FIntVector(FCString::Atoi(*Parts[0]), FCString::Atoi(*Parts[1]), FCString::Atoi(*Parts[2])));
				}
			}
		}
		// -McTrade right-clicks the villager that -McSpawnMobs=villager,1 put two blocks south of the player, six seconds in.
		static bool bDevTradeDone = false;
		if (!bDevTradeDone && GetWorld()->GetTimeSeconds() > 6.0 && FParse::Param(FCommandLine::Get(), TEXT("McTrade")))
		{
			bDevTradeDone = true;
			if (AMcPlayerPawn* User = Cast<AMcPlayerPawn>(GetOwningPawn()))
			{
				User->RequestUseMob(FVector(0.0, 0.0, 1.0));
			}
		}
		// -McRideMob right-clicks the mount that -McSpawnMobs=horse,1 -McMobSaddled put two blocks south, six seconds in.
		static bool bDevRideDone = false;
		if (!bDevRideDone && GetWorld()->GetTimeSeconds() > 6.0 && FParse::Param(FCommandLine::Get(), TEXT("McRideMob")))
		{
			bDevRideDone = true;
			if (AMcPlayerPawn* User = Cast<AMcPlayerPawn>(GetOwningPawn()))
			{
				User->RequestUseMob(FVector(0.0, 0.0, 1.0));
			}
		}
		// -McInventory opens the inventory once the world is up (-McInventory=log also searches the creative list).
		static bool bDevInventoryDone = false;
		FString DevSearch;
		if (!bDevInventoryDone && (FParse::Param(FCommandLine::Get(), TEXT("McInventory")) || FParse::Value(FCommandLine::Get(), TEXT("-McInventory="), DevSearch)))
		{
			bDevInventoryDone = true;
			Data->OpenInventoryWindow();
			return;
		}
		// -McHorseWindow opens a donkey's window (the saddle, an apple and diamonds in the chest slots) for a shot.
		static bool bDevHorseDone = false;
		if (!bDevHorseDone && GetWorld()->GetTimeSeconds() > 6.0 && FParse::Param(FCommandLine::Get(), TEXT("McHorseWindow")))
		{
			bDevHorseDone = true;
			const TSharedPtr<FMcContainer> Slots = MakeShared<FMcContainer>(17);
			Slots->Set(0, FMcItemStack(FName(TEXT("minecraft:saddle")), 1));
			Slots->Set(4, FMcItemStack(FName(TEXT("minecraft:apple")), 5));
			Slots->Set(7, FMcItemStack(FName(TEXT("minecraft:diamond")), 3));
			const AMcPlayerPawn* DevPawn = Cast<AMcPlayerPawn>(GetOwningPawn());
			const FVector Feet = DevPawn ? DevPawn->GetFeetPosition() : FVector::ZeroVector;
			Data->OpenBlockWindow(EMcWindowKind::Horse, FIntVector(FMath::FloorToInt32(Feet.X), FMath::FloorToInt32(Feet.Y), FMath::FloorToInt32(Feet.Z)), TEXT("Donkey"), Slots, nullptr);
			return;
		}
#endif
		bool bPause = Player->WasInputKeyJustPressed(EKeys::Escape);
#if !UE_BUILD_SHIPPING
		// Development: -McMenu=pause opens the pause menu once the world is up.
		static bool bDevPauseDone = false;
		FString DevMenu;
		bool bDevOptions = false, bDevPacks = false, bDevLanguage = false;
		if (!bDevPauseDone && FParse::Value(FCommandLine::Get(), TEXT("-McMenu="), DevMenu) && (DevMenu == TEXT("pause") || DevMenu == TEXT("options") || DevMenu == TEXT("packs") || DevMenu == TEXT("language")))
		{
			bDevPauseDone = true;
			bPause = true;
			bDevOptions = DevMenu == TEXT("options") || DevMenu == TEXT("packs") || DevMenu == TEXT("language");
			bDevLanguage = DevMenu == TEXT("language");
			bDevPacks = DevMenu == TEXT("packs");
		}
#endif
		if (bPause)
		{
			OpenMenu(EMenu::Pause);
#if !UE_BUILD_SHIPPING
			if (bDevOptions)
			{
				OpenOptions(EMenu::Pause);
			}
			if (bDevPacks)
			{
				OpenResourcePacks();
			}
			if (bDevLanguage)
			{
				OpenLanguage();
			}
#endif
		}
		return;
	}
	if (Screen->Sign)
	{
		PollSign(*Player, *Data, Width, Height);
		return;
	}
	if (Screen->Lectern)
	{
		PollLectern(*Player, *Data, Width, Height);
		return;
	}
	if (Player->WasInputKeyJustPressed(EKeys::Escape))
	{
		CloseScreen();
		return;
	}

	float MouseX = 0.0f, MouseY = 0.0f;
	Player->GetMousePosition(MouseX, MouseY);
	GuiMouse = FVector2D(MouseX, MouseY) / Scale;
	const FIntPoint Local = FIntPoint(FMath::FloorToInt(GuiMouse.X), FMath::FloorToInt(GuiMouse.Y)) - Screen->Layout.Origin(Width, Height);
	const bool bInside = Local.X >= 0 && Local.Y >= 0 && Local.X < Screen->Layout.Size.X && Local.Y < Screen->Layout.Size.Y;
	HoverSlot = bInside ? Screen->Layout.FindSlot(Local) : FMcMenuInput::Outside;
	FMcMenuInput& Input = *Screen->Input;
#if !UE_BUILD_SHIPPING
	// Development: -McShiftClick=29,30 shift-clicks those slots of the first window -McUse opens.
	static bool bDevClickDone = false;
	FString DevClicks;
	if (!bDevClickDone && !Screen->bCreative && FParse::Value(FCommandLine::Get(), TEXT("-McShiftClick="), DevClicks, false))
	{
		bDevClickDone = true;
		TArray<FString> Clicks;
		DevClicks.ParseIntoArray(Clicks, TEXT(","));
		for (const FString& Click : Clicks)
		{
			Input.MouseDown(FCString::Atoi(*Click), 0, true, -10.0);
			Input.MouseUp(FCString::Atoi(*Click), 0);
		}
	}
#endif
	// Over the picture but no slot (a label, the gap between slots): clicks do nothing, rather than count as outside.
	const int32 Slot = HoverSlot;
	const bool bShift = Player->IsInputKeyDown(EKeys::LeftShift) || Player->IsInputKeyDown(EKeys::RightShift);
	const double Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	bool bTaken = Screen->bCreative && PollCreative(*Player, Local, *Data);
	Data->PrepareMenuClick();
	if (Screen->Anvil)
	{
		FScreenState& State = *Screen;
		// A new item on the left starts the field at its name (custom, or the usual one); the usual name typed back
		// is no custom name at all.
		const FMcItemStack& Left = State.Anvil->GetStack(FMcAnvilMenu::LeftSlot);
		const FString Usual = Left.IsEmpty() ? FString() : DisplayName(Left.Item);
		if (Left.Item != State.AnvilFor.Item || Left.GetCustomName() != State.AnvilFor.GetCustomName())
		{
			State.AnvilFor = Left;
			State.AnvilName = Left.IsEmpty() ? FString() : McGui::Anvil::GetFieldFor(Left.GetCustomName(), Usual);
			Data->SetMenuText(McGui::Anvil::GetNameToSend(State.AnvilName, Usual));
		}
		const FString Before = State.AnvilName;
		const FString Typed = UMcGameViewportClient::TakeTyped();
		if (!Left.IsEmpty())
		{
			State.AnvilName.Append(Typed.Left(FMath::Max(0, FMcAnvilMenu::MaxNameLength - State.AnvilName.Len())));
			if (Player->WasInputKeyJustPressed(EKeys::BackSpace) && !State.AnvilName.IsEmpty())
			{
				State.AnvilName.LeftChopInline(1);
			}
		}
		if (State.AnvilName != Before)
		{
			Data->SetMenuText(McGui::Anvil::GetNameToSend(State.AnvilName, Usual));
		}
	}
	if (Screen->Stonecutter && bInside)
	{
		namespace Cutter = McGui::Stonecutter;
		const int32 Count = Screen->Stonecutter->GetRecipes().Num();
		if (Player->WasInputKeyJustPressed(EKeys::MouseScrollDown))
		{
			Screen->RecipeScroll = FMath::Min(Screen->RecipeScroll + 1, Cutter::MaxScroll(Count));
		}
		if (Player->WasInputKeyJustPressed(EKeys::MouseScrollUp))
		{
			Screen->RecipeScroll = FMath::Max(Screen->RecipeScroll - 1, 0);
		}
		Screen->RecipeScroll = FMath::Clamp(Screen->RecipeScroll, 0, Cutter::MaxScroll(Count));
		const int32 Recipe = Cutter::FindRecipe(Local, Screen->RecipeScroll, Count);
		if (Recipe >= 0 && Player->WasInputKeyJustPressed(EKeys::LeftMouseButton))
		{
			Data->ClickMenuButton(Recipe);
			bTaken = true;
		}
	}
	if (Screen->Trading && bInside)
	{
		namespace Shop = McGui::Trading;
		const int32 Count = Screen->Trading->GetOffers().Num();
		if (Player->WasInputKeyJustPressed(EKeys::MouseScrollDown))
		{
			Screen->RecipeScroll = FMath::Min(Screen->RecipeScroll + 1, Shop::MaxScroll(Count));
		}
		if (Player->WasInputKeyJustPressed(EKeys::MouseScrollUp))
		{
			Screen->RecipeScroll = FMath::Max(Screen->RecipeScroll - 1, 0);
		}
		Screen->RecipeScroll = FMath::Clamp(Screen->RecipeScroll, 0, Shop::MaxScroll(Count));
		const int32 Offer = Shop::FindRow(Local, Screen->RecipeScroll, Count);
		if (Offer >= 0 && Player->WasInputKeyJustPressed(EKeys::LeftMouseButton))
		{
			Data->ClickMenuButton(Offer);
			bTaken = true;
		}
	}
	if (Screen->Loom && bInside)
	{
		namespace LoomGui = McGui::Loom;
		const int32 Count = Screen->Loom->GetPatterns().Num();
		if (Player->WasInputKeyJustPressed(EKeys::MouseScrollDown))
		{
			Screen->RecipeScroll = FMath::Min(Screen->RecipeScroll + 1, LoomGui::MaxScroll(Count));
		}
		if (Player->WasInputKeyJustPressed(EKeys::MouseScrollUp))
		{
			Screen->RecipeScroll = FMath::Max(Screen->RecipeScroll - 1, 0);
		}
		Screen->RecipeScroll = FMath::Clamp(Screen->RecipeScroll, 0, LoomGui::MaxScroll(Count));
		const int32 Pattern = LoomGui::FindPattern(Local, Screen->RecipeScroll, Count);
		if (Pattern >= 0 && Player->WasInputKeyJustPressed(EKeys::LeftMouseButton))
		{
			Data->ClickMenuButton(Pattern);
			bTaken = true;
		}
	}
	if (Screen->Enchanting && bInside && Player->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		const int32 Option = McGui::Enchanting::FindButton(Local);
		if (Option >= 0)
		{
			if (Screen->Enchanting->CanEnchant(Option, Data->Stats.XpLevel, Data->bCreative))
			{
				Data->Enchant(Option);
			}
			bTaken = true;
		}
	}
	Input.MouseMove(Slot);
	const FKey Buttons[3] = { EKeys::LeftMouseButton, EKeys::RightMouseButton, EKeys::MiddleMouseButton };
	for (int32 Button = 0; Button < 3; ++Button)
	{
		if (Player->WasInputKeyJustPressed(Buttons[Button]) && !(bInside && Slot < 0) && !bTaken)
		{
			Input.MouseDown(Slot, Button, bShift, Time);
		}
		if (Player->WasInputKeyJustReleased(Buttons[Button]))
		{
			Input.MouseUp(Slot, Button);
		}
	}
	if (Screen->IsTyping())
	{
		// The keys are the text field's.
		FlushDrops();
		return;
	}
	const FKey Numbers[9] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 Number = 0; Number < 9; ++Number)
	{
		if (Player->WasInputKeyJustPressed(Numbers[Number]))
		{
			Input.NumberKey(Slot, Number);
		}
	}
	if (Player->WasInputKeyJustPressed(EKeys::Q))
	{
		Input.DropKey(Slot, Player->IsInputKeyDown(EKeys::LeftControl));
	}
	FlushDrops();
}

bool AMcHUD::PollCreative(APlayerController& Player, FIntPoint Local, UMcPlayerDataComponent& Data)
{
	namespace Creative = McGui::Creative;
	FScreenState& State = *Screen;
	const FString Before = State.Search;
	State.Search.Append(UMcGameViewportClient::TakeTyped().Left(FMath::Max(0, Creative::MaxSearch - State.Search.Len())));
	if (Player.WasInputKeyJustPressed(EKeys::BackSpace) && !State.Search.IsEmpty())
	{
		State.Search.LeftChopInline(1);
	}
	if (State.Search != Before)
	{
		State.CreativeItems = CreativeList(State.Search);
		State.CreativeScroll = 0;
	}

	// The wheel scrolls a row at a time; the thumb can be dragged (a click on the track jumps it there).
	const int32 MaxScroll = Creative::MaxScroll(State.CreativeItems.Num());
	if (Player.WasInputKeyJustPressed(EKeys::MouseScrollDown))
	{
		State.CreativeScroll = FMath::Min(MaxScroll, State.CreativeScroll + 1);
	}
	if (Player.WasInputKeyJustPressed(EKeys::MouseScrollUp))
	{
		State.CreativeScroll = FMath::Max(0, State.CreativeScroll - 1);
	}
	const bool bPressed = Player.WasInputKeyJustPressed(EKeys::LeftMouseButton);
	const bool bOnTrack = Local.X >= Creative::ScrollX && Local.X < Creative::ScrollX + 14 && Local.Y >= Creative::ScrollY && Local.Y < Creative::ScrollY + Creative::ScrollHeight;
	if (bPressed && bOnTrack && MaxScroll > 0)
	{
		State.bScrollDrag = true;
	}
	if (!Player.IsInputKeyDown(EKeys::LeftMouseButton))
	{
		State.bScrollDrag = false;
	}
	if (State.bScrollDrag)
	{
		const float Fraction = (Local.Y - Creative::ScrollY - 7.5f) / (Creative::ScrollHeight - 15.0f);
		State.CreativeScroll = FMath::Clamp(FMath::RoundToInt(Fraction * MaxScroll), 0, MaxScroll);
	}
	State.CreativeScroll = FMath::Clamp(State.CreativeScroll, 0, MaxScroll);

	const int32 Cell = Creative::FindCell(Local);
	const int32 Index = Cell >= 0 ? State.CreativeScroll * Creative::Columns + Cell : -1;
	State.CreativeHover = State.CreativeItems.IsValidIndex(Index) ? Index : -1;
	if (Cell < 0)
	{
		return bPressed && bOnTrack;
	}
	int32 Button = -1;
	const FKey Buttons[3] = { EKeys::LeftMouseButton, EKeys::RightMouseButton, EKeys::MiddleMouseButton };
	for (int32 Each = 0; Each < 3 && Button < 0; ++Each)
	{
		Button = Player.WasInputKeyJustPressed(Buttons[Each]) ? Each : -1;
	}
	if (Button < 0)
	{
		return false;
	}
	if (!Screen->Input->GetMenu().GetCarried().IsEmpty())
	{
		// What is put back on the list is gone.
		Data.CreativeTake(NAME_None, 0, false);
	}
	else if (State.CreativeHover >= 0)
	{
		// One onto the cursor; the middle button takes a full stack, and shift-click puts a full stack in the inventory.
		const FName Item = State.CreativeItems[State.CreativeHover];
		const int32 Full = FMcItemStack(Item, 1).GetMaxStackSize();
		const bool bShift = Player.IsInputKeyDown(EKeys::LeftShift) || Player.IsInputKeyDown(EKeys::RightShift);
		if (bShift && Button == 0)
		{
			Data.CreativeTake(Item, Full, true);
		}
		else
		{
			Data.CreativeTake(Item, Button == 2 ? Full : 1, false);
		}
	}
	return true;
}

void AMcHUD::DrawCreative(FIntPoint Origin)
{
	namespace Creative = McGui::Creative;
	const FScreenState& State = *Screen;
	const bool bCaret = FMath::Fmod(FPlatformTime::Seconds(), 0.6) < 0.3;
	FString Shown = State.Search;
	while (!Shown.IsEmpty() && TextWidth(Shown) > Creative::SearchWidth - 6)
	{
		Shown.RightChopInline(1);
	}
	DrawGuiText(bCaret ? Shown + TEXT("_") : Shown, Origin.X + Creative::SearchX, Origin.Y + Creative::SearchY, 0xFFFFFF, true);
	for (int32 Cell = 0; Cell < Creative::Columns * Creative::Rows; ++Cell)
	{
		const int32 Index = State.CreativeScroll * Creative::Columns + Cell;
		if (!State.CreativeItems.IsValidIndex(Index))
		{
			break;
		}
		const float X = Origin.X + Creative::ListX + Creative::Pitch * (Cell % Creative::Columns);
		const float Y = Origin.Y + Creative::ListY + Creative::Pitch * (Cell / Creative::Columns);
		DrawStack(FMcItemStack(State.CreativeItems[Index], 1), X, Y);
		if (Index == State.CreativeHover)
		{
			DrawFill(X, Y, 16, 16, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
		}
	}
	const int32 MaxScroll = Creative::MaxScroll(State.CreativeItems.Num());
	const float Thumb = MaxScroll > 0 ? (Creative::ScrollHeight - 15.0f) * State.CreativeScroll / MaxScroll : 0.0f;
	DrawSprite(LoadSprite(FString(SpriteRoot) + (MaxScroll > 0 ? TEXT("container/creative_inventory/scroller.png") : TEXT("container/creative_inventory/scroller_disabled.png"))),
		Origin.X + Creative::ScrollX, Origin.Y + Creative::ScrollY + Thumb, 12, 15);
}

// Text in the standard galactic alphabet, wrapped at words to Width and cut after two lines (the table's names).
void AMcHUD::DrawGalacticText(const FString& Text, float X, float Y, float Width, uint32 Rgb)
{
	LoadFont();
	UTexture2D* Font = LoadSprite(TEXT("textures/font/ascii_sga.png"));
	const float Cell = Font ? Font->GetSizeX() / 16.0f : 8.0f;
	const auto WidthOf = [this](const FString& Piece)
	{
		float Total = 0.0f;
		for (const TCHAR Char : Piece)
		{
			Total += (GalacticWidths.IsValidIndex(Char) ? GalacticWidths[Char] : 5) + 1;
		}
		return Total;
	};
	TArray<FString> Words;
	Text.ParseIntoArray(Words, TEXT(" "));
	FString Line;
	int32 Row = 0;
	const auto Flush = [&]()
	{
		float At = X;
		for (const TCHAR Char : Line)
		{
			const int32 Code = Char < 256 ? Char : '?';
			DrawRegion(Font, At, Y + Row * 9, 8, 8, (Code % 16) * Cell, (Code / 16) * Cell, Cell, Cell, FromRgb(Rgb));
			At += GalacticWidths[Code] + 1;
		}
		Line.Reset();
		++Row;
	};
	for (const FString& Word : Words)
	{
		const FString Next = Line.IsEmpty() ? Word : Line + TEXT(" ") + Word;
		if (!Line.IsEmpty() && WidthOf(Next) > Width)
		{
			Flush();
			if (Row >= 2)
			{
				return;
			}
			Line = Word;
		}
		else
		{
			Line = Next;
		}
	}
	if (!Line.IsEmpty() && Row < 2)
	{
		Flush();
	}
}

void AMcHUD::DrawEnchanting(FIntPoint Origin, FIntPoint Local)
{
	namespace Enchanting = McGui::Enchanting;
	const FMcEnchantMenu& Menu = *Screen->Enchanting;
	const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwningPawn());
	const UMcPlayerDataComponent* Data = Pawn ? Pawn->FindComponentByClass<UMcPlayerDataComponent>() : nullptr;
	const int32 Levels = Data ? Data->Stats.XpLevel : 0;
	const bool bCreative = Data && Data->bCreative;
	const FString Table = FString(SpriteRoot) + TEXT("container/enchanting_table/");
	const int32 Hovered = Enchanting::FindButton(Local);
	const TArray<FString> Names = Enchanting::GetGalacticNames(Data ? Data->EnchantSeed : 0);
	for (int32 Option = 0; Option < 3; ++Option)
	{
		const float X = Origin.X + Enchanting::ButtonX;
		const float Y = Origin.Y + Enchanting::ButtonY + Option * Enchanting::ButtonHeight;
		const int32 Cost = Menu.GetOffers().Costs[Option];
		if (Cost <= 0)
		{
			DrawSprite(LoadSprite(Table + TEXT("enchantment_slot_disabled.png")), X, Y, Enchanting::ButtonWidth, Enchanting::ButtonHeight);
			continue;
		}
		// Affordable (the levels, ignoring the lapis, as the game shows it) is lit; else greyed.
		const bool bAffordable = bCreative || Levels >= Cost;
		const bool bUsable = Menu.CanEnchant(Option, Levels, bCreative);
		const TCHAR* Slot = !bUsable ? TEXT("enchantment_slot_disabled.png") : Option == Hovered ? TEXT("enchantment_slot_highlighted.png") : TEXT("enchantment_slot.png");
		DrawSprite(LoadSprite(Table + Slot), X, Y, Enchanting::ButtonWidth, Enchanting::ButtonHeight);
		DrawSprite(LoadSprite(Table + FString::Printf(TEXT("level_%d%s.png"), Option + 1, bUsable ? TEXT("") : TEXT("_disabled"))), X + 1, Y + 1, 16, 16);
		const FString Text = FString::FromInt(Cost);
		DrawGuiText(Text, X + Enchanting::ButtonWidth - 2 - TextWidth(Text), Y + 9, bAffordable ? 0x80FF20 : 0x407F10, true);
		// The made-up writing the table gives in place of the enchantment: grey-brown, halved when it can't be done.
		const uint32 Ink = bUsable ? 0x685E4A : (0x685E4A & 0xFEFEFE) >> 1;
		DrawGalacticText(Names[Option], X + 20, Y + 2, 86 - TextWidth(Text), Ink);
	}
}

void AMcHUD::DrawAnvil(FIntPoint Origin)
{
	const FMcAnvilMenu& Menu = *Screen->Anvil;
	const FString Anvil = FString(SpriteRoot) + TEXT("container/anvil/");
	const bool bHasItem = !Menu.GetStack(FMcAnvilMenu::LeftSlot).IsEmpty();
	DrawSprite(LoadSprite(Anvil + (bHasItem ? TEXT("text_field.png") : TEXT("text_field_disabled.png"))), Origin.X + 59, Origin.Y + 20, 110, 16);
	if (bHasItem)
	{
		// The name, with a caret blinking after it.
		const bool bCaret = FMath::Fmod(GetWorld() ? GetWorld()->GetRealTimeSeconds() : 0.0, 0.6) < 0.3;
		DrawGuiText(Screen->AnvilName + (bCaret ? TEXT("_") : TEXT("")), Origin.X + 62.0f, Origin.Y + 24.0f, 0xFFFFFF, true);
	}
	const bool bResult = !Menu.GetStack(FMcAnvilMenu::ResultSlot).IsEmpty();
	if (bHasItem && !Menu.GetStack(FMcAnvilMenu::RightSlot).IsEmpty() && !bResult)
	{
		DrawSprite(LoadSprite(Anvil + TEXT("error.png")), Origin.X + 99, Origin.Y + 45, 28, 21);
	}
	if (Menu.GetCost() <= 0)
	{
		return;
	}
	const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwningPawn());
	const UMcPlayerDataComponent* Data = Pawn ? Pawn->FindComponentByClass<UMcPlayerDataComponent>() : nullptr;
	const bool bAfford = Data && (Data->bCreative || Data->Stats.XpLevel >= Menu.GetCost());
	FString Text;
	uint32 Color = 0x80FF20;
	if (Menu.IsTooExpensive())
	{
		Text = McLang::Get(TEXT("container.repair.expensive"));
		Color = 0xFF6060;
	}
	else if (bResult)
	{
		Text = McLang::Format(TEXT("container.repair.cost"), { FString::FromInt(Menu.GetCost()) });
		Color = bAfford ? 0x80FF20 : 0xFF6060;
	}
	if (Text.IsEmpty())
	{
		return;
	}
	const float X = Origin.X + 176 - 8 - TextWidth(Text) - 2;
	DrawFill(X - 2, Origin.Y + 67.0f, Origin.X + 168.0f - (X - 2), 12.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.31f));
	DrawGuiText(Text, X, Origin.Y + 69.0f, Color, true);
}

void AMcHUD::DrawStonecutter(FIntPoint Origin, FIntPoint Local)
{
	namespace Layout = McGui::Stonecutter;
	const FMcStonecutterMenu& Menu = *Screen->Stonecutter;
	const TArray<const FMcRecipe*>& Recipes = Menu.GetRecipes();
	const FString Cutter = FString(SpriteRoot) + TEXT("container/stonecutter/");
	const int32 MaxScroll = Layout::MaxScroll(Recipes.Num());
	const float Thumb = MaxScroll > 0 ? FMath::FloorToFloat((Layout::ScrollHeight - 15) * Screen->RecipeScroll / static_cast<float>(MaxScroll)) : 0.0f;
	DrawSprite(LoadSprite(Cutter + (MaxScroll > 0 ? TEXT("scroller.png") : TEXT("scroller_disabled.png"))), Origin.X + Layout::ScrollX, Origin.Y + Layout::ScrollY + Thumb, 12, 15);
	const int32 Hovered = Layout::FindRecipe(Local, Screen->RecipeScroll, Recipes.Num());
	for (int32 Cell = 0; Cell < Layout::Columns * Layout::Rows; ++Cell)
	{
		const int32 Index = Screen->RecipeScroll * Layout::Columns + Cell;
		if (Index >= Recipes.Num())
		{
			break;
		}
		const float X = Origin.X + Layout::RecipesX + (Cell % Layout::Columns) * Layout::CellWidth;
		const float Y = Origin.Y + Layout::RecipesY + (Cell / Layout::Columns) * Layout::CellHeight + 2;
		const TCHAR* Button = Index == Menu.GetSelected() ? TEXT("recipe_selected.png") : Index == Hovered ? TEXT("recipe_highlighted.png") : TEXT("recipe.png");
		DrawSprite(LoadSprite(Cutter + Button), X, Y - 1, 16, 18);
		// The item alone, without its count.
		FMcItemStack Icon = Recipes[Index]->Result;
		Icon.Count = 1;
		DrawStack(Icon, X, Y);
	}
}

void AMcHUD::DrawTrading(FIntPoint Origin, FIntPoint Local)
{
	namespace Shop = McGui::Trading;
	const FMcTradeMenu& Menu = *Screen->Trading;
	const TArray<McTrading::FOffer>& Offers = Menu.GetOffers();
	const FString Villager = FString(SpriteRoot) + TEXT("container/villager/");
	const FString Widget = FString(SpriteRoot) + TEXT("widget/");
	const int32 MaxScroll = Shop::MaxScroll(Offers.Num());

	if (Menu.HasLevels())
	{
		const float Fill = Shop::GetBarFill(Menu.GetLevel(), Menu.GetXp());
		DrawSprite(LoadSprite(Villager + TEXT("experience_bar_background.png")), Origin.X + Shop::BarX, Origin.Y + Shop::BarY, Shop::BarWidth, 5);
		const float Filled = FMath::FloorToFloat(Shop::BarWidth * Fill);
		if (Filled > 0.0f)
		{
			DrawRegion(LoadSprite(Villager + TEXT("experience_bar_current.png")), Origin.X + Shop::BarX, Origin.Y + Shop::BarY, Filled, 5, 0, 0, Filled, 5);
		}
	}
	const float Thumb = MaxScroll > 0 ? FMath::FloorToFloat((Shop::ScrollHeight - 27) * Screen->RecipeScroll / static_cast<float>(MaxScroll)) : 0.0f;
	DrawSprite(LoadSprite(Villager + (MaxScroll > 0 ? TEXT("scroller.png") : TEXT("scroller_disabled.png"))), Origin.X + Shop::ScrollX, Origin.Y + Shop::ScrollY + Thumb, 6, 27);

	const int32 Hovered = Shop::FindRow(Local, Screen->RecipeScroll, Offers.Num());
	for (int32 Row = 0; Row < Shop::Rows; ++Row)
	{
		const int32 Index = Screen->RecipeScroll + Row;
		if (Index >= Offers.Num())
		{
			break;
		}
		const McTrading::FOffer& Offer = Offers[Index];
		const float X = Origin.X + Shop::ListX;
		const float Y = Origin.Y + Shop::ListY + Row * Shop::RowHeight;
		// The row is a button 88 wide: the left half of the 200 wide picture and the right half.
		UTexture2D* Button = LoadSprite(Widget + (Index == Menu.GetSelected() ? TEXT("button_disabled.png") : Index == Hovered ? TEXT("button_highlighted.png") : TEXT("button.png")));
		DrawRegion(Button, X, Y, 44, 20, 0, 0, 44, 20);
		DrawRegion(Button, X + 44, Y, 44, 20, 156, 0, 44, 20);
		const int32 Cost = Menu.GetCost(Index);
		FMcItemStack Pay = Offer.Wants;
		Pay.Count = Cost;
		DrawStack(Pay, X + 4, Y + 2);
		if (Cost != Offer.Wants.Count)
		{
			// What it was, struck through, small above the price.
			DrawGuiText(FString::FromInt(Offer.Wants.Count), X + 4, Y + 1, 0xA0A0A0, false);
			DrawSprite(LoadSprite(Villager + TEXT("discount_strikethrough.png")), X + 4, Y + 5, 9, 2);
		}
		if (!Offer.Extra.IsEmpty())
		{
			DrawStack(Offer.Extra, X + 24, Y + 2);
		}
		DrawSprite(LoadSprite(Villager + (Offer.IsOutOfStock() ? TEXT("trade_arrow_out_of_stock.png") : TEXT("trade_arrow.png"))), X + 47, Y + 6, 10, 9);
		DrawStack(Offer.Gives, X + 66, Y + 2);
	}
	if (Offers.IsValidIndex(Menu.GetSelected()) && Offers[Menu.GetSelected()].IsOutOfStock())
	{
		DrawSprite(LoadSprite(Villager + TEXT("out_of_stock.png")), Origin.X + 182, Origin.Y + 35, 28, 21);
	}
}

void AMcHUD::DrawLoom(FIntPoint Origin, FIntPoint Local)
{
	namespace Layout = McGui::Loom;
	const FMcLoomMenu& Menu = *Screen->Loom;
	const TArray<FName>& Patterns = Menu.GetPatterns();
	const FString Loom = FString(SpriteRoot) + TEXT("container/loom/");
	const int32 MaxScroll = Layout::MaxScroll(Patterns.Num());
	const float Thumb = MaxScroll > 0 ? FMath::FloorToFloat((Layout::ScrollHeight - 15) * Screen->RecipeScroll / static_cast<float>(MaxScroll)) : 0.0f;
	DrawSprite(LoadSprite(Loom + (MaxScroll > 0 ? TEXT("scroller.png") : TEXT("scroller_disabled.png"))), Origin.X + Layout::ScrollX, Origin.Y + Layout::ScrollY + Thumb, 12, 15);
	const bool bBanner = !Menu.GetStack(FMcLoomMenu::BannerSlot).IsEmpty();
	const bool bDye = !Menu.GetStack(FMcLoomMenu::DyeSlot).IsEmpty();
	if (bBanner && bDye && Patterns.IsEmpty())
	{
		// Nothing can go on (six patterns already, or an item that gives none).
		DrawSprite(LoadSprite(Loom + TEXT("error.png")), Origin.X + 109, Origin.Y + 12, 17, 17);
	}
	const int32 Hovered = Layout::FindPattern(Local, Screen->RecipeScroll, Patterns.Num());
	// What each button shows: the banner's colour with the pattern over it in the dye's, as a small banner.
	FString BaseColour = Menu.GetStack(FMcLoomMenu::BannerSlot).Item.ToString();
	BaseColour.RemoveFromStart(TEXT("minecraft:"));
	BaseColour.RemoveFromEnd(TEXT("_banner"));
	FString DyeColour = Menu.GetStack(FMcLoomMenu::DyeSlot).Item.ToString();
	DyeColour.RemoveFromStart(TEXT("minecraft:"));
	DyeColour.RemoveFromEnd(TEXT("_dye"));
	for (int32 Cell = 0; Cell < Layout::Columns * Layout::Rows; ++Cell)
	{
		const int32 Index = Screen->RecipeScroll * Layout::Columns + Cell;
		if (Index >= Patterns.Num())
		{
			break;
		}
		const float X = Origin.X + Layout::PatternsX + (Cell % Layout::Columns) * Layout::CellSize;
		const float Y = Origin.Y + Layout::PatternsY + (Cell / Layout::Columns) * Layout::CellSize;
		const TCHAR* Button = Index == Menu.GetSelected() ? TEXT("pattern_selected.png") : Index == Hovered ? TEXT("pattern_highlighted.png") : TEXT("pattern.png");
		DrawSprite(LoadSprite(Loom + Button), X, Y, 14, 14);
		const FString Banner = TEXT("textures/entity/banner/");
		DrawRegion(LoadSprite(Banner + TEXT("banner_base.png")), X + 4, Y + 2, 6, 10, 1, 1, 20, 40, FromRgb(McGui::Loom::GetDyeColour(BaseColour)));
		DrawRegion(LoadSprite(Banner + McCache::PathOf(Patterns[Index].ToString()) + TEXT(".png")), X + 4, Y + 2, 6, 10, 1, 1, 20, 40, FromRgb(McGui::Loom::GetDyeColour(DyeColour)));
	}
}

void AMcHUD::DrawScreen(int32 Width, int32 Height)
{
	if (Screen->Sign)
	{
		DrawSign(Width, Height);
		return;
	}
	if (Screen->Lectern)
	{
		DrawLectern(Width, Height);
		return;
	}
	const McGui::FScreenLayout& Layout = Screen->Layout;
	const FMcMenu& Menu = Screen->Input->GetMenu();
	const FIntPoint Origin = Layout.Origin(Width, Height);
	DrawFill(0, 0, static_cast<float>(Width), static_cast<float>(Height), FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));

	UTexture2D* Background = LoadSprite(TEXT("textures/gui/container/") + Layout.Texture);
	if (Layout.Kind == McGui::EScreen::Chest)
	{
		// The picture holds six rows; the top is cut after the rows there are and the bottom (the player's part) follows.
		const float Top = Layout.Rows * 18 + 17;
		DrawRegion(Background, Origin.X, Origin.Y, 176, Top, 0, 0, 176, Top);
		DrawRegion(Background, Origin.X, Origin.Y + Top, 176, 96, 0, 126, 176, 96);
	}
	else
	{
		DrawRegion(Background, Origin.X, Origin.Y, Layout.Size.X, Layout.Size.Y, 0, 0, Layout.Size.X, Layout.Size.Y);
	}
	if (Layout.Kind == McGui::EScreen::Horse)
	{
		// The slot frames the picture leaves out: the saddle's, the armour's (a horse's only) and the chest's 5 x 3.
		const FString HorseParts = TEXT("textures/gui/sprites/container/");
		DrawRegion(LoadSprite(HorseParts + TEXT("slot.png")), Origin.X + 7, Origin.Y + 17, 18, 18, 0, 0, 18, 18);
		if (Layout.IsShown(1))
		{
			DrawRegion(LoadSprite(HorseParts + TEXT("slot.png")), Origin.X + 7, Origin.Y + 35, 18, 18, 0, 0, 18, 18);
		}
		if (Layout.Slots.Num() - 36 > 2)
		{
			DrawRegion(LoadSprite(HorseParts + TEXT("horse/chest_slots.png")), Origin.X + 79, Origin.Y + 17, 90, 54, 0, 0, 90, 54);
		}
	}
	if (Layout.Kind == McGui::EScreen::Furnace && Screen->Furnace.IsValid())
	{
		const FMcFurnace& Furnace = *Screen->Furnace;
		const float Flame = McGui::GetFlameFill(Furnace.GetLitTimeRemaining(), Furnace.GetLitTotalTime());
		if (Flame > 0.0f)
		{
			const float Lit = FMath::CeilToFloat(14.0f * Flame);
			DrawRegion(LoadSprite(FString(SpriteRoot) + TEXT("container/furnace/lit_progress.png")), Origin.X + Layout.Flame.X, Origin.Y + Layout.Flame.Y + 14 - Lit, 14, Lit, 0, 14 - Lit, 14, Lit);
		}
		const float Arrow = FMath::CeilToFloat(24.0f * McGui::GetArrowFill(Furnace.GetCookingProgress(), Furnace.GetCookingTotalTime()));
		if (Arrow > 0.0f)
		{
			DrawRegion(LoadSprite(FString(SpriteRoot) + TEXT("container/furnace/burn_progress.png")), Origin.X + Layout.Arrow.X, Origin.Y + Layout.Arrow.Y, Arrow, 16, 0, 0, Arrow, 16);
		}
	}

	if (Layout.Kind == McGui::EScreen::BrewingStand && Screen->Brewing.IsValid())
	{
		const FMcBrewingStand& Stand = *Screen->Brewing;
		const FString Parts = FString(SpriteRoot) + TEXT("container/brewing_stand/");
		// The fuel bar shows the brews left of the powder in it.
		const float Fuel = FMath::Clamp(FMath::CeilToFloat(18.0f * Stand.GetFuel() / FMcBrewingStand::FuelPerPowder), 0.0f, 18.0f);
		if (Fuel > 0.0f)
		{
			DrawRegion(LoadSprite(Parts + TEXT("fuel_length.png")), Origin.X + 60, Origin.Y + 44, Fuel, 4, 0, 0, Fuel, 4);
		}
		if (Stand.IsBrewing())
		{
			// The arrow fills down as the brew goes; the bubbles rise over and over.
			const float Progress = FMath::CeilToFloat(28.0f * (1.0f - static_cast<float>(Stand.GetBrewTimeLeft()) / FMcBrewingStand::BrewTime));
			if (Progress > 0.0f)
			{
				DrawRegion(LoadSprite(Parts + TEXT("brew_progress.png")), Origin.X + 97, Origin.Y + 16, 9, Progress, 0, 0, 9, Progress);
			}
			static constexpr int32 BubbleHeights[] = { 29, 24, 20, 16, 11, 6, 0 };
			const int32 Bubbles = BubbleHeights[(Stand.GetBrewTimeLeft() / 2) % 7];
			if (Bubbles > 0)
			{
				DrawRegion(LoadSprite(Parts + TEXT("bubbles.png")), Origin.X + 63, Origin.Y + 14 + 29 - Bubbles, 12, Bubbles, 0, 29 - Bubbles, 12, Bubbles);
			}
		}
	}

	// Labels in the dark grey the game uses, without shadow.
	switch (Layout.Kind)
	{
	case McGui::EScreen::Inventory:
		DrawGuiText(TEXT("Crafting"), Origin.X + 97.0f, Origin.Y + 8.0f, 0x404040, false);
		break;
	case McGui::EScreen::CraftingTable:
	case McGui::EScreen::Furnace:
		DrawGuiText(Screen->Title, Origin.X + (Layout.Kind == McGui::EScreen::Furnace ? 60.0f : 28.0f), Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		break;
	case McGui::EScreen::Stonecutter:
		DrawGuiText(Screen->Title, Origin.X + 8.0f, Origin.Y + 4.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		if (Screen->Stonecutter)
		{
			DrawStonecutter(Origin, FIntPoint(FMath::FloorToInt(GuiMouse.X), FMath::FloorToInt(GuiMouse.Y)) - Origin);
		}
		break;
	case McGui::EScreen::Trading:
		DrawGuiText(Screen->Title, Origin.X + 100.0f + (176.0f - TextWidth(Screen->Title)) / 2.0f, Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 108.0f, Origin.Y + 72.0f, 0x404040, false);
		if (Screen->Trading)
		{
			DrawTrading(Origin, FIntPoint(FMath::FloorToInt(GuiMouse.X), FMath::FloorToInt(GuiMouse.Y)) - Origin);
		}
		break;
	case McGui::EScreen::Loom:
		DrawGuiText(Screen->Title, Origin.X + 8.0f, Origin.Y + 4.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		if (Screen->Loom)
		{
			DrawLoom(Origin, FIntPoint(FMath::FloorToInt(GuiMouse.X), FMath::FloorToInt(GuiMouse.Y)) - Origin);
		}
		break;
	case McGui::EScreen::Anvil:
		DrawGuiText(Screen->Title, Origin.X + 60.0f, Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		if (Screen->Anvil)
		{
			DrawAnvil(Origin);
		}
		break;
	case McGui::EScreen::Horse:
		DrawGuiText(Screen->Title, Origin.X + 8.0f, Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		break;
	case McGui::EScreen::Hopper:
		DrawGuiText(Screen->Title, Origin.X + 8.0f, Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 39.0f, 0x404040, false);
		break;
	case McGui::EScreen::Dispenser:
	case McGui::EScreen::BrewingStand:
		DrawGuiText(Screen->Title, Origin.X + 176 / 2.0f - TextWidth(Screen->Title) / 2.0f, Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		break;
	case McGui::EScreen::Grindstone:
		DrawGuiText(Screen->Title, Origin.X + 8.0f, Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		break;
	case McGui::EScreen::Smithing:
		DrawGuiText(Screen->Title, Origin.X + 44.0f, Origin.Y + 15.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		break;
	case McGui::EScreen::Enchanting:
		DrawGuiText(Screen->Title, Origin.X + 12.0f, Origin.Y + 5.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + 72.0f, 0x404040, false);
		DrawEnchanting(Origin, FIntPoint(FMath::FloorToInt(GuiMouse.X), FMath::FloorToInt(GuiMouse.Y)) - Origin);
		break;
	case McGui::EScreen::Chest:
		DrawGuiText(Screen->Title, Origin.X + 8.0f, Origin.Y + 6.0f, 0x404040, false);
		DrawGuiText(TEXT("Inventory"), Origin.X + 8.0f, Origin.Y + Layout.Rows * 18.0f + 20.0f, 0x404040, false);
		break;
	case McGui::EScreen::Creative:
		DrawCreative(Origin);
		break;
	}

	for (int32 Slot = 0; Slot < Menu.NumSlots() && Slot < Layout.Slots.Num(); ++Slot)
	{
		if (!Layout.IsShown(Slot))
		{
			continue;
		}
		const FIntPoint At = Origin + Layout.Slots[Slot];
		DrawStack(Menu.GetStack(Slot), static_cast<float>(At.X), static_cast<float>(At.Y));
		if (Slot == HoverSlot)
		{
			DrawFill(static_cast<float>(At.X), static_cast<float>(At.Y), 16, 16, FLinearColor(1.0f, 1.0f, 1.0f, 0.5f));
		}
	}

	// The stack on the cursor, centred on it; else the name of what is under it, or what an enchanting option gives.
	const int32 Option = Screen->Enchanting ? McGui::Enchanting::FindButton(FIntPoint(FMath::FloorToInt(GuiMouse.X), FMath::FloorToInt(GuiMouse.Y)) - Origin) : -1;
	if (Menu.GetCarried().IsEmpty() && Option >= 0 && Screen->Enchanting->GetOffers().Costs[Option] > 0)
	{
		const McEnchanting::FOffers& Offers = Screen->Enchanting->GetOffers();
		const McEnchanting::FEnchant& Clue = Offers.Clues[Option];
		const FString Name = McLang::Get(TEXT("enchantment.") + Clue.Name.ToString().Replace(TEXT(":"), TEXT("."))) + TEXT(" ") + McLang::Get(FString::Printf(TEXT("enchantment.level.%d"), Clue.Level));
		TArray<TPair<FString, uint32>> Lines;
		Lines.Add({ McLang::Format(TEXT("container.enchant.clue"), { Name }), 0xFFFFFF });
		const AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwningPawn());
		const UMcPlayerDataComponent* Viewer = Pawn ? Pawn->FindComponentByClass<UMcPlayerDataComponent>() : nullptr;
		if (Viewer && !Viewer->bCreative)
		{
			const int32 Need = Option + 1;
			const FMcItemStack& Fuel = Menu.GetStack(FMcEnchantMenu::LapisSlot);
			const bool bLevel = Viewer->Stats.XpLevel >= Offers.Costs[Option];
			if (!bLevel)
			{
				Lines.Add({ FString(), 0xFFFFFF });
				Lines.Add({ McLang::Format(TEXT("container.enchant.level.requirement"), { FString::FromInt(Offers.Costs[Option]) }), 0xFF5555 });
			}
			else
			{
				Lines.Add({ FString(), 0xFFFFFF });
				Lines.Add({ Need == 1 ? McLang::Get(TEXT("container.enchant.lapis.one")) : McLang::Format(TEXT("container.enchant.lapis.many"), { FString::FromInt(Need) }), Fuel.Count >= Need ? 0xAAAAAAu : 0xFF5555u });
				Lines.Add({ Need == 1 ? McLang::Get(TEXT("container.enchant.level.one")) : McLang::Format(TEXT("container.enchant.level.many"), { FString::FromInt(Need) }), 0xAAAAAA });
			}
		}
		float Wide = 0.0f;
		for (const TPair<FString, uint32>& Line : Lines)
		{
			Wide = FMath::Max(Wide, TextWidth(Line.Key));
		}
		const float X = static_cast<float>(GuiMouse.X) + 12.0f;
		const float Y = static_cast<float>(GuiMouse.Y) - 12.0f;
		DrawFill(X - 3, Y - 3, Wide + 5, Lines.Num() * 10.0f + 4, FLinearColor(0.06f, 0.0f, 0.06f, 0.9f));
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			DrawGuiText(Lines[Index].Key, X, Y + Index * 10.0f, Lines[Index].Value, true);
		}
	}
	else if (Menu.GetCarried().IsEmpty() && Screen->Stonecutter && HoverSlot < 0)
	{
		const int32 Recipe = McGui::Stonecutter::FindRecipe(FIntPoint(FMath::FloorToInt(GuiMouse.X), FMath::FloorToInt(GuiMouse.Y)) - Origin, Screen->RecipeScroll, Screen->Stonecutter->GetRecipes().Num());
		if (Recipe >= 0)
		{
			const FString Name = GetItemName(Screen->Stonecutter->GetRecipes()[Recipe]->Result);
			const float X = static_cast<float>(GuiMouse.X) + 12.0f;
			const float Y = static_cast<float>(GuiMouse.Y) - 12.0f;
			DrawFill(X - 3, Y - 3, TextWidth(Name) + 5, 14, FLinearColor(0.06f, 0.0f, 0.06f, 0.9f));
			DrawGuiText(Name, X, Y, 0xFFFFFF, true);
		}
	}
	else if (!Menu.GetCarried().IsEmpty())
	{
		DrawStack(Menu.GetCarried(), static_cast<float>(GuiMouse.X) - 8.0f, static_cast<float>(GuiMouse.Y) - 8.0f);
	}
	else if ((HoverSlot >= 0 && HoverSlot < Menu.NumSlots() && !Menu.GetStack(HoverSlot).IsEmpty()) || (Screen->bCreative && Screen->CreativeHover >= 0))
	{
		const FString Name = HoverSlot >= 0 ? GetItemName(Menu.GetStack(HoverSlot)) : DisplayName(Screen->CreativeItems[Screen->CreativeHover]);
		const float Wide = TextWidth(Name);
		const float X = static_cast<float>(GuiMouse.X) + 12.0f;
		const float Y = static_cast<float>(GuiMouse.Y) - 12.0f;
		DrawFill(X - 3, Y - 3, Wide + 5, 14, FLinearColor(0.06f, 0.0f, 0.06f, 0.9f));
		DrawGuiText(Name, X, Y, 0xFFFFFF, true);
	}
}

void AMcHUD::ShowDeathScreen(int32 Width, int32 Height, AMcPlayerPawn& Dead)
{
	APlayerController* Player = GetOwningPlayerController();
	if (!bDeathShown)
	{
		bDeathShown = true;
		DeathShownTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (Screen.IsValid())
		{
			HideScreen();
		}
		if (Player)
		{
			Player->bShowMouseCursor = true;
			Player->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
			Player->SetIgnoreLookInput(true);
			Player->SetIgnoreMoveInput(true);
		}
	}
	DrawFill(0, 0, static_cast<float>(Width), static_cast<float>(Height), FLinearColor(0.5f, 0.0f, 0.0f, 0.5f));
	// The title is drawn at twice the size, centred on the screen, and the score under it.
	const FString Title = TEXT("You died!");
	DrawGuiText(Title, FMath::RoundToFloat(Width / 2.0f - TextWidth(Title)), 30.0f, 0xFFFFFF, true, 1.0f, 2.0f);
	const FString ScoreLabel = TEXT("Score: ");
	const FString ScoreValue = FString::FromInt(Dead.GetDeathScore());
	const float ScoreX = Width / 2.0f - (TextWidth(ScoreLabel) + TextWidth(ScoreValue)) / 2.0f;
	DrawGuiText(ScoreLabel, ScoreX, 85.0f, 0xFFFFFF, true);
	DrawGuiText(ScoreValue, ScoreX + TextWidth(ScoreLabel), 85.0f, 0xFFFF55, true);

	// Two 200 by 20 buttons: Respawn, then Title Screen. Both are asleep for the first second.
	float MouseX = 0.0f, MouseY = 0.0f;
	if (Player)
	{
		Player->GetMousePosition(MouseX, MouseY);
	}
	GuiMouse = FVector2D(MouseX, MouseY) / Scale;
	bMenuClick = Player && Player->WasInputKeyJustPressed(EKeys::LeftMouseButton);
	const bool bAwake = GetWorld() && GetWorld()->GetTimeSeconds() - DeathShownTime >= 1.0;
	const float ButtonX = Width / 2.0f - 100.0f;
	const float ButtonY = Height / 4.0f + 72.0f;
	if (MenuButton(TEXT("Respawn"), ButtonX, ButtonY, 200.0f, 20.0f, bAwake))
	{
		Dead.RequestRespawn();
	}
	else if (MenuButton(TEXT("Title Screen"), ButtonX, ButtonY + 24.0f, 200.0f, 20.0f, bAwake))
	{
		// Leaving the map saves the world and the player, like Save and Quit to Title.
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true, TEXT("McTitle"));
	}
}

// ---- The HUD ----

void AMcHUD::DrawHUD()
{
	Super::DrawHUD();
	// Chat is the one world menu that leaves the world sharp.
	UpdateMenuBlur(IsWorldMenu() && ActiveMenu != EMenu::Chat);
#if !UE_BUILD_SHIPPING
	// Development: -McReload=<seconds> asks for the reload (F3+T) that long into the run, once.
	static double DevReloadAt = -1.0;
	static bool bDevReloadRead = false;
	if (!bDevReloadRead)
	{
		bDevReloadRead = true;
		double Seconds = 0.0;
		if (FParse::Value(FCommandLine::Get(), TEXT("-McReload="), Seconds))
		{
			DevReloadAt = FPlatformTime::Seconds() + Seconds;
		}
	}
	if (DevReloadAt > 0.0 && FPlatformTime::Seconds() >= DevReloadAt)
	{
		DevReloadAt = -1.0;
		McPacks::RequestReload();
	}
#endif
	if (McPacks::TakeReloadRequest())
	{
		ReloadResources();
		return;
	}
	if (!DropTarget.IsValid() && GEngine && GEngine->GameViewport)
	{
		DropTarget = SNew(SMcDropTarget).OnFiles(FMcOnFilesDropped::CreateUObject(this, &AMcHUD::OnFilesDropped));
		GEngine->GameViewport->AddViewportWidgetContent(DropTarget.ToSharedRef(), 1000);
	}
	if (!Canvas)
	{
		return;
	}
	// The look chosen last time (options.json), once a run; -McEnhanced picks the enhanced one for development.
	static bool bLookLoaded = false;
	if (!bLookLoaded)
	{
		bLookLoaded = true;
		McRenderMode::SetEnhanced(FMcClientOptions::Load().bEnhancedGraphics || FParse::Param(FCommandLine::Get(), TEXT("McEnhanced")));
		McRenderMode::SetHd(FMcClientOptions::Load().bHdTextures || FParse::Param(FCommandLine::Get(), TEXT("McHd")));
		McRenderMode::SetRayTracing(FMcClientOptions::Load().bRayTracing || FParse::Param(FCommandLine::Get(), TEXT("McRayTracing")));
		int32 Quality = FMcClientOptions::Load().Quality;
		FParse::Value(FCommandLine::Get(), TEXT("-McQuality="), Quality);
		McRenderMode::SetQuality(Quality);
		if (FMcClientOptions::Load().bShowFps || FParse::Param(FCommandLine::Get(), TEXT("McFps")))
		{
			GetOwningPlayerController()->ConsoleCommand(TEXT("stat fps"));
		}
	}
	++ShakeCounter;
	Scale = McGui::GetGuiScale(Canvas->SizeX, Canvas->SizeY, GuiScaleSetting);
	const int32 Width = Canvas->SizeX / Scale;
	const int32 Height = Canvas->SizeY / Scale;
	if (McLaunch::IsMenu(GetWorld()))
	{
		DrawMenus(Width, Height, true);
		return;
	}

	AMcPlayerPawn* Player = Cast<AMcPlayerPawn>(GetOwningPawn());
	UMcPlayerDataComponent* Data = Player ? Player->FindComponentByClass<UMcPlayerDataComponent>() : nullptr;
	const bool bDead = Player && Player->IsDead();
	if (Player && (!bSettingsApplied || SettingsPawn != Player))
	{
		// The saved Field of View, sensitivity, volume and render distance, once the world and the player are there.
		Settings = FMcClientOptions::Load();
		ApplyOptions();
	}
	if (!bDead && bDeathShown)
	{
		// Back from the dead: give the player the world again.
		bDeathShown = false;
		if (APlayerController* Controller = GetOwningPlayerController())
		{
			Controller->bShowMouseCursor = false;
			Controller->SetInputMode(FInputModeGameOnly());
			Controller->SetIgnoreLookInput(false);
			Controller->SetIgnoreMoveInput(false);
		}
	}
	if (bDead && IsWorldMenu())
	{
		// The death screen takes over (and the input with it).
		ActiveMenu = EMenu::None;
		bMenuInput = false;
	}
	SyncScreen(Data);
	if (!bDead && ActiveMenu == EMenu::None)
	{
		PollInput(Width, Height, Data);
	}
	// A click may have closed the window (or the server did).
	SyncScreen(Data);

	const McGui::FHudLayout Layout = McGui::ComputeHud(Width, Height, Data ? Data->Stats : FMcPlayerStats());
	// Lying in bed, the world fades to dark.
	if (Player && Player->GetSleepFade() > 0.0f)
	{
		DrawRect(FLinearColor(0.0f, 0.0f, 0.05f, 0.85f * Player->GetSleepFade()), 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height));
	}
	// F1 hides everything but the world (and the sleep fade above).
	if (Player && Player->IsHudHidden() && !Screen.IsValid() && ActiveMenu == EMenu::None && !bDead)
	{
		return;
	}
	// The screen's edges are darkened, under everything else the HUD draws (it goes with F1, above).
	if (!bDead)
	{
		if (UTexture2D* Vignette = LoadVignette())
		{
			DrawRegion(Vignette, 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 0.0f, 256.0f, 256.0f,
				FLinearColor(1.0f, 1.0f, 1.0f, Player ? StepVignette(*Player) : 0.0f));
		}
	}
	// Minecraft draws the crosshair inverting what is behind it; plain white for now.
	if (!Screen.IsValid() && ActiveMenu == EMenu::None)
	{
		DrawSprite(LoadSprite(TEXT("textures/gui/sprites/hud/crosshair.png")), Layout.Crosshair.X, Layout.Crosshair.Y, 15, 15);
	}
	if (!Player)
	{
		return;
	}
	// A totem that just saved the player flies up at them, spinning (its picture narrows edge on, then shows its back).
	if (const McTotem::FFlight Flight = McTotem::GetFlight(Player->GetTotemAge()); Flight.bVisible)
	{
		const float Size = Flight.Size * static_cast<float>(Height);
		const float Face = FMath::Cos(Flight.Turn);
		const float Wide = Size * FMath::Abs(Face);
		const float X = static_cast<float>(Flight.Centre.X * Width) - Wide / 2.0f;
		const float Y = static_cast<float>(Flight.Centre.Y * Height) - Size / 2.0f;
		DrawRegion(LoadSprite(TEXT("textures/item/totem_of_undying.png")), X, Y, Wide, Size, Face < 0.0f ? 16.0f : 0.0f, 0.0f, Face < 0.0f ? -16.0f : 16.0f, 16.0f);
	}
	const int32 Selected = Data ? Data->SelectedSlot : Player->SelectedSlot;
	DrawSprite(LoadSprite(TEXT("textures/gui/sprites/hud/hotbar.png")), Layout.Hotbar.X, Layout.Hotbar.Y, 182, 22);
	DrawSprite(LoadSprite(TEXT("textures/gui/sprites/hud/hotbar_selection.png")), Layout.Hotbar.X - 1 + Selected * 20, Layout.Hotbar.Y - 1, 24, 23);
	if (Data)
	{
		// The rod held while its bobber is out is drawn cast, its line hanging.
		static const FName Rod(TEXT("minecraft:fishing_rod"));
		const AMcPlayerPawn* Angler = Cast<AMcPlayerPawn>(GetOwningPawn());
		const bool bCast = Angler && Angler->IsRodCast();
		for (int32 Slot = 0; Slot < 9; ++Slot)
		{
			const FMcItemStack& Stack = Data->Inventory->Get(Slot);
			const FName Picture = bCast && Slot == Selected && Stack.Item == Rod ? FName(TEXT("minecraft:fishing_rod_cast")) : NAME_None;
			DrawStack(Stack, static_cast<float>(Layout.HotbarSlots[Slot].X), static_cast<float>(Layout.HotbarSlots[Slot].Y), Picture);
		}
		DrawHeldName(Width, Height, Data->Inventory->Get(Selected), !Data->bCreative);
		if (!Data->Offhand->Get(0).IsEmpty())
		{
			DrawSprite(LoadSprite(TEXT("textures/gui/sprites/hud/hotbar_offhand_left.png")), Layout.Hotbar.X - 29, Layout.Hotbar.Y - 1, 29, 24);
			DrawStack(Data->Offhand->Get(0), static_cast<float>(Layout.Hotbar.X - 26), static_cast<float>(Layout.Hotbar.Y + 3));
		}
		DrawEffects(Width, Height, Data);
		DrawRaidBar(Width, Data);
		// Creative has no health, hunger, armour, air or experience to show (minecraft.wiki: Creative).
		if (!Data->bCreative)
		{
			DrawBars(Width, Height, Data);
		}
	}
	if (Player && Player->IsDebugShown() && !bDead)
	{
		DrawDebug(Width, Height, *Player);
	}
	// A -McShot picture shows the scene, not the chat lines its -McChat setup printed (unless -McShowChat: text for a font test).
	static const bool bShot = FCommandLine::Get() && FCString::Strifind(FCommandLine::Get(), TEXT("-McShot=")) != nullptr
		&& !FParse::Param(FCommandLine::Get(), TEXT("McShowChat"));
	if (!bDead && !bShot && !Screen.IsValid() && ActiveMenu != EMenu::Pause && ActiveMenu != EMenu::Options && ActiveMenu != EMenu::ResourcePacks && ActiveMenu != EMenu::ConfirmPacks && ActiveMenu != EMenu::ConfirmIncompatible)
	{
		DrawChat(Width, Height, ActiveMenu == EMenu::Chat);
	}
	if (!bDead && !Screen.IsValid() && ActiveMenu != EMenu::Pause && ActiveMenu != EMenu::Options)
	{
		DrawOverlay(Width, Height);
	}
	if (Screen.IsValid() && !bDead)
	{
		DrawScreen(Width, Height);
	}
	if (bDead)
	{
		ShowDeathScreen(Width, Height, *Player);
	}
	else if (IsWorldMenu())
	{
		DrawMenus(Width, Height, false);
	}
	DrawToasts(Width, Height);
}

void AMcHUD::DrawToasts(int32 Width, int32 Height)
{
	TArray<UMcPlayerDataComponent::FToast>& Toasts = UMcPlayerDataComponent::GetToasts();
	if (Toasts.IsEmpty())
	{
		return;
	}
	LoadFont();
	// Measured against the reference client (toast1 and toast2 runs, an advancement granted with the command): the
	// panel is the 160 by 32 toast sprite at the top right; `ui.toast.in` (volume 0.4, heard everywhere) plays as it
	// arrives; it slides in over about 13 ticks, with the part still outside the screen 1 - s^2 of its width (s the
	// share of the time gone, fitted to shots every two ticks), stays until tick 112 (5.6 seconds), then `ui.toast.out`
	// plays and it slides out in 12 ticks, the part outside s^2 of its width (fitted the same way). The heading is at
	// (30, 7) and the title at (30, 18), the icon at (8, 8). Slots below one another every 32 pixels, up to five.
	constexpr double InSeconds = 13.0 / 20.0;
	constexpr double HoldSeconds = 112.0 / 20.0;
	constexpr double OutSeconds = 12.0 / 20.0;
	constexpr int32 Slots = 5;
	const double Now = FPlatformTime::Seconds();
	FVector Eye = FVector::ZeroVector;
	FRotator Look;
	if (APlayerController* Player = GetOwningPlayerController())
	{
		Player->GetPlayerViewPoint(Eye, Look);
	}
	const auto Play = [&Eye](const TCHAR* Name)
	{
		FMcSoundBus::Get().OnSound.Broadcast(FMcSoundEvent{ FName(Name), McCoords::UnrealToBlock(Eye), 0.4f, 1.0f });
	};
	int32 Slot = 0;
	for (int32 Index = 0; Index < Toasts.Num();)
	{
		UMcPlayerDataComponent::FToast& Toast = Toasts[Index];
		// Only the first five show; the rest wait their turn (their clock starts when they get a slot).
		if (Slot >= Slots)
		{
			Toast.Time = FMath::Max(Toast.Time, Now);
			++Index;
			continue;
		}
		const double Age = Now - Toast.Time;
		if (Age >= HoldSeconds + OutSeconds)
		{
			Toasts.RemoveAt(Index);
			continue;
		}
		if (!Toast.bInSounded)
		{
			Toast.bInSounded = true;
			Play(TEXT("ui.toast.in"));
		}
		double Hidden = 0.0;
		if (Age < InSeconds)
		{
			const double S = Age / InSeconds;
			Hidden = 1.0 - S * S;
		}
		else if (Age >= HoldSeconds)
		{
			if (!Toast.bOutSounded)
			{
				Toast.bOutSounded = true;
				Play(TEXT("ui.toast.out"));
			}
			const double S = (Age - HoldSeconds) / OutSeconds;
			Hidden = S * S;
		}
		const float X = Width - 160.0f + static_cast<float>(Hidden) * 160.0f;
		const float Y = Slot * 32.0f;
		DrawSprite(LoadSprite(TEXT("textures/gui/sprites/toast/advancement.png")), X, Y, 160, 32);
		DrawGuiText(Toast.Heading, X + 30.0f, Y + 7.0f, Toast.HeadingRgb, false);
		DrawGuiText(Toast.Title, X + 30.0f, Y + 18.0f, 0xFFFFFF, false);
		if (Toast.Icon != NAME_None)
		{
			DrawStack(FMcItemStack(Toast.Icon, 1), X + 8.0f, Y + 8.0f);
		}
		++Slot;
		++Index;
	}
}

// ---- Chat ----

TArray<FString> AMcHUD::WrapText(const FString& Text, float MaxWidth) const
{
	TArray<FString> Lines;
	FString Line;
	int32 LastSpace = INDEX_NONE;
	for (const TCHAR Char : Text)
	{
		if (Char == TEXT(' '))
		{
			LastSpace = Line.Len();
		}
		Line.AppendChar(Char);
		if (TextWidth(Line) > MaxWidth && Line.Len() > 1)
		{
			// Break at the last space if there is one, else mid-word.
			if (LastSpace > 0)
			{
				Lines.Add(Line.Left(LastSpace));
				Line = Line.Mid(LastSpace + 1);
			}
			else
			{
				Lines.Add(Line.LeftChop(1));
				Line = Line.Right(1);
			}
			LastSpace = INDEX_NONE;
		}
	}
	Lines.Add(Line);
	return Lines;
}

void AMcHUD::DrawChat(int32 Width, int32 Height, bool bOpen)
{
	LoadFont();
	const TArray<UMcPlayerDataComponent::FChatLine>& Log = UMcPlayerDataComponent::GetChatLog();
	// As the game draws it: 320 wide, 9 a line, the newest at the bottom, just above the hotbar.
	const float ChatWidth = FMath::Min(320.0f, Width - 8.0f);
	const double Now = FPlatformTime::Seconds();
	const int32 MaxLines = bOpen ? 20 : 10;
	float Y = Height - 48.0f;
	int32 Shown = 0;
	for (int32 Index = Log.Num() - 1; Index >= 0 && Shown < MaxLines; --Index)
	{
		const UMcPlayerDataComponent::FChatLine& Entry = Log[Index];
		float Alpha = 1.0f;
		// -McShowChat (a font test's shot) keeps the lines at full strength.
		static const bool bKeepLines = FParse::Param(FCommandLine::Get(), TEXT("McShowChat"));
		if (!bOpen && !bKeepLines)
		{
			// Ten seconds on screen, the last of them fading.
			const double Age = Now - Entry.Time;
			if (Age >= 10.0)
			{
				break;
			}
			Alpha = static_cast<float>(FMath::Clamp(10.0 - Age, 0.0, 1.0));
		}
		const TArray<FString> Wrapped = WrapText(Entry.Text, ChatWidth - 4.0f);
		for (int32 Part = Wrapped.Num() - 1; Part >= 0 && Shown < MaxLines; --Part, ++Shown)
		{
			Y -= 9.0f;
			DrawFill(2.0f, Y, ChatWidth, 9.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f * Alpha));
			DrawGuiText(Wrapped[Part], 4.0f, Y + 1.0f, Entry.Rgb, true, Alpha);
		}
	}
}

void AMcHUD::DrawHeldName(int32 Width, int32 Height, const FMcItemStack& Held, bool bBars)
{
	const double Now = FPlatformTime::Seconds();
	if (Held.IsEmpty())
	{
		HeldNameItem = NAME_None;
		HeldNameText.Reset();
		return;
	}
	const FString Name = GetItemName(Held);
	if (Held.Item != HeldNameItem || Name != HeldNameText)
	{
		HeldNameItem = Held.Item;
		HeldNameText = Name;
		HeldNameTime = Now;
	}
	// Measured against the reference client (helditem runs: single shots a tick after a slot change, creative and
	// survival; a run of shots every two ticks): the label shows for 40 ticks, full for the first 30 and fading in a
	// straight line over the last ten (strength 0.8, 0.6, 0.4, 0.2 at ticks 32 to 38); its text top is 45 GUI pixels
	// above the bottom with no bars over the hotbar and 59 with them; its colour is the item's rarity (rare aqua
	// 55FFFF, epic magenta FF55FF, uncommon yellow FFFF55, else white).
	const double Age = Now - HeldNameTime;
	if (Age >= 2.0)
	{
		return;
	}
	const float Alpha = static_cast<float>(FMath::Clamp((2.0 - Age) / 0.5, 0.0, 1.0));
	uint32 Rgb = 0xFFFFFF;
	const TSharedPtr<FJsonValue> Rarity = FMcItemRegistry::Get().GetDefault(Held.Item, TEXT("minecraft:rarity"));
	if (Rarity.IsValid() && Rarity->Type == EJson::String)
	{
		const FString Kind = Rarity->AsString();
		Rgb = Kind == TEXT("rare") ? 0x55FFFF : Kind == TEXT("epic") ? 0xFF55FF : Kind == TEXT("uncommon") ? 0xFFFF55 : 0xFFFFFF;
	}
	LoadFont();
	DrawGuiText(Name, FMath::RoundToFloat(Width / 2.0f - TextWidth(Name) / 2.0f), Height - (bBars ? 59.0f : 45.0f), Rgb, true, Alpha);
}

void AMcHUD::DrawOverlay(int32 Width, int32 Height)
{
	const UMcPlayerDataComponent::FOverlay& Overlay = UMcPlayerDataComponent::GetOverlay();
	const double Age = FPlatformTime::Seconds() - Overlay.Time;
	if (Overlay.Text.IsEmpty() || Age < 0.0 || Age >= UMcPlayerDataComponent::OverlaySeconds)
	{
		return;
	}
	LoadFont();
	// Measured against the reference client (jukebox run 8: 90 shots two ticks apart, game tick logged, chat cleared): a
	// record's title is lime (153, 255, 45) for its first ten ticks (the last lime shot at tick 2, the first hue at 4,
	// with the first shot 6.7 ticks after the disc went in), then runs round the rainbow, hue falling 0.0201 a tick
	// (1/50) from 1.0 at tick ten (saturation 0.7, value 0.6). Its last twenty ticks fade it out in a straight line
	// (strength 0.9, 0.8, 0.68, 0.57, 0.46, 0.36, 0.28, 0.21 at ticks 36 to 50 against the 1/20 a tick of
	// remaining / 20); it is gone at tick 52 and the text lasts 60 ticks.
	uint32 Rgb = 0xFFFFFF;
	if (Overlay.bRainbow)
	{
		const double Ticks = Age * 20.0;
		if (Ticks < 10.0)
		{
			Rgb = 0x99FF2D;
		}
		else
		{
			const float Hue = static_cast<float>(FMath::Frac((60.0 - Ticks) / 50.0)) * 6.0f;
			const float Value = 0.6f;
			const float Chroma = Value * 0.7f;
			const float X = Chroma * (1.0f - FMath::Abs(FMath::Fmod(Hue, 2.0f) - 1.0f));
			const float Low = Value - Chroma;
			float R = Chroma, G = 0.0f, B = X;
			const int32 Sector = FMath::Clamp(FMath::FloorToInt(Hue), 0, 5);
			switch (Sector)
			{
			case 0: R = Chroma; G = X; B = 0.0f; break;
			case 1: R = X; G = Chroma; B = 0.0f; break;
			case 2: R = 0.0f; G = Chroma; B = X; break;
			case 3: R = 0.0f; G = X; B = Chroma; break;
			case 4: R = X; G = 0.0f; B = Chroma; break;
			default: R = Chroma; G = 0.0f; B = X; break;
			}
			Rgb = (FMath::RoundToInt((R + Low) * 255.0f) << 16) | (FMath::RoundToInt((G + Low) * 255.0f) << 8) | FMath::RoundToInt((B + Low) * 255.0f);
		}
	}
	const float Alpha = static_cast<float>(FMath::Clamp(UMcPlayerDataComponent::OverlaySeconds - Age, 0.0, 1.0));
	DrawGuiText(Overlay.Text, FMath::RoundToFloat(Width / 2.0f - TextWidth(Overlay.Text) / 2.0f), Height - 72.0f, Rgb, true, Alpha);
}

void AMcHUD::DrawChatInput(int32 Width, int32 Height)
{
	LoadFont();
	APlayerController* Player = GetOwningPlayerController();
	UMcPlayerDataComponent* Data = GetOwningPawn() ? GetOwningPawn()->FindComponentByClass<UMcPlayerDataComponent>() : nullptr;
	if (!Data)
	{
		OpenMenu(EMenu::None);
		return;
	}
	ChatDraft.Append(MenuTyped.Left(FMath::Max(0, 256 - ChatDraft.Len())));
	if (bMenuBackspace && !ChatDraft.IsEmpty())
	{
		ChatDraft.LeftChopInline(1);
	}
	if (Player && SentChat.Num() > 0)
	{
		if (Player->WasInputKeyJustPressed(EKeys::Up))
		{
			SentChatIndex = FMath::Max(0, SentChatIndex - 1);
			ChatDraft = SentChat[SentChatIndex];
		}
		else if (Player->WasInputKeyJustPressed(EKeys::Down))
		{
			SentChatIndex = FMath::Min(SentChat.Num(), SentChatIndex + 1);
			ChatDraft = SentChat.IsValidIndex(SentChatIndex) ? SentChat[SentChatIndex] : FString();
		}
	}
	if (bMenuEnter)
	{
		const FString Line = ChatDraft.TrimStartAndEnd();
		if (!Line.IsEmpty())
		{
			if (SentChat.Num() == 0 || SentChat.Last() != Line)
			{
				SentChat.Add(Line);
			}
			Data->SendChat(Line);
		}
		OpenMenu(EMenu::None);
		return;
	}
	// The box along the bottom, showing the end of a long line, with a blinking caret.
	DrawFill(2.0f, Height - 14.0f, Width - 4.0f, 12.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.5f));
	FString Shown = ChatDraft;
	while (!Shown.IsEmpty() && TextWidth(Shown) > Width - 14.0f)
	{
		Shown.RightChopInline(1);
	}
	const bool bCaret = FMath::Fmod(FPlatformTime::Seconds(), 0.6) < 0.3;
	DrawGuiText(bCaret ? Shown + TEXT("_") : Shown, 4.0f, Height - 12.0f, 0xFFFFFF, true);
}

// ---- The vignette ----

float AMcHUD::GetEyeLight(const AMcPlayerPawn& Player)
{
	const double Now = FPlatformTime::Seconds();
	if (Now - VignetteLightTime < 0.5)
	{
		return VignetteEyeLight;
	}
	VignetteLightTime = Now;
	const FVector Feet = Player.GetFeetPosition();
	const FIntVector Eye(FMath::FloorToInt32(Feet.X), FMath::FloorToInt32(Feet.Y + 1.62), FMath::FloorToInt32(Feet.Z));
	const FMcBlockWorld* World = Player.GetBlockWorld();
	const FIntPoint Chunk(McCoords::BlockToChunk(Eye.X), McCoords::BlockToChunk(Eye.Z));
	if (World && World->HasChunk(Chunk))
	{
		static TSharedPtr<const FMcLightPalette> Palette;
		if (!Palette.IsValid())
		{
			Palette = FMcLightPalette::Build();
		}
		FMcLightRegion Region = FMcLightRegion::Capture(*World, Chunk);
		Region.Compute(*Palette);
		const int32 X = Eye.X - (Chunk.X - 1) * 16;
		const int32 Z = Eye.Z - (Chunk.Y - 1) * 16;
		// The sky's light counts as much as the day lets through (the night's is a fraction of it).
		VignetteEyeLight = FMath::Max<float>(Region.GetBlockLight(X, Eye.Y, Z), Region.GetSkyLight(X, Eye.Y, Z) * McSky::GetSkyLightFactor());
	}
	return VignetteEyeLight;
}

float AMcHUD::StepVignette(const AMcPlayerPawn& Player)
{
	const double Now = FPlatformTime::Seconds();
	const float Target = McGui::GetVignetteTarget(GetEyeLight(Player));
	// The first frame starts at its target; after that the strength eases a little each game tick (20 a second).
	VignetteStrength = VignetteStrength < 0.0f ? Target : McGui::EaseVignette(VignetteStrength, Target, static_cast<float>((Now - VignetteLastTime) * 20.0));
	VignetteLastTime = Now;
	return VignetteStrength;
}

// ---- F3 ----

void AMcHUD::DrawDebug(int32 Width, int32 Height, AMcPlayerPawn& Player)
{
	const FVector Feet = Player.GetFeetPosition();
	const FIntVector Block(FMath::FloorToInt32(Feet.X), FMath::FloorToInt32(Feet.Y), FMath::FloorToInt32(Feet.Z));
	const FMcBlockWorld* World = Player.GetBlockWorld();
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();

	// What the game's debug screen shows of the light and biome at the feet: sky and block light, and the biome's name.
	const double Now = FPlatformTime::Seconds();
	if (World && Now - DebugSampleTime > 1.0)
	{
		DebugSampleTime = Now;
		static TSharedPtr<const FMcLightPalette> Palette;
		if (!Palette.IsValid())
		{
			Palette = FMcLightPalette::Build();
		}
		const FIntPoint Chunk(McCoords::BlockToChunk(Block.X), McCoords::BlockToChunk(Block.Z));
		if (World->HasChunk(Chunk))
		{
			FMcLightRegion Region = FMcLightRegion::Capture(*World, Chunk);
			Region.Compute(*Palette);
			const int32 X = Block.X - (Chunk.X - 1) * 16;
			const int32 Z = Block.Z - (Chunk.Y - 1) * 16;
			DebugSky = Region.GetSkyLight(X, Block.Y, Z);
			DebugBlockLight = Region.GetBlockLight(X, Block.Y, Z);
			const FMcBiomeId Biome = Region.GetBiome(X, Block.Y, Z);
			DebugBiome = Biome == FMcLightRegion::NoBiome ? FString(TEXT("unknown")) : FMcBiomeRegistry::Get().GetName(Biome).ToString();
		}
	}
	DebugDelta = FMath::Lerp(DebugDelta, FMath::Max(GetWorld()->GetDeltaSeconds(), 0.0001f), 0.05f);

	// Facing: Minecraft's yaw runs 0 south, 90 west, 180 north, 270 east.
	const float Yaw = FRotator::NormalizeAxis(Player.GetLookYaw());
	const float Pitch = Player.GetLookPitch();
	const float Wrapped = FMath::Fmod(FMath::Fmod(Yaw, 360.0f) + 360.0f, 360.0f);
	static const TCHAR* Names[4] = { TEXT("south (Towards positive Z)"), TEXT("west (Towards negative X)"), TEXT("north (Towards negative Z)"), TEXT("east (Towards positive X)") };
	const int32 Quarter = FMath::RoundToInt32(Wrapped / 90.0f) & 3;

	TArray<FString> Left;
	Left.Add(TEXT("Minecraft 26.3 (Unreal port)"));
	Left.Add(FString::Printf(TEXT("%d fps"), FMath::RoundToInt32(1.0f / DebugDelta)));
	Left.Add(FString());
	Left.Add(FString::Printf(TEXT("XYZ: %.3f / %.5f / %.3f"), Feet.X, Feet.Y, Feet.Z));
	Left.Add(FString::Printf(TEXT("Block: %d %d %d"), Block.X, Block.Y, Block.Z));
	Left.Add(FString::Printf(TEXT("Chunk: %d %d %d in %d %d"), Block.X & 15, Block.Y & 15, Block.Z & 15, McCoords::BlockToChunk(Block.X), McCoords::BlockToChunk(Block.Z)));
	Left.Add(FString::Printf(TEXT("Facing: %s (%.1f / %.1f)"), Names[Quarter], Yaw, Pitch));
	Left.Add(FString::Printf(TEXT("Light: %d (%d sky, %d block)"), FMath::Max(DebugSky, DebugBlockLight), DebugSky, DebugBlockLight));
	Left.Add(TEXT("Biome: ") + DebugBiome);

	TArray<FString> Right;
	if (Player.GetTarget().IsSet() && World)
	{
		const FMcBlockHit& Hit = Player.GetTarget().GetValue();
		Right.Add(FString::Printf(TEXT("Targeted Block: %d, %d, %d"), Hit.Block.X, Hit.Block.Y, Hit.Block.Z));
		Right.Add(Registry.GetBlockOf(Hit.State).Name.ToString());
	}

	auto DrawLines = [this](const TArray<FString>& Lines, bool bRight, int32 Width)
	{
		float Y = 2.0f;
		for (const FString& Line : Lines)
		{
			if (!Line.IsEmpty())
			{
				const float W = TextWidth(Line);
				const float X = bRight ? Width - 2.0f - W : 2.0f;
				DrawFill(X - 1.0f, Y - 1.0f, W + 2.0f, 10.0f, FLinearColor(0.31f, 0.31f, 0.31f, 0.56f));
				DrawGuiText(Line, X, Y, 0xE0E0E0, false);
			}
			Y += 9.0f;
		}
	};
	DrawLines(Left, false, Width);
	DrawLines(Right, true, Width);
}

// ---- Sign editor ----

namespace
{
	namespace SignEdit
	{
		// McSign::EKind as the menu carries it.
		bool IsHanging(uint8 Kind) { return Kind == 3 || Kind == 4; }
		// A line may be as wide as this many font pixels: the board's width at the text's scale.
		float MaxWidth(uint8 Kind) { return IsHanging(Kind) ? 60.0f : 90.0f; }
		float LineHeight(uint8 Kind) { return IsHanging(Kind) ? 9.0f : 10.0f; }
		// GUI pixels per font pixel.
		float TextScale(uint8 Kind) { return IsHanging(Kind) ? 0.9f : 1.0f; }
		// The text's centre, and the board's size.
		constexpr float CentreY = 90.0f;
		constexpr float BoardWidth = 96.0f;
		constexpr float BoardHeight = 48.0f;
		constexpr float HangingSize = 72.0f;
		constexpr float DoneY = 144.0f;

		// "minecraft:dark_oak_wall_hanging_sign" -> "dark_oak".
		FString WoodOf(FName Block)
		{
			FString Wood = Block.ToString();
			Wood.RemoveFromStart(TEXT("minecraft:"));
			for (const TCHAR* Suffix : { TEXT("_wall_hanging_sign"), TEXT("_hanging_sign"), TEXT("_wall_sign"), TEXT("_sign") })
			{
				if (Wood.RemoveFromEnd(Suffix))
				{
					break;
				}
			}
			return Wood;
		}

		bool IsOverDone(const FVector2D& Mouse, int32 Width, int32 Height)
		{
			const float X = Width / 2.0f - 100.0f;
			const float Y = Height / 4.0f + DoneY;
			return Mouse.X >= X && Mouse.X < X + 200.0f && Mouse.Y >= Y && Mouse.Y < Y + 20.0f;
		}
	}
}

void AMcHUD::PollSign(APlayerController& Player, UMcPlayerDataComponent& Data, int32 Width, int32 Height)
{
	FScreenState& State = *Screen;
	FMcSignMenu& Menu = *State.Sign;
	LoadFont();
	float MouseX = 0.0f, MouseY = 0.0f;
	Player.GetMousePosition(MouseX, MouseY);
	GuiMouse = FVector2D(MouseX, MouseY) / Scale;

	// Done and Esc both keep what was written: the lines go to the server, which writes them as the window closes.
	if (Player.WasInputKeyJustPressed(EKeys::Escape)
		|| (Player.WasInputKeyJustPressed(EKeys::LeftMouseButton) && SignEdit::IsOverDone(GuiMouse, Width, Height)))
	{
		Data.SetMenuText(FMcSignMenu::Join(Menu.Lines));
		CloseScreen();
		return;
	}

	int32& Line = State.SignLine;
	int32& Cursor = State.SignCursor;
	FString& Text = Menu.Lines[Line];
	Cursor = FMath::Clamp(Cursor, 0, Text.Len());
	// What was typed goes in at the cursor, a character at a time while the line still fits the board.
	const float Max = SignEdit::MaxWidth(Menu.SignKind);
	for (const TCHAR Char : UMcGameViewportClient::TakeTyped())
	{
		if (Char == TEXT('§'))
		{
			continue;
		}
		const FString Longer = Text.Left(Cursor) + Char + Text.Mid(Cursor);
		if (Longer.Len() <= FMcSignMenu::MaxLineLength && TextWidth(Longer) <= Max)
		{
			Text = Longer;
			++Cursor;
		}
	}
	for (int32 Back = UMcGameViewportClient::TakeBackspaces(); Back > 0 && Cursor > 0; --Back)
	{
		Text.RemoveAt(--Cursor);
	}
	if (Player.WasInputKeyJustPressed(EKeys::Delete) && Cursor < Text.Len())
	{
		Text.RemoveAt(Cursor);
	}
	if (Player.WasInputKeyJustPressed(EKeys::Left))
	{
		Cursor = FMath::Max(0, Cursor - 1);
	}
	if (Player.WasInputKeyJustPressed(EKeys::Right))
	{
		Cursor = FMath::Min(Text.Len(), Cursor + 1);
	}
	if (Player.WasInputKeyJustPressed(EKeys::Home))
	{
		Cursor = 0;
	}
	if (Player.WasInputKeyJustPressed(EKeys::End))
	{
		Cursor = Text.Len();
	}
	// Up goes to the line above, Down and Enter to the one below, round from the last to the first.
	int32 Move = 0;
	if (Player.WasInputKeyJustPressed(EKeys::Up))
	{
		Move = -1;
	}
	if (Player.WasInputKeyJustPressed(EKeys::Down) || Player.WasInputKeyJustPressed(EKeys::Enter))
	{
		Move = 1;
	}
	if (Move != 0)
	{
		Line = (Line + Move + FMcSignMenu::NumLines) % FMcSignMenu::NumLines;
		Cursor = Menu.Lines[Line].Len();
	}
}

void AMcHUD::DrawSign(int32 Width, int32 Height)
{
	const FMcSignMenu& Menu = *Screen->Sign;
	const uint8 Kind = Menu.SignKind;
	const float CentreX = Width / 2.0f;
	LoadFont();
	DrawFill(0, 0, static_cast<float>(Width), static_cast<float>(Height), FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
	DrawCentredText(TEXT("Edit Sign Message"), CentreX, 40.0f, 0xFFFFFF);

	const FString Wood = SignEdit::WoodOf(Menu.Block);
	float TextCentreY = SignEdit::CentreY;
	if (SignEdit::IsHanging(Kind))
	{
		// The screen's own picture of the hanging sign, chains and board, its sixteen pixels drawn four and a half times.
		const float Top = SignEdit::CentreY - SignEdit::HangingSize / 2.0f;
		DrawSprite(LoadSprite(TEXT("textures/gui/hanging_signs/") + Wood + TEXT(".png")), CentreX - SignEdit::HangingSize / 2.0f, Top, SignEdit::HangingSize, SignEdit::HangingSize);
		TextCentreY = Top + SignEdit::HangingSize * 0.625f;
	}
	else
	{
		// The board's front from the block's picture, and the stick under it for one on the ground.
		UTexture2D* Picture = LoadSprite(TEXT("textures/block/") + Wood + TEXT("_sign.png"));
		const float Top = SignEdit::CentreY - SignEdit::BoardHeight / 2.0f;
		DrawRegion(Picture, CentreX - SignEdit::BoardWidth / 2.0f, Top, SignEdit::BoardWidth, SignEdit::BoardHeight, 0, 2, 24, 12);
		if (Kind == 1)
		{
			DrawRegion(Picture, CentreX - 4.0f, Top + SignEdit::BoardHeight, 8.0f, 56.0f, 28, 0, 2, 14);
		}
	}

	// The lines in the side's dye, centred; the cursor blinks on the one being written.
	const float TextScale = SignEdit::TextScale(Kind);
	const float LineHeight = SignEdit::LineHeight(Kind);
	const bool bBlink = FMath::FloorToInt(FPlatformTime::Seconds() / 0.3) % 2 == 0;
	UTexture2D* Font = LoadSprite(TEXT("textures/font/ascii.png"));
	const FLinearColor Colour = FromRgb(Menu.TextColour);
	const auto DrawGlyph = [&](TCHAR Char, float X, float Y)
	{
		const int32 Code = Char < 256 ? Char : '?';
		DrawRegion(Font, X, Y, 8 * TextScale, 8 * TextScale, (Code % 16) * 8, (Code / 16) * 8, 8, 8, Colour);
		return (McFont::GetGlyphWidths()[Code] + 1) * TextScale;
	};
	for (int32 Index = 0; Index < Menu.Lines.Num(); ++Index)
	{
		const FString& Text = Menu.Lines[Index];
		const float Y = TextCentreY + (Index - 2) * LineHeight * TextScale;
		float At = FMath::RoundToFloat(CentreX - TextWidth(Text) * TextScale / 2.0f);
		float CursorX = At;
		for (int32 Char = 0; Char < Text.Len(); ++Char)
		{
			if (Char == Screen->SignCursor)
			{
				CursorX = At;
			}
			At += DrawGlyph(Text[Char], At, Y);
		}
		if (Index != Screen->SignLine || !bBlink)
		{
			continue;
		}
		// At the end of the line the cursor is an underscore after the text; within it, a thin bar before the character.
		if (Screen->SignCursor >= Text.Len())
		{
			DrawGlyph(TEXT('_'), At, Y);
		}
		else
		{
			DrawFill(CursorX - 1.0f, Y - 1.0f, 1.0f, LineHeight * TextScale, Colour);
		}
	}

	const bool bOver = SignEdit::IsOverDone(GuiMouse, Width, Height);
	const float DoneTop = Height / 4.0f + SignEdit::DoneY;
	DrawNineSlice(LoadSprite(bOver ? TEXT("textures/gui/sprites/widget/button_highlighted.png") : TEXT("textures/gui/sprites/widget/button.png")),
		CentreX - 100.0f, DoneTop, 200.0f, 20.0f, 200.0f, 20.0f, 3.0f);
	DrawCentredText(TEXT("Done"), CentreX, DoneTop + 6.0f, 0xFFFFFF);
}

// ---- Book (on a lectern) ----

namespace
{
	namespace BookView
	{
		// The page picture is the top left 192 by 192 of textures/gui/book.png, centred at the top of the screen; the
		// text 114 wide from 36 in and 32 down, 9 a line, at most 14 lines; the arrows 23 by 13; the buttons under it.
		constexpr float Size = 192.0f;
		constexpr float Top = 2.0f;
		constexpr float TextX = 36.0f;
		constexpr float TextY = 32.0f;
		constexpr float TextWidth = 114.0f;
		constexpr float LineHeight = 9.0f;
		constexpr int32 MaxLines = 14;
		constexpr float ArrowW = 23.0f;
		constexpr float ArrowH = 13.0f;
		constexpr float ForwardX = 116.0f;
		constexpr float BackX = 43.0f;
		constexpr float ArrowY = 159.0f;
		constexpr float ButtonsY = 196.0f;

		float Left(int32 Width) { return FMath::FloorToFloat((Width - Size) / 2.0f); }

		bool IsOver(const FVector2D& Mouse, float X, float Y, float W, float H)
		{
			return Mouse.X >= X && Mouse.X < X + W && Mouse.Y >= Y && Mouse.Y < Y + H;
		}
		bool IsOverForward(const FVector2D& Mouse, int32 Width) { return IsOver(Mouse, Left(Width) + ForwardX, Top + ArrowY, ArrowW, ArrowH); }
		bool IsOverBack(const FVector2D& Mouse, int32 Width) { return IsOver(Mouse, Left(Width) + BackX, Top + ArrowY, ArrowW, ArrowH); }
		// Done, then Take Book, each 98 wide, side by side.
		bool IsOverDone(const FVector2D& Mouse, int32 Width) { return IsOver(Mouse, Width / 2.0f - 100.0f, ButtonsY, 98.0f, 20.0f); }
		bool IsOverTake(const FVector2D& Mouse, int32 Width) { return IsOver(Mouse, Width / 2.0f + 2.0f, ButtonsY, 98.0f, 20.0f); }
	}
}

void AMcHUD::PollLectern(APlayerController& Player, UMcPlayerDataComponent& Data, int32 Width, int32 Height)
{
	const FMcLecternMenu& Menu = *Screen->Lectern;
	float MouseX = 0.0f, MouseY = 0.0f;
	Player.GetMousePosition(MouseX, MouseY);
	GuiMouse = FVector2D(MouseX, MouseY) / Scale;
	const bool bClick = Player.WasInputKeyJustPressed(EKeys::LeftMouseButton);
	if (Player.WasInputKeyJustPressed(EKeys::Escape) || (bClick && BookView::IsOverDone(GuiMouse, Width)))
	{
		CloseScreen();
		return;
	}
	if (bClick && BookView::IsOverTake(GuiMouse, Width))
	{
		// The server hands the book over; the window closes once the lectern is bare.
		Data.ClickMenuButton(FMcLecternMenu::TakeBook);
		return;
	}
	const bool bCanBack = Menu.Page > 0;
	const bool bCanForward = Menu.Page < Menu.Pages.Num() - 1;
	if (bCanBack && (Player.WasInputKeyJustPressed(EKeys::PageUp) || (bClick && BookView::IsOverBack(GuiMouse, Width))))
	{
		Data.ClickMenuButton(FMcLecternMenu::PreviousPage);
	}
	else if (bCanForward && (Player.WasInputKeyJustPressed(EKeys::PageDown) || (bClick && BookView::IsOverForward(GuiMouse, Width))))
	{
		Data.ClickMenuButton(FMcLecternMenu::NextPage);
	}
}

void AMcHUD::DrawLectern(int32 Width, int32 Height)
{
	const FMcLecternMenu& Menu = *Screen->Lectern;
	LoadFont();
	DrawFill(0, 0, static_cast<float>(Width), static_cast<float>(Height), FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
	const float Left = BookView::Left(Width);
	DrawRegion(LoadSprite(TEXT("textures/gui/book.png")), Left, BookView::Top, BookView::Size, BookView::Size, 0, 0, BookView::Size, BookView::Size);

	// "Page 1 of 3" in the top right corner of the page, then the page's text in black, wrapped to the page.
	const int32 NumPages = FMath::Max(1, Menu.Pages.Num());
	const FString Indicator = FString::Printf(TEXT("Page %d of %d"), Menu.Page + 1, NumPages);
	DrawGuiText(Indicator, Left + BookView::Size - 44.0f - TextWidth(Indicator), BookView::Top + 16.0f, 0x000000, false);
	if (Menu.Pages.IsValidIndex(Menu.Page))
	{
		TArray<FString> Lines;
		TArray<FString> Paragraphs;
		Menu.Pages[Menu.Page].ParseIntoArray(Paragraphs, TEXT("\n"), false);
		for (const FString& Paragraph : Paragraphs)
		{
			Lines.Append(WrapText(Paragraph, BookView::TextWidth));
		}
		for (int32 Line = 0; Line < FMath::Min(Lines.Num(), BookView::MaxLines); ++Line)
		{
			DrawGuiText(Lines[Line], Left + BookView::TextX, BookView::Top + BookView::TextY + Line * BookView::LineHeight, 0x000000, false);
		}
	}

	// The arrows, where there is a page that way; lit under the mouse.
	const FString Widget = TEXT("textures/gui/sprites/widget/");
	if (Menu.Page < Menu.Pages.Num() - 1)
	{
		const bool bOver = BookView::IsOverForward(GuiMouse, Width);
		DrawSprite(LoadSprite(Widget + (bOver ? TEXT("page_forward_highlighted.png") : TEXT("page_forward.png"))),
			Left + BookView::ForwardX, BookView::Top + BookView::ArrowY, BookView::ArrowW, BookView::ArrowH);
	}
	if (Menu.Page > 0)
	{
		const bool bOver = BookView::IsOverBack(GuiMouse, Width);
		DrawSprite(LoadSprite(Widget + (bOver ? TEXT("page_backward_highlighted.png") : TEXT("page_backward.png"))),
			Left + BookView::BackX, BookView::Top + BookView::ArrowY, BookView::ArrowW, BookView::ArrowH);
	}

	const auto Button = [this](const FString& Label, float X, bool bOver)
	{
		DrawNineSlice(LoadSprite(bOver ? TEXT("textures/gui/sprites/widget/button_highlighted.png") : TEXT("textures/gui/sprites/widget/button.png")),
			X, BookView::ButtonsY, 98.0f, 20.0f, 200.0f, 20.0f, 3.0f);
		DrawCentredText(Label, X + 49.0f, BookView::ButtonsY + 6.0f, 0xFFFFFF);
	};
	Button(TEXT("Done"), Width / 2.0f - 100.0f, BookView::IsOverDone(GuiMouse, Width));
	Button(TEXT("Take Book"), Width / 2.0f + 2.0f, BookView::IsOverTake(GuiMouse, Width));
}
