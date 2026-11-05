#include "McItemDefinition.h"
#include "McCache.h"
#include "McItemStack.h"
#include "McResourcePacks.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"

namespace
{
	using McItemDef::EContext;
	using McItemDef::FModelRef;

	constexpr int32 MaxDepth = 24;

	FString Short(const FString& Id)
	{
		return Id.StartsWith(TEXT("minecraft:")) ? Id.RightChop(10) : Id;
	}

	FString ContextName(EContext Context)
	{
		switch (Context)
		{
		case EContext::Gui: return TEXT("gui");
		case EContext::FirstPersonRightHand: return TEXT("firstperson_righthand");
		case EContext::Ground: return TEXT("ground");
		default: return TEXT("fixed");
		}
	}

	// The stack's custom_model_data component: {"floats":[..], "flags":[..], "strings":[..], "colors":[..]}.
	const TArray<TSharedPtr<FJsonValue>>* CustomData(const FMcItemStack& Stack, const TCHAR* Field)
	{
		const TSharedPtr<FJsonValue> Value = Stack.Get(TEXT("minecraft:custom_model_data"));
		const TSharedPtr<FJsonObject>* Object;
		const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
		if (Value.IsValid() && Value->TryGetObject(Object))
		{
			(*Object)->TryGetArrayField(Field, Array);
		}
		return Array;
	}

	int32 IndexOf(const TSharedPtr<FJsonObject>& Node)
	{
		double Index = 0;
		Node->TryGetNumberField(TEXT("index"), Index);
		return static_cast<int32>(Index);
	}

	// One tint of a model node as 0xAARRGGBB; the game's tint sources that need the world (grass, foliage, potion ...) give white.
	uint32 ReadTint(const TSharedPtr<FJsonObject>& Tint, const FMcItemStack& Stack)
	{
		FString Type;
		Tint->TryGetStringField(TEXT("type"), Type);
		Type = Short(Type);
		auto FromValue = [](const TSharedPtr<FJsonValue>& Value, uint32 Default)
		{
			if (!Value.IsValid())
			{
				return Default;
			}
			double Number;
			if (Value->TryGetNumber(Number))
			{
				return static_cast<uint32>(static_cast<int64>(Number)) | 0xFF000000u;
			}
			const TArray<TSharedPtr<FJsonValue>>* Rgb;
			if (Value->TryGetArray(Rgb) && Rgb->Num() == 3)
			{
				const auto Byte = [&](int32 I) { return static_cast<uint32>(FMath::Clamp(FMath::RoundToInt((*Rgb)[I]->AsNumber() * 255.0), 0, 255)); };
				return 0xFF000000u | (Byte(0) << 16) | (Byte(1) << 8) | Byte(2);
			}
			return Default;
		};
		if (Type == TEXT("constant"))
		{
			return FromValue(Tint->TryGetField(TEXT("value")), 0xFFFFFFFFu);
		}
		const uint32 Default = FromValue(Tint->TryGetField(TEXT("default")), 0xFFFFFFFFu);
		if (Type == TEXT("dye"))
		{
			const TSharedPtr<FJsonValue> Dyed = Stack.Get(TEXT("minecraft:dyed_color"));
			if (Dyed.IsValid())
			{
				const TSharedPtr<FJsonObject>* Object;
				return Dyed->TryGetObject(Object) ? FromValue((*Object)->TryGetField(TEXT("rgb")), Default) : FromValue(Dyed, Default);
			}
			return Default;
		}
		if (Type == TEXT("custom_model_data"))
		{
			const TArray<TSharedPtr<FJsonValue>>* Colors = CustomData(Stack, TEXT("colors"));
			const int32 Index = IndexOf(Tint);
			return Colors && Colors->IsValidIndex(Index) ? FromValue((*Colors)[Index], Default) : Default;
		}
		return Default;
	}

	class FResolver
	{
	public:
		FResolver(const FMcItemStack& InStack, EContext InContext) : Stack(InStack), Context(InContext) {}

