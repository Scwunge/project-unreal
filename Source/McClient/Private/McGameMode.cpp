#include "McGameMode.h"
#include "McPerf.h"
#include "Tasks/Task.h"
#include "Misc/ScopeExit.h"
#include "McMob.h"
#include "McMount.h"
#include "McSound.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "McBed.h"
#include "McBlockEntity.h"
#include "McBlockWorld.h"
#include "McChunk.h"
#include "McEntityActor.h"
#include "McClient.h"
#include "McBlockShapes.h"
#include "McEntityHost.h"
#include "McTnt.h"
#include "McVehicle.h"
#include "McPlayerController.h"
#include "McPlayerData.h"
#include "McPlayerSave.h"
#include "McTravel.h"
#include "McPlayerPawn.h"
#include "McItemFrames.h"
#include "McWorldActor.h"
#include "McWorldSave.h"
#include "McLaunch.h"
#include "McCommands.h"
#include "McAudioSubsystem.h"
#include "McItemRegistry.h"
#include "McPot.h"
#include "McLang.h"
#include "McSurvival.h"
#include "McEffects.h"
#include "McSimulation.h"
#include "McConduitHunt.h"
#include "McEntityManager.h"
#include "McBannerEntity.h"
#include "McBanner.h"
#include "McCache.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/GameSession.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformMisc.h"
#include "McBlockRegistry.h"
#include "McBlockTags.h"
#include "McCache.h"
#include "McDataPacks.h"
#include "McTextComponent.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"
#include "McRecipe.h"

#if MC_WITH_RENDER
#include "McHUD.h"
#endif

AMcGameMode::AMcGameMode()
{
	DefaultPawnClass = AMcPlayerPawn::StaticClass();
	PlayerControllerClass = AMcPlayerController::StaticClass();
#if MC_WITH_RENDER
	HUDClass = AMcHUD::StaticClass();
#endif
}

namespace
{
	// Grants or revokes an advancement for a player (the advancement command's and the triggers'), with its toast.
	// 1 changed, 0 already so, -1 no such player or advancement.
	int32 AwardAdvancement(AMcPlayerPawn* Pawn, const FString& Id, bool bGrant, FString& OutTitle)
	{
		const FString Full = McCache::Namespaced(Id);
		const TSharedPtr<FJsonObject> Def = [&Full]() -> TSharedPtr<FJsonObject>
		{
			const FString Path = McData::PathOf(Full, TEXT("advancement"), TEXT(".json"));
			return Path.IsEmpty() ? nullptr : McCache::LoadJsonObject(Path);
		}();
		if (!Pawn || !Pawn->Data || !Def.IsValid())
		{
			return -1;
		}
		// Its display (the game's data): the title's lang key, the icon item and the frame (task, goal, challenge).
		const TSharedPtr<FJsonObject>* Display = nullptr;
		FString Title = Full;
		FName Icon;
		FString Frame = TEXT("task");
		bool bToast = false;
		if (Def->TryGetObjectField(TEXT("display"), Display))
		{
			const TSharedPtr<FJsonObject>* TitleObject = nullptr;
			FString Key;
			FString Plain;
			if ((*Display)->TryGetStringField(TEXT("title"), Plain))
			{
				Title = Plain;
			}
			else if ((*Display)->TryGetObjectField(TEXT("title"), TitleObject) && (*TitleObject)->TryGetStringField(TEXT("translate"), Key))
			{
				Title = McLang::Get(Key);
			}
			else if (TitleObject)
			{
				// Any other text component: its words.
				FString Json;
				FJsonSerializer::Serialize(TitleObject->ToSharedRef(), TJsonWriterFactory<>::Create(&Json));
				TArray<McText::FSpan> Spans;
				if (McText::Parse(Json, Spans))
				{
					Title = McText::ToPlain(Spans);
				}
			}
			const TSharedPtr<FJsonObject>* IconObject = nullptr;
			FString IconId;
			if ((*Display)->TryGetObjectField(TEXT("icon"), IconObject) && (*IconObject)->TryGetStringField(TEXT("id"), IconId))
			{
				Icon = FName(*IconId);
			}
			(*Display)->TryGetStringField(TEXT("frame"), Frame);
			bToast = true;
			(*Display)->TryGetBoolField(TEXT("show_toast"), bToast);
		}
		OutTitle = Def->HasField(TEXT("display")) ? TEXT("[") + Title + TEXT("]") : Full;
		const FName Key(*Full);
		if (!bGrant)
		{
			return Pawn->Data->Advancements.Remove(Key) > 0 ? 1 : 0;
		}
		if (Pawn->Data->Advancements.Contains(Key))
		{
			return 0;
		}
		Pawn->Data->Advancements.Add(Key);
		UE_LOG(LogMcClient, Log, TEXT("Advancement %s made: %s"), *Full, *Title);
		if (bToast)
		{
			const bool bChallenge = Frame == TEXT("challenge");
			// The challenge's heading colour is the wiki's purple (not measured).
			Pawn->Data->ServerToast(McLang::Get(Frame == TEXT("goal") ? TEXT("advancements.toast.goal") : bChallenge ? TEXT("advancements.toast.challenge") : TEXT("advancements.toast.task")),
				bChallenge ? 0xFF55FF : 0xFFFF00, Title, Icon);
		}
		// What it gives: the recipes of its rewards go into the recipe book.
		const TSharedPtr<FJsonObject>* Rewards = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* RewardRecipes = nullptr;
		if (Def->TryGetObjectField(TEXT("rewards"), Rewards) && (*Rewards)->TryGetArrayField(TEXT("recipes"), RewardRecipes))
		{
			TArray<FName> Ids;
			for (const TSharedPtr<FJsonValue>& Recipe : *RewardRecipes)
			{
				Ids.Add(FName(*McCache::Namespaced(Recipe->AsString())));
			}
			AMcGameMode::UnlockRecipes(*Pawn, Ids, true);
		}
		return 1;

	}

	using FPredicate = TPair<TArray<FString>, int32>;

	struct FInventoryTrigger
	{
		FString Id;
		// Each group needs one of its criteria; a criterion needs every item predicate met.
		TArray<TArray<TArray<FPredicate>>> Groups;
	};

	// Both advancement tables read every advancement file; a world start builds them on a worker (WarmAdvancementTables)
	// and the game thread waits for that before its first use, instead of parsing them all in one frame.
	UE::Tasks::FTask AdvancementWarm;
	void WaitForAdvancementWarm()
	{
		if (AdvancementWarm.IsValid())
		{
			AdvancementWarm.Wait();
			AdvancementWarm = UE::Tasks::FTask();
		}
	}

	// The advancements that fire on what the player carries (the game's data: trigger minecraft:inventory_changed with
	// plain item predicates; those with component or slot conditions are left out).
	const TArray<FInventoryTrigger>& InventoryTriggers()
	{
		static TArray<FInventoryTrigger> Triggers;
		static uint32 LoadedGeneration = 0;
		// Read again after /reload or a change of data packs.
		if (LoadedGeneration == McData::GetGeneration())
		{
			return Triggers;
		}
		LoadedGeneration = McData::GetGeneration();
		Triggers.Reset();
		const double Began = FPlatformTime::Seconds();
		ON_SCOPE_EXIT { McPerf::Event(FString::Printf(TEXT("ADVANCEMENTS_INVENTORY %d in %.0f ms"), Triggers.Num(), (FPlatformTime::Seconds() - Began) * 1000.0)); };
		for (const McData::FFile& Listed : McData::List(TEXT("advancement")))
		{
			const FString& File = Listed.Paths.Last();
			TSharedPtr<FJsonObject> Def;
			FString Text;
			if (!FFileHelper::LoadFileToString(Text, *File) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Def) || !Def.IsValid())
			{
				continue;
			}
			const TSharedPtr<FJsonObject>* Criteria = nullptr;
			if (!Def->TryGetObjectField(TEXT("criteria"), Criteria))
			{
				continue;
			}
			TMap<FString, TArray<FPredicate>> Parsed;
			bool bReadable = true;
			for (const auto& Entry : (*Criteria)->Values)
			{
				const TSharedPtr<FJsonObject>* Criterion = nullptr;
				FString Trigger;
				const TSharedPtr<FJsonObject>* Conditions = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
				if (!Entry.Value->TryGetObject(Criterion) || !(*Criterion)->TryGetStringField(TEXT("trigger"), Trigger) || Trigger != TEXT("minecraft:inventory_changed")
					|| !(*Criterion)->TryGetObjectField(TEXT("conditions"), Conditions) || (*Conditions)->Values.Num() != 1 || !(*Conditions)->TryGetArrayField(TEXT("items"), Items))
				{
					bReadable = false;
					break;
				}
				TArray<FPredicate>& Predicates = Parsed.FindOrAdd(FString(Entry.Key));
				for (const TSharedPtr<FJsonValue>& Value : *Items)
				{
					const TSharedPtr<FJsonObject>* Predicate = nullptr;
					if (!Value->TryGetObject(Predicate))
					{
						bReadable = false;
						break;
					}
					TArray<FString> Names;
					FString One;
					const TArray<TSharedPtr<FJsonValue>>* Many = nullptr;
					if ((*Predicate)->TryGetStringField(TEXT("items"), One))
					{
						Names.Add(One);
					}
					else if ((*Predicate)->TryGetArrayField(TEXT("items"), Many))
					{
						for (const TSharedPtr<FJsonValue>& Name : *Many)
						{
							Names.Add(Name->AsString());
						}
					}
					else
					{
						bReadable = false;
						break;
					}
					int32 Min = 1;
					const TSharedPtr<FJsonObject>* Count = nullptr;
					const bool bHasCount = (*Predicate)->TryGetObjectField(TEXT("count"), Count);
					if (bHasCount)
					{
						double MinValue = 1.0;
						(*Count)->TryGetNumberField(TEXT("min"), MinValue);
						Min = static_cast<int32>(MinValue);
					}
					if ((*Predicate)->Values.Num() != (bHasCount ? 2 : 1))
					{
						bReadable = false;
						break;
					}
					Predicates.Add(FPredicate(Names, Min));
				}
				if (!bReadable)
				{
					break;
				}
			}
			if (!bReadable || Parsed.IsEmpty())
			{
				continue;
			}
			FInventoryTrigger Trigger;
			Trigger.Id = Listed.Id;
			const TArray<TSharedPtr<FJsonValue>>* Requirements = nullptr;
			if (Def->TryGetArrayField(TEXT("requirements"), Requirements))
			{
				for (const TSharedPtr<FJsonValue>& Group : *Requirements)
				{
					TArray<TArray<FPredicate>> Alternatives;
					for (const TSharedPtr<FJsonValue>& Name : Group->AsArray())
					{
						if (const TArray<FPredicate>* Found = Parsed.Find(Name->AsString()))
						{
							Alternatives.Add(*Found);
						}
					}
					Trigger.Groups.Add(Alternatives);
				}
			}
			else
			{
				for (const auto& Entry : Parsed)
				{
					Trigger.Groups.Add({ Entry.Value });
				}
			}
			Triggers.Add(MoveTemp(Trigger));
		}
		return Triggers;
	}

	// Once a second: whatever a player carries that an advancement asks for grants it (parents are not needed, as in the game).
	struct FCriterionDef
	{
		FString Advancement;
		FString Name;
		TSharedPtr<FJsonObject> Conditions;
	};

	struct FTriggerTable
	{
		// By trigger id.
		TMap<FString, TArray<FCriterionDef>> ByTrigger;
		// By advancement: the groups of criteria names, a group being met by any one of them.
		TMap<FString, TArray<TArray<FString>>> Requirements;
	};

	// Every advancement's criteria that wait on an event (the inventory ones are polled, see InventoryTriggers).
	const FTriggerTable& EventTriggers()
	{
		static FTriggerTable Table;
		static uint32 LoadedGeneration = 0;
		if (LoadedGeneration == McData::GetGeneration())
		{
			return Table;
		}
		LoadedGeneration = McData::GetGeneration();
		Table = FTriggerTable();
		const double Began = FPlatformTime::Seconds();
		ON_SCOPE_EXIT { McPerf::Event(FString::Printf(TEXT("ADVANCEMENTS_EVENTS %d in %.0f ms"), Table.ByTrigger.Num(), (FPlatformTime::Seconds() - Began) * 1000.0)); };
		for (const McData::FFile& Listed : McData::List(TEXT("advancement")))
		{
			TSharedPtr<FJsonObject> Def;
			FString Text;
			const TSharedPtr<FJsonObject>* Criteria = nullptr;
			if (!FFileHelper::LoadFileToString(Text, *Listed.Paths.Last()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Def) || !Def.IsValid()
				|| !Def->TryGetObjectField(TEXT("criteria"), Criteria))
			{
				continue;
			}
			bool bAny = false;
			for (const auto& Entry : (*Criteria)->Values)
			{
				const TSharedPtr<FJsonObject>* Criterion = nullptr;
				FString Trigger;
				if (!Entry.Value->TryGetObject(Criterion) || !(*Criterion)->TryGetStringField(TEXT("trigger"), Trigger) || Trigger == TEXT("minecraft:inventory_changed"))
				{
					continue;
				}
				FCriterionDef Out;
				Out.Advancement = Listed.Id;
				Out.Name = FString(Entry.Key);
				const TSharedPtr<FJsonObject>* Conditions = nullptr;
				Out.Conditions = (*Criterion)->TryGetObjectField(TEXT("conditions"), Conditions) ? *Conditions : MakeShared<FJsonObject>();
				Table.ByTrigger.FindOrAdd(Trigger).Add(MoveTemp(Out));
				bAny = true;
			}
			if (!bAny)
			{
				continue;
			}
			TArray<TArray<FString>>& Groups = Table.Requirements.Add(Listed.Id);
			const TArray<TSharedPtr<FJsonValue>>* Requirements = nullptr;
			if (Def->TryGetArrayField(TEXT("requirements"), Requirements))
			{
				for (const TSharedPtr<FJsonValue>& Group : *Requirements)
				{
					TArray<FString>& Names = Groups.AddDefaulted_GetRef();
					for (const TSharedPtr<FJsonValue>& Name : Group->AsArray())
					{
						Names.Add(Name->AsString());
					}
				}
			}
			else
			{
				for (const auto& Entry : (*Criteria)->Values)
				{
					Groups.Add(TArray<FString>{ FString(Entry.Key) });
				}
			}
		}
		return Table;
	}

	bool NameIn(const TSharedPtr<FJsonValue>& Wanted, const FName& Name)
	{
		if (Name.IsNone() || !Wanted.IsValid())
		{
			return false;
		}
		FString One;
		if (Wanted->TryGetString(One))
		{
			return !One.StartsWith(TEXT("#")) && McCache::Namespaced(One) == Name.ToString();
		}
		const TArray<TSharedPtr<FJsonValue>>* Many = nullptr;
		if (Wanted->TryGetArray(Many))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Many)
			{
				if (NameIn(Value, Name))
				{
					return true;
				}
			}
		}
		return false;
	}

	// An entity predicate in a condition: only the entity type is understood; anything else (components, equipment, flags)
	// counts as not met, so no advancement is made that the game would not have made.
	bool EntityMatches(const TSharedPtr<FJsonValue>& Value, const FName& Entity)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(Object))
		{
			return false;
		}
		const TSharedPtr<FJsonObject>* Predicate = nullptr;
		if (!(*Object)->TryGetObjectField(TEXT("predicate"), Predicate) || (*Predicate)->Values.Num() != 1 || !(*Predicate)->HasField(TEXT("minecraft:entity_type")))
		{
			return false;
		}
		return NameIn((*Predicate)->TryGetField(TEXT("minecraft:entity_type")), Entity);
	}

	bool ConditionsMatch(const FJsonObject& Conditions, const FMcTriggerEvent& Event)
	{
		for (const auto& Entry : Conditions.Values)
		{
			const FString Key(Entry.Key);
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (Key == TEXT("item"))
			{
				const TSharedPtr<FJsonObject>* Item = nullptr;
				FString Reference;
				if (!Entry.Value->TryGetObject(Item) || (*Item)->Values.Num() != 1 || !(*Item)->TryGetStringField(TEXT("items"), Reference)
					|| Event.Item.IsNone() || !FMcItemRegistry::Get().Matches(Event.Item, Reference))
				{
					return false;
				}
			}
			else if (Key == TEXT("entity") || Key == TEXT("child"))
			{
				if (!EntityMatches(Entry.Value, Event.Entity))
				{
					return false;
				}
			}
			else if (Key == TEXT("to") || Key == TEXT("from"))
			{
				FString Wanted;
				const FName Actual = Key == TEXT("to") ? Event.To : Event.From;
				if (!Entry.Value->TryGetString(Wanted) || Actual.IsNone() || McCache::Namespaced(Wanted) != Actual.ToString())
				{
					return false;
				}
			}
			else if (Key == TEXT("recipes"))
			{
				if (!NameIn(Entry.Value, Event.Recipe))
				{
					return false;
				}
			}
			else if (Key == TEXT("player"))
			{
				// Where the player is: the biome (the location trigger's way of asking).
				const TSharedPtr<FJsonObject>* Predicate = nullptr;
				const TSharedPtr<FJsonObject>* Location = nullptr;
				if (!Entry.Value->TryGetObject(Object) || !(*Object)->TryGetObjectField(TEXT("predicate"), Predicate) || (*Predicate)->Values.Num() != 1
					|| !(*Predicate)->TryGetObjectField(TEXT("minecraft:location"), Location) || (*Location)->Values.Num() != 1 || !NameIn((*Location)->TryGetField(TEXT("biomes")), Event.Biome))
				{
					return false;
				}
			}
				else if (Key == TEXT("location") && Event.Trigger == TEXT("minecraft:item_used_on_block"))
			{
				// all_of a location_check on the block (names or a tag, no state) and a match_tool on the item in hand.
				FString Type;
				const TArray<TSharedPtr<FJsonValue>>* Terms = nullptr;
				if (!Entry.Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("type"), Type) || Type != TEXT("minecraft:all_of")
					|| !(*Object)->TryGetArrayField(TEXT("terms"), Terms))
				{
					return false;
				}
				for (const TSharedPtr<FJsonValue>& Term : *Terms)
				{
					const TSharedPtr<FJsonObject>* TermObject = nullptr;
					const TSharedPtr<FJsonObject>* Predicate = nullptr;
					FString TermType;
					if (!Term->TryGetObject(TermObject) || !(*TermObject)->TryGetStringField(TEXT("type"), TermType) || !(*TermObject)->TryGetObjectField(TEXT("predicate"), Predicate))
					{
						return false;
					}
					if (TermType == TEXT("minecraft:location_check"))
					{
						const TSharedPtr<FJsonObject>* BlockPredicate = nullptr;
						if ((*Predicate)->Values.Num() != 1 || !(*Predicate)->TryGetObjectField(TEXT("block"), BlockPredicate) || (*BlockPredicate)->Values.Num() != 1
							|| !(*BlockPredicate)->HasField(TEXT("blocks")))
						{
							return false;
						}
						const TSharedPtr<FJsonValue> Blocks = (*BlockPredicate)->TryGetField(TEXT("blocks"));
						FString Tag;
						if (Blocks->TryGetString(Tag) && Tag.StartsWith(TEXT("#")))
						{
							if (!FMcBlockTags::Get().Has(Tag.RightChop(1), Event.State))
							{
								return false;
							}
						}
						else if (!NameIn(Blocks, Event.Block))
						{
							return false;
						}
					}
					else if (TermType == TEXT("minecraft:match_tool"))
					{
						const TSharedPtr<FJsonValue> Items = (*Predicate)->TryGetField(TEXT("items"));
						if ((*Predicate)->Values.Num() != 1 || !Items.IsValid() || Event.Item.IsNone())
						{
							return false;
						}
						bool bAny = false;
						const TArray<TSharedPtr<FJsonValue>>* Many = nullptr;
						FString One;
						if (Items->TryGetString(One))
						{
							bAny = FMcItemRegistry::Get().Matches(Event.Item, One);
						}
						else if (Items->TryGetArray(Many))
						{
							for (const TSharedPtr<FJsonValue>& Value : *Many)
							{
								bAny = bAny || FMcItemRegistry::Get().Matches(Event.Item, Value->AsString());
							}
						}
						if (!bAny)
						{
							return false;
						}
					}
					else
					{
						return false;
					}
				}
			}
			else if (Key == TEXT("location"))
			{
				// A block placed: match_block with plain block names, no state.
				FString Type;
				if (!Entry.Value->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("type"), Type) || Type != TEXT("minecraft:match_block")
					|| (*Object)->HasField(TEXT("state")) || (*Object)->HasField(TEXT("nbt")) || !NameIn((*Object)->TryGetField(TEXT("blocks")), Event.Block))
				{
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		return true;
	}

	void PollInventoryTriggers(UWorld* World)
	{
		if (!World)
		{
			return;
		}
		const FMcItemRegistry& Items = FMcItemRegistry::Get();
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			AMcPlayerPawn* Pawn = It->IsValid() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
			if (!Pawn || !Pawn->Data || !Pawn->Data->Inventory)
			{
				continue;
			}
			// The game's tick trigger (the crafting table's recipe is unlocked right away by it).
			FMcTriggerEvent Tick;
			Tick.Trigger = FName(TEXT("minecraft:tick"));
			AMcGameMode::FireTrigger(*Pawn, Tick);
			TMap<FName, int32> Carried;
			const TSharedPtr<FMcContainer> Containers[3] = { Pawn->Data->Inventory, Pawn->Data->Armor, Pawn->Data->Offhand };
			for (const TSharedPtr<FMcContainer>& Container : Containers)
			{
				for (int32 Slot = 0; Container && Slot < Container->Num(); ++Slot)
				{
					const FMcItemStack& Stack = Container->Get(Slot);
					if (Stack.Count > 0 && !Stack.Item.IsNone())
					{
						Carried.FindOrAdd(Stack.Item) += Stack.Count;
					}
				}
			}
			const auto Has = [&](const FPredicate& Predicate)
			{
				int32 Total = 0;
				for (const auto& Slot : Carried)
				{
					for (const FString& Name : Predicate.Key)
					{
						if (Items.Matches(Slot.Key, Name))
						{
							Total += Slot.Value;
							break;
						}
					}
				}
				return Total >= Predicate.Value;
			};
			WaitForAdvancementWarm();
			for (const FInventoryTrigger& Trigger : InventoryTriggers())
			{
				if (Pawn->Data->Advancements.Contains(FName(*Trigger.Id)))
				{
					continue;
				}
				bool bDone = !Trigger.Groups.IsEmpty();
				for (const auto& Group : Trigger.Groups)
				{
					bool bAny = false;
					for (const auto& Criterion : Group)
					{
						bool bAll = true;
						for (const FPredicate& Predicate : Criterion)
						{
							bAll = bAll && Has(Predicate);
						}
						bAny = bAny || bAll;
					}
					bDone = bDone && bAny;
				}
				if (bDone)
				{
					FString Title;
					AwardAdvancement(Pawn, Trigger.Id, true, Title);
				}
			}
		}
	}
}

