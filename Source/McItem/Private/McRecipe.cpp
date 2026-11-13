#include "McRecipe.h"
#include "McMapStore.h"
#include "McPot.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "McCache.h"
#include "McDataPacks.h"
#include "McItem.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

namespace
{
	FMcIngredient ParseIngredient(const TSharedPtr<FJsonValue>& Value)
	{
		FMcIngredient Result;
		if (!Value.IsValid())
		{
			return Result;
		}
		const FMcItemRegistry& Items = FMcItemRegistry::Get();
		FString Text;
		if (Value->TryGetString(Text))
		{
			Items.Expand(Text, Result.Items);
		}
		else if (Value->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Each : Value->AsArray())
			{
				if (Each->TryGetString(Text))
				{
					Items.Expand(Text, Result.Items);
				}
			}
		}
		else if (Value->Type == EJson::Object && Value->AsObject()->TryGetStringField(TEXT("item"), Text))
		{
			// A brewing ingredient: { "item": ... }.
			Items.Expand(Text, Result.Items);
		}
		return Result;
	}

	FMcItemStack ParseStack(const TSharedPtr<FJsonObject>& Json)
	{
		FMcItemStack Result;
		FString Id;
		if (!Json.IsValid() || !Json->TryGetStringField(TEXT("id"), Id))
		{
			return Result;
		}
		Result.Item = FName(*McCache::Namespaced(Id));
		double Count = 1;
		Json->TryGetNumberField(TEXT("count"), Count);
		Result.Count = static_cast<int32>(Count);
		const TSharedPtr<FJsonObject>* Components;
		if (Json->TryGetObjectField(TEXT("components"), Components))
		{
			for (const auto& Entry : (*Components)->Values)
			{
				Result.Components.Add(FName(*Entry.Key), Entry.Value);
			}
		}
		return Result;
	}

	FName PotionOf(const FMcItemStack& Stack)
	{
		const TSharedPtr<FJsonValue> Contents = Stack.Get(TEXT("minecraft:potion_contents"));
		FString Potion;
		if (Contents.IsValid() && Contents->Type == EJson::Object && Contents->AsObject()->TryGetStringField(TEXT("potion"), Potion))
		{
			return FName(*McCache::Namespaced(Potion));
		}
		return FName();
	}

	// The grid's stacks that aren't empty, in order.
	void CollectItems(const TArray<FMcItemStack>& Grid, TArray<const FMcItemStack*>& Out)
	{
		for (const FMcItemStack& Stack : Grid)
		{
			if (!Stack.IsEmpty())
			{
				Out.Add(&Stack);
			}
		}
	}

	// Gives each ingredient its own item, trying every way round; handles ingredients that overlap (tags).
	bool AssignIngredients(const TArray<FMcIngredient>& Ingredients, const TArray<const FMcItemStack*>& Items, int32 Index, uint64 Used)
	{
		if (Index == Ingredients.Num())
		{
			return true;
		}
		for (int32 Item = 0; Item < Items.Num(); ++Item)
		{
			if (!(Used & (1ull << Item)) && Ingredients[Index].Test(*Items[Item]) && AssignIngredients(Ingredients, Items, Index + 1, Used | (1ull << Item)))
			{
				return true;
			}
		}
		return false;
	}

	// The colour of a dye item ("minecraft:red_dye"), or -1.
	int32 DyeColor(FName Item)
	{
		static const TMap<FString, int32> Colors = {
			{ TEXT("white"), 16383998 }, { TEXT("orange"), 16351261 }, { TEXT("magenta"), 13061821 }, { TEXT("light_blue"), 3847130 },
			{ TEXT("yellow"), 16701501 }, { TEXT("lime"), 8439583 }, { TEXT("pink"), 15961002 }, { TEXT("gray"), 4673362 },
			{ TEXT("light_gray"), 10329495 }, { TEXT("cyan"), 1481884 }, { TEXT("purple"), 8991416 }, { TEXT("blue"), 3949738 },
			{ TEXT("brown"), 8606770 }, { TEXT("green"), 6192150 }, { TEXT("red"), 11546150 }, { TEXT("black"), 1908001 } };
		FString Name = McCache::PathOf(Item.ToString());
		if (!Name.RemoveFromEnd(TEXT("_dye")))
		{
			return -1;
		}
		const int32* Color = Colors.Find(Name);
		return Color ? *Color : -1;
	}

	FMcItemStack Single(const FMcItemStack& Stack)
	{
		FMcItemStack Result = Stack;
		Result.Count = 1;
		return Result;
	}

	bool HasBannerPatterns(const FMcItemStack& Stack)
	{
		const TSharedPtr<FJsonValue> Patterns = Stack.Get(TEXT("minecraft:banner_patterns"));
		return Patterns.IsValid() && Patterns->Type == EJson::Array && Patterns->AsArray().Num() > 0;
	}

	// The trim material an addition item stands for: the ingot or shard it is made of.
	FString TrimMaterialOf(FName Item)
	{
		FString Name = McCache::PathOf(Item.ToString());
		for (const TCHAR* Suffix : { TEXT("_ingot"), TEXT("_shard"), TEXT("_brick") })
		{
			Name.RemoveFromEnd(Suffix);
		}
		if (Name == TEXT("lapis_lazuli"))
		{
			Name = TEXT("lapis");
		}
		return TEXT("minecraft:") + Name;
	}
}

