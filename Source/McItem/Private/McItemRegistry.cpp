#include "McItemRegistry.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McDataPacks.h"
#include "McItem.h"
#include "Misc/ScopeLock.h"
#include "Misc/Paths.h"

namespace
{
	constexpr int32 MaxProviderDepth = 8;

	// A string or a list of strings.
	void ReadReferences(const TSharedPtr<FJsonValue>& Value, TArray<FString>& Out)
	{
		if (!Value.IsValid())
		{
			return;
		}
		FString Text;
		if (Value->TryGetString(Text))
		{
			Out.Add(Text);
		}
		else if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Each : Value->AsArray())
			{
				if (Each->TryGetString(Text))
				{
					Out.Add(Text);
				}
			}
		}
	}

	// A tag entry: a name, or { "id": name, "required": false }.
	bool ReadTagEntry(const TSharedPtr<FJsonValue>& Value, FString& Out)
	{
		return Value->TryGetString(Out) || (Value->Type == EJson::Object && Value->AsObject()->TryGetStringField(TEXT("id"), Out));
	}
}

bool McSplitReference(const FString& Reference, FName& OutName)
{
	const bool bTag = Reference.StartsWith(TEXT("#"));
	OutName = FName(*McCache::Namespaced(bTag ? Reference.Mid(1) : Reference));
	return bTag;
}

FMcItemRegistry& FMcItemRegistry::Get()
{
	static FMcItemRegistry Instance;
	return Instance;
}

void FMcItemRegistry::LoadTags(const FString& Folder, TMap<FName, TSet<FName>>& Out)
{
	TMap<FName, TArray<FString>> Pending;
	for (const McData::FFile& File : McData::List(Folder.LeftChop(1)))
	{
		Pending.Add(FName(*File.Id), McData::ReadTagValues(File.Paths));
	}
	// Tags include other tags; resolve depth first, each once.
	TSet<FName> Visiting;
	TFunction<void(FName)> Resolve = [&](FName Name)
	{
		if (Out.Contains(Name) || Visiting.Contains(Name))
		{
			return;
		}
		Visiting.Add(Name);
		TSet<FName> Members;
		if (const TArray<FString>* Values = Pending.Find(Name))
		{
			for (const FString& Entry : *Values)
			{
				FName Target;
				if (McSplitReference(Entry, Target))
				{
					Resolve(Target);
					if (const TSet<FName>* Inner = Out.Find(Target))
					{
						Members.Append(*Inner);
					}
				}
				else
				{
					Members.Add(Target);
				}
			}
		}
		Visiting.Remove(Name);
		Out.Add(Name, MoveTemp(Members));
	};
	for (const TPair<FName, TArray<FString>>& Entry : Pending)
	{
		Resolve(Entry.Key);
	}
}