void AMcGameMode::FireTrigger(AMcPlayerPawn& Pawn, const FMcTriggerEvent& Event)
{
	if (!Pawn.Data || Event.Trigger.IsNone())
	{
		return;
	}
	WaitForAdvancementWarm();
	const FTriggerTable& Table = EventTriggers();
	const TArray<FCriterionDef>* Waiting = Table.ByTrigger.Find(Event.Trigger.ToString());
	if (!Waiting)
	{
		return;
	}
	for (const FCriterionDef& Criterion : *Waiting)
	{
		const FName Done(*(Criterion.Advancement + TEXT("/") + Criterion.Name));
		if (Pawn.Data->Advancements.Contains(FName(*Criterion.Advancement)) || Pawn.Data->Criteria.Contains(Done) || !ConditionsMatch(*Criterion.Conditions, Event))
		{
			continue;
		}
		Pawn.Data->Criteria.Add(Done);
		Pawn.Data->CriteriaTimes.Add(Done, static_cast<double>(FDateTime::UtcNow().ToUnixTimestamp()));
		bool bAll = true;
		for (const TArray<FString>& Group : Table.Requirements.FindRef(Criterion.Advancement))
		{
			bool bAny = false;
			for (const FString& Name : Group)
			{
				bAny = bAny || Pawn.Data->Criteria.Contains(FName(*(Criterion.Advancement + TEXT("/") + Name)));
			}
			bAll = bAll && bAny;
		}
		if (bAll)
		{
			FString Title;
			AwardAdvancement(&Pawn, Criterion.Advancement, true, Title);
		}
	}
}

int32 AMcGameMode::UnlockRecipes(AMcPlayerPawn& Pawn, const TArray<FName>& Ids, bool bGrant)
{
	if (!Pawn.Data)
	{
		return 0;
	}
	int32 Changed = 0;
	for (const FName Id : Ids)
	{
		if (!bGrant)
		{
			if (Pawn.Data->Recipes.Remove(Id) > 0)
			{
				Pawn.Data->RecipesToDisplay.Remove(Id);
				++Changed;
			}
			continue;
		}
		if (Pawn.Data->Recipes.Contains(Id))
		{
			continue;
		}
		Pawn.Data->Recipes.Add(Id);
		Pawn.Data->RecipesToDisplay.Add(Id);
		++Changed;
		// The toast shows the recipe's result (vanilla: for a recipe that is to be displayed).
		if (const FMcRecipe* Recipe = FMcRecipeBook::Get().Find(Id))
		{
			if (!Recipe->Result.IsEmpty())
			{
				Pawn.Data->ServerRecipeToast(Recipe->Result.Item);
			}
		}
		FMcTriggerEvent Unlocked;
		Unlocked.Trigger = FName(TEXT("minecraft:recipe_unlocked"));
		Unlocked.Recipe = Id;
		FireTrigger(Pawn, Unlocked);
	}
	return Changed;
}

void AMcGameMode::StartPlay()
{
	Super::StartPlay();
	if (McLaunch::IsMenu(GetWorld()))
	{
		// The title screen (AMcHUD) has no world behind it; it opens one by loading the map again with ?McWorld.
		return;
	}
	if (IsRunningDedicatedServer())
	{
		StartServerConsole();
	}
	WaitForAdvancementWarm();
	AdvancementWarm = UE::Tasks::Launch(UE_SOURCE_LOCATION, [] { InventoryTriggers(); EventTriggers(); }, UE::Tasks::ETaskPriority::BackgroundNormal);
	ReadFunctionTags();
	GetWorldTimerManager().SetTimer(FunctionTimer, this, &AMcGameMode::TickFunctions, 0.05f, true);
	ReloadHandle = McData::OnReload().AddWeakLambda(this, [this]()
	{
		ReadFunctionTags();
		RunFunctionTag(TEXT("minecraft:load"));
	});
	// The world has players a moment after it starts.
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]() { RunFunctionTag(TEXT("minecraft:load")); }));
	if (GetNetMode() == NM_DedicatedServer)
	{
		const FMcServerProperties& Properties = FMcServerProperties::Get();
		if (GameSession)
		{
			// The player limit is max-players, checked in PreLogin so operators can be let past it.
			GameSession->MaxPlayers = 10000;
		}
		// Makes the allowlist and ops files if they aren't there yet, so there is something to edit.
		FMcServerAccess::Load();
		UE_LOG(LogMcClient, Log, TEXT("Dedicated server \"%s\" on port %d: up to %d players, allowlist %s."), *Properties.GetString(TEXT("motd")),
			GetWorld()->URL.Port, Properties.GetInt(TEXT("max-players"), 20), Properties.GetBool(TEXT("white-list")) ? TEXT("on") : TEXT("off"));
	}

	// The compass and the clock read where the local player is and what time it is, once a game tick.
	GetWorldTimerManager().SetTimer(ItemFrameTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		const APlayerController* Local = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		const AMcPlayerPawn* Pawn = Local ? Cast<AMcPlayerPawn>(Local->GetPawn()) : nullptr;
		const AMcPlayerController* Controller = Cast<AMcPlayerController>(Local);
		const AMcWorldActor* Here = Controller ? Controller->GetWorldActor() : nullptr;
		if (!Pawn || !Here)
		{
			return;
		}
		McItemFrames::FContext Context;
		Context.Position = FVector2D(Pawn->GetFeetPosition().X, Pawn->GetFeetPosition().Z);
		Context.Yaw = Pawn->GetLookYaw();
		Context.Spawn = FVector2D(Here->GetSpawnColumn().X + 0.5, Here->GetSpawnColumn().Y + 0.5);
		Context.DayTime = Here->GetDayTime();
		Context.bOverworld = Here->Dimension == TEXT("minecraft:overworld");
		Context.GameTicks = Here->GetGameTime();
		Context.Dimension = FName(*Here->Dimension);
		if (Pawn->Data && Pawn->Data->Stats.bHasDeath)
		{
			Context.bHasDeath = true;
			Context.DeathPosition = FVector2D(Pawn->Data->Stats.DeathPos.X + 0.5, Pawn->Data->Stats.DeathPos.Z + 0.5);
			Context.DeathDimension = Pawn->Data->Stats.DeathDimension;
		}
		McItemFrames::SetContext(Context);
		static bool bShotMapGiven = !FParse::Param(FCommandLine::Get(), TEXT("McMapShot"));
		if (!bShotMapGiven && Here->GetGameTime() > 40)
		{
			bShotMapGiven = true;
			const_cast<AMcPlayerPawn*>(Pawn)->GiveShotMap();
		}
	}), 0.05f, true);

	// Advancements that fire on what a player carries.
	GetWorldTimerManager().SetTimer(AdvancementTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { PollInventoryTriggers(GetWorld()); }), 1.0f, true);

	// Spawned after Super::StartPlay so the world has begun play and BeginPlay runs straight away.
	WorldActor = GetWorld()->SpawnActor<AMcWorldActor>();
	if (WorldActor)
	{
		WorldActor->SetEntityProxyClass(AMcEntityActor::StaticClass());
	}
	if (!WorldActor || !WorldActor->GetBlockWorld())
	{
		UE_LOG(LogMcClient, Error, TEXT("No world to play in; see the McServer and McCore errors above."));
#if !UE_BUILD_SHIPPING
		// A development run (a -McShot screenshot, a test) that can't start has nothing to wait for.
		FString ShotPath;
		if (FParse::Value(FCommandLine::Get(), TEXT("-McShot="), ShotPath))
		{
			FPlatformMisc::RequestExit(true, TEXT("McNoWorld"));
		}
#endif
		return;
	}

	FMcBlockShapes::Get().EnsureLoaded();

	SetUpWorld(*WorldActor);
	if (WorldActor->HasStorage())
	{
		SavingHandle = WorldActor->OnSaving.AddUObject(this, &AMcGameMode::SavePlayers);
		// Leaving for another map (Save and Quit to Title) takes the pawns away before anything here ends play, so the
		// players are saved as the engine starts loading it.
		PreLoadMapHandle = FCoreUObjectDelegates::PreLoadMapWithContext.AddWeakLambda(this, [this](const FWorldContext& Context, const FString&)
		{
			if (Context.World() == GetWorld())
			{
				SavePlayers();
			}
		});
	}

	// Players that joined before the world existed started at a guessed height; move them above the surface (or to
	// where the saved world left them).
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr)
		{
			bool bFlying = false;
			bool bAtSpawn = false;
			const FTransform Start = GetStartTransform(It->Get(), bFlying, bAtSpawn);
			Pawn->SetActorLocation(Start.GetLocation());
			if (AMcPlayerPawn* McPawn = Cast<AMcPlayerPawn>(Pawn))
			{
				if (bAtSpawn)
				{
					McPawn->ServerStandOnSpawnColumn();
				}
				// (The place alone was set before, so a saved world's player looked along +x whatever way they faced.)
				McPawn->SetStartLook(Start.Rotator());
				McPawn->SetGameRules(GetGameRules());
				McPawn->SetFlying(bFlying);
				if (!RestoreSurvival(It->Get(), *McPawn))
				{
					ApplyDefaultGameMode(*McPawn);
				}
			}
		}
	}

	StartDevCapture();
}

void AMcGameMode::SetUpWorld(AMcWorldActor& World)
{
	Worlds.AddUnique(&World);
	ApplyGameRules();
	World.SetEntityProxyClass(AMcEntityActor::StaticClass());
	if (FMcEntityHost* Host = World.GetEntityHost())
	{
		Host->OnPlayerHit = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FVector& Position, float Damage)
		{
			if (const AMcWorldActor* Here = Weak.Get())
			{
				HurtPlayerNear(*Here, Position, Damage);
			}
		};
		Host->OnPlayerHitBy = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FVector& Position, float Damage, FName Mob)
		{
			if (const AMcWorldActor* Here = Weak.Get())
			{
				HurtPlayerNear(*Here, Position, Damage, McSurvival::EDamage::Mob, Mob);
			}
		};
		Host->OnPlayerHitThroughArmor = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FVector& Position, float Damage)
		{
			// A warden's sonic boom: armor does not reduce it (Magic is the kind armor lets through whole).
			if (const AMcWorldActor* Here = Weak.Get())
			{
				HurtPlayerNear(*Here, Position, Damage, McSurvival::EDamage::Magic);
			}
		};
		Host->OnPlayerBlast = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FVector& Position, float Damage, const FVector& Push, const FVector& Center)
		{
			if (const AMcWorldActor* Here = Weak.Get())
			{
				BlastPlayerNear(*Here, Position, Damage, Push, Center);
			}
		};
		Host->PlayerWearsGold = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FVector& Position)
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
				if (Player && Player->GetWorldActor() && Player->GetWorldActor() != Weak.Get())
				{
					continue;
				}
				const AMcPlayerPawn* McPawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
				if (McPawn && McPawn->Data && McPawn->Data->Armor && FVector::Dist(McPawn->GetFeetPosition(), Position) < 0.5)
				{
					for (int32 Slot = 0; Slot < McPawn->Data->Armor->Num(); ++Slot)
					{
						if (McPawn->Data->Armor->Get(Slot).Item.ToString().StartsWith(TEXT("minecraft:golden_")))
						{
							return true;
						}
					}
				}
			}
			return false;
		};
		Host->PlayerWearsPumpkin = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FVector& Position)
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
				if (Player && Player->GetWorldActor() && Player->GetWorldActor() != Weak.Get())
				{
					continue;
				}
				const AMcPlayerPawn* McPawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
				if (McPawn && McPawn->Data && McPawn->Data->Armor && FVector::Dist(McPawn->GetFeetPosition(), Position) < 0.5
					&& McPawn->Data->Armor->Get(0).Item == FName(TEXT("minecraft:carved_pumpkin")))
				{
					return true;
				}
			}
			return false;
		};
		Host->PlayerLooksAt = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FVector& PlayerPos, const FVector& Point)
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
				if (Player && Player->GetWorldActor() && Player->GetWorldActor() != Weak.Get())
				{
					continue;
				}
				const AMcPlayerPawn* McPawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
				if (!McPawn || !McPawn->Data || McPawn->Data->bCreative || FVector::Dist(McPawn->GetFeetPosition(), PlayerPos) >= 0.5)
				{
					continue;
				}
				// Both the yaw and the pitch of the way they face must be within 60 degrees of the way to the point.
				const FVector Eye = PlayerPos + FVector(0.0, 1.62, 0.0);
				const FVector To = (Point - Eye).GetSafeNormal();
				const double WantYaw = FMath::RadiansToDegrees(FMath::Atan2(-To.X, To.Z));
				const double WantPitch = FMath::RadiansToDegrees(FMath::Asin(-FMath::Clamp(To.Y, -1.0, 1.0)));
				return FMath::Abs(FMath::FindDeltaAngleDegrees(WantYaw, McPawn->GetLookYaw())) <= 60.0 && FMath::Abs(WantPitch - McPawn->GetLookPitch()) <= 60.0;
			}
			return false;
		};
		Host->FindPlayerById = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](int32 Id, FVector& OutFeet)
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
				if (Player && Player->GetWorldActor() && Player->GetWorldActor() != Weak.Get())
				{
					continue;
				}
				const AMcPlayerPawn* McPawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
				if (McPawn && static_cast<int32>(McPawn->GetUniqueID()) == Id)
				{
					if (!McPawn->Data || McPawn->Data->bCreative)
					{
						return false;
					}
					OutFeet = McPawn->GetFeetPosition();
					return true;
				}
			}
			return false;
		};
		Host->PlayerIdAt = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FVector& Feet)
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
				if (Player && Player->GetWorldActor() && Player->GetWorldActor() != Weak.Get())
				{
					continue;
				}
				const AMcPlayerPawn* McPawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
				if (McPawn && FVector::Dist(McPawn->GetFeetPosition(), Feet) < 0.5)
				{
					return static_cast<int32>(McPawn->GetUniqueID());
				}
			}
			return 0;
		};
		Host->OnPlayerEffect = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FVector& Position, FName Effect, int32 Amplifier, int32 Duration)
		{
			if (const AMcWorldActor* Here = Weak.Get())
			{
				EffectPlayerNear(*Here, Position, Effect, Amplifier, Duration);
			}
		};
		Host->OnRaidWon = [this, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FVector& Center, int32 Level)
		{
			if (const AMcWorldActor* Here = Weak.Get())
			{
				HonourHeroes(*Here, Center, Level);
			}
		};
		if (!GetWorldTimerManager().IsTimerActive(VillageTimer))
		{
			GetWorldTimerManager().SetTimer(VillageTimer, this, &AMcGameMode::TickVillages, 1.0f, true);
		}
	}
	if (FMcSimulation* Sim = World.GetSimulation())
	{
		// Each hunting conduit's target, by where it is.
		TSharedRef<TMap<FIntVector, int32>> Targets = MakeShared<TMap<FIntVector, int32>>();
		Sim->OnConduitPulse = [this, Targets, Weak = TWeakObjectPtr<AMcWorldActor>(&World)](const FIntVector& Pos, int32 Range, bool bHunting)
		{
			AMcWorldActor* Here = Weak.Get();
			if (!Here)
			{
				return;
			}
			GiveConduitPower(*Here, FVector(Pos) + FVector(0.5), Range);
			FMcEntityHost* Host = Here->GetEntityHost();
			const FMcSimulation* HereSim = Here->GetSimulation();
			if (!bHunting || !Host || !HereSim)
			{
				Targets->Remove(Pos);
				return;
			}
			const int32 Target = McConduitHunt::Pulse(Host->GetManager(), Pos, Targets->FindRef(Pos), [HereSim](const FMcEntity& Mob)
			{
				const FVector& Feet = Mob.Body.Position;
				return Mob.Body.bInWater || HereSim->IsRainingAt(FIntVector(FMath::FloorToInt32(Feet.X), FMath::FloorToInt32(Feet.Y), FMath::FloorToInt32(Feet.Z)));
			});
			if (Target != 0)
			{
				Targets->Add(Pos, Target);
			}
			else
			{
				Targets->Remove(Pos);
			}
		};
	}
	// With more than one world each looks after the players who are in it.
	if (Worlds.Num() > 1)
	{
		for (AMcWorldActor* Each : Worlds)
		{
			Each->PlayerFilter = [Weak = TWeakObjectPtr<AMcWorldActor>(Each)](const AController& Controller)
			{
				const AMcPlayerController* Player = Cast<AMcPlayerController>(&Controller);
				const AMcWorldActor* Home = Player ? Player->GetWorldActor() : nullptr;
				const AMcWorldActor* Self = Weak.Get();
				return !Player || !Self || Home == Self || (!Home && !Self->bSecondary);
			};
		}
	}
}