		void Walk(const TSharedPtr<FJsonObject>& Node, int32 Depth, TArray<FModelRef>& Out) const
		{
			if (!Node.IsValid() || Depth > MaxDepth)
			{
				return;
			}
			FString Type;
			Node->TryGetStringField(TEXT("type"), Type);
			Type = Short(Type);
			if (Type == TEXT("model"))
			{
				FModelRef Ref;
				if (Node->TryGetStringField(TEXT("model"), Ref.Model))
				{
					const TArray<TSharedPtr<FJsonValue>>* Tints;
					if (Node->TryGetArrayField(TEXT("tints"), Tints))
					{
						for (const TSharedPtr<FJsonValue>& Tint : *Tints)
						{
							const TSharedPtr<FJsonObject>* Object;
							Ref.Tints.Add(Tint->TryGetObject(Object) ? ReadTint(*Object, Stack) : 0xFFFFFFFFu);
						}
					}
					Out.Add(MoveTemp(Ref));
				}
			}
			else if (Type == TEXT("special"))
			{
				FModelRef Ref;
				if (Node->TryGetStringField(TEXT("base"), Ref.Model))
				{
					Out.Add(MoveTemp(Ref));
				}
			}
			else if (Type == TEXT("composite"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Models;
				if (Node->TryGetArrayField(TEXT("models"), Models))
				{
					for (const TSharedPtr<FJsonValue>& Child : *Models)
					{
						const TSharedPtr<FJsonObject>* Object;
						if (Child->TryGetObject(Object))
						{
							Walk(*Object, Depth + 1, Out);
						}
					}
				}
			}
			else if (Type == TEXT("condition"))
			{
				WalkField(Node, Test(Node) ? TEXT("on_true") : TEXT("on_false"), Depth, Out);
			}
			else if (Type == TEXT("select"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Cases;
				const TOptional<FString> Value = SelectValue(Node);
				if (Value.IsSet() && Node->TryGetArrayField(TEXT("cases"), Cases))
				{
					for (const TSharedPtr<FJsonValue>& Case : *Cases)
					{
						const TSharedPtr<FJsonObject>* CaseObject;
						if (Case->TryGetObject(CaseObject) && Matches(*CaseObject, Value.GetValue()))
						{
							WalkField(*CaseObject, TEXT("model"), Depth, Out);
							return;
						}
					}
				}
				WalkField(Node, TEXT("fallback"), Depth, Out);
			}
			else if (Type == TEXT("range_dispatch"))
			{
				const double Value = RangeValue(Node);
				const TArray<TSharedPtr<FJsonValue>>* Entries;
				const TSharedPtr<FJsonObject>* Chosen = nullptr;
				double Best = -TNumericLimits<double>::Max();
				if (Node->TryGetArrayField(TEXT("entries"), Entries))
				{
					for (const TSharedPtr<FJsonValue>& Entry : *Entries)
					{
						const TSharedPtr<FJsonObject>* EntryObject;
						double Threshold;
						if (Entry->TryGetObject(EntryObject) && (*EntryObject)->TryGetNumberField(TEXT("threshold"), Threshold) && Threshold <= Value && Threshold >= Best)
						{
							Best = Threshold;
							Chosen = EntryObject;
						}
					}
				}
				WalkField(Chosen ? *Chosen : Node, Chosen ? TEXT("model") : TEXT("fallback"), Depth, Out);
			}
			// "empty" and every unknown type draw nothing.
		}

	private:
		void WalkField(const TSharedPtr<FJsonObject>& Node, const TCHAR* Field, int32 Depth, TArray<FModelRef>& Out) const
		{
			const TSharedPtr<FJsonObject>* Child;
			if (Node->TryGetObjectField(Field, Child))
			{
				Walk(*Child, Depth + 1, Out);
			}
		}

		bool Test(const TSharedPtr<FJsonObject>& Node) const
		{
			FString Property;
			Node->TryGetStringField(TEXT("property"), Property);
			Property = Short(Property);
			if (Property == TEXT("damaged"))
			{
				return Stack.IsDamaged();
			}
			if (Property == TEXT("broken"))
			{
				return Stack.IsDamageable() && Stack.GetDamage() >= Stack.GetMaxDamage() - 1;
			}
			if (Property == TEXT("has_component"))
			{
				FString Component;
				return Node->TryGetStringField(TEXT("component"), Component) && Stack.Has(FName(*McCache::Namespaced(Component)));
			}
			if (Property == TEXT("custom_model_data"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Flags = CustomData(Stack, TEXT("flags"));
				const int32 Index = IndexOf(Node);
				return Flags && Flags->IsValidIndex(Index) && (*Flags)[Index]->AsBool();
			}
			// using_item, carried, selected, view_entity, extended_view, keybind_down, fishing_rod/cast, bundle/has_selected_item ...:
			// nothing is being used, held by the cursor or selected here.
			return false;
		}

		TOptional<FString> SelectValue(const TSharedPtr<FJsonObject>& Node) const
		{
			FString Property;
			Node->TryGetStringField(TEXT("property"), Property);
			Property = Short(Property);
			if (Property == TEXT("main_hand"))
			{
				return FString(TEXT("right"));
			}
			if (Property == TEXT("display_context"))
			{
				return ContextName(Context);
			}
			if (Property == TEXT("context_dimension"))
			{
				return FString(TEXT("minecraft:overworld"));
			}
			if (Property == TEXT("custom_model_data"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Strings = CustomData(Stack, TEXT("strings"));
				const int32 Index = IndexOf(Node);
				if (Strings && Strings->IsValidIndex(Index))
				{
					return (*Strings)[Index]->AsString();
				}
			}
			return TOptional<FString>();
		}

		static bool Matches(const TSharedPtr<FJsonObject>& Case, const FString& Value)
		{
			const TSharedPtr<FJsonValue> When = Case->TryGetField(TEXT("when"));
			if (!When.IsValid())
			{
				return false;
			}
			const TArray<TSharedPtr<FJsonValue>>* List;
			if (When->TryGetArray(List))
			{
				for (const TSharedPtr<FJsonValue>& Item : *List)
				{
					if (Item->AsString() == Value)
					{
						return true;
					}
				}
				return false;
			}
			return When->AsString() == Value;
		}

		double RangeValue(const TSharedPtr<FJsonObject>& Node) const
		{
			FString Property;
			Node->TryGetStringField(TEXT("property"), Property);
			Property = Short(Property);
			double Scale = 1.0;
			Node->TryGetNumberField(TEXT("scale"), Scale);
			bool bNormalize = true;
			Node->TryGetBoolField(TEXT("normalize"), bNormalize);
			double Value = 0.0;
			if (Property == TEXT("custom_model_data"))
			{
				const TArray<TSharedPtr<FJsonValue>>* Floats = CustomData(Stack, TEXT("floats"));
				const int32 Index = IndexOf(Node);
				if (Floats && Floats->IsValidIndex(Index))
				{
					Value = (*Floats)[Index]->AsNumber();
				}
			}
			else if (Property == TEXT("damage"))
			{
				const int32 Max = Stack.GetMaxDamage();
				Value = bNormalize ? (Max > 0 ? FMath::Clamp(static_cast<double>(Stack.GetDamage()) / Max, 0.0, 1.0) : 0.0) : Stack.GetDamage();
			}
			else if (Property == TEXT("count"))
			{
				const int32 Max = Stack.GetMaxStackSize();
				Value = bNormalize ? (Max > 0 ? FMath::Clamp(static_cast<double>(Stack.Count) / Max, 0.0, 1.0) : 0.0) : Stack.Count;
			}
			// use_duration, use_cycle, cooldown, time, compass, crossbow/pull, bundle/fullness ...: not in use here, 0.
			return Value * Scale;
		}

		const FMcItemStack& Stack;
		EContext Context;
	};

	FString DefinitionPath(FName Item)
	{
		return McCache::RelativeOf(Item.ToString(), TEXT("items/"), TEXT(".json"));
	}
}

namespace McItemDef
{
	TArray<FModelRef> ResolveJson(const TSharedPtr<FJsonObject>& Definition, const FMcItemStack& Stack, EContext Context)
	{
		TArray<FModelRef> Out;
		const TSharedPtr<FJsonObject>* Root;
		if (Definition.IsValid() && Definition->TryGetObjectField(TEXT("model"), Root))
		{
			FResolver(Stack, Context).Walk(*Root, 0, Out);
		}
		return Out;
	}

	TArray<FModelRef> Resolve(const FMcItemStack& Stack, EContext Context)
	{
		const FString File = McCache::Asset(DefinitionPath(Stack.Item));
		return ResolveJson(FPaths::FileExists(File) ? McCache::LoadJsonObject(File) : nullptr, Stack, Context);
	}

	bool UsesPackModels(const FMcItemStack& Stack)
	{
		if (Stack.Item.IsNone() || !McPacks::HasPacks())
		{
			return false;
		}
		if (McPacks::IsFromPack(DefinitionPath(Stack.Item)))
		{
			return true;
		}
		for (const FModelRef& Ref : Resolve(Stack, EContext::Gui))
		{
			if (McPacks::IsFromPack(McCache::RelativeOf(Ref.Model, TEXT("models/"), TEXT(".json"))))
			{
				return true;
			}
		}
		return false;
	}
}