bool FMcItemRegistry::EnsureLoaded()
{
	if (bLoaded)
	{
		return true;
	}
	FScopeLock Lock(&LoadLock);
	if (bLoaded)
	{
		return true;
	}
	Items.Reset();
	Ids.Reset();
	ItemTags.Reset();
	EnchantmentTags.Reset();
	Enchantments.Reset();
	EnchantmentIds.Reset();
	const TSharedPtr<FJsonObject> Registries = McCache::LoadJsonObject(McCache::Report(TEXT("registries.json")));
	const TSharedPtr<FJsonObject>* ItemRegistry;
	if (!Registries.IsValid() || !Registries->TryGetObjectField(TEXT("minecraft:item"), ItemRegistry))
	{
		UE_LOG(LogMcItem, Error, TEXT("No item registry in reports/registries.json; run tools/fetch_cache.py."));
		return false;
	}
	const TSharedPtr<FJsonObject>* Entries;
	if (!(*ItemRegistry)->TryGetObjectField(TEXT("entries"), Entries))
	{
		return false;
	}
	Items.SetNum((*Entries)->Values.Num());
	int32 Missing = 0;
	for (const TPair<FString, TSharedPtr<FJsonValue>> Entry : (*Entries)->Values)
	{
		const int32 Id = static_cast<int32>(Entry.Value->AsObject()->GetNumberField(TEXT("protocol_id")));
		if (!Items.IsValidIndex(Id))
		{
			UE_LOG(LogMcItem, Error, TEXT("Item %s has id %d outside the registry"), *Entry.Key, Id);
			return false;
		}
		FMcItemDef& Def = Items[Id];
		Def.Name = FName(*Entry.Key);
		Def.Id = Id;
		const FString Path = McCache::Report(TEXT("minecraft/components/item/") + McCache::PathOf(Entry.Key) + TEXT(".json"));
		const TSharedPtr<FJsonObject> Json = IFileManager::Get().FileExists(*Path) ? McCache::LoadJsonObject(Path) : nullptr;
		const TSharedPtr<FJsonObject>* Components;
		if (Json.IsValid() && Json->TryGetObjectField(TEXT("components"), Components))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>> Component : (*Components)->Values)
			{
				Def.Components.Add(FName(*Component.Key), Component.Value);
			}
		}
		else
		{
			++Missing;
		}
		Ids.Add(Def.Name, Id);
	}
	if (Missing > 0)
	{
		UE_LOG(LogMcItem, Warning, TEXT("%d items have no component report"), Missing);
	}

	LoadTags(TEXT("tags/item/"), ItemTags);
	LoadTags(TEXT("tags/enchantment/"), EnchantmentTags);
	static bool bHooked = false;
	if (!bHooked)
	{
		// /reload reads the tags again, a data pack's files added.
		bHooked = true;
		McData::OnReload().AddLambda([]()
		{
			FMcItemRegistry& Registry = FMcItemRegistry::Get();
			Registry.ItemTags.Reset();
			Registry.EnchantmentTags.Reset();
			Registry.LoadTags(TEXT("tags/item/"), Registry.ItemTags);
			Registry.LoadTags(TEXT("tags/enchantment/"), Registry.EnchantmentTags);
		});
	}

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *McCache::Data(TEXT("enchantment/*.json")), true, false);
	for (const FString& File : Files)
	{
		const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(McCache::Data(TEXT("enchantment/") + File));
		if (!Json.IsValid())
		{
			continue;
		}
		FMcEnchantmentDef Def;
		Def.Name = FName(*McCache::Namespaced(FPaths::GetBaseFilename(File)));
		Def.MaxLevel = static_cast<int32>(Json->GetNumberField(TEXT("max_level")));
		Def.Weight = static_cast<int32>(Json->GetNumberField(TEXT("weight")));
		Def.AnvilCost = static_cast<int32>(Json->GetNumberField(TEXT("anvil_cost")));
		const TSharedPtr<FJsonObject>* Cost;
		if (Json->TryGetObjectField(TEXT("min_cost"), Cost))
		{
			Def.MinCostBase = static_cast<int32>((*Cost)->GetNumberField(TEXT("base")));
			Def.MinCostPerLevel = static_cast<int32>((*Cost)->GetNumberField(TEXT("per_level_above_first")));
		}
		if (Json->TryGetObjectField(TEXT("max_cost"), Cost))
		{
			Def.MaxCostBase = static_cast<int32>((*Cost)->GetNumberField(TEXT("base")));
			Def.MaxCostPerLevel = static_cast<int32>((*Cost)->GetNumberField(TEXT("per_level_above_first")));
		}
		ReadReferences(Json->TryGetField(TEXT("supported_items")), Def.Supported);
		ReadReferences(Json->TryGetField(TEXT("primary_items")), Def.Primary);
		Json->TryGetStringField(TEXT("exclusive_set"), Def.ExclusiveSet);
		TArray<FString> Slots;
		ReadReferences(Json->TryGetField(TEXT("slots")), Slots);
		for (const FString& Slot : Slots)
		{
			Def.Slots.Add(FName(*Slot));
		}
		EnchantmentIds.Add(Def.Name, Enchantments.Num());
		Enchantments.Add(MoveTemp(Def));
	}
	// A stable order, by name, so a seeded pick of an enchantment always means the same one.
	Enchantments.Sort([](const FMcEnchantmentDef& A, const FMcEnchantmentDef& B) { return A.Name.LexicalLess(B.Name); });
	EnchantmentIds.Reset();
	for (int32 Index = 0; Index < Enchantments.Num(); ++Index)
	{
		EnchantmentIds.Add(Enchantments[Index].Name, Index);
	}
	UE_LOG(LogMcItem, Log, TEXT("Loaded %d items, %d item tags, %d enchantments"), Items.Num(), ItemTags.Num(), Enchantments.Num());
	bLoaded = true;
	return true;
}

// ---- Tags ----

const TSet<FName>* FMcItemRegistry::GetTag(FName Tag) const
{
	FName Name;
	McSplitReference(Tag.ToString(), Name);
	return ItemTags.Find(Name);
}

bool FMcItemRegistry::HasTag(FName Item, FName Tag) const
{
	const TSet<FName>* Members = GetTag(Tag);
	return Members && Members->Contains(Item);
}

bool FMcItemRegistry::Matches(FName Item, const FString& Reference) const
{
	FName Target;
	if (McSplitReference(Reference, Target))
	{
		const TSet<FName>* Members = ItemTags.Find(Target);
		return Members && Members->Contains(Item);
	}
	return Item == Target;
}