AMcWorldActor* AMcGameMode::FindWorld(const FString& Dimension) const
{
	for (AMcWorldActor* Each : Worlds)
	{
		if (Each && Each->Dimension == Dimension)
		{
			return Each;
		}
	}
	return nullptr;
}

AMcWorldActor* AMcGameMode::GetOrCreateWorld(const FString& Dimension)
{
	if (AMcWorldActor* Existing = FindWorld(Dimension))
	{
		return Existing;
	}
	if (!WorldActor || !WorldActor->GetBlockWorld())
	{
		return nullptr;
	}
	AMcWorldActor* Made = GetWorld()->SpawnActorDeferred<AMcWorldActor>(AMcWorldActor::StaticClass(), FTransform::Identity);
	if (!Made)
	{
		return nullptr;
	}
	Made->Dimension = Dimension;
	Made->NoiseSettings = Dimension == TEXT("minecraft:the_end") ? TEXT("minecraft:end") : Dimension == TEXT("minecraft:the_nether") ? TEXT("minecraft:nether") : TEXT("minecraft:overworld");
	Made->InheritFrom(*WorldActor);
	Made->FinishSpawning(FTransform::Identity);
	if (!Made->GetBlockWorld())
	{
		UE_LOG(LogMcClient, Error, TEXT("Can't make the world for %s; see the McServer errors above."), *Dimension);
		Made->Destroy();
		return nullptr;
	}
	SetUpWorld(*Made);
	if (Made->HasStorage())
	{
		Made->OnSaving.AddWeakLambda(this, [this]() { SavePlayers(); });
	}
	UE_LOG(LogMcClient, Display, TEXT("Made the world for %s"), *Dimension);
	return Made;
}

void AMcGameMode::LoadDevScene(const FString& Name, FString& OutCamera)
{
	// Scenes are boxes of block states, shared with tools/scene_to_mcfunction.py so the same scene can be
	// built in real 26.3 for side-by-side screenshots.
	const FString Path = FPaths::ProjectDir() / TEXT("tools/scenes") / Name + TEXT(".json");
	const TSharedPtr<FJsonObject> Scene = McCache::LoadJsonObject(Path);
	if (!Scene.IsValid())
	{
		return;
	}

	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	auto ToBlock = [](const TArray<TSharedPtr<FJsonValue>>& Values)
	{
		return FIntVector(FMath::RoundToInt32(Values[0]->AsNumber()), FMath::RoundToInt32(Values[1]->AsNumber()), FMath::RoundToInt32(Values[2]->AsNumber()));
	};
	int32 Placed = 0;
	for (const TSharedPtr<FJsonValue>& BoxValue : Scene->GetArrayField(TEXT("blocks")))
	{
		const TSharedPtr<FJsonObject>& Box = BoxValue->AsObject();
		const FString StateText = Box->GetStringField(TEXT("state"));
		const TOptional<FMcStateId> State = Registry.ParseState(StateText);
		const TArray<TSharedPtr<FJsonValue>>& From = Box->GetArrayField(TEXT("from"));
		const TArray<TSharedPtr<FJsonValue>>& To = Box->GetArrayField(TEXT("to"));
		if (!State.IsSet() || From.Num() != 3 || To.Num() != 3)
		{
			UE_LOG(LogMcClient, Error, TEXT("%s: can't place %s"), *Path, *StateText);
			continue;
		}

		const FIntVector A = ToBlock(From);
		const FIntVector B = ToBlock(To);
		for (int32 Y = FMath::Min(A.Y, B.Y); Y <= FMath::Max(A.Y, B.Y); ++Y)
		{
			for (int32 Z = FMath::Min(A.Z, B.Z); Z <= FMath::Max(A.Z, B.Z); ++Z)
			{
				for (int32 X = FMath::Min(A.X, B.X); X <= FMath::Max(A.X, B.X); ++X)
				{
					WorldActor->SetBlock(FIntVector(X, Y, Z), State.GetValue());
					++Placed;
				}
			}
		}
	}

	// Signs' words: { "at": [x, y, z], "front": [up to four lines], "color": "red", "glowing": true, and "back",
	// "back_color", "back_glowing" for the other side, "waxed" }. Written once the sign's chunk is there.
	struct FSceneSign
	{
		FIntVector Pos;
		FMcBlockEntity Entity;
	};
	TSharedRef<TArray<FSceneSign>> Signs = MakeShared<TArray<FSceneSign>>();
	const TArray<TSharedPtr<FJsonValue>>* SignValues = nullptr;
	if (Scene->TryGetArrayField(TEXT("signs"), SignValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SignValues)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("at"), At) || At->Num() != 3)
			{
				UE_LOG(LogMcClient, Error, TEXT("%s: a sign needs \"at\": [x, y, z]"), *Path);
				continue;
			}
			FSceneSign& Sign = Signs->Emplace_GetRef();
			Sign.Pos = ToBlock(*At);
			Sign.Entity = FMcBlockEntity::MakeSign();
			for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
			{
				FMcSignSide& Side = Sign.Entity.SignSides[SideIndex];
				const FString Prefix = SideIndex == 0 ? FString() : FString(TEXT("back_"));
				const TArray<TSharedPtr<FJsonValue>>* Lines = nullptr;
				if (Object->TryGetArrayField(SideIndex == 0 ? TEXT("front") : TEXT("back"), Lines))
				{
					for (int32 Line = 0; Line < FMath::Min(Lines->Num(), FMcSignSide::NumLines); ++Line)
					{
						Side.Lines[Line] = (*Lines)[Line]->AsString();
					}
				}
				FString Color;
				if (Object->TryGetStringField(Prefix + TEXT("color"), Color))
				{
					Side.Color = FName(*Color);
				}
				Object->TryGetBoolField(Prefix + TEXT("glowing"), Side.bGlowing);
			}
			Object->TryGetBoolField(TEXT("waxed"), Sign.Entity.bWaxed);
		}
	}
	// Lecterns' books: { "at": [x, y, z], "pages": ["text", ...], "page": 0, "title": ..., "author": ... }, a written
	// book. Written the same way as the signs; the lectern (placed with has_book=false) then gets has_book=true, so
	// that a comparator reading it reads the book.
	const TArray<TSharedPtr<FJsonValue>>* LecternValues = nullptr;
	if (Scene->TryGetArrayField(TEXT("lecterns"), LecternValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *LecternValues)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("at"), At) || At->Num() != 3)
			{
				UE_LOG(LogMcClient, Error, TEXT("%s: a lectern needs \"at\": [x, y, z]"), *Path);
				continue;
			}
			const TSharedRef<FJsonObject> Content = MakeShared<FJsonObject>();
			TArray<TSharedPtr<FJsonValue>> Pages;
			const TArray<TSharedPtr<FJsonValue>>* PageValues = nullptr;
			if (Object->TryGetArrayField(TEXT("pages"), PageValues))
			{
				for (const TSharedPtr<FJsonValue>& Page : *PageValues)
				{
					const TSharedRef<FJsonObject> Raw = MakeShared<FJsonObject>();
					Raw->SetStringField(TEXT("raw"), Page->AsString());
					Pages.Add(MakeShared<FJsonValueObject>(Raw));
				}
			}
			Content->SetArrayField(TEXT("pages"), Pages);
			FString TitleText, Author;
			Object->TryGetStringField(TEXT("title"), TitleText);
			Object->TryGetStringField(TEXT("author"), Author);
			const TSharedRef<FJsonObject> Title = MakeShared<FJsonObject>();
			Title->SetStringField(TEXT("raw"), TitleText);
			Content->SetObjectField(TEXT("title"), Title);
			Content->SetStringField(TEXT("author"), Author);
			FMcItemStack Book(TEXT("minecraft:written_book"));
			Book.Set(TEXT("minecraft:written_book_content"), MakeShared<FJsonValueObject>(Content));
			FSceneSign& Lectern = Signs->Emplace_GetRef();
			Lectern.Pos = ToBlock(*At);
			Lectern.Entity = FMcBlockEntity::MakeLectern(Book);
			Object->TryGetNumberField(TEXT("page"), Lectern.Entity.Page);
		}
	}
	// Decorated pots' sides: { "at": [x, y, z], "sides": [back, left, right, front] }, items (bricks or sherds).
	const TArray<TSharedPtr<FJsonValue>>* PotValues = nullptr;
	if (Scene->TryGetArrayField(TEXT("pots"), PotValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *PotValues)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* SideValues = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("at"), At) || At->Num() != 3 || !Object->TryGetArrayField(TEXT("sides"), SideValues))
			{
				UE_LOG(LogMcClient, Error, TEXT("%s: a pot needs \"at\": [x, y, z] and \"sides\""), *Path);
				continue;
			}
			TArray<FName> Sides;
			for (const TSharedPtr<FJsonValue>& Side : *SideValues)
			{
				Sides.Add(FName(*Side->AsString()));
			}
			FSceneSign& Pot = Signs->Emplace_GetRef();
			Pot.Pos = ToBlock(*At);
			Pot.Entity = FMcBlockEntity::MakePot(Sides);
		}
	}
	// Dropped items that stay put and are never picked up: { "at": [x, y, z], "item": id, "count": n, "sides": [pot sides] }.
	const TArray<TSharedPtr<FJsonValue>>* ItemValues = nullptr;
	FMcEntityHost* Host = WorldActor ? WorldActor->GetEntityHost() : nullptr;
	if (Host && Scene->TryGetArrayField(TEXT("items"), ItemValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ItemValues)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			FString Id;
			if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("at"), At) || At->Num() != 3 || !Object->TryGetStringField(TEXT("item"), Id))
			{
				UE_LOG(LogMcClient, Error, TEXT("%s: an item needs \"at\": [x, y, z] and \"item\""), *Path);
				continue;
			}
			FMcItemStack Stack{ FName(*Id) };
			double CountValue = 1.0;
			if (Object->TryGetNumberField(TEXT("count"), CountValue))
			{
				Stack.Count = FMath::Max(1, static_cast<int32>(CountValue));
			}
			const TArray<TSharedPtr<FJsonValue>>* SideValues = nullptr;
			if (Object->TryGetArrayField(TEXT("sides"), SideValues))
			{
				TArray<FName> Sides;
				for (const TSharedPtr<FJsonValue>& Side : *SideValues)
				{
					Sides.Add(FName(*Side->AsString()));
				}
				McPot::SetSides(Stack, Sides);
			}
			FMcItemEntity& Item = Host->GetManager().DropItem(FVector((*At)[0]->AsNumber(), (*At)[1]->AsNumber(), (*At)[2]->AsNumber()), Stack, TNumericLimits<int32>::Max());
			Item.Body.Velocity = FVector::ZeroVector;
		}
	}
	// Particles repeated for a shot: { "name": "minecraft:heart", "at": [x, y, z], "delta": [dx, dy, dz], "speed": 0, "count": 1, "every": 40 }
	// emits the particle every "every" ticks (default 20), starting at once, as /particle would.
	const TArray<TSharedPtr<FJsonValue>>* ParticleValues = nullptr;
	if (Scene->TryGetArrayField(TEXT("particles"), ParticleValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ParticleValues)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			FString Id;
			if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("at"), At) || At->Num() != 3 || !Object->TryGetStringField(TEXT("name"), Id))
			{
				UE_LOG(LogMcClient, Error, TEXT("%s: a particle needs \"at\": [x, y, z] and \"name\""), *Path);
				continue;
			}
			FVector Delta = FVector::ZeroVector;
			const TArray<TSharedPtr<FJsonValue>>* DeltaValues = nullptr;
			if (Object->TryGetArrayField(TEXT("delta"), DeltaValues) && DeltaValues->Num() == 3)
			{
				Delta = FVector((*DeltaValues)[0]->AsNumber(), (*DeltaValues)[1]->AsNumber(), (*DeltaValues)[2]->AsNumber());
			}
			double Speed = 0.0, Count = 1.0, Every = 20.0;
			Object->TryGetNumberField(TEXT("speed"), Speed);
			Object->TryGetNumberField(TEXT("count"), Count);
			Object->TryGetNumberField(TEXT("every"), Every);
			const FVector At3((*At)[0]->AsNumber(), (*At)[1]->AsNumber(), (*At)[2]->AsNumber());
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
				[Name = FName(*Id), At3, Delta, Speed, Count](float)
				{
					FMcSoundBus::Get().OnParticle.Broadcast(Name, At3, Delta, static_cast<float>(Speed), static_cast<int32>(Count));
					return true;
				}), static_cast<float>(FMath::Max(1.0, Every) / 20.0));
		}
	}
	// Banners' patterns: { "at": [x, y, z], "patterns": [{ "pattern": "minecraft:border", "color": "blue" }, ...] }. The banner
	// block is placed by "blocks"; its flag stands once the chunk is there.
	struct FSceneBanner
	{
		FIntVector Pos;
		TArray<McBanner::FLayer> Layers;
	};
	TSharedRef<TArray<FSceneBanner>> Banners = MakeShared<TArray<FSceneBanner>>();
	const TArray<TSharedPtr<FJsonValue>>* BannerValues = nullptr;
	if (Scene->TryGetArrayField(TEXT("banners"), BannerValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *BannerValues)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("at"), At) || At->Num() != 3)
			{
				UE_LOG(LogMcClient, Error, TEXT("%s: a banner needs \"at\": [x, y, z]"), *Path);
				continue;
			}
			FSceneBanner& Banner = Banners->Emplace_GetRef();
			Banner.Pos = ToBlock(*At);
			const TArray<TSharedPtr<FJsonValue>>* Patterns = nullptr;
			if (Object->TryGetArrayField(TEXT("patterns"), Patterns))
			{
				for (const TSharedPtr<FJsonValue>& Pattern : *Patterns)
				{
					const TSharedPtr<FJsonObject> Layer = Pattern->AsObject();
					FString Id, Colour;
					if (Layer.IsValid() && Layer->TryGetStringField(TEXT("pattern"), Id) && Layer->TryGetStringField(TEXT("color"), Colour))
					{
						Banner.Layers.Add({ McCache::PathOf(Id), Colour });
					}
				}
			}
		}
	}
	if (Host && Banners->Num() > 0)
	{
		TSharedRef<FTimerHandle> BannerTimer = MakeShared<FTimerHandle>();
		GetWorldTimerManager().SetTimer(*BannerTimer, FTimerDelegate::CreateWeakLambda(this, [this, Banners, BannerTimer]()
		{
			FMcEntityHost* EntityHost = WorldActor ? WorldActor->GetEntityHost() : nullptr;
			FMcBlockWorld* Blocks = WorldActor ? WorldActor->GetBlockWorld() : nullptr;
			for (int32 Index = Banners->Num() - 1; EntityHost && Blocks && Index >= 0; --Index)
			{
				const FSceneBanner& Banner = (*Banners)[Index];
				if (Blocks->FindChunk(FIntPoint(Banner.Pos.X >> 4, Banner.Pos.Z >> 4)))
				{
					FMcBannerEntity::Stand(EntityHost->GetManager(), Banner.Pos, Banner.Layers);
					Banners->RemoveAt(Index);
				}
			}
			if (Banners->Num() == 0)
			{
				GetWorldTimerManager().ClearTimer(*BannerTimer);
			}
		}), 0.5f, true);
	}
	// Game rules: { "rules": { "spawner_blocks_work": "false" } }.
	const TSharedPtr<FJsonObject>* RuleValues = nullptr;
	if (WorldActor && Scene->TryGetObjectField(TEXT("rules"), RuleValues))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>> Rule : (*RuleValues)->Values)
		{
			WorldActor->SetGameRule(Rule.Key, Rule.Value->AsString());
		}
	}
	// Spawners' mobs: { "at": [x, y, z], "mob": "minecraft:pig" }.
	const TArray<TSharedPtr<FJsonValue>>* SpawnerValues = nullptr;
	if (Scene->TryGetArrayField(TEXT("spawners"), SpawnerValues))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SpawnerValues)
		{
			const TSharedPtr<FJsonObject> Object = Value->AsObject();
			const TArray<TSharedPtr<FJsonValue>>* At = nullptr;
			FString Mob;
			if (!Object.IsValid() || !Object->TryGetArrayField(TEXT("at"), At) || At->Num() != 3 || !Object->TryGetStringField(TEXT("mob"), Mob))
			{
				UE_LOG(LogMcClient, Error, TEXT("%s: a spawner needs \"at\": [x, y, z] and \"mob\""), *Path);
				continue;
			}
			FSceneSign& Spawner = Signs->Emplace_GetRef();
			Spawner.Pos = ToBlock(*At);
			Spawner.Entity = FMcBlockEntity::MakeSpawner(FName(*Mob));
		}
	}
	if (Signs->Num() > 0)
	{
		TSharedRef<FTimerHandle> SignTimer = MakeShared<FTimerHandle>();
		GetWorldTimerManager().SetTimer(*SignTimer, FTimerDelegate::CreateWeakLambda(this, [this, Signs, SignTimer]()
		{
			FMcBlockWorld* Blocks = WorldActor ? WorldActor->GetBlockWorld() : nullptr;
			for (int32 Index = Signs->Num() - 1; Blocks && Index >= 0; --Index)
			{
				const FSceneSign& Sign = (*Signs)[Index];
				if (FMcChunk* Chunk = Blocks->FindChunk(FIntPoint(Sign.Pos.X >> 4, Sign.Pos.Z >> 4)))
				{
					Chunk->SetBlockEntity(Sign.Pos.X & 15, Sign.Pos.Y, Sign.Pos.Z & 15, Sign.Entity);
					WorldActor->MarkChunkDirty(FIntPoint(Sign.Pos.X >> 4, Sign.Pos.Z >> 4));
					Blocks->OnBlockEntityChanged.Broadcast(Sign.Pos);
					if (Sign.Entity.Kind == EMcBlockEntityKind::Lectern)
					{
						const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
						const FString Facing = Registry.GetPropertyValue(Blocks->GetBlock(Sign.Pos), TEXT("facing")).ToString();
						WorldActor->SetBlock(Sign.Pos, Registry.ParseState(TEXT("minecraft:lectern[facing=") + Facing + TEXT(",has_book=true,powered=false]")).Get(0));
					}
					Signs->RemoveAt(Index);
				}
			}
			if (Signs->Num() == 0)
			{
				GetWorldTimerManager().ClearTimer(*SignTimer);
			}
		}), 0.25f, true);
	}

	TArray<FString> CameraParts;
	for (const TSharedPtr<FJsonValue>& Value : Scene->GetArrayField(TEXT("camera")))
	{
		CameraParts.Add(FString::SanitizeFloat(Value->AsNumber()));
	}
	OutCamera = FString::Join(CameraParts, TEXT(","));
	UE_LOG(LogMcClient, Display, TEXT("Scene %s: placed %d blocks"), *Name, Placed);
}

