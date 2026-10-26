// The "template" feature (a structure template picked by weight and put down at the origin: wells, springs) and
// the "fossil" feature (a fossil template, turned at random, with a coal-ore overlay on top, buried underground)
// (minecraft.wiki, "Configured feature").

#include "McFeature.h"
#include "McFeatureTypes.h"
#include "McGenRegion.h"
#include "McRandom.h"
#include "McStructureTemplate.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	TSharedPtr<const FMcStructureTemplate> TemplateOf(const TSharedPtr<FJsonValue>& Json)
	{
		FString Id;
		if (Json.IsValid() && Json->Type == EJson::String)
		{
			Id = Json->AsString();
		}
		else if (Json.IsValid() && Json->Type == EJson::Object)
		{
			// { "id": ... } as the template feature writes it.
			Json->AsObject()->TryGetStringField(TEXT("id"), Id);
		}
		return Id.IsEmpty() ? nullptr : FMcStructureTemplate::Find(Id);
	}

	class FTemplateFeature : public FMcFeature
	{
	public:
		struct FEntry
		{
			TSharedPtr<const FMcStructureTemplate> Template;
			int32 Weight = 1;
		};
		TArray<FEntry> Entries;
		int32 TotalWeight = 0;
		TSharedPtr<const FMcProcessorList> Processors;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			if (Entries.IsEmpty() || TotalWeight <= 0)
			{
				return false;
			}
			int32 Pick = Context.Random.NextInt(TotalWeight);
			const FEntry* Chosen = &Entries.Last();
			for (const FEntry& Entry : Entries)
			{
				if (Pick < Entry.Weight)
				{
					Chosen = &Entry;
					break;
				}
				Pick -= Entry.Weight;
			}
			const FMcStructureTemplate& Template = *Chosen->Template;
			FMcTemplatePlaceSettings Settings;
			Settings.Rotation = McTemplate::RandomRotation(Context.Random);
			Settings.Processors = Processors.Get();
			// Centred on the origin, turning around its middle.
			Settings.Pivot = FIntVector(Template.Size.X / 2, 0, Template.Size.Z / 2);
			const FIntVector Corner = Origin - Settings.Pivot;
			return Template.Place(Context.Region, Corner, Settings, Context.Random) > 0;
		}
	};

	class FFossilFeature : public FMcFeature
	{
	public:
		TArray<TSharedPtr<const FMcStructureTemplate>> Fossils;
		TArray<TSharedPtr<const FMcStructureTemplate>> Overlays;
		TSharedPtr<const FMcProcessorList> FossilProcessors;
		TSharedPtr<const FMcProcessorList> OverlayProcessors;
		int32 MaxEmptyCorners = 4;

		virtual bool Place(FMcFeatureContext& Context, const FIntVector& Origin) const override
		{
			if (Fossils.IsEmpty())
			{
				return false;
			}
			const int32 Index = Context.Random.NextInt(Fossils.Num());
			if (!Fossils[Index].IsValid())
			{
				return false;
			}
			const FMcStructureTemplate& Fossil = *Fossils[Index];
			FMcTemplatePlaceSettings Settings;
			Settings.Rotation = McTemplate::RandomRotation(Context.Random);
			Settings.Pivot = FIntVector(Fossil.Size.X / 2, 0, Fossil.Size.Z / 2);
			const FIntVector Corner = Origin - Settings.Pivot;

			// Hardly any of the fossil's eight corners may hang in the open.
			FIntVector Min, Max;
			Fossil.GetBounds(Corner, Settings, Min, Max);
			int32 Empty = 0;
			for (int32 Bit = 0; Bit < 8; ++Bit)
			{
				const FIntVector At((Bit & 1) ? Max.X : Min.X, (Bit & 2) ? Max.Y : Min.Y, (Bit & 4) ? Max.Z : Min.Z);
				Empty += Context.Region.IsAir(Context.Region.GetBlock(At)) ? 1 : 0;
			}
			if (Empty > MaxEmptyCorners)
			{
				return false;
			}
			Settings.Processors = FossilProcessors.Get();
			// The fossil's bones are protected from the rot twice: the overlay goes on after, same pose.
			const int32 Placed = Fossil.Place(Context.Region, Corner, Settings, Context.Random);
			if (Overlays.IsValidIndex(Index) && Overlays[Index].IsValid())
			{
				Settings.Processors = OverlayProcessors.Get();
				Overlays[Index]->Place(Context.Region, Corner, Settings, Context.Random);
			}
			return Placed > 0;
		}
	};
}

void McRegisterTemplateFeatures()
{
	using FLibrary = FMcFeatureLibrary;
	FLibrary::RegisterType(TEXT("template"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FTemplateFeature> Result = MakeShared<FTemplateFeature>();
		const TArray<TSharedPtr<FJsonValue>>* Templates = nullptr;
		if (!Json.TryGetArrayField(TEXT("templates"), Templates))
		{
			return nullptr;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Templates)
		{
			const TSharedPtr<FJsonObject> Object = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
			if (!Object)
			{
				continue;
			}
			FTemplateFeature::FEntry Entry;
			Entry.Template = TemplateOf(Object->TryGetField(TEXT("data")));
			Object->TryGetNumberField(TEXT("weight"), Entry.Weight);
			if (Entry.Template.IsValid() && Entry.Weight > 0)
			{
				Result->TotalWeight += Entry.Weight;
				Result->Entries.Add(Entry);
			}
		}
		if (Json.HasField(TEXT("processors")))
		{
			Result->Processors = FMcProcessorList::ParseRef(Json.TryGetField(TEXT("processors")));
		}
		return Result->Entries.IsEmpty() ? nullptr : TSharedPtr<const FMcFeature>(Result);
	});
	FLibrary::RegisterType(TEXT("fossil"), [](const FJsonObject& Json, FLibrary&) -> TSharedPtr<const FMcFeature>
	{
		TSharedRef<FFossilFeature> Result = MakeShared<FFossilFeature>();
		const TArray<TSharedPtr<FJsonValue>>* Fossils = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Overlays = nullptr;
		if (!Json.TryGetArrayField(TEXT("fossil_structures"), Fossils) || !Json.TryGetArrayField(TEXT("overlay_structures"), Overlays))
		{
			return nullptr;
		}
		for (const TSharedPtr<FJsonValue>& Value : *Fossils)
		{
			Result->Fossils.Add(TemplateOf(Value));
		}
		for (const TSharedPtr<FJsonValue>& Value : *Overlays)
		{
			Result->Overlays.Add(TemplateOf(Value));
		}
		Result->FossilProcessors = FMcProcessorList::ParseRef(Json.TryGetField(TEXT("fossil_processors")));
		Result->OverlayProcessors = FMcProcessorList::ParseRef(Json.TryGetField(TEXT("overlay_processors")));
		Json.TryGetNumberField(TEXT("max_empty_corners_allowed"), Result->MaxEmptyCorners);
		return Result->Fossils.FindByPredicate([](const TSharedPtr<const FMcStructureTemplate>& Template) { return Template.IsValid(); }) == nullptr ? nullptr : TSharedPtr<const FMcFeature>(Result);
	});
}