void FMcItemRegistry::Expand(const FString& Reference, TArray<FName>& Out) const
{
	FName Target;
	if (McSplitReference(Reference, Target))
	{
		if (const TSet<FName>* Members = ItemTags.Find(Target))
		{
			for (const FName Member : *Members)
			{
				Out.AddUnique(Member);
			}
		}
	}
	else
	{
		Out.AddUnique(Target);
	}
}

// ---- Components ----

TSharedPtr<FJsonValue> FMcItemRegistry::GetDefault(FName Item, FName Component) const
{
	const FMcItemDef* Def = Find(Item);
	if (!Def)
	{
		return nullptr;
	}
	const TSharedPtr<FJsonValue>* Value = Def->Components.Find(Component);
	return Value ? *Value : nullptr;
}

int32 FMcItemRegistry::GetMaxStackSize(FName Item) const
{
	const TSharedPtr<FJsonValue> Value = GetDefault(Item, TEXT("minecraft:max_stack_size"));
	return Value.IsValid() && Value->Type == EJson::Number ? static_cast<int32>(Value->AsNumber()) : 64;
}

int32 FMcItemRegistry::GetMaxDamage(FName Item) const
{
	const TSharedPtr<FJsonValue> Value = GetDefault(Item, TEXT("minecraft:max_damage"));
	return Value.IsValid() && Value->Type == EJson::Number ? static_cast<int32>(Value->AsNumber()) : 0;
}

bool FMcItemRegistry::GetFood(FName Item, FMcFoodData& Out) const
{
	const TSharedPtr<FJsonValue> Value = GetDefault(Item, TEXT("minecraft:food"));
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Object = Value->AsObject();
	Out.Nutrition = static_cast<int32>(Object->GetNumberField(TEXT("nutrition")));
	Out.Saturation = static_cast<float>(Object->GetNumberField(TEXT("saturation")));
	Out.bCanAlwaysEat = Object->HasTypedField<EJson::Boolean>(TEXT("can_always_eat")) && Object->GetBoolField(TEXT("can_always_eat"));
	return true;
}

bool FMcItemRegistry::GetTool(FName Item, FMcToolData& Out) const
{
	const TSharedPtr<FJsonValue> Value = GetDefault(Item, TEXT("minecraft:tool"));
	if (!Value.IsValid() || Value->Type != EJson::Object)
	{
		return false;
	}
	const TSharedPtr<FJsonObject> Object = Value->AsObject();
	Out = FMcToolData();
	double Number;
	if (Object->TryGetNumberField(TEXT("default_mining_speed"), Number))
	{
		Out.DefaultMiningSpeed = static_cast<float>(Number);
	}
	if (Object->TryGetNumberField(TEXT("damage_per_block"), Number))
	{
		Out.DamagePerBlock = static_cast<int32>(Number);
	}
	Object->TryGetBoolField(TEXT("can_destroy_blocks_in_creative"), Out.bCanDestroyBlocksInCreative);
	const TArray<TSharedPtr<FJsonValue>>* Rules;
	if (Object->TryGetArrayField(TEXT("rules"), Rules))
	{
		for (const TSharedPtr<FJsonValue>& RuleValue : *Rules)
		{
			const TSharedPtr<FJsonObject> Rule = RuleValue->AsObject();
			if (!Rule.IsValid())
			{
				continue;
			}
			FMcToolRule Parsed;
			ReadReferences(Rule->TryGetField(TEXT("blocks")), Parsed.Blocks);
			if (Rule->TryGetNumberField(TEXT("speed"), Number))
			{
				Parsed.Speed = static_cast<float>(Number);
			}
			bool bCorrect;
			if (Rule->TryGetBoolField(TEXT("correct_for_drops"), bCorrect))
			{
				Parsed.CorrectForDrops = bCorrect ? 1 : 0;
			}
			Out.Rules.Add(MoveTemp(Parsed));
		}
	}
	return true;
}