void AMcGameMode::StartDevCapture()
{
#if !UE_BUILD_SHIPPING && MC_WITH_RENDER
	const TCHAR* CommandLine = FCommandLine::Get();

	FString Camera;
	FString SceneName;
	if (FParse::Value(CommandLine, TEXT("-McScene="), SceneName))
	{
		LoadDevScene(SceneName, Camera);
	}
	// -McPillarAfter=<seconds>: builds a gold pillar at 3, 0 (in front of a new player) then, to check block changes reach clients.
	// -McEditY=<y> is the pillar's lowest block (default -60, the flat world's first block above the grass).
	float PillarDelay = 0.0f;
	int32 EditY = -60;
	FParse::Value(CommandLine, TEXT("-McEditY="), EditY);
	if (FParse::Value(CommandLine, TEXT("-McPillarAfter="), PillarDelay))
	{
		FTimerHandle PillarTimer;
		GetWorldTimerManager().SetTimer(PillarTimer, FTimerDelegate::CreateWeakLambda(this, [this, EditY]()
		{
			const FMcStateId Gold = FMcBlockRegistry::Get().FindDefaultState(TEXT("minecraft:gold_block")).Get(0);
			for (int32 Y = EditY; Y <= EditY + 6; ++Y)
			{
				WorldActor->SetBlock(FIntVector(3, Y, 0), Gold);
			}
			UE_LOG(LogMcClient, Display, TEXT("Built the gold pillar"));
		}), PillarDelay, false);
	}

	// -McEdit=x,y,z,<state> (or x1,y1,z1,x2,y2,z2,<state> for a box) places blocks two seconds in, to check that edits
	// survive saving a world (-McWorld): place them on a first run, and look for them on the second.
	FString EditSpec;
	if (FParse::Value(CommandLine, TEXT("-McEdit="), EditSpec, /*bShouldStopOnSeparator=*/ false))
	{
		FTimerHandle EditTimer;
		GetWorldTimerManager().SetTimer(EditTimer, FTimerDelegate::CreateWeakLambda(this, [this, EditSpec]()
		{
			TArray<FString> Parts;
			EditSpec.ParseIntoArray(Parts, TEXT(","));
			if (Parts.Num() != 4 && Parts.Num() != 7)
			{
				UE_LOG(LogMcClient, Error, TEXT("-McEdit wants x,y,z,state or x1,y1,z1,x2,y2,z2,state; got %s"), *EditSpec);
				return;
			}
			const TOptional<FMcStateId> State = FMcBlockRegistry::Get().ParseState(Parts.Last());
			if (!State)
			{
				UE_LOG(LogMcClient, Error, TEXT("-McEdit: unknown block %s"), *Parts.Last());
				return;
			}
			const FIntVector A(FCString::Atoi(*Parts[0]), FCString::Atoi(*Parts[1]), FCString::Atoi(*Parts[2]));
			const FIntVector B = Parts.Num() == 7 ? FIntVector(FCString::Atoi(*Parts[3]), FCString::Atoi(*Parts[4]), FCString::Atoi(*Parts[5])) : A;
			int32 Placed = 0;
			for (int32 Y = FMath::Min(A.Y, B.Y); Y <= FMath::Max(A.Y, B.Y); ++Y)
			{
				for (int32 Z = FMath::Min(A.Z, B.Z); Z <= FMath::Max(A.Z, B.Z); ++Z)
				{
					for (int32 X = FMath::Min(A.X, B.X); X <= FMath::Max(A.X, B.X); ++X)
					{
						WorldActor->SetBlock(FIntVector(X, Y, Z), State.GetValue());
						++Placed;
					}
				}
			}
			UE_LOG(LogMcClient, Display, TEXT("McEdit: placed %d blocks of %s"), Placed, *Parts.Last());
		}), 2.0f, false);
	}

	FParse::Value(CommandLine, TEXT("-McCam="), Camera, /*bShouldStopOnSeparator=*/ false);
	if (!Camera.IsEmpty())
	{
		TArray<FString> Parts;
		Camera.ParseIntoArray(Parts, TEXT(","));
		if (Parts.Num() == 5)
		{
			const FVector Block(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (AMcPlayerPawn* Pawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr)
				{
					Pawn->SetView(Block, FCString::Atof(*Parts[3]), FCString::Atof(*Parts[4]));
				}
			}
		}
		else
		{
			UE_LOG(LogMcClient, Error, TEXT("-McCam wants x,y,z,yaw,pitch; got %s"), *Camera);
		}
	}

#endif
}

void AMcGameMode::RestartPlayer(AController* NewPlayer)
{
	// The title screen has no world to be in.
	if (McLaunch::IsMenu(GetWorld()))
	{
		return;
	}
	// There are no PlayerStart actors: players start above the middle of block 0, 0.
	if (NewPlayer && !NewPlayer->IsPendingKillPending())
	{
		bool bFlying = false;
		bool bAtSpawn = false;
		RestartPlayerAtTransform(NewPlayer, GetStartTransform(NewPlayer, bFlying, bAtSpawn));
		if (AMcPlayerPawn* McPawn = Cast<AMcPlayerPawn>(NewPlayer->GetPawn()))
		{
			if (bAtSpawn)
			{
				McPawn->ServerStandOnSpawnColumn();
			}
			McPawn->SetFlying(bFlying);
			McPawn->SetGameRules(GetGameRules());
			if (!RestoreSurvival(NewPlayer, *McPawn))
			{
				ApplyDefaultGameMode(*McPawn);
			}
		}
		// A player last saved in another dimension goes back there, once it is ready.
		FMcPlayerData Saved;
		AMcPlayerController* Player = Cast<AMcPlayerController>(NewPlayer);
		if (Player && WorldActor && LoadSavedPlayer(NewPlayer, Saved) && !Saved.Dimension.IsEmpty() && Saved.Dimension != WorldActor->Dimension)
		{
			if (UMcTravelSubsystem* Travel = GetWorld()->GetSubsystem<UMcTravelSubsystem>())
			{
				Travel->StartExactTrip(*Player, Saved.Dimension, Saved.Position, Saved.Yaw);
			}
		}
	}
}

void AMcGameMode::RespawnPlayer(AController* Controller)
{
	if (!Controller)
	{
		return;
	}
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Weak = TWeakObjectPtr<AController>(Controller)]()
	{
		AController* Who = Weak.Get();
		if (!Who)
		{
			return;
		}
		// With keep_inventory what they carried, and their experience, goes with them to the new body.
		TArray<FMcItemStack> Kept[3];
		int32 KeptLevel = 0;
		float KeptProgress = 0.0f;
		int32 KeptSlot = 0;
		bool bKeep = false;
		TOptional<FIntVector> Bed;
		TOptional<FIntVector> Point;
		if (const AMcPlayerPawn* Dead = Cast<AMcPlayerPawn>(Who->GetPawn()))
		{
			if (Dead->Data)
			{
				Bed = Dead->Data->SpawnBed;
				Point = Dead->Data->SpawnPoint;
			}
			bKeep = Dead->Data && Dead->GetGameRules().bKeepInventory;
			if (bKeep)
			{
				const TSharedPtr<FMcContainer> Containers[3] = { Dead->Data->Inventory, Dead->Data->Armor, Dead->Data->Offhand };
				for (int32 Index = 0; Index < 3; ++Index)
				{
					for (int32 Slot = 0; Slot < Containers[Index]->Num(); ++Slot)
					{
						Kept[Index].Add(Containers[Index]->Get(Slot));
					}
				}
				KeptLevel = Dead->Data->Stats.XpLevel;
				KeptProgress = Dead->Data->Stats.XpProgress;
				KeptSlot = Dead->Data->SelectedSlot;
			}
		}
		if (APawn* Old = Who->GetPawn())
		{
			Who->UnPossess();
			Old->Destroy();
		}
		// The spawn is in the first world: a player who died in the Nether comes back there.
		AMcPlayerController* Player = Cast<AMcPlayerController>(Who);
		if (Player && WorldActor && Player->GetWorldActor() != WorldActor)
		{
			Player->EnterWorld(*WorldActor);
		}
		// Beside the bed they last slept in, if it is still there and has room beside it (minecraft.wiki: "Respawn").
		FTransform Spawn = GetSpawnTransform();
		bool bBedMissing = false;
		const FMcBlockWorld* Blocks = WorldActor ? WorldActor->GetBlockWorld() : nullptr;
		if (Bed.IsSet() && Blocks)
		{
			const FIntPoint Chunk(McCoords::BlockToChunk(Bed->X), McCoords::BlockToChunk(Bed->Z));
			if (!Blocks->HasChunk(Chunk))
			{
				// Not loaded: they come down on the bed, and the world loads round them.
				Spawn.SetLocation(McCoords::BlockToUnreal(FVector(Bed->X + 0.5, Bed->Y + 1.0, Bed->Z + 0.5)));
			}
			else if (const TOptional<FVector> Stand = McBed::FindStandPosition(*Blocks, Bed.GetValue()))
			{
				Spawn.SetLocation(McCoords::BlockToUnreal(Stand.GetValue()));
			}
			else
			{
				bBedMissing = true;
			}
		}
		if (!Bed.IsSet() && Point.IsSet())
		{
			Spawn.SetLocation(McCoords::BlockToUnreal(FVector(Point->X + 0.5, Point->Y, Point->Z + 0.5)));
		}
		RestartPlayerAtTransform(Who, Spawn);
		if (AMcPlayerPawn* McPawn = Cast<AMcPlayerPawn>(Who->GetPawn()))
		{
			// At the world spawn (no bed that stands, no set point): on the column's top, as a new player.
			if ((!Bed.IsSet() || bBedMissing) && !Point.IsSet() && !WorldSpawnOverride.IsSet() && Blocks && !Blocks->GetDimension().bHasCeiling)
			{
				McPawn->ServerStandOnSpawnColumn();
			}
			McPawn->SetGameRules(GetGameRules());
			if (McPawn->Data)
			{
				McPawn->Data->SpawnBed = bBedMissing ? TOptional<FIntVector>() : Bed;
				if (bBedMissing)
				{
					McPawn->Data->TellOwner(TEXT("Your home bed was missing or obstructed"), 0xFFFFFF);
				}
			}
			ApplyDefaultGameMode(*McPawn);
			if (bKeep && McPawn->Data)
			{
				const TSharedPtr<FMcContainer> Containers[3] = { McPawn->Data->Inventory, McPawn->Data->Armor, McPawn->Data->Offhand };
				for (int32 Index = 0; Index < 3; ++Index)
				{
					for (int32 Slot = 0; Slot < Containers[Index]->Num() && Slot < Kept[Index].Num(); ++Slot)
					{
						Containers[Index]->Set(Slot, Kept[Index][Slot]);
					}
				}
				McPawn->Data->Stats.XpLevel = KeptLevel;
				McPawn->Data->Stats.XpProgress = KeptProgress;
				McPawn->Data->SelectedSlot = KeptSlot;
				McPawn->SelectedSlot = KeptSlot;
			}
		}
	}));
}

FMcGameRules AMcGameMode::GetGameRules() const
{
	if (const AMcWorldActor* Primary = GetPrimaryWorld())
	{
		return Primary->GetGameRules();
	}
	return FMcGameRules();
}

void AMcGameMode::ApplyGameRules()
{
	const FMcGameRules Rules = GetGameRules();
	for (const TObjectPtr<AMcWorldActor>& World : Worlds)
	{
		if (World)
		{
			World->ApplyGameRules(Rules);
		}
	}
	if (UWorld* Game = GetWorld())
	{
		for (FConstPlayerControllerIterator It = Game->GetPlayerControllerIterator(); It; ++It)
		{
			if (AMcPlayerPawn* Pawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr)
			{
				Pawn->SetGameRules(Rules);
			}
		}
	}
}

void AMcGameMode::BlastPlayerNear(const AMcWorldActor& World, const FVector& Position, float Damage, const FVector& Push, const FVector& Center)
{
	AMcPlayerPawn* Nearest = nullptr;
	double NearestDistance = 0.5;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
		if (Player && Player->GetWorldActor() && Player->GetWorldActor() != &World)
		{
			continue;
		}
		AMcPlayerPawn* McPawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
		const double Distance = McPawn ? FVector::Dist(McPawn->GetFeetPosition(), Position) : NearestDistance;
		if (McPawn && Distance < NearestDistance)
		{
			Nearest = McPawn;
			NearestDistance = Distance;
		}
	}
	if (Nearest)
	{
		Nearest->ServerBlast(Damage, Push, Center);
	}
}

