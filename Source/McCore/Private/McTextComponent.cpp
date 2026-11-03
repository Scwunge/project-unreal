#include "McTextComponent.h"
#include "McLang.h"
#include "Dom/JsonObject.h"
#include "Misc/Parse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	struct FNamedColour
	{
		const TCHAR* Name;
		TCHAR Code;
		int32 Rgb;
	};

	const FNamedColour GColours[] = {
		{ TEXT("black"), '0', 0x000000 }, { TEXT("dark_blue"), '1', 0x0000AA }, { TEXT("dark_green"), '2', 0x00AA00 },
		{ TEXT("dark_aqua"), '3', 0x00AAAA }, { TEXT("dark_red"), '4', 0xAA0000 }, { TEXT("dark_purple"), '5', 0xAA00AA },
		{ TEXT("gold"), '6', 0xFFAA00 }, { TEXT("gray"), '7', 0xAAAAAA }, { TEXT("dark_gray"), '8', 0x555555 },
		{ TEXT("blue"), '9', 0x5555FF }, { TEXT("green"), 'a', 0x55FF55 }, { TEXT("aqua"), 'b', 0x55FFFF },
		{ TEXT("red"), 'c', 0xFF5555 }, { TEXT("light_purple"), 'd', 0xFF55FF }, { TEXT("yellow"), 'e', 0xFFFF55 },
		{ TEXT("white"), 'f', 0xFFFFFF },
	};

	// The style a component sets; -1 / empty leaves the parent's.
	struct FStyle
	{
		int32 Rgb = -1;
		int8 Bold = -1, Italic = -1, Underlined = -1, Strikethrough = -1, Obfuscated = -1;
		FString Font;
		FString ClickAction, ClickValue, HoverText;
	};

	void Inherit(FStyle& Style, const FStyle& Parent)
	{
		if (Style.Rgb < 0) Style.Rgb = Parent.Rgb;
		if (Style.Bold < 0) Style.Bold = Parent.Bold;
		if (Style.Italic < 0) Style.Italic = Parent.Italic;
		if (Style.Underlined < 0) Style.Underlined = Parent.Underlined;
		if (Style.Strikethrough < 0) Style.Strikethrough = Parent.Strikethrough;
		if (Style.Obfuscated < 0) Style.Obfuscated = Parent.Obfuscated;
		if (Style.Font.IsEmpty()) Style.Font = Parent.Font;
		if (Style.ClickAction.IsEmpty())
		{
			Style.ClickAction = Parent.ClickAction;
			Style.ClickValue = Parent.ClickValue;
		}
		if (Style.HoverText.IsEmpty()) Style.HoverText = Parent.HoverText;
	}

	void Emit(const FString& Text, const FStyle& Style, TArray<McText::FSpan>& Out)
	{
		if (Text.IsEmpty())
		{
			return;
		}
		McText::FSpan& Span = Out.AddDefaulted_GetRef();
		Span.Text = Text;
		Span.Rgb = Style.Rgb;
		Span.bBold = Style.Bold > 0;
		Span.bItalic = Style.Italic > 0;
		Span.bUnderlined = Style.Underlined > 0;
		Span.bStrikethrough = Style.Strikethrough > 0;
		Span.bObfuscated = Style.Obfuscated > 0;
		Span.Font = Style.Font;
		Span.ClickAction = Style.ClickAction;
		Span.ClickValue = Style.ClickValue;
		Span.HoverText = Style.HoverText;
	}

	void Walk(const TSharedPtr<FJsonValue>& Value, const FStyle& Parent, TArray<McText::FSpan>& Out, int32 Depth);

	int8 ReadFlag(const FJsonObject& Object, const TCHAR* Field)
	{
		bool bValue = false;
		return Object.TryGetBoolField(Field, bValue) ? (bValue ? 1 : 0) : -1;
	}

	// The key a binding has by default ("key.jump" is Space); an unknown one reads as its own name, as the game's does.
	FString KeyName(const FString& Keybind)
	{
		static const TMap<FString, FString> Defaults = {
			{ TEXT("key.forward"), TEXT("W") }, { TEXT("key.left"), TEXT("A") }, { TEXT("key.back"), TEXT("S") }, { TEXT("key.right"), TEXT("D") },
			{ TEXT("key.jump"), TEXT("Space") }, { TEXT("key.sneak"), TEXT("Left Shift") }, { TEXT("key.sprint"), TEXT("Left Control") },
			{ TEXT("key.inventory"), TEXT("E") }, { TEXT("key.drop"), TEXT("Q") }, { TEXT("key.chat"), TEXT("T") }, { TEXT("key.command"), TEXT("/") },
			{ TEXT("key.swapOffhand"), TEXT("F") }, { TEXT("key.attack"), TEXT("Left Button") }, { TEXT("key.use"), TEXT("Right Button") },
		};
		const FString* Found = Defaults.Find(Keybind);
		return Found ? *Found : Keybind;
	}

	void WalkObject(const FJsonObject& Object, const FStyle& Parent, TArray<McText::FSpan>& Out, int32 Depth)
	{
		FStyle Style;
		FString Colour;
		if (Object.TryGetStringField(TEXT("color"), Colour))
		{
			if (Colour.StartsWith(TEXT("#")) && Colour.Len() == 7)
			{
				Style.Rgb = static_cast<int32>(FParse::HexNumber(*Colour.Mid(1)) & 0xFFFFFF);
			}
			else
			{
				Style.Rgb = McText::ColourFromName(Colour);
			}
		}
		Style.Bold = ReadFlag(Object, TEXT("bold"));
		Style.Italic = ReadFlag(Object, TEXT("italic"));
		Style.Underlined = ReadFlag(Object, TEXT("underlined"));
		Style.Strikethrough = ReadFlag(Object, TEXT("strikethrough"));
		Style.Obfuscated = ReadFlag(Object, TEXT("obfuscated"));
		FString Font;
		if (Object.TryGetStringField(TEXT("font"), Font) && Font != TEXT("minecraft:default") && Font != TEXT("default"))
		{
			Style.Font = Font.Contains(TEXT(":")) ? Font : TEXT("minecraft:") + Font;
		}
		const TSharedPtr<FJsonObject>* Click;
		if (Object.TryGetObjectField(TEXT("click_event"), Click))
		{
			(*Click)->TryGetStringField(TEXT("action"), Style.ClickAction);
			// The value is "url", "command", "value" (copy_to_clipboard), "path" or "page", by action.
			for (const TCHAR* Field : { TEXT("url"), TEXT("command"), TEXT("value"), TEXT("path") })
			{
				if ((*Click)->TryGetStringField(Field, Style.ClickValue))
				{
					break;
				}
			}
			double Page = 0.0;
			if (Style.ClickValue.IsEmpty() && (*Click)->TryGetNumberField(TEXT("page"), Page))
			{
				Style.ClickValue = FString::FromInt(static_cast<int32>(Page));
			}
		}
		const TSharedPtr<FJsonObject>* Hover;
		if (Object.TryGetObjectField(TEXT("hover_event"), Hover))
		{
			FString Action;
			(*Hover)->TryGetStringField(TEXT("action"), Action);
			if (Action == TEXT("show_text") && (*Hover)->HasField(TEXT("value")))
			{
				TArray<McText::FSpan> Inner;
				McText::FromValue((*Hover)->TryGetField(TEXT("value")), Inner);
				Style.HoverText = McText::ToLegacy(Inner);
			}
		}
		Inherit(Style, Parent);

		FString Text, Key;
		if (Object.TryGetStringField(TEXT("text"), Text))
		{
			Emit(Text, Style, Out);
		}
		else if (Object.TryGetStringField(TEXT("translate"), Key))
		{
			TArray<FString> Args;
			const TArray<TSharedPtr<FJsonValue>>* With;
			if (Object.TryGetArrayField(TEXT("with"), With))
			{
				for (const TSharedPtr<FJsonValue>& Arg : *With)
				{
					TArray<McText::FSpan> Inner;
					McText::FromValue(Arg, Inner);
					Args.Add(McText::ToLegacy(Inner));
				}
			}
			FString Pattern = McLang::Get(Key);
			FString Fallback;
			if (Pattern == Key && Object.TryGetStringField(TEXT("fallback"), Fallback))
			{
				Pattern = Fallback;
			}
			Emit(McLang::FormatPattern(Pattern, Args), Style, Out);
		}
		else if (Object.TryGetStringField(TEXT("keybind"), Key))
		{
			Emit(KeyName(Key), Style, Out);
		}
		const TArray<TSharedPtr<FJsonValue>>* Extra;
		if (Object.TryGetArrayField(TEXT("extra"), Extra))
		{
			for (const TSharedPtr<FJsonValue>& Child : *Extra)
			{
				Walk(Child, Style, Out, Depth + 1);
			}
		}
	}

	void Walk(const TSharedPtr<FJsonValue>& Value, const FStyle& Parent, TArray<McText::FSpan>& Out, int32 Depth)
	{
		if (!Value.IsValid() || Depth > 32)
		{
			return;
		}
		switch (Value->Type)
		{
		case EJson::String:
			Emit(Value->AsString(), Parent, Out);
			break;
		case EJson::Number:
			Emit(FString::SanitizeFloat(Value->AsNumber(), 0), Parent, Out);
			break;
		case EJson::Boolean:
			Emit(Value->AsBool() ? TEXT("true") : TEXT("false"), Parent, Out);
			break;
		case EJson::Object:
			WalkObject(*Value->AsObject(), Parent, Out, Depth);
			break;
		case EJson::Array:
		{
			// The first element is the parent of the others: they take its style (its own colour and flags) on top of the array's.
			const TArray<TSharedPtr<FJsonValue>>& Items = Value->AsArray();
			if (Items.Num() == 0)
			{
				break;
			}
			TArray<McText::FSpan> First;
			Walk(Items[0], Parent, First, Depth + 1);
			Out.Append(First);
			FStyle FirstStyle = Parent;
			if (First.Num() > 0 && Items[0]->Type == EJson::Object)
			{
				// What the first element set is what its span shows; later elements inherit that.
				const McText::FSpan& Span = First[0];
				FirstStyle.Rgb = Span.Rgb;
				FirstStyle.Bold = Span.bBold;
				FirstStyle.Italic = Span.bItalic;
				FirstStyle.Underlined = Span.bUnderlined;
				FirstStyle.Strikethrough = Span.bStrikethrough;
				FirstStyle.Obfuscated = Span.bObfuscated;
				FirstStyle.Font = Span.Font;
			}
			for (int32 Index = 1; Index < Items.Num(); ++Index)
			{
				Walk(Items[Index], FirstStyle, Out, Depth + 1);
			}
			break;
		}
		default:
			break;
		}
	}
}

