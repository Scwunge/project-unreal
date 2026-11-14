#include "McNbt.h"
#include "Misc/FileHelper.h"
#include "Misc/Compression.h"

namespace
{
	// Reads big-endian values from a byte span; Failed once it runs out.
	struct FReader
	{
		TArrayView<const uint8> Bytes;
		int32 Pos = 0;
		bool bFailed = false;

		bool Has(int64 Count)
		{
			if (Count < 0 || Pos + Count > Bytes.Num())
			{
				bFailed = true;
			}
			return !bFailed;
		}
		uint64 ReadBig(int32 Size)
		{
			if (!Has(Size))
			{
				return 0;
			}
			uint64 Value = 0;
			for (int32 Index = 0; Index < Size; ++Index)
			{
				Value = (Value << 8) | Bytes[Pos++];
			}
			return Value;
		}
		FString ReadString()
		{
			const int32 Length = static_cast<int32>(ReadBig(2));
			if (!Has(Length))
			{
				return FString();
			}
			const FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData() + Pos), Length);
			Pos += Length;
			return FString(Converted.Length(), Converted.Get());
		}
	};

	constexpr int32 MaxDepth = 64;

	bool ReadPayload(FReader& Reader, FMcNbt::EType Type, FMcNbt& Out, int32 Depth)
	{
		using EType = FMcNbt::EType;
		if (Depth > MaxDepth)
		{
			return false;
		}
		Out.Type = Type;
		switch (Type)
		{
		case EType::Byte:
			Out.Integer = static_cast<int8>(Reader.ReadBig(1));
			break;
		case EType::Short:
			Out.Integer = static_cast<int16>(Reader.ReadBig(2));
			break;
		case EType::Int:
			Out.Integer = static_cast<int32>(Reader.ReadBig(4));
			break;
		case EType::Long:
			Out.Integer = static_cast<int64>(Reader.ReadBig(8));
			break;
		case EType::Float:
		{
			const uint32 Bits = static_cast<uint32>(Reader.ReadBig(4));
			float Value;
			FMemory::Memcpy(&Value, &Bits, 4);
			Out.Real = Value;
			break;
		}
		case EType::Double:
		{
			const uint64 Bits = Reader.ReadBig(8);
			double Value;
			FMemory::Memcpy(&Value, &Bits, 8);
			Out.Real = Value;
			break;
		}
		case EType::String:
			Out.String = Reader.ReadString();
			break;
		case EType::ByteArray:
		case EType::IntArray:
		case EType::LongArray:
		{
			const int32 Count = static_cast<int32>(Reader.ReadBig(4));
			const int32 Size = Type == EType::ByteArray ? 1 : Type == EType::IntArray ? 4 : 8;
			if (Count < 0 || !Reader.Has(static_cast<int64>(Count) * Size))
			{
				return false;
			}
			Out.Integer = Count;
			Out.Values.SetNum(Count);
			for (FMcNbt& Element : Out.Values)
			{
				Element.Type = Type == EType::ByteArray ? EType::Byte : Type == EType::IntArray ? EType::Int : EType::Long;
				const uint64 Raw = Reader.ReadBig(Size);
				Element.Integer = Size == 1 ? static_cast<int8>(Raw) : Size == 4 ? static_cast<int32>(Raw) : static_cast<int64>(Raw);
			}
			break;
		}
		case EType::List:
		{
			const EType Element = static_cast<EType>(Reader.ReadBig(1));
			const int32 Count = static_cast<int32>(Reader.ReadBig(4));
			if (Reader.bFailed || Count < 0 || static_cast<uint8>(Element) > static_cast<uint8>(EType::LongArray) || (Element == EType::End && Count > 0)
				|| Count > Reader.Bytes.Num())
			{
				return false;
			}
			Out.Values.SetNum(Count);
			for (FMcNbt& Child : Out.Values)
			{
				if (!ReadPayload(Reader, Element, Child, Depth + 1))
				{
					return false;
				}
			}
			break;
		}
		case EType::Compound:
		{
			while (true)
			{
				const EType Tag = static_cast<EType>(Reader.ReadBig(1));
				if (Reader.bFailed)
				{
					return false;
				}
				if (Tag == EType::End)
				{
					break;
				}
				if (static_cast<uint8>(Tag) > static_cast<uint8>(EType::LongArray))
				{
					return false;
				}
				Out.Keys.Add(Reader.ReadString());
				FMcNbt& Child = Out.Values.AddDefaulted_GetRef();
				if (!ReadPayload(Reader, Tag, Child, Depth + 1))
				{
					return false;
				}
			}
			break;
		}
		default:
			return false;
		}
		return !Reader.bFailed;
	}
}