void AMcGameMode::RunFightBot(APlayerController* Controller)
{
	// Development: "!fightdragon" plays the End in survival through the real attack calls: it gives the player a sword, a bow and
	// arrows, shoots every pillar crystal from the ground, then hits the dragon with the sword while it perches and shoots it
	// otherwise, until the dragon dies or the player does. Positions are set by /tp (a player's own climbing is not played) and
	// the aim is worked out here; the damage goes through AMcPlayerPawn::ServerSwing and ShootBowAlong, the calls a player's
	// click and bow release end in. Each step is logged ("fightbot: ...").
	struct FBot
	{
		int32 Stage = 0;
		double Time = 0.0;
		double LastLog = 0.0;
		double NextAct = 0.0;
		int32 Shots = 0;
		int32 CrystalsAtStart = -1;
		int32 Swings = 0;
		int32 Retreats = 0;
		bool bLocked = false;
		FVector Locked = FVector::ZeroVector;
	};
	TSharedRef<FBot> Bot = MakeShared<FBot>();
	TWeakObjectPtr<APlayerController> Weak(Controller);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Weak, Bot](float Delta)
	{
		AMcPlayerController* Player = Cast<AMcPlayerController>(Weak.Get());
		AMcPlayerPawn* Pawn = Player ? Cast<AMcPlayerPawn>(Player->GetPawn()) : nullptr;
		AMcWorldActor* World = Player ? const_cast<AMcWorldActor*>(Player->GetWorldActor()) : nullptr;
		FMcEntityHost* Host = World ? World->GetEntityHost() : nullptr;
		if (!Pawn || !Pawn->Data || !Host || !World->GetBlockWorld())
		{
			return true;
		}
		Bot->Time += Delta;
		const FMcBlockRegistry& Reg = FMcBlockRegistry::Get();
		const auto GroundAt = [&](double X, double Z)
		{
			for (int32 Y = 100; Y > 40; --Y)
			{
				if (Reg.GetBlockOf(World->GetBlockWorld()->GetBlock(FIntVector(FMath::FloorToInt32(X), Y, FMath::FloorToInt32(Z)))).Name != FName(TEXT("minecraft:air")))
				{
					return static_cast<double>(Y) + 1.0;
				}
			}
			return 64.0;
		};
		const auto Slot = [&](const TCHAR* Item)
		{
			for (int32 I = 0; I < 9; ++I)
			{
				if (Pawn->Data->Inventory->Get(I).Item == FName(Item))
				{
					return I;
				}
			}
			return -1;
		};
		FMcMob* Dragon = nullptr;
		TArray<FMcMob*> Crystals;
		for (const TUniquePtr<FMcEntity>& Entity : Host->GetManager().GetEntities())
		{
			if (Entity->bRemoved)
			{
				continue;
			}
			if (Entity->GetType() == FName(TEXT("minecraft:ender_dragon")))
			{
				Dragon = static_cast<FMcMob*>(Entity.Get());
			}
			else if (Entity->GetType() == FName(TEXT("minecraft:end_crystal")))
			{
				Crystals.Add(static_cast<FMcMob*>(Entity.Get()));
			}
		}
		const float Health = Pawn->Data->Stats.Health;
		const FString DragonText = Dragon ? FString::Printf(TEXT("%.0f hp"), Dragon->Health) : FString(TEXT("none"));
		if (Bot->Time - Bot->LastLog >= 5.0)
		{
			Bot->LastLog = Bot->Time;
			UE_LOG(LogTemp, Display, TEXT("fightbot: t=%.0f stage %d, player %.1f hp, crystals %d, dragon %s (phase %d), arrows %d, swings %d, retreats %d"), Bot->Time, Bot->Stage,
				Health, Crystals.Num(), *DragonText, Dragon ? Dragon->DragonPhase : -1, Bot->Shots, Bot->Swings, Bot->Retreats);
		}
		if (Bot->Stage > 0 && Bot->Time > 8.0 && Health <= 0.0f)
		{
			UE_LOG(LogTemp, Display, TEXT("fightbot: the player died at t=%.0f (stage %d, dragon %s, crystals %d)"), Bot->Time, Bot->Stage, *DragonText, Crystals.Num());
			return false;
		}
		if (Bot->Time > 900.0)
		{
			UE_LOG(LogTemp, Display, TEXT("fightbot: gave up after 900 s (crystals %d, dragon %s)"), Crystals.Num(), *DragonText);
			return false;
		}
		const auto LookAt = [&](const FVector& From, const FVector& To) { return (To - From).GetSafeNormal(); };
		// The way to point a full-draw bow so the arrow arrives at the target: the arrow flies 3 blocks a tick, loses 1% a tick and falls 0.05 a tick
		// (McArrow.h), so a shot at a distant pillar top has to aim over it. Found by flying the arrow in the head and raising the aim by what it fell short.
		const auto Ballistic = [&](const FVector& From, const FVector& To, bool bLob)
		{
			const FVector Flat(To.X - From.X, 0.0, To.Z - From.Z);
			const double Range = Flat.Size();
			if (Range < 1.0)
			{
				return (To - From).GetSafeNormal();
			}
			const FVector Way = Flat / Range;
			// Where the arrow is, high or low, when it has covered the range (a low value if it never does).
			const auto HeightAt = [&](double Angle)
			{
				FVector Velocity = (Way * FMath::Cos(Angle) + FVector(0.0, FMath::Sin(Angle), 0.0)) * 3.0;
				FVector Position = From;
				double Gone = 0.0;
				for (int32 Tick = 0; Tick < 600; ++Tick)
				{
					Position += Velocity;
					Gone += FVector(Velocity.X, 0.0, Velocity.Z).Size();
					Velocity *= 0.99;
					Velocity.Y -= 0.05;
					if (Gone >= Range)
					{
						return Position.Y;
					}
				}
				return -1.0e9;
			};
			double Angle = FMath::Atan2(To.Y - From.Y, Range);
			if (bLob)
			{
				// The high arc: a shot that comes down on the target clears the pillar's top that a flat one cannot (it would hit the side).
				// Between the flattest angle that reaches and nearly straight up, the height at the range falls as the angle rises.
				double Low = 0.6;
				double High = 1.5;
				if (HeightAt(Low) >= To.Y)
				{
					for (int32 Round = 0; Round < 40; ++Round)
					{
						const double Mid = 0.5 * (Low + High);
						(HeightAt(Mid) >= To.Y ? Low : High) = Mid;
					}
					Angle = 0.5 * (Low + High);
					UE_LOG(LogTemp, Display, TEXT("fightbot: lob aim %.1f degrees, range %.1f, rise %.1f, arrow ends %.1f off"), FMath::RadiansToDegrees(Angle), Range, To.Y - From.Y, HeightAt(Angle) - To.Y);
					return Way * FMath::Cos(Angle) + FVector(0.0, FMath::Sin(Angle), 0.0);
				}
			}
			for (int32 Round = 0; Round < 12; ++Round)
			{
				Angle += FMath::Atan2(To.Y - HeightAt(Angle), Range);
			}
			return Way * FMath::Cos(Angle) + FVector(0.0, FMath::Sin(Angle), 0.0);
		};
		if (Bot->Stage == 0)
		{
			// The kit: what a player could have, from the start of a run.
			if (Bot->Time < 1.0 || (!Dragon && Crystals.IsEmpty()))
			{
				return true;
			}
			// The pumpkin goes on and the eyes go down before the player arrives among the endermen: the first bite came from one angered at the arrival.
			Pawn->Data->Armor->Set(0, FMcItemStack(FName("minecraft:carved_pumpkin"), 1));
			Player->SetControlRotation(FRotator(-80.0, Player->GetControlRotation().Yaw, 0.0));
			HandleChat(Player, FString::Printf(TEXT("/tp @s 2.5 %.1f 20.5"), GroundAt(2.5, 20.5)));
			Bot->Stage = 5;
			Bot->NextAct = Bot->Time + 3.0;
			return true;
		}
		if (Bot->Stage == 5 && Bot->Time >= Bot->NextAct)
		{
			HandleChat(Player, TEXT("/gamemode survival"));
			// What a player would have put on: a full set of diamond armour.
			Pawn->Data->Armor->Set(0, FMcItemStack(FName("minecraft:carved_pumpkin"), 1));
			Pawn->Data->Armor->Set(1, FMcItemStack(FName("minecraft:diamond_chestplate"), 1));
			Pawn->Data->Armor->Set(2, FMcItemStack(FName("minecraft:diamond_leggings"), 1));
			Pawn->Data->Armor->Set(3, FMcItemStack(FName("minecraft:diamond_boots"), 1));
			HandleChat(Player, TEXT("/give @s minecraft:diamond_sword"));
			HandleChat(Player, TEXT("/give @s minecraft:bow"));
			HandleChat(Player, TEXT("/give @s minecraft:arrow 64"));
			HandleChat(Player, TEXT("/give @s minecraft:arrow 64"));
			HandleChat(Player, TEXT("/give @s minecraft:arrow 64"));
			Bot->Stage = 1;
			Bot->NextAct = Bot->Time + 4.0;
			return true;
		}
		if (Bot->Time < Bot->NextAct)
		{
			// Between actions the player looks at its feet: an enderman is angered by being looked in the eyes (that is what killed the first bots).
			Player->SetControlRotation(FRotator(-80.0, Player->GetControlRotation().Yaw, 0.0));
			return true;
		}
		const FVector Feet = Pawn->GetFeetPosition();
		const FVector Eye = Feet + FVector(0.0, 1.62, 0.0);
		if (Bot->Stage == 1)
		{
			// Crystals: from the ground well inside the ring, arrows at the nearest one still standing.
			if (Crystals.IsEmpty())
			{
				UE_LOG(LogTemp, Display, TEXT("fightbot: every crystal is gone at t=%.0f after %d arrows (player %.1f hp)"), Bot->Time, Bot->Shots, Health);
				Bot->Stage = 2;
				return true;
			}
			if (Bot->CrystalsAtStart < 0)
			{
				Bot->CrystalsAtStart = Crystals.Num();
			}
			// One crystal at a time: the one aimed at stays the target until it is gone (the nearest one flips as the player crosses the island).
			FMcMob* Target = nullptr;
			if (Bot->bLocked)
			{
				for (FMcMob* Crystal : Crystals)
				{
					if (FVector::DistSquared(Crystal->Body.Position, Bot->Locked) < 1.0)
					{
						Target = Crystal;
					}
				}
			}
			if (!Target)
			{
				Target = Crystals[0];
				for (FMcMob* Crystal : Crystals)
				{
					if (FVector::DistSquared(Crystal->Body.Position, Feet) < FVector::DistSquared(Target->Body.Position, Feet))
					{
						Target = Crystal;
					}
				}
				Bot->bLocked = true;
				Bot->Locked = Target->Body.Position;
			}
			// From the far side of the island, the long way across, lobbing the arrow onto the crystal: close in, the pillar's own side is in the way.
			const FVector2D Out = FVector2D(Target->Body.Position.X, Target->Body.Position.Z).GetSafeNormal();
			const FVector Spot(-Out.X * 48.0 + 0.5, 0.0, -Out.Y * 48.0 + 0.5);
			if (FVector::Dist2D(FVector(Feet.X, Feet.Z, 0.0), FVector(Spot.X, Spot.Z, 0.0)) > 4.0)
			{
				HandleChat(Player, FString::Printf(TEXT("/tp @s %.1f %.1f %.1f"), Spot.X, GroundAt(Spot.X, Spot.Z), Spot.Z));
				Bot->NextAct = Bot->Time + 1.5;
				return true;
			}
			const int32 Bow = Slot(TEXT("minecraft:bow"));
			if (Bow >= 0)
			{
				Pawn->Data->SelectedSlot = Bow;
			}
			Pawn->ShootBowAlong(Ballistic(Eye, Target->Body.Position + FVector(0.0, 1.0, 0.0), true), 20);
			++Bot->Shots;
			Bot->NextAct = Bot->Time + 1.2;
			return true;
		}
		if (!Dragon)
		{
			UE_LOG(LogTemp, Display, TEXT("fightbot: the dragon is dead at t=%.0f after %d arrows and %d swings (player %.1f hp, %d retreats)"), Bot->Time, Bot->Shots, Bot->Swings, Health, Bot->Retreats);
			HandleChat(Player, TEXT("/tp @s 2 70 0"));
			return false;
		}
		// The dragon: sword while it sits, bow while it flies. A hurt player backs off and heals first.
		if (Health < 7.0f)
		{
			++Bot->Retreats;
			HandleChat(Player, FString::Printf(TEXT("/tp @s 0.5 %.1f 40.5"), GroundAt(0.5, 40.5)));
			Bot->NextAct = Bot->Time + 20.0;
			return true;
		}
		const bool bPerched = Dragon->DragonPhase >= 5 && Dragon->DragonPhase <= 7;
		if (bPerched)
		{
			const FVector Head = Dragon->Body.Position + FVector(0.0, 1.0, 0.0);
			if (FVector::Dist(Feet, Head) > 5.0)
			{
				HandleChat(Player, FString::Printf(TEXT("/tp @s %.1f %.1f %.1f"), Head.X + 3.0, GroundAt(Head.X + 3.0, Head.Z), Head.Z));
				Bot->NextAct = Bot->Time + 0.5;
				return true;
			}
			const int32 Sword = Slot(TEXT("minecraft:diamond_sword"));
			if (Sword >= 0)
			{
				Pawn->Data->SelectedSlot = Sword;
			}
			Pawn->ServerSwing(LookAt(Eye, Head));
			++Bot->Swings;
			Bot->NextAct = Bot->Time + 0.65;
			return true;
		}
		const FVector Spot(0.5, GroundAt(0.5, 30.5), 30.5);
		if (FVector::Dist2D(FVector(Feet.X, Feet.Z, 0.0), FVector(Spot.X, Spot.Z, 0.0)) > 6.0)
		{
			HandleChat(Player, FString::Printf(TEXT("/tp @s %.1f %.1f %.1f"), Spot.X, Spot.Y, Spot.Z));
			Bot->NextAct = Bot->Time + 1.0;
			return true;
		}
		const int32 Bow = Slot(TEXT("minecraft:bow"));
		if (Bow >= 0)
		{
			Pawn->Data->SelectedSlot = Bow;
		}
		Pawn->ShootBowAlong(Ballistic(Eye, Dragon->Body.Position, false), 20);
		++Bot->Shots;
		Bot->NextAct = Bot->Time + 1.2;
		return true;
	}), 0.25f);
}

void AMcGameMode::TickVillages()
{
	for (AMcWorldActor* World : Worlds)
	{
		FMcEntityHost* Host = World ? World->GetEntityHost() : nullptr;
		if (!Host)
		{
			continue;
		}
		// The End's fight starts when its main island has loaded: ten crystals on the pillars (positions read from vanilla, dragon8.txt)
		// and the dragon, unless one has been killed in this game already (the kill is not saved yet). Where in the sky the dragon
		// first appears is a guess.
		if (World->Dimension == TEXT("minecraft:the_end") && !Host->GetManager().bEndFightStarted && World->GetBlockWorld())
		{
			const FMcBlockRegistry& Reg = FMcBlockRegistry::Get();
			bool bLoaded = false;
			for (int32 Y = 45; Y <= 70 && !bLoaded; ++Y)
			{
				bLoaded = Reg.GetBlockOf(World->GetBlockWorld()->GetBlock(FIntVector(0, Y, 0))).Name != FName(TEXT("minecraft:air"));
			}
			if (bLoaded)
			{
				Host->GetManager().bEndFightStarted = true;
				if (!Host->GetManager().bDragonKilledBefore)
				{
					for (const FVector& Crystal : FMcMob::GetPillarCrystalPositions())
					{
						Host->SpawnMob(FName(TEXT("minecraft:end_crystal")), Crystal);
					}
					FMcMob::BuildExitPortal(Host->GetManager(), false, false);
					Host->SpawnMob(FName(TEXT("minecraft:ender_dragon")), FVector(0.5, 100.0, 0.5));
					UE_LOG(LogTemp, Display, TEXT("The End: dragon and ten crystals started"));
				}
			}
		}
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
			if (Player && Player->GetWorldActor() && Player->GetWorldActor() != World)
			{
				continue;
			}
			AMcPlayerPawn* Pawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
			if (!Pawn || !Pawn->Data)
			{
				continue;
			}
			FMcPlayerStats& Stats = Pawn->Data->Stats;
			const FVector Feet = Pawn->GetFeetPosition();
			const FMcStatusEffect* Omen = McEffects::Find(Stats, TEXT("bad_omen"));
			FVector Center;
			if (Omen && Stats.Health > 0.0f && Host->FindVillage(Feet, Center))
			{
				const int32 Level = Omen->Amplifier + 1;
				// The raid comes after the omen has run its 30 seconds.
				if (Host->StartRaid(Center, Level, 600) != 0)
				{
					McEffects::Remove(Stats, TEXT("bad_omen"));
					McEffects::Add(Stats, TEXT("raid_omen"), Level - 1, 600);
				}
			}
			FString Title;
			float Fraction = 0.0f;
			if (Host->GetRaidBar(Feet, Title, Fraction))
			{
				Pawn->Data->SetRaidBar(Fraction, Title);
			}
			else
			{
				Pawn->Data->SetRaidBar(-1.0f, FString());
			}
			FString Bars = McCommands::BossbarLines(GetPlayerName(It->Get()));
			// The ender dragon's bar (wiki: light purple, shown to players within 192 blocks of (0, 128, 0); the colour name is unverified).
			for (const TUniquePtr<FMcEntity>& Entity : Host->GetManager().GetEntities())
			{
				if (!Entity->bRemoved && Entity->GetType() == FName(TEXT("minecraft:ender_dragon")))
				{
					const FMcMob& Dragon = static_cast<const FMcMob&>(*Entity);
					if (FVector::DistSquared(Feet, FVector(0.0, 128.0, 0.0)) < 192.0 * 192.0)
					{
						Bars += FString::Printf(TEXT("Ender Dragon\t%.4f\tpink\tprogress\n"), FMath::Clamp(Dragon.Health / Dragon.Type.MaxHealth, 0.0f, 1.0f));
					}
				}
			}
			Pawn->Data->SetCustomBars(Bars);
		}
	}
}

void AMcGameMode::HonourHeroes(const AMcWorldActor& World, const FVector& Center, int32 Level)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
		if (Player && Player->GetWorldActor() && Player->GetWorldActor() != &World)
		{
			continue;
		}
		AMcPlayerPawn* Pawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
		if (Pawn && Pawn->Data && FVector::Dist(Pawn->GetFeetPosition(), Center) <= FMcEntityHost::RaidBarRange)
		{
			// 40 minutes, a level for each level of omen past the first.
			McEffects::Add(Pawn->Data->Stats, TEXT("hero_of_the_village"), FMath::Max(0, Level - 1), 48000);
		}
	}
}

void AMcGameMode::EffectPlayerNear(const AMcWorldActor& World, const FVector& Position, FName Effect, int32 Amplifier, int32 Duration)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
		if (Player && Player->GetWorldActor() && Player->GetWorldActor() != &World)
		{
			continue;
		}
		AMcPlayerPawn* McPawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
		if (McPawn && McPawn->Data && FVector::Dist(McPawn->GetFeetPosition(), Position) < 0.5)
		{
			McEffects::Add(McPawn->Data->Stats, Effect, Amplifier, Duration);
		}
	}
}

void AMcGameMode::HurtPlayerNear(const AMcWorldActor& World, const FVector& Position, float Damage, McSurvival::EDamage Kind, FName Attacker)
{
	AMcPlayerPawn* Nearest = nullptr;
	double NearestDistance = 2.5;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		// Only the players in the dimension the mob is in.
		const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
		if (Player && Player->GetWorldActor() && Player->GetWorldActor() != &World)
		{
			continue;
		}
		AMcPlayerPawn* McPawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
		const double Distance = McPawn ? FVector::Dist(McPawn->GetFeetPosition(), Position) : NearestDistance;
		if (McPawn && Distance < NearestDistance)
		{
			Nearest = McPawn;
			NearestDistance = Distance;
		}
	}
	if (Nearest)
	{
		// The mob that bit is at Position: the player is knocked away from it.
		Nearest->ServerHurt(Damage, Kind, &Position, true, Attacker);
	}
}

void AMcGameMode::GiveConduitPower(const AMcWorldActor& World, const FVector& Middle, int32 Range)
{
	const FMcSimulation* Sim = World.GetSimulation();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMcPlayerController* Player = Cast<AMcPlayerController>(It->Get());
		if (Player && Player->GetWorldActor() && Player->GetWorldActor() != &World)
		{
			continue;
		}
		AMcPlayerPawn* McPawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
		if (!McPawn || !McPawn->Data || FVector::DistSquared(McPawn->GetFeetPosition(), Middle) > FMath::Square(double(Range)))
		{
			continue;
		}
		// Only players in water or out in the rain (minecraft.wiki).
		const FVector Feet = McPawn->GetFeetPosition();
		const FIntVector Block(FMath::FloorToInt32(Feet.X), FMath::FloorToInt32(Feet.Y), FMath::FloorToInt32(Feet.Z));
		if (McPawn->GetBody().bInWater || (Sim && Sim->IsRainingAt(Block)))
		{
			McEffects::Add(McPawn->Data->Stats, TEXT("conduit_power"), 0, FMcSimulation::ConduitPowerTicks);
		}
	}
}