FMcRecipeBook& FMcRecipeBook::Get()
{
	static FMcRecipeBook Instance;
	return Instance;
}

const FMcRecipe* FMcRecipeBook::Find(FName Id) const
{
	const int32* Index = Ids.Find(Id);
	return Index ? &Recipes[*Index] : nullptr;
}

bool FMcRecipeBook::EnsureLoaded()
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
	Recipes.Reset();
	Ids.Reset();
	Skipped = 0;
	if (!FMcItemRegistry::Get().EnsureLoaded())
	{
		return false;
	}
	// The game's recipes with a data pack's added: a pack's file of the same id replaces the game's.
	const TArray<McData::FFile> Files = McData::List(TEXT("recipe"));
	if (Files.IsEmpty())
	{
		UE_LOG(LogMcItem, Error, TEXT("No recipes in %s; run tools/fetch_cache.py."), *McCache::Data(TEXT("recipe")));
		return false;
	}
	for (const McData::FFile& File : Files)
	{
		const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(File.Paths.Last());
		if (!Json.IsValid() || !LoadOne(File.Id, *Json))
		{
			++Skipped;
		}
	}
	Recipes.Sort([](const FMcRecipe& A, const FMcRecipe& B) { return A.Id.LexicalLess(B.Id); });
	for (int32 Index = 0; Index < Recipes.Num(); ++Index)
	{
		Ids.Add(Recipes[Index].Id, Index);
	}
	UE_LOG(LogMcItem, Log, TEXT("Loaded %d recipes (%d skipped)"), Recipes.Num(), Skipped);
	static bool bHooked = false;
	if (!bHooked)
	{
		// /reload reads them again.
		bHooked = true;
		McData::OnReload().AddLambda([]()
		{
			FMcRecipeBook& Book = FMcRecipeBook::Get();
			Book.bLoaded = false;
			Book.EnsureLoaded();
		});
	}
	bLoaded = true;
	return true;
}