int32 McText::ColourFromName(const FString& Name)
{
	for (const FNamedColour& Colour : GColours)
	{
		if (Name.Equals(Colour.Name, ESearchCase::IgnoreCase))
		{
			return Colour.Rgb;
		}
	}
	return -1;
}

TCHAR McText::LegacyCodeOf(int32 Rgb)
{
	for (const FNamedColour& Colour : GColours)
	{
		if (Colour.Rgb == Rgb)
		{
			return Colour.Code;
		}
	}
	return 0;
}

void McText::FromValue(const TSharedPtr<FJsonValue>& Value, TArray<FSpan>& Out)
{
	Walk(Value, FStyle(), Out, 0);
}

bool McText::Parse(const FString& Json, TArray<FSpan>& Out)
{
	Out.Reset();
	TSharedPtr<FJsonValue> Value;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Value) || !Value.IsValid())
	{
		return false;
	}
	FromValue(Value, Out);
	return true;
}

FString McText::ToLegacy(const TArray<FSpan>& Spans)
{
	FString Out;
	bool bPrevStyled = false;
	for (const FSpan& Span : Spans)
	{
		const bool bStyled = Span.Rgb >= 0 || Span.bBold || Span.bItalic || Span.bUnderlined || Span.bStrikethrough || Span.bObfuscated;
		if (bPrevStyled)
		{
			Out += TEXT("§r");
		}
		if (Span.Rgb >= 0)
		{
			const TCHAR Code = LegacyCodeOf(Span.Rgb);
			if (Code)
			{
				Out += TEXT("§");
				Out.AppendChar(Code);
			}
			else
			{
				Out += TEXT("§x");
				for (const TCHAR Digit : FString::Printf(TEXT("%06x"), Span.Rgb))
				{
					Out += TEXT("§");
					Out.AppendChar(Digit);
				}
			}
		}
		// A colour code ends the styles before it, so the styles come after it.
		if (Span.bObfuscated) Out += TEXT("§k");
		if (Span.bBold) Out += TEXT("§l");
		if (Span.bStrikethrough) Out += TEXT("§m");
		if (Span.bUnderlined) Out += TEXT("§n");
		if (Span.bItalic) Out += TEXT("§o");
		Out += Span.Text;
		bPrevStyled = bStyled;
	}
	return Out;
}

FString McText::ToPlain(const TArray<FSpan>& Spans)
{
	FString Out;
	for (const FSpan& Span : Spans)
	{
		Out += Span.Text;
	}
	return Out;
}

FString McText::JsonToLegacy(const FString& Json)
{
	TArray<FSpan> Spans;
	return Parse(Json, Spans) ? ToLegacy(Spans) : Json;
}