bool AMcGameMode::RestoreSurvival(const AController* Controller, AMcPlayerPawn& Pawn) const
{
	FMcPlayerData Saved;
	if (Pawn.Data && WorldActor && WorldActor->HasStorage() && McWorldSave::LoadPlayer(WorldActor->GetWorldDir(), GetSaveId(Controller), Saved)
		&& McPlayerSave::Apply(Saved, *Pawn.Data))
	{
		Pawn.SelectedSlot = Pawn.Data->SelectedSlot;
		UE_LOG(LogMcClient, Log, TEXT("Player restored: %s, %s, hotbar slot %d"), *GetSaveId(Controller), Pawn.Data->bCreative ? TEXT("creative") : TEXT("survival"), Pawn.Data->SelectedSlot);
		return true;
	}
	return false;
}

void AMcGameMode::ApplyTickSpeed()
{
	// The world's clock runs at Rate/20 of normal; frozen, as slow as the engine lets it (a few steps of ticks run through it).
	if (AWorldSettings* Settings = GetWorld() ? GetWorld()->GetWorldSettings() : nullptr)
	{
		Settings->SetTimeDilation(bTickFrozen ? 0.0001f : float(TickRate / 20.0));
	}
}

void AMcGameMode::SetTickRate(double Rate)
{
	TickRate = Rate;
	ApplyTickSpeed();
}

void AMcGameMode::SetTickFrozen(bool bFrozen)
{
	bTickFrozen = bFrozen;
	ApplyTickSpeed();
}

void AMcGameMode::StepTicks(int32 Count)
{
	// Unfreezes for Count ticks of game time (real time at the target rate), then freezes again.
	bTickFrozen = false;
	ApplyTickSpeed();
	GetWorldTimerManager().SetTimer(StepTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { SetTickFrozen(true); }), float(Count) / 20.0f, false);
}

void AMcGameMode::SetDefaultGameMode(const FString& NewMode)
{
	// Kept for the session (not written to the world).
	DefaultGameModeOverride = NewMode;
}

void AMcGameMode::ApplyDefaultGameMode(AMcPlayerPawn& Pawn) const
{
	if (!Pawn.Data || Pawn.Data->bCreative)
	{
		// Already creative (-McCreative).
		return;
	}
	// A dedicated server's gamemode setting is for everyone new, whatever the world was made with.
	const FString Mode = !DefaultGameModeOverride.IsEmpty() ? DefaultGameModeOverride : GetNetMode() == NM_DedicatedServer ? FMcServerProperties::Get().GetString(TEXT("gamemode"))
		: WorldActor ? WorldActor->GetLevel().GameMode : FString();
	if (McLaunch::IsCreativeMode(Mode))
	{
		Pawn.Data->bCreative = true;
		Pawn.Data->FillCreativeHotbar();
	}
}

void AMcGameMode::PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty() || GetNetMode() != NM_DedicatedServer)
	{
		return;
	}
	// Read afresh, so an edited allowlist or ops file applies to the next player to join.
	const FString Name = UGameplayStatics::ParseOption(Options, TEXT("Name"));
	FString Ip = Address;
	Ip.Split(TEXT(":"), &Ip, nullptr);
	ErrorMessage = FMcServerAccess::CheckJoin(FMcServerProperties::Get(), FMcServerAccess::Load(), Name, GetNumPlayers(), Ip);
	if (!ErrorMessage.IsEmpty())
	{
		UE_LOG(LogMcClient, Log, TEXT("%s (%s) can't join: %s"), *Name, *Address, *ErrorMessage);
	}
}

FTransform AMcGameMode::GetStartTransform(const AController* Controller, bool& bOutFlying, bool& bOutAtSpawn) const
{
	bOutFlying = false;
	bOutAtSpawn = false;
	FMcPlayerData Saved;
	const FMcBlockWorld* Blocks = WorldActor ? WorldActor->GetBlockWorld() : nullptr;
	if (Blocks && WorldActor->HasStorage() && McWorldSave::LoadPlayer(WorldActor->GetWorldDir(), GetSaveId(Controller), Saved))
	{
		const FMcDimensionType& Dimension = Blocks->GetDimension();
		// A player saved in another dimension, or at a height this one doesn't have (the file was edited, or the world
		// changed), starts at the spawn rather than outside the world.
		if (Saved.Dimension == WorldActor->Dimension && Saved.Position.Y >= Dimension.MinY && Saved.Position.Y <= Dimension.MinY + Dimension.Height + 64)
		{
			bOutFlying = Saved.bFlying;
			// The pawn takes its look from the transform; see AMcPlayerPawn::SetView for the offsets.
			return FTransform(FRotator(-Saved.Pitch, Saved.Yaw + 90.0f, 0.0f), McCoords::BlockToUnreal(Saved.Position));
		}
	}
	bOutAtSpawn = Blocks && !WorldSpawnOverride.IsSet() && !Blocks->GetDimension().bHasCeiling;
	return GetSpawnTransform();
}

bool AMcGameMode::LoadSavedPlayer(const AController* Controller, FMcPlayerData& OutSaved) const
{
	return WorldActor && WorldActor->HasStorage() && McWorldSave::LoadPlayer(WorldActor->GetWorldDir(), GetSaveId(Controller), OutSaved);
}

FString AMcGameMode::GetSaveId(const AController* Controller) const
{
	if (const APlayerState* State = Controller ? Controller->PlayerState.Get() : nullptr)
	{
		// By name for now; a platform account id replaces it when sign-in exists.
		if (!State->GetPlayerName().IsEmpty())
		{
			return State->GetPlayerName();
		}
	}
	return TEXT("player");
}

void AMcGameMode::SavePlayer(const AController* Controller) const
{
	const APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
	if (!Pawn || !WorldActor || !WorldActor->HasStorage())
	{
		return;
	}
	FMcPlayerData Data;
	Data.Position = McCoords::UnrealToBlock(Pawn->GetActorLocation());
	const AMcPlayerController* McController = Cast<AMcPlayerController>(Controller);
	Data.Dimension = McController && McController->GetWorldActor() ? McController->GetWorldActor()->Dimension : WorldActor->Dimension;
	if (const AMcPlayerPawn* McPawn = Cast<AMcPlayerPawn>(Pawn))
	{
		Data.Yaw = McPawn->GetLookYaw();
		Data.Pitch = McPawn->GetLookPitch();
		Data.bFlying = McPawn->IsFlying();
		if (McPawn->Data)
		{
			McPlayerSave::Capture(*McPawn->Data, Data);
		}
	}
	UE_LOG(LogMcClient, Log, TEXT("Player saved: %s, %s, hotbar slot %d"), *GetSaveId(Controller), Data.bCreative ? TEXT("creative") : TEXT("survival"), Data.SelectedSlot);
	McWorldSave::SavePlayer(WorldActor->GetWorldDir(), GetSaveId(Controller), Data);
}

void AMcGameMode::SavePlayers() const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		SavePlayer(It->Get());
	}
}

void AMcGameMode::Logout(AController* Exiting)
{
	SavePlayer(Exiting);
	const FString Name = GetPlayerName(Exiting);
	Super::Logout(Exiting);
	if (!Name.IsEmpty() && GetNetMode() != NM_Standalone)
	{
		BroadcastChat(McLang::Format(TEXT("multiplayer.player.left"), { Name }), 0xFFFF55);
	}
}

void AMcGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	// A world played alone says nothing; anywhere others can join, everyone is told (in yellow, as in the game).
	if (GetNetMode() != NM_Standalone)
	{
		BroadcastChat(McLang::Format(TEXT("multiplayer.player.joined"), { GetPlayerName(NewPlayer) }), 0xFFFF55);
	}

	// Development: -McChat="<line>|<line>" sends these lines as the player's chat from 8 seconds in, half a second apart (a
	// command script for a shot or sound run, like the reference client's "cmd").
	FString Lines;
	// A line "!respawn" presses Respawn on the death screen.
	// -McChatFile=<path> reads the lines from a file, one a line, for text a command line can't carry (JSON with quotes).
	FString ChatFile;
	const bool bFromFile = FParse::Value(FCommandLine::Get(), TEXT("-McChatFile="), ChatFile) && FFileHelper::LoadFileToString(Lines, *ChatFile);
	if (bFromFile || FParse::Value(FCommandLine::Get(), TEXT("-McChat="), Lines, /*bShouldStopOnSeparator=*/false))
	{
		TArray<FString> Parts;
		Lines.ParseIntoArray(Parts, bFromFile ? TEXT("\n") : TEXT("|"));
		for (FString& Part : Parts)
		{
			Part.TrimEndInline();
		}
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
				[this, Player = TWeakObjectPtr<APlayerController>(NewPlayer), Line = Parts[Index]](float)
				{
					if (Player.IsValid() && Line == TEXT("!respawn"))
					{
						// A line "!respawn" presses the death screen's Respawn (a shot script can't click it).
						RespawnPlayer(Player.Get());
					}
					else if (Player.IsValid() && Line == TEXT("!showdragon"))
					{
						// Development: a dragon with no AI 16 blocks in front of this player (yaw 0, towards +z), 3 up, in the world the player is in,
						// for a close look at the model. Waits (up to a minute) for the pawn and the world to exist.
						FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Player, Tries = MakeShared<int32>(0)](float)
						{
							AMcPlayerController* McPlayer = Cast<AMcPlayerController>(Player.Get());
							AMcPlayerPawn* Pawn = McPlayer ? Cast<AMcPlayerPawn>(McPlayer->GetPawn()) : nullptr;
							AMcWorldActor* World = McPlayer ? const_cast<AMcWorldActor*>(McPlayer->GetWorldActor()) : nullptr;
							FMcEntityHost* Host = World ? World->GetEntityHost() : nullptr;
							if (!Pawn || !Host)
							{
								return ++(*Tries) < 60;
							}
							const FVector Feet = Pawn->GetFeetPosition();
							const FVector At(Feet.X, Feet.Y + 3.0, Feet.Z + 16.0);
							FMcMob* Shown = Host->SpawnMob(FName(TEXT("minecraft:ender_dragon")), At);
							if (Shown)
							{
								Shown->bNoAI = true;
							}
							UE_LOG(LogTemp, Display, TEXT("showdragon: spawned %s at %s"), Shown ? TEXT("a dragon") : TEXT("nothing"), *At.ToString());
							return false;
						}), 1.0f);
					}
					else if (Player.IsValid() && Line == TEXT("!fightdragon"))
					{
						RunFightBot(Player.Get());
					}
					else if (Player.IsValid() && (Line == TEXT("!killdragon") || Line == TEXT("!freezedragon")))
					{
						// Development: "!killdragon" waits (by game state, since a slow world load makes timed lines all fire at once)
						// for the ender dragon, deals it 1000 damage (no command reaches mobs), waits for its death to end, then drops
						// this player from (0, 72, 0) onto the exit portal: a run script can see the death, the portal and the credits
						// without winning the fight by hand.
						struct FRun { int32 Stage = 0; int32 Wait = 0; };
						UE_LOG(LogTemp, Display, TEXT("killdragon: armed, %d worlds"), Worlds.Num());
						FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this,
							[this, Player, bFreeze = (Line == TEXT("!freezedragon")), Run = MakeShared<FRun>()](float)
							{
								if (!Player.IsValid())
								{
									return false;
								}
								int32 Seen = 0;
								FMcMob* Dragon = nullptr;
								for (AMcWorldActor* World : Worlds)
								{
									FMcEntityHost* Host = World ? World->GetEntityHost() : nullptr;
									if (!Host)
									{
										continue;
									}
									for (const TUniquePtr<FMcEntity>& Entity : Host->GetManager().GetEntities())
									{
										if (!Entity->bRemoved && Entity->GetType() == FName(TEXT("minecraft:ender_dragon")))
										{
											++Seen;
											Dragon = static_cast<FMcMob*>(Entity.Get());
										}
									}
								}
								UE_LOG(LogTemp, Display, TEXT("killdragon: stage %d, dragons %d"), Run->Stage, Seen);
								if (Run->Stage == 0 && Dragon)
								{
									if (bFreeze)
									{
										// "!freezedragon": hold the dragon still (no AI) at (0.5, 66, 0.5) and put this player at -McKillTp, for a shot of
										// the model.
										Dragon->bNoAI = true;
										Dragon->Body.Position = FVector(0.5, 66.0, 0.5);
										Dragon->Body.Velocity = FVector::ZeroVector;
										FString Spot;
										FParse::Value(FCommandLine::Get(), TEXT("-McKillTp="), Spot, false);
										HandleChat(Player.Get(), TEXT("/tp @s ") + (Spot.IsEmpty() ? FString(TEXT("0.5 66 28.5")) : Spot));
										return false;
									}
									Dragon->Damage(1000.0f, nullptr);
									UE_LOG(LogTemp, Display, TEXT("killdragon: dragon found (%.0f hp), damaged"), Dragon->Health);
									Run->Stage = 1;
								}
								else if (Run->Stage == 1 && !Dragon)
								{
									UE_LOG(LogTemp, Display, TEXT("killdragon: dragon gone, dropping the player on the portal in 3 s"));
									Run->Stage = 2;
									Run->Wait = 0;
								}
								else if (Run->Stage == 2 && ++Run->Wait >= 3)
								{
									FString Spot;
									FParse::Value(FCommandLine::Get(), TEXT("-McKillTp="), Spot, false);
									HandleChat(Player.Get(), TEXT("/tp @s ") + (Spot.IsEmpty() ? FString(TEXT("2 70 0")) : Spot));
									Run->Stage = 3;
									Run->Wait = 0;
								}
								else if (Run->Stage == 3 && ++Run->Wait >= 4)
								{
									// Where the player is and what stands at the middle, for the run's log.
									const AMcPlayerPawn* Pawn = Player.IsValid() ? Cast<AMcPlayerPawn>(Player->GetPawn()) : nullptr;
									UE_LOG(LogTemp, Display, TEXT("killdragon: player at %s"), Pawn ? *Pawn->GetFeetPosition().ToString() : TEXT("none"));
									for (AMcWorldActor* World : Worlds)
									{
										if (World && World->GetBlockWorld())
										{
											FString Column;
											for (int32 Y = 58; Y <= 67; ++Y)
											{
												Column += FString::Printf(TEXT(" %d:%s"), Y, *FMcBlockRegistry::Get().GetBlockOf(World->GetBlockWorld()->GetBlock(FIntVector(0, Y, 0))).Name.ToString());
											}
											UE_LOG(LogTemp, Display, TEXT("killdragon: column at 0,0 (%s):%s"), *World->Dimension, *Column);
										}
									}
									return false;
								}
								return true;
							}), 1.0f);
					}
					else if (Player.IsValid())
					{
						HandleChat(Player.Get(), Line);
					}
					return false;
				}), 8.0f + 0.5f * Index);
		}
	}
}

// ---- Chat ----

namespace
{
	// What commands do, done to this game's players and worlds.
	class FGameCommandTarget : public IMcCommandTarget
	{
	public:
		explicit FGameCommandTarget(AMcGameMode& InMode) : Mode(InMode) {}

		AMcPlayerPawn* FindPawn(const FString& Player) const
		{
			const APlayerController* Controller = Mode.FindPlayer(Player);
			return Controller ? Cast<AMcPlayerPawn>(Controller->GetPawn()) : nullptr;
		}