bool FMcRecipeBook::LoadOne(const FString& Id, const FJsonObject& Json)
{
	FString TypeName;
	if (!Json.TryGetStringField(TEXT("type"), TypeName))
	{
		return false;
	}
	TypeName = McCache::PathOf(TypeName);
	FMcRecipe Recipe;
	Recipe.Id = FName(*McCache::Namespaced(Id));
	Json.TryGetStringField(TEXT("group"), Recipe.Group);
	Json.TryGetStringField(TEXT("category"), Recipe.Category);
	const TSharedPtr<FJsonObject>* Result;
	if (Json.TryGetObjectField(TEXT("result"), Result))
	{
		Recipe.Result = ParseStack(*Result);
	}
	const auto Field = [&Json](const TCHAR* Name) { return Json.TryGetField(Name); };

	if (TypeName == TEXT("crafting_shaped"))
	{
		Recipe.Type = EMcRecipeType::CraftingShaped;
		const TArray<TSharedPtr<FJsonValue>>* Pattern;
		const TSharedPtr<FJsonObject>* Key;
		if (!Json.TryGetArrayField(TEXT("pattern"), Pattern) || !Json.TryGetObjectField(TEXT("key"), Key))
		{
			return false;
		}
		Recipe.Height = Pattern->Num();
		for (const TSharedPtr<FJsonValue>& Row : *Pattern)
		{
			Recipe.Width = FMath::Max(Recipe.Width, Row->AsString().Len());
		}
		Recipe.Ingredients.SetNum(Recipe.Width * Recipe.Height);
		for (int32 Y = 0; Y < Recipe.Height; ++Y)
		{
			const FString Row = (*Pattern)[Y]->AsString();
			for (int32 X = 0; X < Row.Len(); ++X)
			{
				if (Row[X] == ' ')
				{
					continue;
				}
				const TSharedPtr<FJsonValue> Entry = (*Key)->TryGetField(FString(1, &Row[X]));
				if (!Entry.IsValid())
				{
					return false;
				}
				Recipe.Ingredients[Y * Recipe.Width + X] = ParseIngredient(Entry);
			}
		}
	}
	else if (TypeName == TEXT("crafting_shapeless"))
	{
		Recipe.Type = EMcRecipeType::CraftingShapeless;
		const TArray<TSharedPtr<FJsonValue>>* List;
		if (!Json.TryGetArrayField(TEXT("ingredients"), List))
		{
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Each : *List)
		{
			Recipe.Ingredients.Add(ParseIngredient(Each));
		}
	}
	else if (TypeName == TEXT("crafting_transmute"))
	{
		Recipe.Type = EMcRecipeType::CraftingTransmute;
		Recipe.Input = ParseIngredient(Field(TEXT("input")));
		Recipe.Material = ParseIngredient(Field(TEXT("material")));
		const TSharedPtr<FJsonObject>* Count;
		if (Json.TryGetObjectField(TEXT("material_count"), Count))
		{
			double Min = 1.0, Max = 1.0;
			(*Count)->TryGetNumberField(TEXT("min"), Min);
			(*Count)->TryGetNumberField(TEXT("max"), Max);
			Recipe.MaterialMin = static_cast<int32>(Min);
			Recipe.MaterialMax = static_cast<int32>(Max);
		}
		Json.TryGetBoolField(TEXT("add_material_count_to_result"), Recipe.bAddMaterialCount);
	}
	else if (TypeName == TEXT("crafting_decorated_pot"))
	{
		Recipe.Type = EMcRecipeType::CraftingDecoratedPot;
		for (const TCHAR* Side : { TEXT("back"), TEXT("left"), TEXT("right"), TEXT("front") })
		{
			Recipe.Named.Add(FName(Side), ParseIngredient(Field(Side)));
		}
	}
	else if (TypeName.StartsWith(TEXT("crafting_special_")) || TypeName == TEXT("crafting_dye") || TypeName == TEXT("crafting_imbue"))
	{
		Recipe.Type = EMcRecipeType::CraftingSpecial;
		Recipe.Special = FName(*TypeName);
		for (const auto& Entry : Json.Values)
		{
			if (Entry.Value->Type == EJson::String || Entry.Value->Type == EJson::Array)
			{
				if (Entry.Key != TEXT("type") && Entry.Key != TEXT("group") && Entry.Key != TEXT("category"))
				{
					Recipe.Named.Add(FName(*Entry.Key), ParseIngredient(Entry.Value));
				}
			}
		}
	}
	else if (TypeName == TEXT("smelting") || TypeName == TEXT("blasting") || TypeName == TEXT("smoking") || TypeName == TEXT("campfire_cooking"))
	{
		Recipe.Type = TypeName == TEXT("smelting") ? EMcRecipeType::Smelting
			: TypeName == TEXT("blasting") ? EMcRecipeType::Blasting
			: TypeName == TEXT("smoking") ? EMcRecipeType::Smoking
			: EMcRecipeType::CampfireCooking;
		Recipe.Ingredients.Add(ParseIngredient(Field(TEXT("ingredient"))));
		double Number;
		Recipe.CookingTime = Json.TryGetNumberField(TEXT("cookingtime"), Number) ? static_cast<int32>(Number) : 200;
		Recipe.Experience = Json.TryGetNumberField(TEXT("experience"), Number) ? static_cast<float>(Number) : 0.0f;
	}
	else if (TypeName == TEXT("stonecutting"))
	{
		Recipe.Type = EMcRecipeType::Stonecutting;
		Recipe.Ingredients.Add(ParseIngredient(Field(TEXT("ingredient"))));
	}
	else if (TypeName == TEXT("smithing_transform") || TypeName == TEXT("smithing_trim"))
	{
		Recipe.Type = TypeName == TEXT("smithing_transform") ? EMcRecipeType::SmithingTransform : EMcRecipeType::SmithingTrim;
		Recipe.Template = ParseIngredient(Field(TEXT("template")));
		Recipe.Base = ParseIngredient(Field(TEXT("base")));
		Recipe.Addition = ParseIngredient(Field(TEXT("addition")));
		FString Pattern;
		if (Json.TryGetStringField(TEXT("pattern"), Pattern))
		{
			Recipe.TrimPattern = FName(*McCache::Namespaced(Pattern));
		}
	}
	else if (TypeName == TEXT("brewing"))
	{
		Recipe.Type = EMcRecipeType::Brewing;
		const TSharedPtr<FJsonObject>* Input;
		const TSharedPtr<FJsonObject>* Output;
		if (!Json.TryGetObjectField(TEXT("input"), Input) || !Json.TryGetObjectField(TEXT("output"), Output))
		{
			return false;
		}
		Recipe.Input = ParseIngredient(MakeShared<FJsonValueObject>(*Input));
		Recipe.Material = ParseIngredient(Field(TEXT("reagent")));
		const TSharedPtr<FJsonObject>* Contents;
		FString Potion;
		if ((*Input)->TryGetObjectField(TEXT("potion_contents"), Contents) && (*Contents)->TryGetStringField(TEXT("potions"), Potion))
		{
			Recipe.InputPotion = FName(*McCache::Namespaced(Potion));
		}
		Recipe.Result = ParseStack(*Output);
		Recipe.OutputPotion = PotionOf(Recipe.Result);
	}
	else
	{
		return false;
	}
	Recipes.Add(MoveTemp(Recipe));
	return true;
}