bool FMcItemRegistry::GetArmor(FName Item, FMcArmorData& Out) const
{
	const TSharedPtr<FJsonValue> Equippable = GetDefault(Item, TEXT("minecraft:equippable"));
	if (!Equippable.IsValid() || Equippable->Type != EJson::Object)
	{
		return false;
	}
	Out = FMcArmorData();
	FString Text;
	if (Equippable->AsObject()->TryGetStringField(TEXT("slot"), Text))
	{
		Out.Slot = FName(*Text);
	}
	if (Equippable->AsObject()->TryGetStringField(TEXT("asset_id"), Text))
	{
		Out.AssetId = FName(*Text);
	}
	const TSharedPtr<FJsonValue> Modifiers = GetDefault(Item, TEXT("minecraft:attribute_modifiers"));
	if (Modifiers.IsValid() && Modifiers->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Each : Modifiers->AsArray())
		{
			const TSharedPtr<FJsonObject> Modifier = Each->AsObject();
			FString Type;
			if (!Modifier.IsValid() || !Modifier->TryGetStringField(TEXT("type"), Type))
			{
				continue;
			}
			const double Amount = Modifier->GetNumberField(TEXT("amount"));
			if (Type == TEXT("minecraft:armor"))
			{
				Out.Armor += Amount;
			}
			else if (Type == TEXT("minecraft:armor_toughness"))
			{
				Out.Toughness += Amount;
			}
			else if (Type == TEXT("minecraft:knockback_resistance"))
			{
				Out.KnockbackResistance += Amount;
			}
		}
	}
	return true;
}

int32 FMcItemRegistry::GetEnchantability(FName Item) const
{
	const TSharedPtr<FJsonValue> Value = GetDefault(Item, TEXT("minecraft:enchantable"));
	return Value.IsValid() && Value->Type == EJson::Object ? static_cast<int32>(Value->AsObject()->GetNumberField(TEXT("value"))) : 0;
}

int32 FMcItemRegistry::GetBurnTime(FName Item, bool bFast) const
{
	const TSharedPtr<FJsonValue> Fuel = GetDefault(Item, TEXT("minecraft:cooking_fuel"));
	if (!Fuel.IsValid() || Fuel->Type != EJson::Object)
	{
		return 0;
	}
	return EvaluateIntProvider(Fuel->AsObject()->TryGetField(TEXT("burn_time")), bFast);
}

float FMcItemRegistry::GetFuelSpeed(FName Item, bool bFast) const
{
	const TSharedPtr<FJsonValue> Fuel = GetDefault(Item, TEXT("minecraft:cooking_fuel"));
	if (!Fuel.IsValid() || Fuel->Type != EJson::Object)
	{
		return 0.0f;
	}
	return EvaluateFloatProvider(Fuel->AsObject()->TryGetField(TEXT("speed_multiplier")), bFast);
}

FName FMcItemRegistry::GetUseRemainder(FName Item) const
{
	const TSharedPtr<FJsonValue> Value = GetDefault(Item, TEXT("minecraft:use_remainder"));
	FString Id;
	if (Value.IsValid() && Value->Type == EJson::Object && Value->AsObject()->TryGetStringField(TEXT("id"), Id))
	{
		return FName(*McCache::Namespaced(Id));
	}
	// The one that the item data doesn't say: a lava bucket used as fuel or in a recipe leaves its bucket.
	if (Item == TEXT("minecraft:lava_bucket"))
	{
		return TEXT("minecraft:bucket");
	}
	return FName();
}

namespace
{
	bool RuleMatches(const FMcToolRule& Rule, FMcStateId State)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		for (const FString& Reference : Rule.Blocks)
		{
			if (Reference.StartsWith(TEXT("#")))
			{
				if (FMcBlockTags::Get().Has(Reference.Mid(1), State))
				{
					return true;
				}
			}
			else if (Registry.GetBlockOf(State).Name == FName(*McCache::Namespaced(Reference)))
			{
				return true;
			}
		}
		return false;
	}
}

float FMcItemRegistry::GetDestroySpeed(FName Item, FMcStateId State) const
{
	FMcToolData Tool;
	if (!GetTool(Item, Tool))
	{
		return 1.0f;
	}
	for (const FMcToolRule& Rule : Tool.Rules)
	{
		if (Rule.Speed >= 0.0f && RuleMatches(Rule, State))
		{
			return Rule.Speed;
		}
	}
	return Tool.DefaultMiningSpeed;
}

bool FMcItemRegistry::IsCorrectToolForDrops(FName Item, FMcStateId State) const
{
	FMcToolData Tool;
	if (!GetTool(Item, Tool))
	{
		return false;
	}
	for (const FMcToolRule& Rule : Tool.Rules)
	{
		if (Rule.CorrectForDrops >= 0 && RuleMatches(Rule, State))
		{
			return Rule.CorrectForDrops == 1;
		}
	}
	return false;
}

// ---- Enchantments ----

const FMcEnchantmentDef* FMcItemRegistry::FindEnchantment(FName Name) const
{
	const int32* Index = EnchantmentIds.Find(Name);
	return Index ? &Enchantments[*Index] : nullptr;
}