		virtual FString GetWorldFolder() const override
		{
			const AMcWorldActor* Overworld = Mode.FindWorld(TEXT("minecraft:overworld"));
			return Overworld ? Overworld->GetWorldDir() : FString();
		}
		virtual TArray<FString> GetPlayerNames() const override
		{
			TArray<FString> Names;
			for (FConstPlayerControllerIterator It = Mode.GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				const FString Name = AMcGameMode::GetPlayerName(It->Get());
				if (!Name.IsEmpty())
				{
					Names.Add(Name);
				}
			}
			return Names;
		}
		virtual int32 GetMaxPlayers() const override
		{
			return Mode.GetNetMode() == NM_DedicatedServer ? FMcServerProperties::Get().GetInt(TEXT("max-players"), 20) : 8;
		}
		virtual bool IsDedicated() const override { return Mode.GetNetMode() == NM_DedicatedServer; }
		virtual void SendTo(const FString& Player, const FMcCommandReply& Line) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (Pawn && Pawn->Data)
			{
				Pawn->Data->TellOwner(Line.Text, Line.bError ? 0xFF5555 : 0xFFFFFF);
			}
			else if (Player.IsEmpty())
			{
				UE_LOG(LogMcClient, Log, TEXT("%s"), *Line.Text);
			}
		}
		virtual void Broadcast(const FString& Line) override { Mode.BroadcastChat(Line); }
		virtual void SendTitle(const FString& Player, int32 Kind, const FString& Text, int32 FadeIn, int32 Stay, int32 FadeOut) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (Pawn && Pawn->Data)
			{
				Pawn->Data->ServerTitle(static_cast<UMcPlayerDataComponent::ETitleKind>(Kind), Text, FadeIn, Stay, FadeOut);
			}
		}

		virtual bool IsCreative(const FString& Player) const override
		{
			const AMcPlayerPawn* Pawn = FindPawn(Player);
			return Pawn && Pawn->Data && Pawn->Data->bCreative;
		}
		virtual void SetCreative(const FString& Player, bool bCreative) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (Pawn && Pawn->Data)
			{
				Pawn->Data->bCreative = bCreative;
			}
		}
		virtual bool IsSpectator(const FString& Player) const override { return Mode.Spectators.Contains(Player); }
		virtual void SetSpectator(const FString& Player, bool bSpectator) override
		{
			if (bSpectator)
			{
				Mode.Spectators.Add(Player);
				if (AMcPlayerPawn* Pawn = FindPawn(Player))
				{
					Pawn->SetFlying(true);
				}
			}
			else
			{
				Mode.Spectators.Remove(Player);
				Mode.Spectated.Remove(Player);
			}
		}
		virtual void Spectate(const FString& Player, int32 Entity, const FString& OtherPlayer) override
		{
			if (Entity == 0 && OtherPlayer.IsEmpty())
			{
				Mode.Spectated.Remove(Player);
				return;
			}
			AMcGameMode::FSpectated& Watched = Mode.Spectated.FindOrAdd(Player);
			Watched.Entity = Entity;
			Watched.Player = OtherPlayer;
		}
		virtual bool GetPosition(const FString& Player, FVector& OutFeet) const override
		{
			const AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn)
			{
				return false;
			}
			OutFeet = Pawn->GetFeetPosition();
			return true;
		}
		virtual void Teleport(const FString& Player, const FVector& Feet) override
		{
			if (AMcPlayerPawn* Pawn = FindPawn(Player))
			{
				Pawn->ServerTeleport(Feet, Pawn->GetLookYaw());
			}
		}
		virtual void Kill(const FString& Player) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data || Pawn->IsServerDead())
			{
				return;
			}
			// /kill reaches creative players too, which nothing else hurts.
			const bool bCreative = Pawn->Data->bCreative;
			Pawn->Data->bCreative = false;
			Pawn->ServerHurt(1.0e9f, McSurvival::EDamage::Kill);
			Pawn->Data->bCreative = bCreative;
		}
		virtual void Kick(const FString& Player, const FString& Reason) override
		{
			APlayerController* Controller = Mode.FindPlayer(Player);
			if (Controller && Mode.GameSession)
			{
				Mode.GameSession->KickPlayer(Controller, FText::FromString(Reason));
			}
		}
		virtual int32 GetMaxStackSize(FName Item) const override
		{
			FMcItemRegistry& Items = FMcItemRegistry::Get();
			return Items.EnsureLoaded() && Items.Exists(Item) ? Items.GetMaxStackSize(Item) : 0;
		}
		virtual FString GetItemName(FName Item) const override
		{
			const FString Path = McCache::PathOf(Item.ToString());
			const FString AsItem = McLang::Get(TEXT("item.minecraft.") + Path);
			return AsItem.StartsWith(TEXT("item.")) ? McLang::Get(TEXT("block.minecraft.") + Path) : AsItem;
		}
		virtual void Give(const FString& Player, FName Item, int32 Count) override
		{
			GiveWith(Player, Item, Count, {});
		}
		virtual void GiveWith(const FString& Player, FName Item, int32 Count, const TMap<FName, FString>& Components) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data)
			{
				return;
			}
			const int32 MaxStack = FMath::Max(1, GetMaxStackSize(Item));
			while (Count > 0)
			{
				const int32 Now = FMath::Min(Count, MaxStack);
				Count -= Now;
				// What doesn't fit falls at the player's feet.
				FMcItemStack Stack(Item, Now);
				for (const TPair<FName, FString>& Component : Components)
				{
					const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TEXT("{\"v\":") + Component.Value.Replace(TEXT("'"), TEXT("\"")) + TEXT("}"));
					TSharedPtr<FJsonObject> Wrapper;
					if (FJsonSerializer::Deserialize(Reader, Wrapper) && Wrapper.IsValid())
					{
						Stack.Set(Component.Key, Wrapper->TryGetField(TEXT("v")));
					}
				}
				const FMcItemStack Left = Pawn->Data->Inventory->AddItem(Stack);
				if (!Left.IsEmpty() && Pawn->Data->ServerDrop)
				{
					Pawn->Data->ServerDrop(Left);
				}
			}
		}

		virtual double GetDayTime() const override { return Mode.GetPrimaryWorld() ? Mode.GetPrimaryWorld()->GetDayTime() : 0.0; }
		virtual void SetDayTime(double Ticks) override
		{
			if (AMcWorldActor* World = Mode.GetPrimaryWorld())
			{
				World->SetDayTime(Ticks);
			}
		}
		virtual double GetGameTime() const override { return Mode.GetPrimaryWorld() ? static_cast<double>(Mode.GetPrimaryWorld()->GetGameTime()) : 0.0; }
		virtual void SetWeather(EMcWeather Weather, int32 DurationTicks) override
		{
			if (AMcWorldActor* World = Mode.GetPrimaryWorld())
			{
				World->SetWeather(Weather, DurationTicks);
			}
		}
		virtual int64 GetSeed() const override { return Mode.GetPrimaryWorld() ? Mode.GetPrimaryWorld()->GetLevel().Seed : 0; }
		virtual int32 GetDifficulty() const override { return Mode.GetPrimaryWorld() ? Mode.GetPrimaryWorld()->GetLevel().Difficulty : 2; }
		virtual void SetDifficulty(int32 Difficulty) override
		{
			if (AMcWorldActor* World = Mode.GetPrimaryWorld())
			{
				World->SetDifficulty(Difficulty);
			}
			Mode.ApplyGameRules();
		}
		virtual FString GetGameRule(const FString& Rule) const override
		{
			const FString* Value = Mode.GetPrimaryWorld() ? Mode.GetPrimaryWorld()->GetLevel().GameRules.Find(Rule) : nullptr;
			return Value ? *Value : FString();
		}
		virtual void SetGameRule(const FString& Rule, const FString& Value) override
		{
			if (AMcWorldActor* World = Mode.GetPrimaryWorld())
			{
				World->SetGameRule(Rule, Value);
			}
			Mode.ApplyGameRules();
		}

		virtual bool GetBlock(const FIntVector& Pos, FMcStateId& OutState) const override
		{
			const AMcWorldActor* World = Mode.GetPrimaryWorld();
			const FMcBlockWorld* Blocks = World ? World->GetBlockWorld() : nullptr;
			if (!Blocks || !Blocks->HasChunk(FIntPoint(FMath::FloorToInt(Pos.X / 16.0), FMath::FloorToInt(Pos.Z / 16.0)))
				|| Pos.Y < Blocks->GetDimension().MinY || Pos.Y >= Blocks->GetDimension().MinY + Blocks->GetDimension().Height)
			{
				return false;
			}
			OutState = Blocks->GetBlock(Pos);
			return true;
		}
		virtual int32 PlaceFeature(const FString& Id, const FIntVector& Pos) override
		{
			AMcWorldActor* World = Mode.GetPrimaryWorld();
			return World ? World->PlaceFeature(Id, Pos) : 2;
		}
		virtual void SetBlock(const FIntVector& Pos, FMcStateId State) override
		{
			if (AMcWorldActor* World = Mode.GetPrimaryWorld())
			{
				World->SetBlock(Pos, State);
			}
		}
		virtual void StopSounds(const FString& Player, const FString& Source, const FString& Sound) override
		{
			// This machine's own sounds (a player on another machine hears theirs).
			if (UGameInstance* Instance = Mode.GetGameInstance())
			{
				if (UMcAudioSubsystem* Audio = Instance->GetSubsystem<UMcAudioSubsystem>())
				{
					FString Name = Sound;
					Name.RemoveFromStart(TEXT("minecraft:"));
					Audio->StopSounds(Source, Name.IsEmpty() ? NAME_None : FName(*Name));
				}
			}
		}
		virtual double GetTickRate() const override { return Mode.TickRate; }
		virtual void SetTickRate(double Rate) override { Mode.SetTickRate(Rate); }
		virtual bool IsTickFrozen() const override { return Mode.bTickFrozen; }
		virtual void SetTickFrozen(bool bFrozen) override { Mode.SetTickFrozen(bFrozen); }
		virtual void StepTicks(int32 Count) override { Mode.StepTicks(Count); }
		virtual double GetAverageTickMs() const override { return Mode.GetWorld() ? Mode.GetWorld()->GetDeltaSeconds() * 1000.0 / FMath::Max(0.0001, double(Mode.GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation())) : 0.0; }
		virtual bool FindBiome(uint8 Biome, const FVector& From, FIntVector& OutPos) const override
		{
			const AMcWorldActor* World = Mode.GetPrimaryWorld();
			const FMcBlockWorld* Blocks = World ? World->GetBlockWorld() : nullptr;
			if (!Blocks)
			{
				return false;
			}
			double Best = TNumericLimits<double>::Max();
			for (const TPair<FIntPoint, TUniquePtr<FMcChunk>>& Pair : Blocks->GetChunks())
			{
				// A sample every 4 blocks (a biome cell) at the height asked from.
				for (int32 LX = 0; LX < 16; LX += 4)
				{
					for (int32 LZ = 0; LZ < 16; LZ += 4)
					{
						const int32 X = Pair.Key.X * 16 + LX + 2;
						const int32 Z = Pair.Key.Y * 16 + LZ + 2;
						const double Distance = FMath::Square(X - From.X) + FMath::Square(Z - From.Z);
						if (Distance < Best && Pair.Value->GetBiome(LX, FMath::FloorToInt32(From.Y), LZ) == Biome)
						{
							Best = Distance;
							OutPos = FIntVector(X, FMath::FloorToInt32(From.Y), Z);
						}
					}
				}
			}
			return Best < TNumericLimits<double>::Max();
		}
		virtual void SwingArm(const FString& Player) override
		{
			if (AMcPlayerPawn* Pawn = FindPawn(Player))
			{
				Pawn->SwingArm();
			}
		}
		virtual bool GetYaw(const FString& Player, double& OutYaw) const override
		{
			const AMcPlayerPawn* Pawn = FindPawn(Player);
			OutYaw = Pawn ? Pawn->GetLookYaw() : 0.0;
			return Pawn != nullptr;
		}
		virtual void SetYaw(const FString& Player, double Yaw) override
		{
			if (AMcPlayerPawn* Pawn = FindPawn(Player))
			{
				Pawn->ServerTeleport(Pawn->GetFeetPosition(), float(Yaw));
			}
		}
		virtual bool Hurt(const FString& Player, float Amount, const FString& Type) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data || Pawn->Data->bCreative || Pawn->IsServerDead())
			{
				return false;
			}
			using McSurvival::EDamage;
			const EDamage Kind = Type == TEXT("fall") ? EDamage::Fall : Type == TEXT("drown") ? EDamage::Drown : Type == TEXT("in_fire") ? EDamage::InFire
				: Type == TEXT("on_fire") ? EDamage::OnFire : Type == TEXT("lava") ? EDamage::Lava : Type == TEXT("starve") ? EDamage::Starve
				: Type == TEXT("out_of_world") ? EDamage::Void : Type == TEXT("generic_kill") ? EDamage::Kill : Type == TEXT("magic") || Type == TEXT("indirect_magic") ? EDamage::Magic
				: Type == TEXT("wither") ? EDamage::Wither : Type == TEXT("explosion") || Type == TEXT("player_explosion") ? EDamage::Explosion
				: Type == TEXT("mob_attack") ? EDamage::Mob : EDamage::Generic;
			Pawn->ServerHurt(Amount, Kind, nullptr, false);
			return true;
		}
		virtual void SetWorldSpawn(const FIntVector& Pos) override
		{
			Mode.WorldSpawnOverride = Pos;
		}
		virtual void SetPlayerSpawn(const FString& Player, const FIntVector& Pos) override
		{
			if (AMcPlayerPawn* Pawn = FindPawn(Player))
			{
				if (Pawn->Data)
				{
					// A spawn point is one place: it replaces a bed, and a bed slept in later replaces it.
					Pawn->Data->SpawnPoint = Pos;
					Pawn->Data->SpawnBed.Reset();
				}
			}
		}
		virtual void SetDefaultGameMode(const FString& Mode_) override
		{
			Mode.SetDefaultGameMode(Mode_);
		}
		virtual void PlaySoundAt(FName Event, const FVector& Position, float Volume, float Pitch) override
		{
			if (AMcWorldActor* World = Mode.GetPrimaryWorld())
			{
				World->MulticastSound(Event, FVector_NetQuantize10(Position), Volume, Pitch);
			}
		}
		virtual bool Summon(FName Kind, const FVector& Feet) override
		{
			return SummonMade(Kind, Feet) != nullptr;
		}
		virtual bool SummonWithTag(FName Kind, const FVector& Feet, const FMcNbt& Tag) override
		{
			FMcEntity* Made = SummonMade(Kind, Feet);
			if (Made)
			{
				McCommands::ApplyEntityTag(*Made, Tag);
			}
			return Made != nullptr;
		}
		FMcEntity* SummonMade(FName Kind, const FVector& Feet)
		{
			AMcWorldActor* World = Mode.GetPrimaryWorld();
			FMcEntityHost* Host = World ? World->GetEntityHost() : nullptr;
			if (Host && FMcVehicle::IsVehicleType(Kind))
			{
				TUniquePtr<FMcVehicle> Vehicle = FMcVehicle::Create(Kind);
				Vehicle->Body.Position = Feet;
				return &Host->GetManager().Add(MoveTemp(Vehicle));
			}
			if (Host && FMcPrimedTnt::IsTnt(Kind))
			{
				TUniquePtr<FMcPrimedTnt> Tnt = MakeUnique<FMcPrimedTnt>();
				Tnt->Body.Position = Feet;
				return &Host->GetManager().Add(MoveTemp(Tnt));
			}
			return Host ? Host->SpawnMob(Kind, Feet) : nullptr;
		}
		virtual TArray<int32> FindEntities(FName Kind, const FVector& Origin, double MaxDistance, int32 Limit) const override
		{
			TArray<TPair<double, int32>> Found;
			AMcWorldActor* World = Mode.GetPrimaryWorld();
			FMcEntityHost* Host = World ? World->GetEntityHost() : nullptr;
			if (Host)
			{
				for (const TUniquePtr<FMcEntity>& Each : Host->GetManager().GetEntities())
				{
					const double Distance = FVector::Dist(Each->Body.Position, Origin);
					if (!Each->bRemoved && (Kind.IsNone() || Each->GetType() == Kind) && (MaxDistance <= 0.0 || Distance <= MaxDistance))
					{
						Found.Add({ Distance, Each->Id });
					}
				}
			}
			Found.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key < B.Key; });
			TArray<int32> Ids;
			for (const TPair<double, int32>& Each : Found)
			{
				if (Limit > 0 && Ids.Num() >= Limit)
				{
					break;
				}
				Ids.Add(Each.Value);
			}
			return Ids;
		}
		virtual int32 SetEntitySlot(int32 EntityId, const FString& Slot, FName Item, int32 Count) override
		{
			AMcWorldActor* World = Mode.GetPrimaryWorld();
			FMcEntityHost* Host = World ? World->GetEntityHost() : nullptr;
			FMcEntity* Found = Host ? Host->GetManager().Find(EntityId) : nullptr;
			return Found ? McCommands::SetEntitySlot(*Found, Slot, Item, Count) : 0;
		}
		virtual FString GetEntityName(int32 EntityId) const override
		{
			AMcWorldActor* World = Mode.GetPrimaryWorld();
			FMcEntityHost* Host = World ? World->GetEntityHost() : nullptr;
			FMcEntity* Found = Host ? Host->GetManager().Find(EntityId) : nullptr;
			if (!Found)
			{
				return FString();
			}
			if (Found->IsLiving() && !static_cast<FMcMob*>(Found)->CustomName.IsEmpty())
			{
				return static_cast<FMcMob*>(Found)->CustomName;
			}
			return McLang::Get(TEXT("entity.minecraft.") + McCache::PathOf(Found->GetType().ToString()));
		}

		FMcEntityManager* RideManager() const
		{
			AMcWorldActor* World = Mode.GetPrimaryWorld();
			FMcEntityHost* Host = World ? World->GetEntityHost() : nullptr;
			return Host ? &Host->GetManager() : nullptr;
		}

		virtual int32 Ride(const FString& RiderPlayer, int32 RiderEntity, int32 VehicleEntity, FString& OutOther) override
		{
			FMcEntityManager* Manager = RideManager();
			FMcEntity* Vehicle = Manager ? Manager->Find(VehicleEntity) : nullptr;
			if (!Vehicle)
			{
				return 3;
			}
			if (!RiderPlayer.IsEmpty())
			{
				AMcPlayerPawn* Pawn = FindPawn(RiderPlayer);
				if (!Pawn)
				{
					return 3;
				}
				if (Pawn->GetRidingId() != 0)
				{
					OutOther = GetEntityName(Pawn->GetRidingId());
					return 1;
				}
				if (Vehicle->IsLiving())
				{
					FMcMob& Mob = static_cast<FMcMob&>(*Vehicle);
					if (Mob.PassengerId != 0 || (Mob.Mount.IsValid() && Mob.Mount->RiderId != 0) || !McMount::IsRideable(Mob))
					{
						return 3;
					}
					McMount::Ensure(Mob).RiderId = Pawn->GetUniqueID();
				}
				else if (!Vehicle->IsVehicle())
				{
					return 3;
				}
				Pawn->ServerRide(VehicleEntity);
				return 0;
			}
			FMcEntity* RiderBase = Manager->Find(RiderEntity);
			if (!RiderBase || !RiderBase->IsLiving() || !Vehicle->IsLiving())
			{
				return 3;
			}
			FMcMob& Rider = static_cast<FMcMob&>(*RiderBase);
			FMcMob& Mount = static_cast<FMcMob&>(*Vehicle);
			if (Rider.RidingId != 0)
			{
				OutOther = GetEntityName(Rider.RidingId);
				return 1;
			}
			for (const FMcMob* Chain = &Mount; Chain; )
			{
				if (Chain == &Rider)
				{
					return 2;
				}
				const FMcEntity* Next = Chain->RidingId != 0 ? Manager->Find(Chain->RidingId) : nullptr;
				Chain = Next && Next->IsLiving() ? static_cast<const FMcMob*>(Next) : nullptr;
			}
			if (Mount.PassengerId != 0 || (Mount.Mount.IsValid() && Mount.Mount->RiderId != 0))
			{
				return 3;
			}
			Rider.RidingId = Mount.Id;
			Mount.PassengerId = Rider.Id;
			Rider.Body.Position = Mount.Body.Position + Mount.GetSeatOffset(Rider);
			return 0;
		}

		virtual bool Dismount(const FString& RiderPlayer, int32 RiderEntity, FString& OutVehicleName) override
		{
			FMcEntityManager* Manager = RideManager();
			if (!RiderPlayer.IsEmpty())
			{
				AMcPlayerPawn* Pawn = FindPawn(RiderPlayer);
				if (!Pawn || Pawn->GetRidingId() == 0)
				{
					return false;
				}
				OutVehicleName = GetEntityName(Pawn->GetRidingId());
				Pawn->ServerDismount();
				return true;
			}
			FMcEntity* Found = Manager ? Manager->Find(RiderEntity) : nullptr;
			if (!Found || !Found->IsLiving() || static_cast<FMcMob*>(Found)->RidingId == 0)
			{
				return false;
			}
			FMcMob& Rider = *static_cast<FMcMob*>(Found);
			OutVehicleName = GetEntityName(Rider.RidingId);
			if (FMcEntity* Under = Manager->Find(Rider.RidingId))
			{
				if (Under->IsLiving() && static_cast<FMcMob*>(Under)->PassengerId == Rider.Id)
				{
					static_cast<FMcMob*>(Under)->PassengerId = 0;
				}
			}
			Rider.RidingId = 0;
			return true;
		}

		virtual bool SaveAll(bool bFlush) override
		{
			AMcWorldActor* World = Mode.GetPrimaryWorld();
			if (!World)
			{
				return false;
			}
			// SaveWorld writes the chunks, the entities, the players (OnSaving) and the level, and waits for the files.
			World->SaveWorld();
			return true;
		}
		virtual bool IsAutosaveOn() const override
		{
			const AMcWorldActor* World = Mode.GetPrimaryWorld();
			return !World || World->IsAutosaveOn();
		}
		virtual void SetAutosaveOn(bool bOn) override
		{
			if (AMcWorldActor* World = Mode.GetPrimaryWorld())
			{
				World->SetAutosaveOn(bOn);
			}
		}
		virtual void RequestStop() override
		{
			SaveAll(true);
			UE_LOG(LogMcClient, Display, TEXT("Stopping the server"));
			FPlatformMisc::RequestExit(false, TEXT("McStop"));
		}

		virtual bool SpawnParticle(FName Name, const FVector& Pos, const FVector& Delta, double Speed, int32 Count) override
		{
			AMcWorldActor* World = Mode.GetPrimaryWorld();
			if (!World)
			{
				return false;
			}
			World->MulticastParticle(Name, FVector_NetQuantize10(Pos), FVector_NetQuantize10(Delta), static_cast<float>(Speed), Count);
			return true;
		}

		virtual void GetXp(const FString& Player, int32& OutLevel, int32& OutPoints) const override
		{
			const AMcPlayerPawn* Pawn = FindPawn(Player);
			if (Pawn && Pawn->Data)
			{
				const FMcPlayerStats& Stats = Pawn->Data->Stats;
				OutLevel = Stats.XpLevel;
				OutPoints = McSurvival::GetTotalXp(Stats) - McSurvival::GetTotalXpForLevel(Stats.XpLevel);
			}
		}
		virtual void AddXp(const FString& Player, int32 Amount, bool bLevels) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data)
			{
				return;
			}
			FMcPlayerStats& Stats = Pawn->Data->Stats;
			if (bLevels)
			{
				Stats.XpLevel = FMath::Max(0, Stats.XpLevel + Amount);
			}
			else
			{
				McSurvival::AddXp(Stats, Amount);
			}
		}
		virtual bool SetXp(const FString& Player, int32 Amount, bool bLevels) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data)
			{
				return false;
			}
			FMcPlayerStats& Stats = Pawn->Data->Stats;
			if (bLevels)
			{
				Stats.XpLevel = FMath::Max(0, Amount);
				return true;
			}
			const int32 InLevel = McSurvival::GetXpToNextLevel(Stats.XpLevel);
			if (Amount > InLevel)
			{
				return false;
			}
			Stats.XpProgress = static_cast<float>(Amount) / InLevel;
			return true;
		}
		virtual int32 Clear(const FString& Player, FName Item, int32 MaxCount, bool bTest) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data)
			{
				return 0;
			}
			int32 Found = 0;
			const auto Take = [&](FMcItemStack& Stack)
			{
				if (Stack.IsEmpty() || (!Item.IsNone() && Stack.Item != Item))
				{
					return;
				}
				const int32 Count = MaxCount < 0 ? Stack.Count : FMath::Min(Stack.Count, MaxCount - Found);
				if (bTest || MaxCount == 0)
				{
					Found += Stack.Count;
					return;
				}
				Found += Count;
				Stack.Count -= Count;
				if (Stack.Count <= 0)
				{
					Stack = FMcItemStack();
				}
			};
			for (const TSharedPtr<FMcContainer>& Container : { Pawn->Data->Inventory, Pawn->Data->Armor, Pawn->Data->Offhand })
			{
				for (FMcItemStack& Stack : Container->GetAll())
				{
					Take(Stack);
				}
			}
			return Found;
		}

		static TSharedPtr<FMcContainer> SlotGroup(AMcPlayerPawn* Pawn, int32 Group)
		{
			if (!Pawn || !Pawn->Data)
			{
				return nullptr;
			}
			return Group == 0 ? Pawn->Data->Inventory : Group == 1 ? Pawn->Data->Armor : Group == 2 ? Pawn->Data->Offhand : nullptr;
		}

		virtual bool SetSlot(const FString& Player, int32 Group, int32 Index, FName Item, int32 Count) override
		{
			const TSharedPtr<FMcContainer> Container = SlotGroup(FindPawn(Player), Group);
			if (!Container || Index < 0 || Index >= Container->Num())
			{
				return false;
			}
			Container->Set(Index, Item.IsNone() || Count <= 0 ? FMcItemStack() : FMcItemStack(Item, Count));
			return true;
		}

		virtual bool GetSlot(const FString& Player, int32 Group, int32 Index, FName& OutItem, int32& OutCount) const override
		{
			const TSharedPtr<FMcContainer> Container = SlotGroup(FindPawn(Player), Group);
			if (!Container || Index < 0 || Index >= Container->Num() || Container->Get(Index).IsEmpty())
			{
				return false;
			}
			OutItem = Container->Get(Index).Item;
			OutCount = Container->Get(Index).Count;
			return true;
		}

		virtual void SetAttribute(const FString& Player, const FString& Id, double Value) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (Pawn && Pawn->Data && Id == TEXT("max_health"))
			{
				Pawn->Data->Stats.MaxHealth = FMath::Max(1.0f, (float)Value);
				Pawn->Data->Stats.Health = FMath::Min(Pawn->Data->Stats.Health, Pawn->Data->Stats.MaxHealth);
			}
		}

		virtual bool GetVehicleName(const FString& Player, FString& OutName) const override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			FMcEntityManager* Entities = Pawn && Pawn->GetRidingId() != 0 && Pawn->GetEntities ? Pawn->GetEntities() : nullptr;
			const FMcEntity* Vehicle = Entities ? Entities->Find(Pawn->GetRidingId()) : nullptr;
			if (!Vehicle)
			{
				return false;
			}
			OutName = McLang::Get(TEXT("entity.minecraft.") + McCache::PathOf(Vehicle->GetType().ToString()));
			return true;
		}

		virtual void Dismount(const FString& Player) override
		{
			if (AMcPlayerPawn* Pawn = FindPawn(Player))
			{
				Pawn->ServerDismount();
			}
		}

		virtual int32 GetSelectedSlot(const FString& Player) const override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			return Pawn && Pawn->Data ? Pawn->Data->SelectedSlot : 0;
		}

		virtual int32 SetAdvancement(const FString& Player, const FString& Id, bool bGrant, FString& OutTitle) override
		{
			return AwardAdvancement(FindPawn(Player), Id, bGrant, OutTitle);
		}
		virtual int32 SetRecipes(const FString& Player, const FString& Id, bool bGrant, FString& OutUnknown) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			FMcRecipeBook& Book = FMcRecipeBook::Get();
			Book.EnsureLoaded();
			TArray<FName> Ids;
			if (Id == TEXT("*"))
			{
				// Every recipe the book shows (measured: 1739 in 26.3, the ones an advancement unlocks): not brewing, not the
				// special crafting recipes and not the decorated pot.
				for (const FMcRecipe& Recipe : Book.GetAll())
				{
					if (Recipe.Type != EMcRecipeType::Brewing && Recipe.Type != EMcRecipeType::CraftingSpecial && Recipe.Type != EMcRecipeType::CraftingDecoratedPot)
					{
						Ids.Add(Recipe.Id);
					}
				}
			}
			else
			{
				const FName Full(*McCache::Namespaced(Id));
				if (!Book.Find(Full))
				{
					OutUnknown = Full.ToString();
					return -1;
				}
				Ids.Add(Full);
			}
			return Pawn ? AMcGameMode::UnlockRecipes(*Pawn, Ids, bGrant) : 0;
		}
		virtual bool IsEffect(FName Effect) const override
		{
			FName Parsed;
			return McEffects::Parse(Effect.ToString(), Parsed);
		}
		virtual bool GiveEffect(const FString& Player, FName Effect, int32 Amplifier, int32 DurationTicks) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			return Pawn && Pawn->Data && McEffects::Add(Pawn->Data->Stats, Effect, Amplifier, DurationTicks);
		}
		virtual int32 ClearEffects(const FString& Player, FName Effect) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			return Pawn && Pawn->Data ? McEffects::Remove(Pawn->Data->Stats, Effect) : 0;
		}
		virtual int32 GetEnchantmentMaxLevel(FName Enchantment) const override
		{
			const FMcEnchantmentDef* Def = FMcItemRegistry::Get().FindEnchantment(Enchantment);
			return Def ? Def->MaxLevel : 0;
		}
		virtual EEnchantResult Enchant(const FString& Player, FName Enchantment, int32 Level) override
		{
			AMcPlayerPawn* Pawn = FindPawn(Player);
			if (!Pawn || !Pawn->Data || Pawn->Data->Inventory->Get(Pawn->Data->SelectedSlot).IsEmpty())
			{
				return EEnchantResult::NoItem;
			}
			FMcItemStack& Held = Pawn->Data->Inventory->GetMutable(Pawn->Data->SelectedSlot);
			const FMcItemRegistry& Registry = FMcItemRegistry::Get();
			TArray<FName> Allowed;
			Registry.GetEnchantmentsFor(Held.Item, Allowed);
			TMap<FName, int32> Have = Held.GetEnchantments();
			if (!Allowed.Contains(Enchantment))
			{
				return EEnchantResult::Unsupported;
			}
			for (const TPair<FName, int32>& Other : Have)
			{
				if (Other.Key != Enchantment && !Registry.AreCompatible(Other.Key, Enchantment))
				{
					return EEnchantResult::Unsupported;
				}
			}
			Have.Add(Enchantment, Level);
			Held.SetEnchantments(Have);
			return EEnchantResult::Done;
		}

		virtual FString GetPlayerAddress(const FString& Player) const override
		{
			const APlayerController* Controller = Mode.FindPlayer(Player);
			FString Ip = Controller && Controller->NetConnection ? Controller->NetConnection->LowLevelGetRemoteAddress() : FString();
			Ip.Split(TEXT(":"), &Ip, nullptr);
			return Ip;
		}
		virtual FMcServerAccess LoadAccess() const override { return FMcServerAccess::Load(); }
		virtual void SaveAccess(const FMcServerAccess& Access) override { Access.Save(); }
		virtual bool IsAllowlistOn() const override { return FMcServerProperties::Get().GetBool(TEXT("white-list")); }
		virtual void SetAllowlistOn(bool bOn) override { FMcServerProperties::SetAndSave(TEXT("white-list"), bOn ? TEXT("true") : TEXT("false")); }

	private:
		AMcGameMode& Mode;
	};
}

