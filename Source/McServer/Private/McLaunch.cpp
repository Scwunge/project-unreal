#include "McLaunch.h"
#include "McServer.h"
#include "McProcess.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// Java properties escape ':', '=' and '\' with a backslash; the game writes "minecraft\:normal".
	FString Unescape(const FString& Text)
	{
		FString Out;
		Out.Reserve(Text.Len());
		for (int32 Index = 0; Index < Text.Len(); ++Index)
		{
			if (Text[Index] == TEXT('\\') && Index + 1 < Text.Len())
			{
				++Index;
			}
			Out.AppendChar(Text[Index]);
		}
		return Out;
	}

	FString Escape(const FString& Text)
	{
		FString Out;
		for (const TCHAR Char : Text)
		{
			if (Char == TEXT(':') || Char == TEXT('=') || Char == TEXT('\\'))
			{
				Out.AppendChar(TEXT('\\'));
			}
			Out.AppendChar(Char);
		}
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> ReadArray(const FString& Json, bool& bOutOk)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		bOutOk = FJsonSerializer::Deserialize(Reader, Values);
		return Values;
	}

	// What a file holds, made with Empty if it isn't there.
	FString ReadOrMake(const FString& Path, const FString& Empty)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), /*Tree=*/true);
			FFileHelper::SaveStringToFile(Empty, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			Text = Empty;
		}
		return Text;
	}

	// The server.properties key a launch option comes from on a dedicated server.
	const TCHAR* GetPropertyFor(const TCHAR* Key)
	{
		if (FCString::Stricmp(Key, TEXT("McWorld")) == 0)
		{
			return TEXT("level-name");
		}
		if (FCString::Stricmp(Key, TEXT("McSeed")) == 0)
		{
			return TEXT("level-seed");
		}
		if (FCString::Stricmp(Key, TEXT("McGameMode")) == 0)
		{
			return TEXT("gamemode");
		}
		return nullptr;
	}

	bool IsDedicated(const UWorld* World)
	{
		return World ? World->GetNetMode() == NM_DedicatedServer : McProcess::IsServer();
	}
}

// ---- server.properties ----

const TArray<TPair<FString, FString>>& FMcServerProperties::GetDefaults()
{
	// The game's defaults for these keys (minecraft.wiki, Server.properties). The port is the game's 25565, which is
	// also the port the game asks for when an address has none (DefaultEngine.ini, [URL] Port).
	static const TArray<TPair<FString, FString>> Defaults = {
		{ TEXT("gamemode"), TEXT("survival") },
		{ TEXT("level-name"), TEXT("world") },
		{ TEXT("level-seed"), TEXT("") },
		{ TEXT("level-type"), TEXT("minecraft:normal") },
		{ TEXT("max-players"), TEXT("20") },
		{ TEXT("motd"), TEXT("A Minecraft Server") },
		{ TEXT("server-port"), TEXT("25565") },
		{ TEXT("white-list"), TEXT("false") },
	};
	return Defaults;
}

void FMcServerProperties::Parse(const FString& Text)
{
	Lines.Reset();
	TArray<FString> RawLines;
	Text.ParseIntoArrayLines(RawLines, /*CullEmpty=*/false);
	for (const FString& Raw : RawLines)
	{
		FLine Line;
		Line.Raw = Raw;
		const FString Trimmed = Raw.TrimStartAndEnd();
		int32 Split = INDEX_NONE;
		if (!Trimmed.IsEmpty() && Trimmed[0] != TEXT('#') && Trimmed[0] != TEXT('!'))
		{
			// The first unescaped '=' or ':' ends the key.
			for (int32 Index = 0; Index < Trimmed.Len(); ++Index)
			{
				if (Trimmed[Index] == TEXT('\\'))
				{
					++Index;
				}
				else if (Trimmed[Index] == TEXT('=') || Trimmed[Index] == TEXT(':'))
				{
					Split = Index;
					break;
				}
			}
			Line.Key = Unescape(Split == INDEX_NONE ? Trimmed : Trimmed.Left(Split).TrimEnd());
			Line.Value = Split == INDEX_NONE ? FString() : Unescape(Trimmed.Mid(Split + 1).TrimStart());
		}
		Lines.Add(MoveTemp(Line));
	}
	// The newline that ends the file isn't a blank line of its own.
	while (Lines.Num() > 0 && Lines.Last().Raw.TrimStartAndEnd().IsEmpty())
	{
		Lines.Pop();
	}
}