bool FMcNbt::Parse(TArrayView<const uint8> Bytes, FMcNbt& Out)
{
	TArray<uint8> Inflated;
	if (Bytes.Num() > 18 && Bytes[0] == 0x1f && Bytes[1] == 0x8b)
	{
		// The last four bytes of a gzip stream are the uncompressed size, little endian.
		const int32 Size = Bytes[Bytes.Num() - 4] | (Bytes[Bytes.Num() - 3] << 8) | (Bytes[Bytes.Num() - 2] << 16) | (Bytes[Bytes.Num() - 1] << 24);
		if (Size <= 0 || Size > (256 << 20))
		{
			return false;
		}
		Inflated.SetNumUninitialized(Size);
		if (!FCompression::UncompressMemory(NAME_Gzip, Inflated.GetData(), Size, Bytes.GetData(), Bytes.Num()))
		{
			return false;
		}
		Bytes = Inflated;
	}
	FReader Reader{ Bytes };
	// The root is a named compound; the name is empty in practice.
	if (static_cast<EType>(Reader.ReadBig(1)) != EType::Compound)
	{
		return false;
	}
	Reader.ReadString();
	Out = FMcNbt();
	return ReadPayload(Reader, EType::Compound, Out, 0);
}

bool FMcNbt::LoadFile(const FString& Path, FMcNbt& Out)
{
	TArray<uint8> Bytes;
	return FFileHelper::LoadFileToArray(Bytes, *Path) && Parse(Bytes, Out);
}

const FMcNbt* FMcNbt::Find(const TCHAR* Key) const
{
	if (Type != EType::Compound)
	{
		return nullptr;
	}
	const int32 Index = Keys.IndexOfByPredicate([Key](const FString& Candidate) { return Candidate == Key; });
	return Index == INDEX_NONE ? nullptr : &Values[Index];
}

int64 FMcNbt::GetInt(const TCHAR* Key, int64 Default) const
{
	const FMcNbt* Child = Find(Key);
	return Child && Child->IsNumber() ? Child->Integer : Default;
}

FString FMcNbt::GetString(const TCHAR* Key) const
{
	const FMcNbt* Child = Find(Key);
	return Child && Child->Type == EType::String ? Child->String : FString();
}

namespace
{
	void WriteBig(TArray<uint8>& Out, uint64 Value, int32 Size)
	{
		for (int32 Shift = (Size - 1) * 8; Shift >= 0; Shift -= 8)
		{
			Out.Add(static_cast<uint8>(Value >> Shift));
		}
	}

	void WriteString(TArray<uint8>& Out, const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		WriteBig(Out, FMath::Min(Utf8.Length(), 65535), 2);
		Out.Append(reinterpret_cast<const uint8*>(Utf8.Get()), FMath::Min(Utf8.Length(), 65535));
	}

	void WritePayload(TArray<uint8>& Out, const FMcNbt& Tag)
	{
		using EType = FMcNbt::EType;
		switch (Tag.Type)
		{
		case EType::Byte: WriteBig(Out, static_cast<uint64>(Tag.Integer), 1); break;
		case EType::Short: WriteBig(Out, static_cast<uint64>(Tag.Integer), 2); break;
		case EType::Int: WriteBig(Out, static_cast<uint64>(Tag.Integer), 4); break;
		case EType::Long: WriteBig(Out, static_cast<uint64>(Tag.Integer), 8); break;
		case EType::Float:
		{
			const float Value = static_cast<float>(Tag.Real);
			uint32 Bits;
			FMemory::Memcpy(&Bits, &Value, 4);
			WriteBig(Out, Bits, 4);
			break;
		}
		case EType::Double:
		{
			uint64 Bits;
			FMemory::Memcpy(&Bits, &Tag.Real, 8);
			WriteBig(Out, Bits, 8);
			break;
		}
		case EType::String: WriteString(Out, Tag.String); break;
		case EType::ByteArray:
			if (!Tag.Bytes.IsEmpty())
			{
				WriteBig(Out, Tag.Bytes.Num(), 4);
				Out.Append(Tag.Bytes);
				break;
			}
			WriteBig(Out, Tag.Num(), 4);
			for (const FMcNbt& Value : Tag.Values) { WriteBig(Out, static_cast<uint64>(Value.Integer), 1); }
			break;
		case EType::IntArray:
			WriteBig(Out, Tag.Num(), 4);
			for (const FMcNbt& Value : Tag.Values) { WriteBig(Out, static_cast<uint64>(Value.Integer), 4); }
			break;
		case EType::LongArray:
			WriteBig(Out, Tag.Num(), 4);
			for (const FMcNbt& Value : Tag.Values) { WriteBig(Out, static_cast<uint64>(Value.Integer), 8); }
			break;
		case EType::List:
			WriteBig(Out, Tag.Num() > 0 ? static_cast<uint8>(Tag.Values[0].Type) : 0, 1);
			WriteBig(Out, Tag.Num(), 4);
			for (const FMcNbt& Value : Tag.Values) { WritePayload(Out, Value); }
			break;
		case EType::Compound:
			for (int32 Index = 0; Index < Tag.Num(); ++Index)
			{
				WriteBig(Out, static_cast<uint8>(Tag.Values[Index].Type), 1);
				WriteString(Out, Tag.Keys[Index]);
				WritePayload(Out, Tag.Values[Index]);
			}
			WriteBig(Out, 0, 1);
			break;
		default: break;
		}
	}
}

