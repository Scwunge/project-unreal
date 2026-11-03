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