FString FMcServerProperties::ToString() const
{
	FString Out;
	for (const FLine& Line : Lines)
	{
		Out += Line.Raw;
		Out += TEXT("\n");
	}
	return Out;
}

const FMcServerProperties::FLine* FMcServerProperties::Find(const FString& Key) const
{
	return Lines.FindByPredicate([&Key](const FLine& Line) { return !Line.Key.IsEmpty() && Line.Key == Key; });
}

bool FMcServerProperties::Has(const FString& Key) const
{
	return Find(Key) != nullptr;
}

FString FMcServerProperties::GetString(const FString& Key) const
{
	if (const FLine* Line = Find(Key))
	{
		return Line->Value;
	}
	const TPair<FString, FString>* Default = GetDefaults().FindByPredicate([&Key](const TPair<FString, FString>& Pair) { return Pair.Key == Key; });
	return Default ? Default->Value : FString();
}

int32 FMcServerProperties::GetInt(const FString& Key, int32 Fallback) const
{
	const FString Value = GetString(Key);
	return Value.IsNumeric() ? FCString::Atoi(*Value) : Fallback;
}

bool FMcServerProperties::GetBool(const FString& Key) const
{
	return GetString(Key).Equals(TEXT("true"), ESearchCase::IgnoreCase);
}

void FMcServerProperties::Set(const FString& Key, const FString& Value)
{
	FLine* Line = const_cast<FLine*>(Find(Key));
	if (!Line)
	{
		Line = &Lines.AddDefaulted_GetRef();
		Line->Key = Key;
	}
	Line->Value = Value;
	Line->Raw = Escape(Key) + TEXT("=") + Escape(Value);
}

bool FMcServerProperties::AddMissingDefaults()
{
	bool bAdded = false;
	for (const TPair<FString, FString>& Default : GetDefaults())
	{
		if (!Has(Default.Key))
		{
			Set(Default.Key, Default.Value);
			bAdded = true;
		}
	}
	return bAdded;
}

FString FMcServerProperties::GetDir()
{
	return FPaths::ProjectSavedDir() / TEXT("Server");
}