void FMcNbt::WriteRoot(TArray<uint8>& Out) const
{
	WriteBig(Out, static_cast<uint8>(EType::Compound), 1);
	WriteString(Out, FString());
	WritePayload(Out, *this);
}

namespace
{
	// The reader of the text form of a tag, one value after another.
	struct FSnbtReader
	{
		const FString& Text;
		int32 At = 0;
		// What the game says when it stops (set by the first thing that fails).
		FString Message;
		bool Fail(const TCHAR* Words) { if (Message.IsEmpty()) { Message = Words; } return false; }
		explicit FSnbtReader(const FString& InText) : Text(InText) {}

		void Skip() { while (At < Text.Len() && FChar::IsWhitespace(Text[At])) { ++At; } }
		bool Eat(TCHAR C) { Skip(); if (At < Text.Len() && Text[At] == C) { ++At; return true; } return false; }
		static bool IsPlain(TCHAR C) { return FChar::IsAlnum(C) || C == '_' || C == '-' || C == '.' || C == '+'; }

		bool ReadQuoted(FString& Out)
		{
			const TCHAR Quote = Text[At++];
			while (At < Text.Len())
			{
				TCHAR C = Text[At++];
				if (C == TEXT('\\') && At < Text.Len())
				{
					C = Text[At++];
					switch (C)
					{
					case 'n': C = '\n'; break;
					case 't': C = '\t'; break;
					case 'r': C = '\r'; break;
					case 'b': C = '\b'; break;
					case 'f': C = '\f'; break;
					default: break;
					}
				}
				else if (C == Quote)
				{
					return true;
				}
				Out.AppendChar(C);
			}
			return false;
		}

		bool ReadKey(FString& Out)
		{
			Skip();
			if (At < Text.Len() && (Text[At] == '"' || Text[At] == '\''))
			{
				return ReadQuoted(Out);
			}
			const int32 Start = At;
			while (At < Text.Len() && IsPlain(Text[At])) { ++At; }
			Out = Text.Mid(Start, At - Start);
			return !Out.IsEmpty();
		}

		// An unquoted word: a number with its suffix, true or false, else a string.
		static FMcNbt FromWord(const FString& Word)
		{
			FMcNbt Result;
			Result.Type = FMcNbt::EType::String;
			Result.String = Word;
			if (Word == TEXT("true") || Word == TEXT("false"))
			{
				Result.Type = FMcNbt::EType::Byte;
				Result.Integer = Word == TEXT("true") ? 1 : 0;
				return Result;
			}
			FString Body = Word;
			TCHAR Suffix = 0;
			if (Body.Len() > 1 && FChar::IsAlpha(Body[Body.Len() - 1]))
			{
				Suffix = FChar::ToLower(Body[Body.Len() - 1]);
				Body.LeftChopInline(1);
			}
			if (Body.IsEmpty() || Body == TEXT("-") || Body == TEXT("+") || !(FChar::IsDigit(Body[Body.Len() - 1]) || (Body.Len() > 1 && Body[Body.Len() - 1] == '.')))
			{
				return Result;
			}
			for (int32 I = 0; I < Body.Len(); ++I)
			{
				if (!(FChar::IsDigit(Body[I]) || Body[I] == '.' || ((Body[I] == '-' || Body[I] == '+') && I == 0)))
				{
					return Result;
				}
			}
			const bool bReal = Body.Contains(TEXT("."));
			if (bReal && Suffix != 0 && Suffix != 'f' && Suffix != 'd')
			{
				return Result;
			}
			switch (Suffix)
			{
			case 'b': Result.Type = FMcNbt::EType::Byte; break;
			case 's': Result.Type = FMcNbt::EType::Short; break;
			case 'l': Result.Type = FMcNbt::EType::Long; break;
			case 'f': Result.Type = FMcNbt::EType::Float; break;
			case 'd': Result.Type = FMcNbt::EType::Double; break;
			case 0: Result.Type = bReal ? FMcNbt::EType::Double : FMcNbt::EType::Int; break;
			default: return Result;
			}
			Result.String.Empty();
			if (Result.Type == FMcNbt::EType::Float || Result.Type == FMcNbt::EType::Double)
			{
				Result.Real = FCString::Atod(*Body);
			}
			else
			{
				Result.Integer = FCString::Atoi64(*Body);
			}
			return Result;
		}