bool FMcItemRegistry::CanEnchant(FName Item, FName Enchantment) const
{
	const FMcEnchantmentDef* Def = FindEnchantment(Enchantment);
	if (!Def)
	{
		return false;
	}
	for (const FString& Reference : Def->Supported)
	{
		if (Matches(Item, Reference))
		{
			return true;
		}
	}
	return false;
}

void FMcItemRegistry::GetEnchantmentsFor(FName Item, TArray<FName>& Out) const
{
	for (const FMcEnchantmentDef& Def : Enchantments)
	{
		if (CanEnchant(Item, Def.Name))
		{
			Out.Add(Def.Name);
		}
	}
}

const TSet<FName>* FMcItemRegistry::GetEnchantmentTag(FName Tag) const
{
	FName Name;
	McSplitReference(Tag.ToString(), Name);
	return EnchantmentTags.Find(Name);
}

bool FMcItemRegistry::AreCompatible(FName A, FName B) const
{
	if (A == B)
	{
		return false;
	}
	for (const FName Pair[2][2] = { { A, B }, { B, A } }; const FName(&Order)[2] : Pair)
	{
		const FMcEnchantmentDef* Def = FindEnchantment(Order[0]);
		if (Def && !Def->ExclusiveSet.IsEmpty())
		{
			const TSet<FName>* Set = GetEnchantmentTag(FName(*Def->ExclusiveSet));
			if (Set && Set->Contains(Order[1]))
			{
				return false;
			}
		}
	}
	return true;
}

// ---- Providers ----

TSharedPtr<FJsonValue> FMcItemRegistry::ReadProvider(const FString& Folder, const FString& Name) const
{
	const FString Path = McCache::Data(Folder + McCache::PathOf(Name) + TEXT(".json"));
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		return nullptr;
	}
	// A provider file may be a bare number, which isn't a JSON document with an object at the top.
	Text.TrimStartAndEndInline();
	if (Text.IsNumeric())
	{
		return MakeShared<FJsonValueNumber>(FCString::Atod(*Text));
	}
	TSharedPtr<FJsonObject> Object;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
	{
		return nullptr;
	}
	return MakeShared<FJsonValueObject>(Object);
}

double FMcItemRegistry::EvaluateProvider(const TSharedPtr<FJsonValue>& Value, bool bFast, int32 Depth) const
{
	if (!Value.IsValid() || Depth > MaxProviderDepth)
	{
		return 0.0;
	}
	if (Value->Type == EJson::Number)
	{
		return Value->AsNumber();
	}
	FString Name;
	if (Value->TryGetString(Name))
	{
		TSharedPtr<FJsonValue> Loaded = ReadProvider(TEXT("context_int_provider/"), Name);
		if (!Loaded.IsValid())
		{
			Loaded = ReadProvider(TEXT("context_float_provider/"), Name);
		}
		return EvaluateProvider(Loaded, bFast, Depth + 1);
	}
	if (Value->Type != EJson::Object)
	{
		return 0.0;
	}
	const TSharedPtr<FJsonObject> Object = Value->AsObject();
	const FString Type = McCache::PathOf(Object->GetStringField(TEXT("type")));
	const auto Side = [&](const TCHAR* Key) { return EvaluateProvider(Object->TryGetField(Key), bFast, Depth + 1); };
	if (Type == TEXT("constant"))
	{
		return Side(TEXT("value"));
	}
	if (Type == TEXT("add"))
	{
		return Side(TEXT("left")) + Side(TEXT("right"));
	}
	if (Type == TEXT("sub"))
	{
		return Side(TEXT("left")) - Side(TEXT("right"));
	}
	if (Type == TEXT("mul"))
	{
		return Side(TEXT("left")) * Side(TEXT("right"));
	}
	if (Type == TEXT("div"))
	{
		const double Divisor = Side(TEXT("right"));
		return Divisor == 0.0 ? 0.0 : Side(TEXT("left")) / Divisor;
	}
	if (Type == TEXT("conditional"))
	{
		// The only condition the item data asks is whether the furnace is a fast one (a blast furnace or smoker).
		return Side(bFast ? TEXT("on_true") : TEXT("on_false"));
	}
	return 0.0;
}

int32 FMcItemRegistry::EvaluateIntProvider(const TSharedPtr<FJsonValue>& Value, bool bFast) const
{
	return FMath::FloorToInt32(EvaluateProvider(Value, bFast, 0));
}

float FMcItemRegistry::EvaluateFloatProvider(const TSharedPtr<FJsonValue>& Value, bool bFast) const
{
	return static_cast<float>(EvaluateProvider(Value, bFast, 0));
}