const FMcServerProperties& FMcServerProperties::Get()
{
	static const FMcServerProperties Properties = []()
	{
		FMcServerProperties Loaded;
		const FString Path = GetDir() / TEXT("server.properties");
		FString Text;
		const bool bHadFile = FFileHelper::LoadFileToString(Text, *Path);
		if (!bHadFile)
		{
			Text = TEXT("#Minecraft server properties\n");
		}
		Loaded.Parse(Text);
		if (Loaded.AddMissingDefaults() || !bHadFile)
		{
			IFileManager::Get().MakeDirectory(*GetDir(), /*Tree=*/true);
			if (!FFileHelper::SaveStringToFile(Loaded.ToString(), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				UE_LOG(LogMcServer, Warning, TEXT("Can't write %s; using the settings as read."), *Path);
			}
		}
		UE_LOG(LogMcServer, Log, TEXT("Server settings from %s"), *FPaths::ConvertRelativePathToFull(Path));
		return Loaded;
	}();
	return Properties;
}

void FMcServerProperties::SetAndSave(const FString& Key, const FString& Value)
{
	FMcServerProperties& Properties = const_cast<FMcServerProperties&>(Get());
	Properties.Set(Key, Value);
	const FString Path = GetDir() / TEXT("server.properties");
	if (!FFileHelper::SaveStringToFile(Properties.ToString(), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogMcServer, Warning, TEXT("Can't write %s"), *Path);
	}
}

// ---- Allowlist and operators ----

bool FMcServerAccess::IsAllowlisted(const FString& Name) const
{
	return Allowlist.ContainsByPredicate([&Name](const FString& Entry) { return Entry.Equals(Name, ESearchCase::IgnoreCase); });
}

const FMcServerAccess::FOp* FMcServerAccess::FindOp(const FString& Name) const
{
	return Ops.FindByPredicate([&Name](const FOp& Op) { return Op.Name.Equals(Name, ESearchCase::IgnoreCase); });
}

const FMcServerAccess::FBan* FMcServerAccess::FindBan(const FString& Name) const
{
	return Bans.FindByPredicate([&Name](const FBan& Ban) { return Ban.Target.Equals(Name, ESearchCase::IgnoreCase); });
}

const FMcServerAccess::FBan* FMcServerAccess::FindIpBan(const FString& Ip) const
{
	return IpBans.FindByPredicate([&Ip](const FBan& Ban) { return Ban.Target == Ip; });
}

bool FMcServerAccess::ParseBans(const FString& Json, bool bIps)
{
	bool bOk = false;
	const TArray<TSharedPtr<FJsonValue>> Values = ReadArray(Json, bOk);
	if (!bOk)
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FBan Ban;
		if (Value && Value->TryGetObject(Object) && (*Object)->TryGetStringField(bIps ? TEXT("ip") : TEXT("name"), Ban.Target) && !Ban.Target.IsEmpty())
		{
			(*Object)->TryGetStringField(TEXT("source"), Ban.Source);
			(*Object)->TryGetStringField(TEXT("reason"), Ban.Reason);
			(*Object)->TryGetStringField(TEXT("created"), Ban.Created);
			(bIps ? IpBans : Bans).Add(MoveTemp(Ban));
		}
	}
	return true;
}

FString FMcServerAccess::BansToJson(bool bIps) const
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FBan& Ban : (bIps ? IpBans : Bans))
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(bIps ? TEXT("ip") : TEXT("name"), Ban.Target);
		Object->SetStringField(TEXT("created"), Ban.Created);
		Object->SetStringField(TEXT("source"), Ban.Source);
		Object->SetStringField(TEXT("expires"), TEXT("forever"));
		Object->SetStringField(TEXT("reason"), Ban.Reason);
		Values.Add(MakeShared<FJsonValueObject>(Object));
	}
	FString Text;
	FJsonSerializer::Serialize(Values, TJsonWriterFactory<>::Create(&Text));
	return Text;
}

bool FMcServerAccess::ParseAllowlist(const FString& Json)
{
	bool bOk = false;
	const TArray<TSharedPtr<FJsonValue>> Values = ReadArray(Json, bOk);
	if (!bOk)
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FString Name;
		if (Value && Value->TryGetObject(Object) && (*Object)->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
		{
			Allowlist.Add(Name);
		}
	}
	return true;
}

bool FMcServerAccess::ParseOps(const FString& Json)
{
	bool bOk = false;
	const TArray<TSharedPtr<FJsonValue>> Values = ReadArray(Json, bOk);
	if (!bOk)
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& Value : Values)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FOp Op;
		if (Value && Value->TryGetObject(Object) && (*Object)->TryGetStringField(TEXT("name"), Op.Name) && !Op.Name.IsEmpty())
		{
			(*Object)->TryGetNumberField(TEXT("level"), Op.Level);
			(*Object)->TryGetBoolField(TEXT("bypassesPlayerLimit"), Op.bBypassesPlayerLimit);
			Ops.Add(MoveTemp(Op));
		}
	}
	return true;
}

