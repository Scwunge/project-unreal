#include "McMapStore.h"
#include "Dom/JsonObject.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	const FName MapIdComponent(TEXT("minecraft:map_id"));
	constexpr int32 MaxScale = 4;
	TMap<int32, FMcMapData> Maps;
	TMap<int32, int32> ZoomCache;
	int32 NextId = 0;
}

namespace McMapStore
{
	int32 GetId(const FMcItemStack& Stack)
	{
		const TSharedPtr<FJsonValue> Value = Stack.Get(MapIdComponent);
		double Number;
		return Value.IsValid() && Value->TryGetNumber(Number) ? static_cast<int32>(Number) : INDEX_NONE;
	}

	void SetId(FMcItemStack& Stack, int32 Id)
	{
		Stack.Set(MapIdComponent, MakeShared<FJsonValueNumber>(Id));
	}

	FMcMapData* Find(int32 Id)
	{
		return Maps.Find(Id);
	}

	int32 Create(int32 Scale, int32 X, int32 Z, FName Dimension)
	{
		FMcMapData Map;
		Map.Scale = FMath::Clamp(Scale, 0, MaxScale);
		Map.Dimension = Dimension;
		Map.CenterOn(X, Z);
		Maps.Add(NextId, MoveTemp(Map));
		return NextId++;
	}

	int32 Zoom(int32 Id)
	{
		if (const int32* Cached = ZoomCache.Find(Id))
		{
			return *Cached;
		}
		const FMcMapData* Source = Maps.Find(Id);
		if (!Source || Source->Scale >= MaxScale)
		{
			return INDEX_NONE;
		}
		FMcMapData Map;
		Map.Scale = Source->Scale + 1;
		Map.Dimension = Source->Dimension;
		Map.CenterOn(Source->CenterX, Source->CenterZ);
		const int32 New = NextId++;
		Maps.Add(New, MoveTemp(Map));
		ZoomCache.Add(Id, New);
		return New;
	}

	bool CanZoom(const FMcItemStack& Stack)
	{
		const FMcMapData* Map = Maps.Find(GetId(Stack));
		return Map && !Map->bLocked && Map->Scale < MaxScale;
	}

	void Lock(int32 Id)
	{
		if (FMcMapData* Map = Maps.Find(Id))
		{
			Map->bLocked = true;
		}
	}

	void Reset()
	{
		Maps.Empty();
		ZoomCache.Empty();
		NextId = 0;
	}

	int32 Num()
	{
		return Maps.Num();
	}

	FString ToJson()
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("next"), NextId);
		TArray<TSharedPtr<FJsonValue>> List;
		for (const TPair<int32, FMcMapData>& Pair : Maps)
		{
			const TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("id"), Pair.Key);
			Entry->SetNumberField(TEXT("scale"), Pair.Value.Scale);
			Entry->SetNumberField(TEXT("x"), Pair.Value.CenterX);
			Entry->SetNumberField(TEXT("z"), Pair.Value.CenterZ);
			Entry->SetBoolField(TEXT("locked"), Pair.Value.bLocked);
			Entry->SetStringField(TEXT("dimension"), Pair.Value.Dimension.ToString());
			Entry->SetStringField(TEXT("colors"), FBase64::Encode(Pair.Value.Colors));
			List.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Root->SetArrayField(TEXT("maps"), List);
		TArray<TSharedPtr<FJsonValue>> Zooms;
		for (const TPair<int32, int32>& Pair : ZoomCache)
		{
			TArray<TSharedPtr<FJsonValue>> Two;
			Two.Add(MakeShared<FJsonValueNumber>(Pair.Key));
			Two.Add(MakeShared<FJsonValueNumber>(Pair.Value));
			Zooms.Add(MakeShared<FJsonValueArray>(Two));
		}
		Root->SetArrayField(TEXT("zooms"), Zooms);
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return Out;
	}

	bool FromJson(const FString& Json)
	{
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
		{
			return false;
		}
		Reset();
		NextId = static_cast<int32>(Root->GetNumberField(TEXT("next")));
		const TArray<TSharedPtr<FJsonValue>>* List;
		if (Root->TryGetArrayField(TEXT("maps"), List))
		{
			for (const TSharedPtr<FJsonValue>& Each : *List)
			{
				const TSharedPtr<FJsonObject> Entry = Each->AsObject();
				FMcMapData Map;
				Map.Scale = static_cast<int32>(Entry->GetNumberField(TEXT("scale")));
				Map.CenterX = static_cast<int32>(Entry->GetNumberField(TEXT("x")));
				Map.CenterZ = static_cast<int32>(Entry->GetNumberField(TEXT("z")));
				Map.bLocked = Entry->GetBoolField(TEXT("locked"));
				Map.Dimension = FName(*Entry->GetStringField(TEXT("dimension")));
				TArray<uint8> Colors;
				if (FBase64::Decode(Entry->GetStringField(TEXT("colors")), Colors) && Colors.Num() == FMcMapData::Size * FMcMapData::Size)
				{
					Map.Colors = MoveTemp(Colors);
				}
				Maps.Add(static_cast<int32>(Entry->GetNumberField(TEXT("id"))), MoveTemp(Map));
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Zooms;
		if (Root->TryGetArrayField(TEXT("zooms"), Zooms))
		{
			for (const TSharedPtr<FJsonValue>& Each : *Zooms)
			{
				const TArray<TSharedPtr<FJsonValue>>& Pair = Each->AsArray();
				if (Pair.Num() == 2)
				{
					ZoomCache.Add(static_cast<int32>(Pair[0]->AsNumber()), static_cast<int32>(Pair[1]->AsNumber()));
				}
			}
		}
		return true;
	}
}
