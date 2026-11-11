// NBT (minecraft.wiki, "NBT format"): the binary tree the structure templates are stored in, gzip compressed.
// Just enough to read them: every tag becomes a node, numbers are kept as integers or reals.

#pragma once

#include "CoreMinimal.h"

class MCGEN_API FMcNbt
{
public:
	enum class EType : uint8
	{
		End,
		Byte,
		Short,
		Int,
		Long,
		Float,
		Double,
		ByteArray,
		String,
		List,
		Compound,
		IntArray,
		LongArray,
	};

	EType Type = EType::End;
	// Byte, short, int and long; the element count of an array.
	int64 Integer = 0;
	// Float and double.
	double Real = 0.0;
	FString String;
	// List elements, array elements (as integers) or compound values (parallel to Keys).
	TArray<FMcNbt> Values;
	TArray<FString> Keys;
	// A byte array made from raw bytes (light data, 2048 at a time): written in place of Values when it has any, which
	// saves a tag per byte. Reading still fills Values.
	TArray<uint8> Bytes;

	// Reads a gzip (or, if it isn't gzip, raw) NBT file; false if it can't be read or is malformed.
	static bool LoadFile(const FString& Path, FMcNbt& Out);
	static bool Parse(TArrayView<const uint8> Bytes, FMcNbt& Out);
	// Writes a compound as an uncompressed NBT file (an unnamed root), so what was read can be kept as it was. An empty
	// list has lost its element type (written as end).
	void WriteRoot(TArray<uint8>& Out) const;

	bool IsNumber() const { return Type >= EType::Byte && Type <= EType::Double; }
	bool IsList() const { return Type == EType::List || Type == EType::IntArray || Type == EType::LongArray || Type == EType::ByteArray; }
	// A compound's child by key, or null.
	const FMcNbt* Find(const TCHAR* Key) const;
	int64 GetInt(const TCHAR* Key, int64 Default = 0) const;
	double GetReal() const { return (Type == EType::Float || Type == EType::Double) ? Real : static_cast<double>(Integer); }
	FString GetString(const TCHAR* Key) const;
	int32 Num() const { return Values.Num(); }
};