FMcServerAccess FMcServerAccess::Load()
{
	FMcServerAccess Access;
	const FString Dir = FMcServerProperties::GetDir();
	if (!Access.ParseAllowlist(ReadOrMake(Dir / TEXT("allowlist.json"), TEXT("[]"))))
	{
		UE_LOG(LogMcServer, Error, TEXT("%s/allowlist.json isn't a JSON list; nobody is on the allowlist."), *Dir);
	}
	if (!Access.ParseOps(ReadOrMake(Dir / TEXT("ops.json"), TEXT("[]"))))
	{
		UE_LOG(LogMcServer, Error, TEXT("%s/ops.json isn't a JSON list; nobody is an operator."), *Dir);
	}
	if (!Access.ParseBans(ReadOrMake(Dir / TEXT("banned-players.json"), TEXT("[]")), false))
	{
		UE_LOG(LogMcServer, Error, TEXT("%s/banned-players.json isn't a JSON list; nobody is banned by name."), *Dir);
	}
	if (!Access.ParseBans(ReadOrMake(Dir / TEXT("banned-ips.json"), TEXT("[]")), true))
	{
		UE_LOG(LogMcServer, Error, TEXT("%s/banned-ips.json isn't a JSON list; no address is banned."), *Dir);
	}
	return Access;
}

FString FMcServerAccess::AllowlistToJson() const
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FString& Name : Allowlist)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Name);
		Values.Add(MakeShared<FJsonValueObject>(Object));
	}
	FString Text;
	FJsonSerializer::Serialize(Values, TJsonWriterFactory<>::Create(&Text));
	return Text;
}

FString FMcServerAccess::OpsToJson() const
{
	TArray<TSharedPtr<FJsonValue>> Values;
	for (const FOp& Op : Ops)
	{
		const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("name"), Op.Name);
		Object->SetNumberField(TEXT("level"), Op.Level);
		Object->SetBoolField(TEXT("bypassesPlayerLimit"), Op.bBypassesPlayerLimit);
		Values.Add(MakeShared<FJsonValueObject>(Object));
	}
	FString Text;
	FJsonSerializer::Serialize(Values, TJsonWriterFactory<>::Create(&Text));
	return Text;
}

