#include "McLang.h"
#include "McCache.h"
#include "McResourcePacks.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/ScopeLock.h"

namespace
{
	const TMap<FString, FString>& GetTable()
	{
		static TMap<FString, FString> Table;
		static FCriticalSection Lock;
		static bool bLoaded = false;
		static uint32 LoadedGeneration = 0;
		FScopeLock Guard(&Lock);
		if (!bLoaded || LoadedGeneration != McPacks::GetGeneration())
		{
			bLoaded = true;
			LoadedGeneration = McPacks::GetGeneration();
			Table.Reset();
			auto Merge = [](const TSharedPtr<FJsonObject>& Object)
			{
				if (!Object.IsValid())
				{
					return;
				}
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
				{
					FString Text;
					if (Pair.Value.IsValid() && Pair.Value->TryGetString(Text))
					{
						Table.Add(Pair.Key, MoveTemp(Text));
					}
				}
			};
			// The game's own file, then each pack's lang/en_us.json over it key by key (a pack gives only the keys it
			// changes; the file is not replaced as pictures are).
			Merge(McCache::LoadJsonObject(McCache::Root() / TEXT("client/assets/minecraft/lang/en_us.json")));
			TArray<TArray<uint8>> FromPacks;
			McPacks::ReadAll(TEXT("lang/en_us.json"), FromPacks);
			for (const TArray<uint8>& Bytes : FromPacks)
			{
				TSharedPtr<FJsonObject> Object;
				const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
				if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), Object))
				{
					Merge(Object);
				}
			}
		}
		return Table;
	}
}

FString McLang::Get(const FString& Key)
{
	const FString* Found = GetTable().Find(Key);
	return Found ? *Found : Key;
}

FString McLang::Format(const FString& Key, const TArray<FString>& Args)
{
	return FormatPattern(Get(Key), Args);
}

FString McLang::FormatPattern(const FString& Pattern, const TArray<FString>& Args)
{
	FString Out;
	Out.Reserve(Pattern.Len() + 32);
	int32 Next = 0;
	for (int32 I = 0; I < Pattern.Len(); ++I)
	{
		if (Pattern[I] != '%' || I + 1 >= Pattern.Len())
		{
			Out.AppendChar(Pattern[I]);
			continue;
		}
		const TCHAR C = Pattern[I + 1];
		if (C == '%')
		{
			Out.AppendChar('%');
			++I;
		}
		else if (C == 's' || C == 'd')
		{
			Out += Args.IsValidIndex(Next) ? Args[Next] : FString();
			++Next;
			++I;
		}
		else if (FChar::IsDigit(C))
		{
			// %2$s: the second argument.
			int32 End = I + 1;
			while (End < Pattern.Len() && FChar::IsDigit(Pattern[End]))
			{
				++End;
			}
			if (End + 1 < Pattern.Len() && Pattern[End] == '$' && (Pattern[End + 1] == 's' || Pattern[End + 1] == 'd'))
			{
				const int32 Index = FCString::Atoi(*Pattern.Mid(I + 1, End - I - 1)) - 1;
				Out += Args.IsValidIndex(Index) ? Args[Index] : FString();
				I = End + 1;
			}
			else
			{
				Out.AppendChar('%');
			}
		}
		else
		{
			Out.AppendChar('%');
		}
	}
	return Out;
}