FString AMcGameMode::GetPlayerName(const AController* Controller)
{
	return Controller && Controller->PlayerState ? Controller->PlayerState->GetPlayerName() : FString();
}

APlayerController* AMcGameMode::FindPlayer(const FString& Name) const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (GetPlayerName(It->Get()).Equals(Name, ESearchCase::IgnoreCase))
		{
			return It->Get();
		}
	}
	return nullptr;
}

void AMcGameMode::BroadcastChat(const FString& Text, uint32 Rgb)
{
	UE_LOG(LogMcClient, Log, TEXT("[Chat] %s"), *Text);
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const AMcPlayerPawn* Pawn = It->Get() ? Cast<AMcPlayerPawn>(It->Get()->GetPawn()) : nullptr;
		if (Pawn && Pawn->Data)
		{
			Pawn->Data->TellOwner(Text, Rgb);
		}
	}
}

void AMcGameMode::ReadFunctionTags()
{
	TickFunctionIds = McCommands::GetFunctionTag(TEXT("minecraft:tick"));
}

void AMcGameMode::RunFunctionTag(const FString& Tag)
{
	// As the console: every command, no name.
	FMcCommandSender Sender;
	Sender.PermissionLevel = 4;
	FGameCommandTarget Target(*this);
	McCommands::RunFunctions(McCommands::GetFunctionTag(Tag), Sender, Target);
}

namespace
{
	// The dedicated server's console: lines typed on standard input (or piped in) are commands as the console, with every permission.
	// A thread reads them (the read blocks) and a ticker on the game thread runs them; when the input ends the thread simply stops.
	class FMcStdinReader : public FRunnable
	{
	public:
		TQueue<FString, EQueueMode::Mpsc> Lines;
		virtual uint32 Run() override
		{
			char Buffer[4096];
			while (fgets(Buffer, sizeof(Buffer), stdin))
			{
				FString Line = FString(UTF8_TO_TCHAR(Buffer));
				Line.TrimStartAndEndInline();
				if (!Line.IsEmpty())
				{
					Lines.Enqueue(Line);
				}
			}
			return 0;
		}
	};
}

void AMcGameMode::StartServerConsole()
{
	static bool bStarted = false;
	if (bStarted)
	{
		return;
	}
	bStarted = true;
	// Never deleted: it may sit in a read until the process ends.
	FMcStdinReader* Reader = new FMcStdinReader();
	FRunnableThread::Create(Reader, TEXT("McServerConsole"), 0, TPri_BelowNormal);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateWeakLambda(this, [this, Reader](float)
	{
		FString Line;
		while (Reader->Lines.Dequeue(Line))
		{
			if (Line.StartsWith(TEXT("/")))
			{
				Line.RightChopInline(1);
			}
			FMcCommandSender Sender;
			Sender.PermissionLevel = 4;
			FGameCommandTarget Target(*this);
			for (const FMcCommandReply& Reply : McCommands::Execute(Line, Sender, Target))
			{
				UE_LOG(LogMcClient, Display, TEXT("%s"), *Reply.Text);
			}
		}
		return true;
	}), 0.1f);
}

void AMcGameMode::TickSpectators()
{
	AMcWorldActor* World = GetPrimaryWorld();
	FMcEntityHost* Host = World ? World->GetEntityHost() : nullptr;
	for (auto It = Spectated.CreateIterator(); It; ++It)
	{
		AMcPlayerPawn* Spectator = nullptr;
		FVector Feet = FVector::ZeroVector;
		bool bFound = false;
		for (FConstPlayerControllerIterator Each = GetWorld()->GetPlayerControllerIterator(); Each; ++Each)
		{
			AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(Each->Get()->GetPawn());
			const FString Name = GetPlayerName(Each->Get());
			if (Pawn && Name == It.Key())
			{
				Spectator = Pawn;
			}
			else if (Pawn && !It.Value().Player.IsEmpty() && Name == It.Value().Player)
			{
				Feet = Pawn->GetFeetPosition();
				bFound = true;
			}
		}
		if (It.Value().Player.IsEmpty())
		{
			const FMcEntity* Watched = Host ? Host->GetManager().Find(It.Value().Entity) : nullptr;
			if (Watched && !Watched->bRemoved)
			{
				Feet = Watched->Body.Position;
				bFound = true;
			}
		}
		if (!bFound)
		{
			// What it watched is gone: it stops.
			It.RemoveCurrent();
		}
		else if (Spectator && FVector::DistSquared(Spectator->GetFeetPosition(), Feet) > 0.0001)
		{
			Spectator->ServerTeleport(Feet, Spectator->GetLookYaw());
		}
	}
}

void AMcGameMode::TickFunctions()
{
	TickSpectators();
	// The world border is a wall: a player past it is put back on its edge (the damage and warning it also has are not done).
	const FMcWorldBorderState& Border = McCommands::GetWorldBorder();
	if (Border.Size < 59999968.0)
	{
		const double Half = Border.Size * 0.5;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(It->Get() ? It->Get()->GetPawn() : nullptr);
			if (!Pawn)
			{
				continue;
			}
			const FVector Feet = Pawn->GetFeetPosition();
			const double X = FMath::Clamp(Feet.X, Border.CentreX - Half, Border.CentreX + Half);
			const double Z = FMath::Clamp(Feet.Z, Border.CentreZ - Half, Border.CentreZ + Half);
			if (X != Feet.X || Z != Feet.Z)
			{
				Pawn->ServerTeleport(FVector(X, Feet.Y, Z), Pawn->GetLookYaw());
			}
		}
	}
	FMcCommandSender Sender;
	Sender.PermissionLevel = 4;
	FGameCommandTarget Target(*this);
	McCommands::RunDueSchedules(Sender, Target);
	if (TickFunctionIds.IsEmpty())
	{
		return;
	}
	McCommands::RunFunctions(TickFunctionIds, Sender, Target);
}

void AMcGameMode::HandleChat(APlayerController* From, const FString& Text)
{
	FMcCommandSender Sender;
	Sender.Name = GetPlayerName(From);
	// The player playing a world of their own (or hosting it) can use every command, as with cheats on; on a dedicated
	// server it is what ops.json says.
	const bool bHost = GetNetMode() != NM_DedicatedServer && From && From->IsLocalController();
	Sender.PermissionLevel = McCommands::GetPermissionLevel(Sender.Name, bHost, bHost ? FMcServerAccess() : FMcServerAccess::Load());
	if (Text.StartsWith(TEXT("/")))
	{
		UE_LOG(LogMcClient, Log, TEXT("%s issued server command: %s"), *Sender.Name, *Text);
	}
	FGameCommandTarget Target(*this);
	for (const FMcCommandReply& Reply : McCommands::HandleChat(Text, Sender, Target))
	{
		McPerf::Event(FString::Printf(TEXT("REPLY%s %s"), Reply.bError ? TEXT("_ERROR") : TEXT(""), *Reply.Text));
		Target.SendTo(Sender.Name, Reply);
	}
}

void AMcGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// The world actor saves the players too when it ends, but which of the two ends first isn't defined, and by the time
	// the second does the pawns may be gone.
	if (IsValid(WorldActor))
	{
		SavePlayers();
		WorldActor->OnSaving.Remove(SavingHandle);
	}
	SavingHandle.Reset();
	FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadMapHandle);
	PreLoadMapHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

FTransform AMcGameMode::GetSpawnTransform() const
{
	// Before the world exists, guess the classic flat surface (y = -61).
	const bool bWorld = WorldActor && WorldActor->GetBlockWorld();
	const int32 SurfaceY = bWorld ? WorldActor->GetSurfaceY() : -61;
	const FIntPoint Column = bWorld ? WorldActor->GetSpawnColumn() : FIntPoint::ZeroValue;
	FVector Block(Column.X + 0.5, SurfaceY + 1 + SpawnHeightAboveSurface, Column.Y + 0.5);
	if (WorldSpawnOverride.IsSet())
	{
		// /setworldspawn: the block's feet position.
		Block = FVector(WorldSpawnOverride->X + 0.5, WorldSpawnOverride->Y, WorldSpawnOverride->Z + 0.5);
	}
	return FTransform(FRotator::ZeroRotator, McCoords::BlockToUnreal(Block));
}