bool FMcServerAccess::Save() const
{
	const FString Dir = FMcServerProperties::GetDir();
	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
	return FFileHelper::SaveStringToFile(AllowlistToJson(), *(Dir / TEXT("allowlist.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		&& FFileHelper::SaveStringToFile(OpsToJson(), *(Dir / TEXT("ops.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		&& FFileHelper::SaveStringToFile(BansToJson(false), *(Dir / TEXT("banned-players.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
		&& FFileHelper::SaveStringToFile(BansToJson(true), *(Dir / TEXT("banned-ips.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

FString FMcServerAccess::CheckJoin(const FMcServerProperties& Properties, const FMcServerAccess& Access, const FString& Name, int32 Players, const FString& Address)
{
	// The game's own refusal text (lang multiplayer.disconnect.banned.reason and banned_ip.reason).
	if (const FBan* Ban = Access.FindBan(Name))
	{
		return TEXT("You are banned from this server.\nReason: ") + Ban->Reason;
	}
	if (!Address.IsEmpty())
	{
		if (const FBan* Ban = Access.FindIpBan(Address))
		{
			return TEXT("Your IP address is banned from this server.\nReason: ") + Ban->Reason;
		}
	}
	const FOp* Op = Access.FindOp(Name);
	// Operators are let in past the allowlist, as in the game.
	if (Properties.GetBool(TEXT("white-list")) && !Op && !Access.IsAllowlisted(Name))
	{
		return TEXT("You are not white-listed on this server!");
	}
	if (Players >= Properties.GetInt(TEXT("max-players"), 20) && !(Op && Op->bBypassesPlayerLimit))
	{
		return TEXT("The server is full!");
	}
	return FString();
}

// ---- Launch options ----

bool McLaunch::GetValue(const UWorld* World, const TCHAR* Key, FString& OutValue)
{
	if (FParse::Value(FCommandLine::Get(), *FString::Printf(TEXT("-%s="), Key), OutValue))
	{
		return true;
	}
	if (World)
	{
		if (const TCHAR* Option = World->URL.GetOption(*FString::Printf(TEXT("%s="), Key), nullptr))
		{
			OutValue = DecodeOption(Option);
			return true;
		}
	}
	const TCHAR* Property = GetPropertyFor(Key);
	if (Property && IsDedicated(World))
	{
		OutValue = FMcServerProperties::Get().GetString(Property);
		if (FCString::Stricmp(Key, TEXT("McSeed")) == 0)
		{
			// The world actor reads a number; text seeds are hashed here.
			OutValue = LexToString(ParseSeed(OutValue));
		}
		return !OutValue.IsEmpty() && OutValue != TEXT("0");
	}
	return false;
}

bool McLaunch::HasFlag(const UWorld* World, const TCHAR* Key)
{
	if (FParse::Param(FCommandLine::Get(), Key) || (World && World->URL.HasOption(Key)))
	{
		return true;
	}
	if (FCString::Stricmp(Key, TEXT("McFlat")) == 0 && IsDedicated(World))
	{
		const FString Type = FMcServerProperties::Get().GetString(TEXT("level-type"));
		return Type.Equals(TEXT("minecraft:flat"), ESearchCase::IgnoreCase) || Type.Equals(TEXT("flat"), ESearchCase::IgnoreCase);
	}
	return false;
}

bool McLaunch::IsMenu(const UWorld* World)
{
	if (!World || World->GetNetMode() != NM_Standalone)
	{
		return false;
	}
	if (World->URL.HasOption(TEXT("McTitle")))
	{
		return true;
	}
	if (World->URL.HasOption(TEXT("McWorld")))
	{
		return false;
	}
	// Development: -McTitle shows it even with other switches (a -McShot of it). Any other switch (-McFlat,
	// -McShot=...) goes straight into a world, as before there was a title screen.
	return FParse::Param(FCommandLine::Get(), TEXT("McTitle")) || !FCString::Strstr(FCommandLine::Get(), TEXT("-Mc"));
}

int64 McLaunch::ParseSeed(const FString& Text)
{
	const FString Trimmed = Text.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		return 0;
	}
	int64 Number = 0;
	const TCHAR* Start = *Trimmed;
	TCHAR* End = nullptr;
	Number = FCString::Strtoi64(Start, &End, 10);
	const bool bWhole = End && *End == TEXT('\0') && End != Start && (Trimmed[0] != TEXT('-') || Trimmed.Len() > 1);
	// Strtoi64 clamps on overflow; the game takes a number out of range as text.
	if (bWhole && LexToString(Number) == (Trimmed[0] == TEXT('+') ? Trimmed.Mid(1) : Trimmed))
	{
		return Number;
	}
	// String.hashCode: h = 31 h + c over the UTF-16 code units, in 32-bit arithmetic.
	int32 Hash = 0;
	for (const TCHAR Char : Trimmed)
	{
		Hash = static_cast<int32>(31u * static_cast<uint32>(Hash) + static_cast<uint32>(static_cast<uint16>(Char)));
	}
	return Hash;
}

FString McLaunch::EncodeOption(const FString& Text)
{
	FString Out;
	for (const TCHAR Char : Text)
	{
		if ((FChar::IsAlnum(Char) && Char < 128) || Char == TEXT('-') || Char == TEXT('_') || Char == TEXT('.'))
		{
			Out.AppendChar(Char);
		}
		else
		{
			Out += FString::Printf(TEXT("%%%04X"), static_cast<uint32>(static_cast<uint16>(Char)));
		}
	}
	return Out;
}

FString McLaunch::DecodeOption(const FString& Text)
{
	FString Out;
	for (int32 Index = 0; Index < Text.Len(); ++Index)
	{
		if (Text[Index] == TEXT('%') && Index + 4 < Text.Len())
		{
			const FString Hex = Text.Mid(Index + 1, 4);
			bool bHex = Hex.Len() == 4;
			for (const TCHAR Char : Hex)
			{
				bHex &= FChar::IsHexDigit(Char);
			}
			if (bHex)
			{
				Out.AppendChar(static_cast<TCHAR>(FParse::HexNumber(*Hex)));
				Index += 4;
				continue;
			}
		}
		Out.AppendChar(Text[Index]);
	}
	return Out;
}

bool McLaunch::IsCreativeMode(const FString& GameMode)
{
	return GameMode.Equals(TEXT("creative"), ESearchCase::IgnoreCase) || GameMode == TEXT("1");
}
