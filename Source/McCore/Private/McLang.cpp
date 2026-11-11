#include "McLang.h"
#include "HAL/FileManager.h"
#include "McCache.h"
#include "McResourcePacks.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace
{
	FCriticalSection GLock;
	uint32 GLanguageGeneration = 0;
	FString GSelected;
	bool bSelectedRead = false;

	FString GetSelectedFile()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("McLanguage.json"));
	}

	// A language file the game or the launcher may not have: absent is not an error.
	TSharedPtr<FJsonObject> LoadIfThere(const FString& Path)
	{
		return IFileManager::Get().FileExists(*Path) ? McCache::LoadJsonObject(Path) : nullptr;
	}

	// Under GLock.
	const FString& Selected()
	{
		if (!bSelectedRead)
		{
			bSelectedRead = true;
			GSelected = TEXT("en_us");
			if (const TSharedPtr<FJsonObject> Saved = LoadIfThere(GetSelectedFile()))
			{
				FString Code;
				if (Saved->TryGetStringField(TEXT("language"), Code) && !Code.IsEmpty())
				{
					GSelected = Code;
				}
			}
			FString FromCommandLine;
			if (FParse::Value(FCommandLine::Get(), TEXT("-McLang="), FromCommandLine) && !FromCommandLine.IsEmpty())
			{
				GSelected = FromCommandLine;
			}
		}
		return GSelected;
	}

	void MergeJson(TMap<FString, FString>& Table, const TSharedPtr<FJsonObject>& Object)
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
	}

	// One language: the game's file (the jar's for English, the launcher's assets for the others), then each pack's lang file
	// over it key by key (a pack gives only the keys it changes; the file is not replaced as pictures are).
	void MergeLanguage(TMap<FString, FString>& Table, const FString& Code)
	{
		MergeJson(Table, LoadIfThere(McCache::Root() / TEXT("client/assets/minecraft/lang") / (Code + TEXT(".json"))));
		MergeJson(Table, LoadIfThere(McCache::Root() / TEXT("assets/minecraft/lang") / (Code + TEXT(".json"))));
		TArray<TArray<uint8>> FromPacks;
		McPacks::ReadAll(TEXT("lang/") + Code + TEXT(".json"), FromPacks);
		for (const TArray<uint8>& Bytes : FromPacks)
		{
			TSharedPtr<FJsonObject> Object;
			const FUTF8ToTCHAR Text(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
			if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FString(Text.Length(), Text.Get())), Object))
			{
				MergeJson(Table, Object);
			}
		}
	}

	const TMap<FString, FString>& GetTable()
	{
		static TMap<FString, FString> Table;
		static bool bLoaded = false;
		static uint32 LoadedGeneration = 0;
		FScopeLock Guard(&GLock);
		const uint32 Generation = McPacks::GetGeneration() + GLanguageGeneration;
		if (!bLoaded || LoadedGeneration != Generation)
		{
			bLoaded = true;
			LoadedGeneration = Generation;
			Table.Reset();
			MergeLanguage(Table, TEXT("en_us"));
			if (Selected() != TEXT("en_us"))
			{
				MergeLanguage(Table, Selected());
			}
		}
		return Table;
	}
}

TArray<McLang::FLanguage> McLang::GetLanguages()
{
	TMap<FString, FLanguage> ByCode;
	const auto Add = [&ByCode](const FString& Code, const FString& Name, const FString& Region, bool bBidirectional)
	{
		FLanguage& Language = ByCode.FindOrAdd(Code);
		Language.Code = Code;
		Language.Name = Name;
		Language.Region = Region;
		Language.bBidirectional = bBidirectional;
	};
	if (const TSharedPtr<FJsonObject> Mcmeta = McCache::LoadJsonObject(McCache::Root() / TEXT("assets/pack.mcmeta")))
	{
		const TSharedPtr<FJsonObject>* Languages;
		if (Mcmeta->TryGetObjectField(TEXT("language"), Languages))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Entry : (*Languages)->Values)
			{
				const TSharedPtr<FJsonObject>* Detail;
				if (Entry.Value.IsValid() && Entry.Value->TryGetObject(Detail))
				{
					FString Name, Region;
					bool bBidirectional = false;
					(*Detail)->TryGetStringField(TEXT("name"), Name);
					(*Detail)->TryGetStringField(TEXT("region"), Region);
					(*Detail)->TryGetBoolField(TEXT("bidirectional"), bBidirectional);
					Add(Entry.Key, Name, Region, bBidirectional);
				}
			}
		}
	}
	const TSharedRef<const FMcPackStack> Stack = McPacks::GetStack();
	for (int32 Index = 0; Index < Stack->Num(); ++Index)
	{
		for (const FMcPackInfo::FLanguage& Language : Stack->Get(Index).GetInfo().Languages)
		{
			Add(Language.Code, Language.Name, Language.Region, Language.bBidirectional);
		}
	}
	TArray<FLanguage> Out;
	ByCode.GenerateValueArray(Out);
	Out.Sort([](const FLanguage& A, const FLanguage& B) { return A.Code < B.Code; });
	return Out;
}

FString McLang::GetLanguage()
{
	FScopeLock Guard(&GLock);
	return Selected();
}

void McLang::SetLanguage(const FString& Code)
{
	{
		FScopeLock Guard(&GLock);
		Selected();
		GSelected = Code;
		++GLanguageGeneration;
	}
	TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField(TEXT("language"), Code);
	FString Text;
	FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text));
	FFileHelper::SaveStringToFile(Text, *GetSelectedFile());
}

uint32 McLang::GetGeneration()
{
	FScopeLock Guard(&GLock);
	return McPacks::GetGeneration() + GLanguageGeneration;
}

TArray<FString> McLang::GetKeysWithPrefix(const FString& Prefix)
{
	TArray<FString> Keys;
	for (const TPair<FString, FString>& Pair : GetTable())
	{
		if (Pair.Key.StartsWith(Prefix))
		{
			Keys.Add(Pair.Key);
		}
	}
	return Keys;
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