// ---- Crafting ----

bool FMcRecipeBook::MatchShaped(const FMcRecipe& Recipe, const TArray<FMcItemStack>& Grid, int32 Width, int32 Height) const
{
	if (Recipe.Width > Width || Recipe.Height > Height)
	{
		return false;
	}
	for (int32 OffsetY = 0; OffsetY <= Height - Recipe.Height; ++OffsetY)
	{
		for (int32 OffsetX = 0; OffsetX <= Width - Recipe.Width; ++OffsetX)
		{
			for (const bool bMirror : { false, true })
			{
				bool bFits = true;
				for (int32 Y = 0; Y < Height && bFits; ++Y)
				{
					for (int32 X = 0; X < Width && bFits; ++X)
					{
						const int32 LocalX = X - OffsetX;
						const int32 LocalY = Y - OffsetY;
						const FMcItemStack& Stack = Grid[Y * Width + X];
						if (LocalX < 0 || LocalY < 0 || LocalX >= Recipe.Width || LocalY >= Recipe.Height)
						{
							bFits = Stack.IsEmpty();
							continue;
						}
						const FMcIngredient& Wanted = Recipe.Ingredients[LocalY * Recipe.Width + (bMirror ? Recipe.Width - 1 - LocalX : LocalX)];
						bFits = Wanted.IsEmpty() ? Stack.IsEmpty() : Wanted.Test(Stack);
					}
				}
				if (bFits)
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool FMcRecipeBook::MatchShapeless(const FMcRecipe& Recipe, const TArray<const FMcItemStack*>& Items) const
{
	return Items.Num() == Recipe.Ingredients.Num() && Items.Num() <= 64 && AssignIngredients(Recipe.Ingredients, Items, 0, 0);
}

bool FMcRecipeBook::MatchSpecial(const FMcRecipe& Recipe, const TArray<FMcItemStack>& Grid, int32 Width, int32 Height, FMcItemStack& Out) const
{
	TArray<const FMcItemStack*> Items;
	CollectItems(Grid, Items);
	const FString Special = Recipe.Special.ToString();
	const auto Named = [&Recipe](const char* Name) -> const FMcIngredient* { return Recipe.Named.Find(FName(Name)); };

	if (Special == TEXT("crafting_special_bannerduplicate"))
	{
		// A patterned banner and a blank one of the colour.
		if (Items.Num() != 2 || !Named("banner"))
		{
			return false;
		}
		const FMcItemStack* Patterned = nullptr;
		const FMcItemStack* Blank = nullptr;
		for (const FMcItemStack* Item : Items)
		{
			if (!Named("banner")->Test(*Item))
			{
				return false;
			}
			(HasBannerPatterns(*Item) ? Patterned : Blank) = Item;
		}
		if (!Patterned || !Blank || Patterned->Item != Blank->Item)
		{
			return false;
		}
		Out = Single(*Patterned);
		return true;
	}
	if (Special == TEXT("crafting_special_shielddecoration"))
	{
		if (Items.Num() != 2 || !Named("banner") || !Named("target"))
		{
			return false;
		}
		const FMcItemStack* Banner = Named("banner")->Test(*Items[0]) ? Items[0] : Items[1];
		const FMcItemStack* Shield = Banner == Items[0] ? Items[1] : Items[0];
		if (!Named("banner")->Test(*Banner) || !Named("target")->Test(*Shield) || Shield->Has(TEXT("minecraft:base_color")))
		{
			return false;
		}
		Out = Single(*Shield);
		FString Color = McCache::PathOf(Banner->Item.ToString());
		Color.RemoveFromEnd(TEXT("_banner"));
		Out.Set(TEXT("minecraft:base_color"), MakeShared<FJsonValueString>(Color));
		if (const TSharedPtr<FJsonValue> Patterns = Banner->Get(TEXT("minecraft:banner_patterns")))
		{
			Out.Set(TEXT("minecraft:banner_patterns"), Patterns);
		}
		return true;
	}
	if (Special == TEXT("crafting_special_repairitem"))
	{
		// Two worn items of one kind make one with their durability added and a bonus of a twentieth.
		if (Items.Num() != 2 || Items[0]->Item != Items[1]->Item || Items[0]->Count != 1 || Items[1]->Count != 1 || !Items[0]->IsDamageable())
		{
			return false;
		}
		const int32 Max = Items[0]->GetMaxDamage();
		const int32 Left = (Max - Items[0]->GetDamage()) + (Max - Items[1]->GetDamage()) + Max / 20;
		Out = Single(*Items[0]);
		Out.Remove(TEXT("minecraft:enchantments"));
		Out.SetDamage(FMath::Max(0, Max - Left));
		return true;
	}
	if (Special == TEXT("crafting_dye"))
	{
		const FMcIngredient* Target = Named("target");
		const FMcIngredient* Dye = Named("dye");
		if (!Target || !Dye || Items.Num() < 2)
		{
			return false;
		}
		const FMcItemStack* Armour = nullptr;
		int32 Sum[3] = { 0, 0, 0 };
		int32 MaxSum = 0;
		int32 NumColors = 0;
		const auto AddColor = [&](int32 Rgb)
		{
			const int32 R = (Rgb >> 16) & 255;
			const int32 G = (Rgb >> 8) & 255;
			const int32 B = Rgb & 255;
			Sum[0] += R;
			Sum[1] += G;
			Sum[2] += B;
			MaxSum += FMath::Max3(R, G, B);
			++NumColors;
		};
		for (const FMcItemStack* Item : Items)
		{
			if (Target->Test(*Item))
			{
				if (Armour)
				{
					return false;
				}
				Armour = Item;
				const TSharedPtr<FJsonValue> Existing = Item->Get(TEXT("minecraft:dyed_color"));
				if (Existing.IsValid() && Existing->Type == EJson::Number)
				{
					AddColor(static_cast<int32>(Existing->AsNumber()));
				}
			}
			else if (Dye->Test(*Item) && DyeColor(Item->Item) >= 0)
			{
				AddColor(DyeColor(Item->Item));
			}
			else
			{
				return false;
			}
		}
		if (!Armour || NumColors == 0 || (NumColors == 1 && !Armour->Has(TEXT("minecraft:dyed_color")) && Items.Num() < 2))
		{
			return false;
		}
		// The average of the colours, brightened or darkened to the average of their strongest channel.
		const float AverageMax = static_cast<float>(MaxSum) / NumColors;
		const float AverageR = static_cast<float>(Sum[0]) / NumColors;
		const float AverageG = static_cast<float>(Sum[1]) / NumColors;
		const float AverageB = static_cast<float>(Sum[2]) / NumColors;
		const float Strongest = FMath::Max3(AverageR, AverageG, AverageB);
		const float Scale = Strongest > 0.0f ? AverageMax / Strongest : 0.0f;
		const int32 R = FMath::Clamp(FMath::FloorToInt32(AverageR * Scale), 0, 255);
		const int32 G = FMath::Clamp(FMath::FloorToInt32(AverageG * Scale), 0, 255);
		const int32 B = FMath::Clamp(FMath::FloorToInt32(AverageB * Scale), 0, 255);
		Out = Single(*Armour);
		Out.Set(TEXT("minecraft:dyed_color"), MakeShared<FJsonValueNumber>((R << 16) | (G << 8) | B));
		return true;
	}
	if (Special == TEXT("crafting_special_bookcloning"))
	{
		// A written book that may still be copied, and blank books to copy it onto.
		const FMcIngredient* Source = Named("source");
		const FMcIngredient* Material = Named("material");
		if (!Source || !Material || Items.Num() < 2)
		{
			return false;
		}
		const FMcItemStack* Original = nullptr;
		int32 NumBlank = 0;
		for (const FMcItemStack* Item : Items)
		{
			if (Source->Test(*Item) && !Original)
			{
				Original = Item;
			}
			else if (Material->Test(*Item))
			{
				++NumBlank;
			}
			else
			{
				return false;
			}
		}
		if (!Original || NumBlank == 0)
		{
			return false;
		}
		int32 Generation = 0;
		const TSharedPtr<FJsonValue> Content = Original->Get(TEXT("minecraft:written_book_content"));
		if (Content.IsValid() && Content->Type == EJson::Object)
		{
			double Number;
			if (Content->AsObject()->TryGetNumberField(TEXT("generation"), Number))
			{
				Generation = static_cast<int32>(Number);
			}
		}
		if (Generation >= 2)
		{
			return false;
		}
		Out = Single(*Original);
		Out.Count = NumBlank;
		if (Content.IsValid() && Content->Type == EJson::Object)
		{
			const TSharedRef<FJsonObject> Copy = MakeShared<FJsonObject>();
			*Copy = *Content->AsObject();
			Copy->SetNumberField(TEXT("generation"), Generation + 1);
			Out.Set(TEXT("minecraft:written_book_content"), MakeShared<FJsonValueObject>(Copy));
		}
		return true;
	}
	if (Special == TEXT("crafting_imbue"))
	{
		// A lingering potion in the middle of eight arrows.
		const FMcIngredient* Source = Named("source");
		const FMcIngredient* Material = Named("material");
		if (!Source || !Material || Width != 3 || Height != 3 || Items.Num() != 9 || !Source->Test(Grid[4]))
		{
			return false;
		}
		for (int32 Index = 0; Index < 9; ++Index)
		{
			if (Index != 4 && !Material->Test(Grid[Index]))
			{
				return false;
			}
		}
		Out = Recipe.Result;
		if (const TSharedPtr<FJsonValue> Contents = Grid[4].Get(TEXT("minecraft:potion_contents")))
		{
			Out.Set(TEXT("minecraft:potion_contents"), Contents);
		}
		return true;
	}
	if (Special == TEXT("crafting_special_mapextending"))
	{
		// A map in the middle of eight paper zooms out one scale: a new map with the same centre.
		const FMcIngredient* MapIngredient = Named("map");
		const FMcIngredient* Paper = Named("material");
		if (!MapIngredient || !Paper || Width != 3 || Height != 3 || Items.Num() != 9 || !MapIngredient->Test(Grid[4]) || !McMapStore::CanZoom(Grid[4]))
		{
			return false;
		}
		for (int32 Index = 0; Index < 9; ++Index)
		{
			if (Index != 4 && !Paper->Test(Grid[Index]))
			{
				return false;
			}
		}
		Out = Single(Grid[4]);
		McMapStore::SetId(Out, McMapStore::Zoom(McMapStore::GetId(Grid[4])));
		return true;
	}
	// Fireworks are not done; see the gaps in the report.
	return false;
}

bool FMcRecipeBook::FindCraftingMatch(const TArray<FMcItemStack>& Grid, int32 Width, int32 Height, FMcCraftingMatch& Out) const
{
	check(Grid.Num() == Width * Height);
	TArray<const FMcItemStack*> Items;
	CollectItems(Grid, Items);
	if (Items.IsEmpty())
	{
		return false;
	}
	for (const FMcRecipe& Recipe : Recipes)
	{
		switch (Recipe.Type)
		{
		case EMcRecipeType::CraftingShaped:
			if (MatchShaped(Recipe, Grid, Width, Height))
			{
				Out = { &Recipe, Recipe.Result };
				return true;
			}
			break;
		case EMcRecipeType::CraftingShapeless:
			if (MatchShapeless(Recipe, Items))
			{
				Out = { &Recipe, Recipe.Result };
				return true;
			}
			break;
		case EMcRecipeType::CraftingTransmute:
			if (Items.Num() >= 1 + Recipe.MaterialMin && Items.Num() <= 1 + Recipe.MaterialMax)
			{
				for (int32 First = 0; First < Items.Num(); ++First)
				{
					if (!Recipe.Input.Test(*Items[First]))
					{
						continue;
					}
					bool bMaterials = true;
					for (int32 Other = 0; Other < Items.Num() && bMaterials; ++Other)
					{
						bMaterials = Other == First || Recipe.Material.Test(*Items[Other]);
					}
					if (bMaterials)
					{
						// The result is the new kind of item (the same one if the recipe names none) with the old one's components.
						FMcItemStack Result = Single(*Items[First]);
						if (!Recipe.Result.Item.IsNone())
						{
							Result.Item = Recipe.Result.Item;
						}
						if (Recipe.bAddMaterialCount)
						{
							Result.Count += Items.Num() - 1;
						}
						Result.Normalize();
						Out = { &Recipe, Result };
						return true;
					}
				}
			}
			break;
		case EMcRecipeType::CraftingDecoratedPot:
			if (Width == 3 && Height == 3 && Items.Num() == 4)
			{
				// Four sides in a diamond (back at the top, front at the bottom); a brick where there's no sherd.
				const int32 Slots[McPot::NumSides] = { 1, 3, 5, 7 };
				bool bFits = true;
				TArray<FName> Decorations;
				for (int32 Side = 0; Side < McPot::NumSides && bFits; ++Side)
				{
					bFits = Recipe.Named[FName(McPot::GetSideName(Side))].Test(Grid[Slots[Side]]);
					Decorations.Add(Grid[Slots[Side]].Item);
				}
				if (bFits)
				{
					FMcItemStack Result = Recipe.Result;
					McPot::SetSides(Result, Decorations);
					Out = { &Recipe, Result };
					return true;
				}
			}
			break;
		case EMcRecipeType::CraftingSpecial:
		{
			FMcItemStack Result;
			if (MatchSpecial(Recipe, Grid, Width, Height, Result))
			{
				Out = { &Recipe, Result };
				return true;
			}
			break;
		}
		default:
			break;
		}
	}
	return false;
}

void FMcRecipeBook::GetRemainders(const FMcRecipe* Recipe, const TArray<FMcItemStack>& Grid, TArray<FMcItemStack>& Out)
{
	const FMcItemRegistry& Items = FMcItemRegistry::Get();
	Out.Reset();
	Out.SetNum(Grid.Num());
	bool bKeptPatterned = false;
	bool bKeptOriginal = false;
	for (int32 Index = 0; Index < Grid.Num(); ++Index)
	{
		const FMcItemStack& Stack = Grid[Index];
		if (Stack.IsEmpty())
		{
			continue;
		}
		if (Recipe && Recipe->Special == TEXT("crafting_special_bannerduplicate") && !bKeptPatterned && HasBannerPatterns(Stack))
		{
			// The banner that was copied stays.
			bKeptPatterned = true;
			Out[Index] = Single(Stack);
			continue;
		}
		if (Recipe && Recipe->Special == TEXT("crafting_special_bookcloning") && !bKeptOriginal && Recipe->Named.FindRef(FName(TEXT("source"))).Test(Stack))
		{
			bKeptOriginal = true;
			Out[Index] = Single(Stack);
			continue;
		}
		const FName Remainder = Items.GetUseRemainder(Stack.Item);
		if (!Remainder.IsNone())
		{
			Out[Index] = FMcItemStack(Remainder, 1);
		}
	}
}

// ---- Cooking, stonecutting, smithing, brewing ----

const FMcRecipe* FMcRecipeBook::FindCooking(EMcRecipeType Kind, const FMcItemStack& Input) const
{
	for (const FMcRecipe& Recipe : Recipes)
	{
		if (Recipe.Type == Kind && Recipe.Ingredients.Num() == 1 && Recipe.Ingredients[0].Test(Input))
		{
			return &Recipe;
		}
	}
	return nullptr;
}

void FMcRecipeBook::FindStonecutting(const FMcItemStack& Input, TArray<const FMcRecipe*>& Out) const
{
	for (const FMcRecipe& Recipe : Recipes)
	{
		if (Recipe.Type == EMcRecipeType::Stonecutting && Recipe.Ingredients.Num() == 1 && Recipe.Ingredients[0].Test(Input))
		{
			Out.Add(&Recipe);
		}
	}
}

bool FMcRecipeBook::FindSmithing(const FMcItemStack& Template, const FMcItemStack& Base, const FMcItemStack& Addition, FMcCraftingMatch& Out) const
{
	for (const FMcRecipe& Recipe : Recipes)
	{
		if ((Recipe.Type != EMcRecipeType::SmithingTransform && Recipe.Type != EMcRecipeType::SmithingTrim)
			|| !Recipe.Template.Test(Template) || !Recipe.Base.Test(Base) || !Recipe.Addition.Test(Addition))
		{
			continue;
		}
		FMcItemStack Result = Single(Base);
		if (Recipe.Type == EMcRecipeType::SmithingTransform)
		{
			// The new item keeps what the old one had: damage, enchantments, name.
			Result.Item = Recipe.Result.Item;
			Result.Normalize();
		}
		else
		{
			const TSharedRef<FJsonObject> Trim = MakeShared<FJsonObject>();
			Trim->SetStringField(TEXT("pattern"), Recipe.TrimPattern.ToString());
			Trim->SetStringField(TEXT("material"), TrimMaterialOf(Addition.Item));
			const TSharedPtr<FJsonValue> Existing = Base.Get(TEXT("minecraft:trim"));
			if (Existing.IsValid() && McComponents::ValuesEqual(Existing, MakeShared<FJsonValueObject>(Trim)))
			{
				continue;
			}
			Result.Set(TEXT("minecraft:trim"), MakeShared<FJsonValueObject>(Trim));
		}
		Out = { &Recipe, Result };
		return true;
	}
	return false;
}

bool FMcRecipeBook::FindBrewing(const FMcItemStack& Bottle, const FMcItemStack& Reagent, FMcItemStack& Out) const
{
	for (const FMcRecipe& Recipe : Recipes)
	{
		if (Recipe.Type == EMcRecipeType::Brewing && Recipe.Input.Test(Bottle) && Recipe.Material.Test(Reagent)
			&& (Recipe.InputPotion.IsNone() || PotionOf(Bottle) == Recipe.InputPotion))
		{
			Out = Single(Recipe.Result);
			return true;
		}
	}
	return false;
}

bool FMcRecipeBook::IsBrewingReagent(const FMcItemStack& Stack) const
{
	for (const FMcRecipe& Recipe : Recipes)
	{
		if (Recipe.Type == EMcRecipeType::Brewing && Recipe.Material.Test(Stack))
		{
			return true;
		}
	}
	return false;
}

bool McCrafterCraft(TArray<FMcItemStack>& Slots, FMcItemStack& Result)
{
	Slots.SetNum(9);
	FMcRecipeBook& Book = FMcRecipeBook::Get();
	Book.EnsureLoaded();
	FMcCraftingMatch Match;
	if (!Book.FindCraftingMatch(Slots, 3, 3, Match) || Match.Result.IsEmpty())
	{
		return false;
	}
	TArray<FMcItemStack> Left;
	FMcRecipeBook::GetRemainders(Match.Recipe, Slots, Left);
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		if (Slots[Index].IsEmpty())
		{
			continue;
		}
		if (--Slots[Index].Count <= 0)
		{
			Slots[Index].Clear();
		}
		if (Slots[Index].IsEmpty() && Left.IsValidIndex(Index) && !Left[Index].IsEmpty())
		{
			Slots[Index] = Left[Index];
		}
	}
	Result = Match.Result;
	return true;
}