		bool ReadValue(FMcNbt& Out, int32 Depth)
		{
			Skip();
			if (At >= Text.Len() || Depth > 64)
			{
				return Fail(TEXT("Expected a valid unquoted string"));
			}
			const TCHAR C = Text[At];
			if (C == '{')
			{
				++At;
				Out.Type = FMcNbt::EType::Compound;
				if (Eat('}'))
				{
					return true;
				}
				do
				{
					FString Key;
					FMcNbt Value;
					if (!ReadKey(Key))
					{
						return Fail(TEXT("Expected a valid unquoted string"));
					}
					if (!Eat(':'))
					{
						return Fail(TEXT("Expected literal :"));
					}
					if (!ReadValue(Value, Depth + 1))
					{
						return false;
					}
					const int32 Existing = Out.Keys.IndexOfByKey(Key);
					if (Existing != INDEX_NONE)
					{
						Out.Values[Existing] = MoveTemp(Value);
					}
					else
					{
						Out.Keys.Add(Key);
						Out.Values.Add(MoveTemp(Value));
					}
				} while (Eat(','));
				return Eat('}') || Fail(TEXT("Expected literal ,"));
			}
			if (C == '[')
			{
				++At;
				Out.Type = FMcNbt::EType::List;
				Skip();
				if (At + 1 < Text.Len() && Text[At + 1] == ';' && (Text[At] == 'B' || Text[At] == 'I' || Text[At] == 'L'))
				{
					Out.Type = Text[At] == 'B' ? FMcNbt::EType::ByteArray : Text[At] == 'I' ? FMcNbt::EType::IntArray : FMcNbt::EType::LongArray;
					At += 2;
				}
				if (Eat(']'))
				{
					Out.Integer = 0;
					return true;
				}
				do
				{
					FMcNbt Element;
					if (!ReadValue(Element, Depth + 1))
					{
						return false;
					}
					Out.Values.Add(MoveTemp(Element));
				} while (Eat(','));
				Out.Integer = Out.Values.Num();
				return Eat(']') || Fail(TEXT("Expected literal ,"));
			}
			if (C == '"' || C == '\'')
			{
				Out.Type = FMcNbt::EType::String;
				return ReadQuoted(Out.String);
			}
			const int32 Start = At;
			while (At < Text.Len() && IsPlain(Text[At])) { ++At; }
			if (At == Start)
			{
				return Fail(TEXT("Expected a valid unquoted string"));
			}
			Out = FromWord(Text.Mid(Start, At - Start));
			return true;
		}
	};
}

bool FMcNbt::ReadSnbt(const FString& Text, FMcNbt& Out, int32& OutEnd, FString& OutMessage)
{
	FSnbtReader Reader(Text);
	Out = FMcNbt();
	const bool bOk = Reader.ReadValue(Out, 0);
	OutEnd = Reader.At;
	OutMessage = bOk ? FString() : (Reader.Message.IsEmpty() ? FString(TEXT("Expected a valid unquoted string")) : Reader.Message);
	return bOk;
}

bool FMcNbt::ParseSnbt(const FString& Text, FMcNbt& Out, int32* OutError)
{
	FSnbtReader Reader(Text);
	Out = FMcNbt();
	const bool bOk = Reader.ReadValue(Out, 0);
	Reader.Skip();
	const bool bWhole = bOk && Reader.At == Text.Len();
	if (!bWhole && OutError)
	{
		*OutError = Reader.At;
	}
	return bWhole;
}
