#include "McBanner.h"
#include "McCache.h"
#include "Dom/JsonObject.h"

namespace McBanner
{
	FLinearColor GetDyeColour(const FString& Name)
	{
		static const TMap<FString, uint32> Colours = {
			{ TEXT("white"), 0xF9FFFE }, { TEXT("orange"), 0xF9801D }, { TEXT("magenta"), 0xC74EBD }, { TEXT("light_blue"), 0x3AB3DA },
			{ TEXT("yellow"), 0xFED83D }, { TEXT("lime"), 0x80C71F }, { TEXT("pink"), 0xF38BAA }, { TEXT("gray"), 0x474F52 },
			{ TEXT("light_gray"), 0x9D9D97 }, { TEXT("cyan"), 0x169C9C }, { TEXT("purple"), 0x8932B8 }, { TEXT("blue"), 0x3C44AA },
			{ TEXT("brown"), 0x835432 }, { TEXT("green"), 0x5E7C16 }, { TEXT("red"), 0xB02E26 }, { TEXT("black"), 0x1D1D21 },
		};
		const uint32* Rgb = Colours.Find(Name);
		return Rgb ? FLinearColor(FColor((*Rgb >> 16) & 0xFF, (*Rgb >> 8) & 0xFF, *Rgb & 0xFF)) : FLinearColor::White;
	}

	TArray<FLayer> GetLayers(const FMcItemStack& Banner)
	{
		TArray<FLayer> Result;
		const TSharedPtr<FJsonValue> Value = Banner.Get(TEXT("minecraft:banner_patterns"));
		if (Value.IsValid() && Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
			{
				const TSharedPtr<FJsonObject>* Object;
				FString Pattern, Colour;
				if (Entry->TryGetObject(Object) && (*Object)->TryGetStringField(TEXT("pattern"), Pattern) && (*Object)->TryGetStringField(TEXT("color"), Colour))
				{
					Result.Add({ McCache::PathOf(Pattern), Colour });
				}
			}
		}
		return Result;
	}

	FString GetBaseColour(FName Item)
	{
		FString Base = McCache::PathOf(Item.ToString());
		Base.RemoveFromEnd(TEXT("_banner"));
		return Base;
	}

	TSharedPtr<FJsonValue> MakeComponent(const TArray<FLayer>& Layers)
	{
		TArray<TSharedPtr<FJsonValue>> Entries;
		for (const FLayer& Layer : Layers)
		{
			TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
			Object->SetStringField(TEXT("pattern"), TEXT("minecraft:") + Layer.Pattern);
			Object->SetStringField(TEXT("color"), Layer.Colour);
			Entries.Add(MakeShared<FJsonValueObject>(Object));
		}
		return MakeShared<FJsonValueArray>(Entries);
	}

	FString Encode(const FString& Base, bool bWall, const TArray<FLayer>& Layers)
	{
		TArray<FString> Parts;
		for (const FLayer& Layer : Layers)
		{
			Parts.Add(Layer.Pattern + TEXT("=") + Layer.Colour);
		}
		return Base + (bWall ? TEXT(";w;") : TEXT(";s;")) + FString::Join(Parts, TEXT(","));
	}

	bool Decode(const FString& Text, FString& OutBase, bool& bOutWall, TArray<FLayer>& OutLayers)
	{
		TArray<FString> Fields;
		Text.ParseIntoArray(Fields, TEXT(";"), false);
		if (Fields.Num() != 3)
		{
			return false;
		}
		OutBase = Fields[0];
		bOutWall = Fields[1] == TEXT("w");
		OutLayers.Reset();
		TArray<FString> Entries;
		Fields[2].ParseIntoArray(Entries, TEXT(","), true);
		for (const FString& Entry : Entries)
		{
			FString Pattern, Colour;
			if (Entry.Split(TEXT("="), &Pattern, &Colour))
			{
				OutLayers.Add({ Pattern, Colour });
			}
		}
		return true;
	}
}
