#include "McPlayerData.h"
#include "McMountMenu.h"
#include "McSignMenu.h"
#include "McLecternMenu.h"
#include "McCraftingMenu.h"
#include "McFurnace.h"
#include "McBrewingStand.h"
#include "McEnchantMenu.h"
#include "McWorkstationMenus.h"
#include "McAnvilMenu.h"
#include "McItemRegistry.h"
#include "McPlayerMenu.h"
#include "McTradeMenu.h"
#include "McSurvival.h"
#include "McEffects.h"
#include "GameFramework/Pawn.h"
#include "McPlayerPawn.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogMcPlayerData, Log, All);

namespace
{
	constexpr int32 StateVersion = 1;
	constexpr int32 InventoryWindowId = 0;
	// Sanity limits on what a window message may carry.
	constexpr int32 MaxWindowSlots = 128;

	void WriteStack(FArchive& Ar, const FMcItemStack& Stack)
	{
		FMcItemStack Copy = Stack;
		Copy.Serialize(Ar);
	}

	bool ReadStack(FArchive& Ar, FMcItemStack& Out)
	{
		return Out.Serialize(Ar);
	}
}

UMcPlayerDataComponent::UMcPlayerDataComponent()
	: Inventory(MakeShared<FMcContainer>(InventorySize))
	, Armor(MakeShared<FMcContainer>(NumArmorSlots))
	, Offhand(MakeShared<FMcContainer>(1))
	, EnderChest(MakeShared<FMcContainer>(EnderChestSize))
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMcPlayerDataComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

int32 UMcPlayerDataComponent::Scroll(int32 Selected, int32 Steps)
{
	return ((Selected + Steps) % 9 + 9) % 9;
}

bool UMcPlayerDataComponent::IsServer() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

bool UMcPlayerDataComponent::IsOwnerRemote() const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	return IsServer() && Pawn && !Pawn->IsLocallyControlled();
}

void UMcPlayerDataComponent::Reset()
{
	Stats = FMcPlayerStats();
	Inventory->Clear();
	Armor->Clear();
	Offhand->Clear();
	SelectedSlot = 0;
	if (bCreative)
	{
		FillCreativeHotbar();
	}
}

void UMcPlayerDataComponent::FillCreativeHotbar()
{
	static const TCHAR* const Items[9] = {
		TEXT("minecraft:grass_block"), TEXT("minecraft:dirt"), TEXT("minecraft:stone"), TEXT("minecraft:cobblestone"),
		TEXT("minecraft:oak_planks"), TEXT("minecraft:oak_log"), TEXT("minecraft:glass"), TEXT("minecraft:torch"),
		TEXT("minecraft:oak_stairs"),
	};
	for (int32 Slot = 0; Slot < 9; ++Slot)
	{
		Inventory->Set(Slot, FMcItemStack(FName(Items[Slot]), 64));
	}
}

void UMcPlayerDataComponent::UpdateArmor()
{
	// The registry may not be loaded yet when a saved player comes back (an imported one in the first frames).
	FMcItemRegistry::Get().EnsureLoaded();
	float Points = 0.0f, Toughness = 0.0f;
	McSurvival::GetArmorValues(*Armor, Points, Toughness);
	Stats.Armor = FMath::RoundToInt32(Points);
}

// ---- Bytes ----

TArray<uint8> UMcPlayerDataComponent::WriteState() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes);
	int32 Version = StateVersion;
	bool bMode = bCreative;
	Ar << Version << bMode;
	FMcPlayerStats Copy = Stats;
	Copy.Serialize(Ar);
	return Bytes;
}

bool UMcPlayerDataComponent::ReadState(TConstArrayView<uint8> Bytes)
{
	FMemoryReaderView Ar(Bytes);
	int32 Version = 0;
	bool bMode = false;
	Ar << Version << bMode;
	FMcPlayerStats Read;
	if (Ar.IsError() || Version != StateVersion || !Read.Serialize(Ar))
	{
		return false;
	}
	Stats = Read;
	bCreative = bMode;
	return true;
}

TArray<uint8> UMcPlayerDataComponent::WriteInventory() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes);
	int32 Version = StateVersion;
	Ar << Version;
	for (int32 Slot = 0; Slot < InventorySize; ++Slot)
	{
		WriteStack(Ar, Inventory->Get(Slot));
	}
	for (int32 Slot = 0; Slot < NumArmorSlots; ++Slot)
	{
		WriteStack(Ar, Armor->Get(Slot));
	}
	WriteStack(Ar, Offhand->Get(0));
	return Bytes;
}

bool UMcPlayerDataComponent::ReadInventory(TConstArrayView<uint8> Bytes)
{
	FMemoryReaderView Ar(Bytes);
	int32 Version = 0;
	Ar << Version;
	if (Ar.IsError() || Version != StateVersion)
	{
		return false;
	}
	TArray<FMcItemStack> Read;
	Read.SetNum(InventorySize + NumArmorSlots + 1);
	for (FMcItemStack& Stack : Read)
	{
		if (!ReadStack(Ar, Stack))
		{
			return false;
		}
	}
	for (int32 Slot = 0; Slot < InventorySize; ++Slot)
	{
		Inventory->Set(Slot, Read[Slot]);
	}
	for (int32 Slot = 0; Slot < NumArmorSlots; ++Slot)
	{
		Armor->Set(Slot, Read[InventorySize + Slot]);
	}
	Offhand->Set(0, Read.Last());
	return true;
}

// ---- Windows ----

FMcWindow UMcPlayerDataComponent::MakeWindow(EMcWindowKind Kind, int32 Id, int32 Aux) const
{
	FMcWindow Made;
	Made.Kind = Kind;
	Made.Id = Id;
	switch (Kind)
	{
	case EMcWindowKind::Inventory:
	{
		const TSharedRef<FMcPlayerMenu> Menu = MakeShared<FMcPlayerMenu>(Inventory, Armor, Offhand);
		Made.Menu = Menu;
		Made.Crafting = &Menu.Get();
		break;
	}
	case EMcWindowKind::CraftingTable:
	{
		const TSharedRef<FMcCraftingMenu> Menu = MakeShared<FMcCraftingMenu>(3, Inventory);
		Made.Menu = Menu;
		Made.Crafting = &Menu.Get();
		break;
	}
	case EMcWindowKind::Chest:
		if (Aux > 0 && Aux <= 54 && Aux % 9 == 0)
		{
			Made.Container = MakeShared<FMcContainer>(Aux);
			Made.Menu = MakeShared<FMcContainerMenu>(Made.Container, Inventory);
		}
		break;
	case EMcWindowKind::Horse:
		if (Aux >= 2 && Aux <= 17)
		{
			Made.Container = MakeShared<FMcContainer>(Aux);
			Made.Menu = MakeShared<FMcMountMenu>(Made.Container, Inventory);
		}
		break;
	case EMcWindowKind::Hopper:
	case EMcWindowKind::Dispenser:
		Made.Container = MakeShared<FMcContainer>(Kind == EMcWindowKind::Hopper ? 5 : 9);
		Made.Menu = MakeShared<FMcContainerMenu>(Made.Container, Inventory);
		break;
	case EMcWindowKind::Stonecutter:
		Made.Menu = MakeShared<FMcStonecutterMenu>(Inventory);
		break;
	case EMcWindowKind::Loom:
		Made.Menu = MakeShared<FMcLoomMenu>(Inventory);
		break;
	case EMcWindowKind::SmithingTable:
		Made.Menu = MakeShared<FMcSmithingMenu>(Inventory);
		break;
	case EMcWindowKind::Grindstone:
		Made.Menu = MakeShared<FMcGrindstoneMenu>(Inventory);
		break;
	case EMcWindowKind::CartographyTable:
		Made.Menu = MakeShared<FMcCartographyMenu>(Inventory);
		break;
	case EMcWindowKind::Anvil:
		Made.Menu = MakeShared<FMcAnvilMenu>(Inventory);
		break;
	case EMcWindowKind::EnchantingTable:
	{
		const TSharedRef<FMcEnchantMenu> Menu = MakeShared<FMcEnchantMenu>(Inventory, Aux, EnchantSeed);
		Made.Menu = Menu;
		Made.Enchanting = &Menu.Get();
		break;
	}
	case EMcWindowKind::Trading:
		Made.Menu = MakeShared<FMcTradeMenu>(Inventory);
		break;
	case EMcWindowKind::Furnace:
		if (Aux >= 0 && Aux <= static_cast<int32>(EMcFurnaceKind::Smoker))
		{
			Made.Furnace = MakeShared<FMcFurnace>(static_cast<EMcFurnaceKind>(Aux));
			Made.Menu = MakeShared<FMcFurnaceMenu>(Made.Furnace, Inventory);
		}
		break;
	case EMcWindowKind::BrewingStand:
		Made.Brewing = MakeShared<FMcBrewingStand>();
		Made.Menu = MakeShared<FMcBrewingMenu>(Made.Brewing, Inventory);
		break;
	case EMcWindowKind::Sign:
		Made.Menu = MakeShared<FMcSignMenu>();
		break;
	case EMcWindowKind::Lectern:
		Made.Menu = MakeShared<FMcLecternMenu>();
		break;
	default:
		break;
	}
	return Made;
}

TArray<uint8> UMcPlayerDataComponent::WriteWindow() const
{
	TArray<uint8> Bytes;
	if (!Window.IsOpen())
	{
		return Bytes;
	}
	FMemoryWriter Ar(Bytes);
	if (Window.Furnace.IsValid())
	{
		Window.Furnace->Serialize(Ar);
	}
	if (Window.Brewing.IsValid())
	{
		Window.Brewing->Serialize(Ar);
	}
	if (Window.Enchanting)
	{
		int32 Seed = Window.Enchanting->GetSeed();
		Ar << Seed;
	}
	for (int32 Slot = 0; Slot < Window.Menu->NumSlots(); ++Slot)
	{
		WriteStack(Ar, Window.Menu->GetStack(Slot));
	}
	WriteStack(Ar, Window.Menu->GetCarried());
	Window.Menu->SerializeState(Ar);
	return Bytes;
}

bool UMcPlayerDataComponent::ReadWindow(TConstArrayView<uint8> Bytes)
{
	if (!Window.IsOpen())
	{
		return false;
	}
	FMemoryReaderView Ar(Bytes);
	if (Window.Furnace.IsValid() && !Window.Furnace->Serialize(Ar))
	{
		return false;
	}
	if (Window.Brewing.IsValid() && !Window.Brewing->Serialize(Ar))
	{
		return false;
	}
	int32 Seed = 0;
	if (Window.Enchanting)
	{
		Ar << Seed;
	}
	const int32 Num = Window.Menu->NumSlots();
	TArray<FMcItemStack> Read;
	Read.SetNum(Num);
	for (FMcItemStack& Stack : Read)
	{
		if (!ReadStack(Ar, Stack))
		{
			return false;
		}
	}
	FMcItemStack Carried;
	if (!ReadStack(Ar, Carried))
	{
		return false;
	}
	for (int32 Slot = 0; Slot < Num; ++Slot)
	{
		Window.Menu->SetStack(Slot, Read[Slot]);
	}
	Window.Menu->SetCarried(Carried);
	if (Window.Crafting)
	{
		Window.Crafting->Refresh();
	}
	if (Window.Enchanting)
	{
		Window.Enchanting->SetSeed(Seed);
	}
	PrepareMenuClick();
	Window.Menu->SerializeState(Ar);
	return !Ar.IsError();
}

void UMcPlayerDataComponent::OpenServerWindow(FMcWindow NewWindow, int32 Aux)
{
	if (Window.IsOpen())
	{
		CloseServerWindow(true);
	}
	if (!NewWindow.IsOpen())
	{
		return;
	}
	Window = MoveTemp(NewWindow);
	if (IsServer())
	{
		// Statistics: what was opened (the kinds the data does not tell apart, such as a blast furnace from a furnace, count as the plain one).
		const TCHAR* Opened = nullptr;
		switch (Window.Kind)
		{
		case EMcWindowKind::CraftingTable: Opened = TEXT("interact_with_crafting_table"); break;
		case EMcWindowKind::Chest: Opened = TEXT("open_chest"); break;
		case EMcWindowKind::Furnace: Opened = TEXT("interact_with_furnace"); break;
		case EMcWindowKind::Stonecutter: Opened = TEXT("interact_with_stonecutter"); break;
		case EMcWindowKind::SmithingTable: Opened = TEXT("interact_with_smithing_table"); break;
		case EMcWindowKind::Grindstone: Opened = TEXT("interact_with_grindstone"); break;
		case EMcWindowKind::Anvil: Opened = TEXT("interact_with_anvil"); break;
		case EMcWindowKind::BrewingStand: Opened = TEXT("interact_with_brewingstand"); break;
		case EMcWindowKind::Hopper: Opened = TEXT("inspect_hopper"); break;
		case EMcWindowKind::Dispenser: Opened = TEXT("inspect_dispenser"); break;
		case EMcWindowKind::Loom: Opened = TEXT("interact_with_loom"); break;
		case EMcWindowKind::Lectern: Opened = TEXT("interact_with_lectern"); break;
		case EMcWindowKind::Trading: Opened = TEXT("talked_to_villager"); break;
		default: break;
		}
		if (Opened)
		{
			AddStat(FString(TEXT("custom:")) + Opened);
		}
		if (Window.Crafting)
		{
			Window.Crafting->MayCraft = [WeakThis = TWeakObjectPtr<UMcPlayerDataComponent>(this)](FName Recipe)
			{
				const UMcPlayerDataComponent* Self = WeakThis.Get();
				const AMcPlayerPawn* Pawn = Self ? Cast<AMcPlayerPawn>(Self->GetOwner()) : nullptr;
				return !Pawn || !Pawn->GetGameRules().bLimitedCrafting || Self->Recipes.Contains(Recipe);
			};
			Window.Crafting->OnCrafted = [WeakThis = TWeakObjectPtr<UMcPlayerDataComponent>(this)](FName Recipe, const FMcItemStack& Result)
			{
				if (UMcPlayerDataComponent* Self = WeakThis.Get())
				{
					Self->AddStat(TEXT("crafted:") + Result.Item.ToString(), Result.Count);
					if (AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(Self->GetOwner()))
					{
						FMcTriggerEvent Crafted;
						Crafted.Trigger = TEXT("minecraft:recipe_crafted");
						Crafted.Recipe = Recipe;
						Crafted.Item = Result.Item;
						Pawn->FireTrigger(Crafted);
					}
				}
			};
		}
	}
	LastSequence = 0;
	TicksSinceWindowSync = 0;
	if (IsOwnerRemote())
	{
		if (Window.Kind == EMcWindowKind::Inventory)
		{
			SendWindowTo(0);
		}
		else
		{
			SentWindow = WriteWindow();
			ClientOpenWindow(Window.Id, static_cast<uint8>(Window.Kind), Window.Title, Window.Pos, Aux, SentWindow);
		}
	}
	else
	{
		SentWindow = WriteWindow();
	}
}

void UMcPlayerDataComponent::CloseServerWindow(bool bTellOwner)
{
	if (!Window.IsOpen())
	{
		return;
	}
	TArray<FMcItemStack> Dropped;
	Window.Menu->Close(Dropped);
	for (const FMcItemStack& Stack : Dropped)
	{
		DropFromWindow(Stack);
	}
	if (Window.OnChanged)
	{
		Window.OnChanged();
	}
	const int32 ClosedId = Window.Id;
	const bool bWasChest = Window.Kind == EMcWindowKind::Chest;
	const FIntVector ClosedPos = Window.Pos;
	Window = FMcWindow();
	if (bWasChest && ServerContainerEvent)
	{
		ServerContainerEvent(ClosedPos, false);
	}
	SentWindow.Reset();
	// The window's changes were sent inside it; now the containers need sending in full.
	SentInventory.Reset();
	if (bTellOwner && IsOwnerRemote())
	{
		ClientCloseWindow(ClosedId);
	}
}

void UMcPlayerDataComponent::OpenInventoryWindow()
{
	if (IsServer())
	{
		OpenServerWindow(MakeWindow(EMcWindowKind::Inventory, InventoryWindowId, 0), 0);
	}
	else if (!HasWindow())
	{
		Window = MakeWindow(EMcWindowKind::Inventory, InventoryWindowId, 0);
		ClickSequence = 0;
		ServerOpenInventory();
	}
}

void UMcPlayerDataComponent::OpenBlockWindow(EMcWindowKind Kind, const FIntVector& Pos, const FString& Title, const TSharedPtr<FMcContainer>& Container, const TSharedPtr<FMcMachine>& Machine, TFunction<void()> OnChanged, const TSharedPtr<FMcContainer>& SecondContainer)
{
	if (!IsServer() || Stats.Health <= 0.0f)
	{
		return;
	}
	int32 Aux = 0;
	FMcWindow Made = MakeWindow(Kind, ++NextWindowId, 0);
	switch (Kind)
	{
	case EMcWindowKind::CraftingTable:
	case EMcWindowKind::Stonecutter:
	case EMcWindowKind::Loom:
	case EMcWindowKind::SmithingTable:
	case EMcWindowKind::Grindstone:
	case EMcWindowKind::CartographyTable:
	case EMcWindowKind::Anvil:
		break;
	case EMcWindowKind::Horse:
		if (!Container.IsValid() || Container->Num() < 2 || Container->Num() > 17)
		{
			return;
		}
		Aux = Container->Num();
		Made.Container = Container;
		Made.Menu = MakeShared<FMcMountMenu>(Container, Inventory);
		break;
	case EMcWindowKind::Chest:
	case EMcWindowKind::Hopper:
	case EMcWindowKind::Dispenser:
		if (!Container.IsValid() || (Kind == EMcWindowKind::Hopper && Container->Num() != 5) || (Kind == EMcWindowKind::Dispenser && Container->Num() != 9))
		{
			return;
		}
		Aux = Container->Num();
		Made.Container = Container;
		if (Kind == EMcWindowKind::Chest && SecondContainer.IsValid())
		{
			// A double chest: both halves in one window.
			Aux += SecondContainer->Num();
			Made.Menu = MakeShared<FMcContainerMenu>(Container, SecondContainer, Inventory);
			break;
		}
		{
			const TSharedRef<FMcContainerMenu> Menu = MakeShared<FMcContainerMenu>(Container, Inventory);
			Menu->bNoShulkerBoxes = Title == TEXT("Shulker Box");
			Made.Menu = Menu;
		}
		break;
	case EMcWindowKind::Furnace:
		if (!Machine.IsValid())
		{
			return;
		}
		Made.Furnace = StaticCastSharedPtr<FMcFurnace>(Machine);
		Aux = static_cast<int32>(Made.Furnace->GetKind());
		Made.Menu = MakeShared<FMcFurnaceMenu>(Made.Furnace, Inventory);
		break;
	case EMcWindowKind::BrewingStand:
		if (!Machine.IsValid())
		{
			return;
		}
		Made.Brewing = StaticCastSharedPtr<FMcBrewingStand>(Machine);
		Made.Menu = MakeShared<FMcBrewingMenu>(Made.Brewing, Inventory);
		break;
	default:
		return;
	}
	Made.Title = Title;
	Made.Pos = Pos;
	Made.OnChanged = MoveTemp(OnChanged);
	const bool bIsChest = Kind == EMcWindowKind::Chest;
	OpenServerWindow(MoveTemp(Made), Aux);
	if (bIsChest && ServerContainerEvent)
	{
		ServerContainerEvent(Pos, true);
	}
}

void UMcPlayerDataComponent::MarkWindowOnEntity(int32 EntityId)
{
	if (Window.IsOpen())
	{
		Window.EntityId = EntityId;
	}
}

void UMcPlayerDataComponent::CloseWindow()
{
	if (!HasWindow())
	{
		return;
	}
	if (IsServer())
	{
		CloseServerWindow(true);
		return;
	}
	const int32 Id = Window.Id;
	TArray<FMcItemStack> Dropped;
	Window.Menu->Close(Dropped);
	Window = FMcWindow();
	ServerCloseWindow(Id);
}

void UMcPlayerDataComponent::NotifyClick(int32 Slot, int32 Button, EMcClickType Type)
{
	if (!HasWindow())
	{
		return;
	}
	if (IsServer())
	{
		// The menu the screen clicked is the server's own: only the follow-ups remain.
		McSurvival::AddXp(Stats, Window.Menu->TakeExperience());
	// Measured (ref run bundlesound, 20 clicks): insert and remove_one volume 0.8, pitch 0.8 + 0.5 * random (0.82 to 1.28 seen); insert_fail 1.0 and 1.0.
	const FName BundleSound = Window.Menu->TakeBundleSound();
	if (!BundleSound.IsNone() && ServerSound)
	{
		const bool bFail = BundleSound == FName("item.bundle.insert_fail");
		ServerSound(BundleSound, bFail ? 1.0f : 0.8f, bFail ? 1.0f : 0.8f + FMath::FRand() * 0.5f);
	}
		Stats.XpLevel = FMath::Max(0, Stats.XpLevel - Window.Menu->TakeLevelsSpent());
		if (Window.OnChanged)
		{
			Window.OnChanged();
		}
		return;
	}
	ServerClick(Window.Id, ++ClickSequence, Slot, Button, static_cast<int32>(Type));
}

void UMcPlayerDataComponent::NotifyDrag(int32 Button, const TArray<int32>& Slots)
{
	if (!HasWindow())
	{
		return;
	}
	if (IsServer())
	{
		if (Window.OnChanged)
		{
			Window.OnChanged();
		}
		return;
	}
	ServerDrag(Window.Id, ++ClickSequence, Button, Slots);
}

void UMcPlayerDataComponent::DropFromWindow(const FMcItemStack& Stack)
{
	if (IsServer() && ServerDrop && !Stack.IsEmpty())
	{
		AddStat(TEXT("dropped:") + Stack.Item.ToString(), Stack.Count);
		AddStat(TEXT("custom:drop"), Stack.Count);
		ServerDrop(Stack);
	}
}

void UMcPlayerDataComponent::CreativeTake(FName Item, int32 Count, bool bToInventory)
{
	if (!HasWindow() || !bCreative)
	{
		return;
	}
	ApplyCreativeTake(Item, Count, bToInventory);
	if (!IsServer())
	{
		ServerCreativeTake(Window.Id, ++ClickSequence, Item, Count, bToInventory);
	}
	else if (Window.OnChanged)
	{
		Window.OnChanged();
	}
}

void UMcPlayerDataComponent::OpenSignEditor(const FIntVector& Pos, FName Block, uint8 Kind, bool bBack, uint32 Colour, const TArray<FString>& Lines, TFunction<void(const TArray<FString>&)> OnDone)
{
	if (!IsServer() || Stats.Health <= 0.0f)
	{
		return;
	}
	FMcWindow Made = MakeWindow(EMcWindowKind::Sign, ++NextWindowId, 0);
	const TSharedPtr<FMcSignMenu> Menu = StaticCastSharedPtr<FMcSignMenu>(Made.Menu);
	Menu->bBack = bBack;
	Menu->SignKind = Kind;
	Menu->Block = Block;
	Menu->TextColour = Colour;
	Menu->SetText(FMcSignMenu::Join(Lines));
	Made.Pos = Pos;
	// Called as the window closes, by then with what the player sent.
	Made.OnChanged = [WeakMenu = TWeakPtr<FMcSignMenu>(Menu), OnDone = MoveTemp(OnDone)]()
	{
		if (const TSharedPtr<FMcSignMenu> Open = WeakMenu.Pin(); Open.IsValid() && OnDone)
		{
			OnDone(Open->Lines);
		}
	};
	OpenServerWindow(MoveTemp(Made), 0);
}

void UMcPlayerDataComponent::OpenLectern(const FIntVector& Pos, const TArray<FString>& Pages, int32 Page, TFunction<void(FMcLecternMenu&)> OnButton)
{
	if (!IsServer() || Stats.Health <= 0.0f)
	{
		return;
	}
	FMcWindow Made = MakeWindow(EMcWindowKind::Lectern, ++NextWindowId, 0);
	const TSharedPtr<FMcLecternMenu> Menu = StaticCastSharedPtr<FMcLecternMenu>(Made.Menu);
	Menu->Pages = Pages;
	if (Menu->Pages.Num() > FMcLecternMenu::MaxPages)
	{
		Menu->Pages.SetNum(FMcLecternMenu::MaxPages);
	}
	Menu->Page = FMath::Clamp(Page, 0, FMath::Max(0, Menu->Pages.Num() - 1));
	Made.Pos = Pos;
	Made.OnChanged = [WeakMenu = TWeakPtr<FMcLecternMenu>(Menu), OnButton = MoveTemp(OnButton)]()
	{
		if (const TSharedPtr<FMcLecternMenu> Open = WeakMenu.Pin(); Open.IsValid() && OnButton)
		{
			OnButton(*Open);
		}
	};
	OpenServerWindow(MoveTemp(Made), 0);
}

void UMcPlayerDataComponent::OpenEnchantingTable(const FIntVector& Pos, int32 Bookshelves)
{
	if (!IsServer() || Stats.Health <= 0.0f)
	{
		return;
	}
	if (EnchantSeed == 0)
	{
		EnchantSeed = FMath::Rand() | 1;
	}
	FMcWindow Made = MakeWindow(EMcWindowKind::EnchantingTable, ++NextWindowId, Bookshelves);
	Made.Title = TEXT("Enchant");
	Made.Pos = Pos;
	OpenServerWindow(MoveTemp(Made), Bookshelves);
}

void UMcPlayerDataComponent::OpenTrading(const FIntVector& Pos, const FString& Title, const TArray<McTrading::FOffer>& Offers, int32 Reputation, int32 Level, int32 Xp, bool bLevels, TFunction<void(int32 Index, int32 TraderXp, TArray<McTrading::FOffer>& OutOffers, int32& OutLevel, int32& OutXp)> OnTrade)
{
	if (!IsServer() || Stats.Health <= 0.0f)
	{
		return;
	}
	FMcWindow Made = MakeWindow(EMcWindowKind::Trading, ++NextWindowId, 0);
	FMcTradeMenu* Menu = static_cast<FMcTradeMenu*>(Made.Menu.Get());
	const FMcStatusEffect* Hero = McEffects::Find(Stats, TEXT("hero_of_the_village"));
	Menu->SetTrader(Offers, Reputation, Level, Xp, bLevels, bLevels && Hero ? Hero->Amplifier : -1);
	// The menu lives as long as the window, and so does this.
	Menu->OnTrade = [Menu, OnTrade = MoveTemp(OnTrade), WeakThis = TWeakObjectPtr<UMcPlayerDataComponent>(this)](int32 Index, int32 TraderXp)
	{
		if (UMcPlayerDataComponent* Self = WeakThis.Get())
		{
			if (AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(Self->GetOwner()))
			{
				Self->AddStat(TEXT("custom:traded_with_villager"));
				FMcTriggerEvent Traded;
				Traded.Trigger = TEXT("minecraft:villager_trade");
				Pawn->FireTrigger(Traded);
			}
		}
		TArray<McTrading::FOffer> Current;
		int32 NewLevel = 1;
		int32 NewXp = 0;
		OnTrade(Index, TraderXp, Current, NewLevel, NewXp);
		Menu->UpdateTrader(Current, NewLevel, NewXp);
	};
	Made.Title = Title;
	Made.Pos = Pos;
	OpenServerWindow(MoveTemp(Made), 0);
}

void UMcPlayerDataComponent::ClickMenuButton(int32 Id)
{
	if (!HasWindow())
	{
		return;
	}
	if (IsServer())
	{
		if (Window.Menu->ClickButton(Id) && Window.OnChanged)
		{
			Window.OnChanged();
		}
		return;
	}
	// Shown at once; the server's answer follows with the window.
	Window.Menu->ClickButton(Id);
	ServerMenuButton(Window.Id, ++ClickSequence, Id);
}

void UMcPlayerDataComponent::PrepareMenuClick()
{
	if (Window.IsOpen())
	{
		Window.Menu->SetPlayerLevels(Stats.XpLevel, bCreative);
	}
}

void UMcPlayerDataComponent::SetMenuText(const FString& Text)
{
	if (!HasWindow())
	{
		return;
	}
	PrepareMenuClick();
	Window.Menu->SetText(Text);
	if (!IsServer())
	{
		ServerMenuText(Window.Id, ++ClickSequence, Text);
	}
}

void UMcPlayerDataComponent::ServerMenuText_Implementation(int32 WindowId, int32 Sequence, const FString& Text)
{
	PrepareMenuClick();
	if (!Window.IsOpen() || WindowId != Window.Id || !Window.Menu->SetText(Text))
	{
		++NumRefused;
	}
	LastSequence = Sequence;
	SendWindowTo(Sequence);
}

void UMcPlayerDataComponent::ServerMenuButton_Implementation(int32 WindowId, int32 Sequence, int32 Id)
{
	if (!Window.IsOpen() || WindowId != Window.Id || Stats.Health <= 0.0f || !Window.Menu->ClickButton(Id))
	{
		++NumRefused;
	}
	else if (Window.OnChanged)
	{
		Window.OnChanged();
	}
	LastSequence = Sequence;
	SendWindowTo(Sequence);
}

void UMcPlayerDataComponent::Enchant(int32 Slot)
{
	if (!HasWindow() || !Window.Enchanting)
	{
		return;
	}
	if (IsServer())
	{
		ApplyEnchant(Slot);
	}
	else
	{
		ServerEnchant(Window.Id, ++ClickSequence, Slot);
	}
}

bool UMcPlayerDataComponent::ApplyEnchant(int32 Slot)
{
	int32 Spent = 0;
	if (!Window.Enchanting || Stats.Health <= 0.0f || !Window.Enchanting->Enchant(Slot, Stats.XpLevel, bCreative, Spent))
	{
		return false;
	}
	Stats.XpLevel = FMath::Max(0, Stats.XpLevel - Spent);
	if (AMcPlayerPawn* Pawn = Cast<AMcPlayerPawn>(GetOwner()))
	{
		AddStat(TEXT("custom:enchant_item"));
		FMcTriggerEvent Enchanted;
		Enchanted.Trigger = TEXT("minecraft:enchanted_item");
		Pawn->FireTrigger(Enchanted);
	}
	// A new seed: the next item gets new offers.
	EnchantSeed = FMath::Rand() | 1;
	Window.Enchanting->SetSeed(EnchantSeed);
	return true;
}

void UMcPlayerDataComponent::ServerEnchant_Implementation(int32 WindowId, int32 Sequence, int32 Slot)
{
	if (!Window.IsOpen() || WindowId != Window.Id || !ApplyEnchant(Slot))
	{
		++NumRefused;
	}
	else if (ServerSound)
	{
		ServerSound(FName("block.enchantment_table.use"), 1.0f, FMath::FRand() * 0.1f + 0.9f);
	}
	LastSequence = Sequence;
	SendWindowTo(Sequence);
}

void UMcPlayerDataComponent::ApplyCreativeTake(FName Item, int32 Count, bool bToInventory)
{
	FMcItemRegistry& Items = FMcItemRegistry::Get();
	const bool bReal = !Item.IsNone() && Count > 0 && Items.EnsureLoaded() && Items.Exists(Item);
	const FMcItemStack Stack = bReal ? FMcItemStack(Item, FMath::Min(Count, FMath::Max(1, Items.GetMaxStackSize(Item)))) : FMcItemStack();
	if (!bToInventory)
	{
		Window.Menu->SetCarried(Stack);
	}
	else if (!Stack.IsEmpty())
	{
		// What doesn't fit is let go, as creative has an endless supply.
		Inventory->AddItem(Stack);
	}
}

void UMcPlayerDataComponent::SelectSlot(int32 Slot)
{
	SelectedSlot = FMath::Clamp(Slot, 0, 8);
	if (!IsServer())
	{
		ServerSelectSlot(SelectedSlot);
	}
}

void UMcPlayerDataComponent::SendWindowTo(int32 Sequence)
{
	if (!IsOwnerRemote() || !Window.IsOpen())
	{
		return;
	}
	SentWindow = WriteWindow();
	ClientSyncWindow(Window.Id, Sequence, SentWindow);
}

void UMcPlayerDataComponent::ServerTick()
{
	if (!IsServer())
	{
		return;
	}
	UpdateArmor();
	if (Window.IsOpen())
	{
		if (Stats.Health <= 0.0f || (ServerWindowValid && !ServerWindowValid(Window)))
		{
			CloseServerWindow(true);
		}
		else if (++TicksSinceWindowSync >= 5)
		{
			// Contents can change behind the player's back (items picked up, a furnace cooking).
			TicksSinceWindowSync = 0;
			if (IsOwnerRemote() && WriteWindow() != SentWindow)
			{
				SendWindowTo(LastSequence);
			}
		}
	}
	if (!IsOwnerRemote())
	{
		return;
	}
	if (RaidBarFraction != SentRaidFraction || RaidBarTitle != SentRaidTitle)
	{
		SentRaidFraction = RaidBarFraction;
		SentRaidTitle = RaidBarTitle;
		ClientSetRaidBar(RaidBarFraction, RaidBarTitle);
	}
	if (CustomBars != SentCustomBars)
	{
		SentCustomBars = CustomBars;
		ClientSetCustomBars(CustomBars);
	}
	if (bCountersChanged && ++TicksSinceCounters >= 20)
	{
		bCountersChanged = false;
		TicksSinceCounters = 0;
		FString Text;
		for (const TPair<FString, int64>& Counter : Counters)
		{
			Text += FString::Printf(TEXT("%s=%lld\n"), *Counter.Key, Counter.Value);
		}
		ClientSetCounters(Text);
	}
	const TArray<uint8> State = WriteState();
	if (State != SentStats)
	{
		SentStats = State;
		ClientSetStats(State);
	}
	if (!Window.IsOpen())
	{
		const TArray<uint8> Items = WriteInventory();
		if (Items != SentInventory)
		{
			SentInventory = Items;
			ClientSetInventory(Items);
		}
	}
}

// ---- Messages ----

TArray<UMcPlayerDataComponent::FChatLine>& UMcPlayerDataComponent::GetChatLog()
{
	static TArray<FChatLine> Log;
	return Log;
}

void UMcPlayerDataComponent::AddChatLine(const FString& Text, uint32 Rgb)
{
	// The game's own chat goes to the log too, which is how a run without eyes sees what the player was told.
	UE_LOG(LogMcPlayerData, Display, TEXT("Chat: %s"), *Text);
	TArray<FChatLine>& Log = GetChatLog();
	FChatLine& Line = Log.AddDefaulted_GetRef();
	Line.Text = Text;
	Line.Rgb = Rgb;
	Line.Time = FPlatformTime::Seconds();
	if (Log.Num() > 100)
	{
		Log.RemoveAt(0, Log.Num() - 100);
	}
}

void UMcPlayerDataComponent::SendChat(const FString& Text)
{
	if (IsServer())
	{
		if (ServerOnChat)
		{
			ServerOnChat(Text);
		}
	}
	else
	{
		ServerChat(Text.Left(Text.StartsWith(TEXT("/")) ? 32500 : 256));
	}
}

void UMcPlayerDataComponent::TellOwner(const FString& Text, uint32 Rgb)
{
	if (IsOwnerRemote())
	{
		ClientChat(Text, static_cast<int32>(Rgb));
	}
	else if (IsServer())
	{
		AddChatLine(Text, Rgb);
	}
}

UMcPlayerDataComponent::FOverlay& UMcPlayerDataComponent::GetOverlay()
{
	static FOverlay Overlay;
	return Overlay;
}

void UMcPlayerDataComponent::ShowOverlay(const FString& Text, bool bRainbow)
{
	UE_LOG(LogMcPlayerData, Display, TEXT("Overlay: %s"), *Text);
	FOverlay& Overlay = GetOverlay();
	Overlay.Text = Text;
	Overlay.bRainbow = bRainbow;
	Overlay.Time = FPlatformTime::Seconds();
}

void UMcPlayerDataComponent::ServerOverlay(const FString& Text, bool bRainbow)
{
	if (IsOwnerRemote())
	{
		ClientOverlay(Text, bRainbow);
	}
	else if (IsServer())
	{
		ShowOverlay(Text, bRainbow);
	}
}

TArray<UMcPlayerDataComponent::FToast>& UMcPlayerDataComponent::GetToasts()
{
	static TArray<FToast> Toasts;
	return Toasts;
}

void UMcPlayerDataComponent::ShowToast(const FString& Heading, uint32 HeadingRgb, const FString& Title, FName Icon)
{
	UE_LOG(LogMcPlayerData, Display, TEXT("Toast: %s / %s"), *Heading, *Title);
	FToast& Toast = GetToasts().AddDefaulted_GetRef();
	Toast.Heading = Heading;
	Toast.HeadingRgb = HeadingRgb;
	Toast.Title = Title;
	Toast.Icon = Icon;
	Toast.Time = FPlatformTime::Seconds();
}

void UMcPlayerDataComponent::ServerToast(const FString& Heading, int32 HeadingRgb, const FString& Title, FName Icon)
{
	if (IsOwnerRemote())
	{
		ClientToast(Heading, HeadingRgb, Title, Icon);
	}
	else if (IsServer())
	{
		ShowToast(Heading, static_cast<uint32>(HeadingRgb), Title, Icon);
	}
}

void UMcPlayerDataComponent::ClientToast_Implementation(const FString& Heading, int32 HeadingRgb, const FString& Title, FName Icon)
{
	ShowToast(Heading, static_cast<uint32>(HeadingRgb), Title, Icon);
}

UMcPlayerDataComponent::FTitle& UMcPlayerDataComponent::GetTitle()
{
	static FTitle Title;
	return Title;
}

void UMcPlayerDataComponent::ApplyTitle(ETitleKind Kind, const FString& Text, int32 FadeIn, int32 Stay, int32 FadeOut)
{
	FTitle& Title = GetTitle();
	switch (Kind)
	{
	case ETitleKind::Title:
		Title.Title = Text;
		Title.Time = FPlatformTime::Seconds();
		break;
	case ETitleKind::Subtitle:
		// The subtitle is kept for the next title; one given alone is not shown by itself.
		Title.Subtitle = Text;
		break;
	case ETitleKind::ActionBar:
		ShowOverlay(Text, false);
		break;
	case ETitleKind::Clear:
		Title.Title.Reset();
		Title.Subtitle.Reset();
		Title.Time = -1000.0;
		break;
	case ETitleKind::Reset:
		Title = FTitle();
		break;
	case ETitleKind::Times:
		Title.FadeIn = FadeIn;
		Title.Stay = Stay;
		Title.FadeOut = FadeOut;
		break;
	}
}

void UMcPlayerDataComponent::ServerTitle(ETitleKind Kind, const FString& Text, int32 FadeIn, int32 Stay, int32 FadeOut)
{
	if (IsOwnerRemote())
	{
		ClientTitle(static_cast<uint8>(Kind), Text, FadeIn, Stay, FadeOut);
	}
	else if (IsServer())
	{
		ApplyTitle(Kind, Text, FadeIn, Stay, FadeOut);
	}
}

void UMcPlayerDataComponent::ClientTitle_Implementation(uint8 Kind, const FString& Text, int32 FadeIn, int32 Stay, int32 FadeOut)
{
	ApplyTitle(static_cast<ETitleKind>(Kind), Text, FadeIn, Stay, FadeOut);
}

void UMcPlayerDataComponent::ClientOverlay_Implementation(const FString& Text, bool bRainbow)
{
	ShowOverlay(Text, bRainbow);
}

void UMcPlayerDataComponent::ClientChat_Implementation(const FString& Text, int32 Rgb)
{
	AddChatLine(Text, static_cast<uint32>(Rgb));
}

void UMcPlayerDataComponent::ServerChat_Implementation(const FString& Text)
{
	// The game takes at most 256 characters a line, and 32500 for a command.
	if (ServerOnChat && Text.Len() <= (Text.StartsWith(TEXT("/")) ? 32500 : 256))
	{
		ServerOnChat(Text);
	}
}

void UMcPlayerDataComponent::SetRaidBar(float Fraction, const FString& Title)
{
	RaidBarFraction = Fraction;
	RaidBarTitle = Title;
}

void UMcPlayerDataComponent::SetCustomBars(const FString& Lines)
{
	CustomBars = Lines;
}

void UMcPlayerDataComponent::ClientSetCustomBars_Implementation(const FString& Lines)
{
	CustomBars = Lines;
}

void UMcPlayerDataComponent::AddStat(const FString& Id, int64 Amount)
{
	if (Amount != 0)
	{
		Counters.FindOrAdd(Id) += Amount;
		bCountersChanged = true;
	}
}

void UMcPlayerDataComponent::ClientSetCounters_Implementation(const FString& Text)
{
	Counters.Reset();
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);
	for (const FString& Line : Lines)
	{
		FString Key;
		FString Value;
		if (Line.Split(TEXT("="), &Key, &Value, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			Counters.Add(Key, FCString::Atoi64(*Value));
		}
	}
}

void UMcPlayerDataComponent::ClientSetRaidBar_Implementation(float Fraction, const FString& Title)
{
	RaidBarFraction = Fraction;
	RaidBarTitle = Title;
}

void UMcPlayerDataComponent::ClientSetStats_Implementation(const TArray<uint8>& Bytes)
{
	if (!ReadState(Bytes))
	{
		UE_LOG(LogMcPlayerData, Warning, TEXT("Dropped a damaged stats message"));
	}
}

void UMcPlayerDataComponent::ClientSetInventory_Implementation(const TArray<uint8>& Bytes)
{
	// While a window is open its own messages carry the containers.
	if (!Window.IsOpen() && !ReadInventory(Bytes))
	{
		UE_LOG(LogMcPlayerData, Warning, TEXT("Dropped a damaged inventory message"));
	}
}

void UMcPlayerDataComponent::ClientOpenWindow_Implementation(int32 Id, uint8 Kind, const FString& Title, const FIntVector& Pos, int32 Aux, const TArray<uint8>& Bytes)
{
	FMcWindow Made = MakeWindow(static_cast<EMcWindowKind>(Kind), Id, Aux);
	if (!Made.IsOpen() || Kind == static_cast<uint8>(EMcWindowKind::Inventory))
	{
		return;
	}
	Made.Title = Title;
	Made.Pos = Pos;
	Window = MoveTemp(Made);
	ClickSequence = 0;
	if (!ReadWindow(Bytes))
	{
		Window = FMcWindow();
	}
}

void UMcPlayerDataComponent::ClientSyncWindow_Implementation(int32 Id, int32 Sequence, const TArray<uint8>& Bytes)
{
	// A correction only counts if it answers the latest click; while later clicks are in flight, a later one will.
	if (!Window.IsOpen() || Id != Window.Id || Sequence < ClickSequence)
	{
		return;
	}
	if (!ReadWindow(Bytes))
	{
		UE_LOG(LogMcPlayerData, Warning, TEXT("Dropped a damaged window message"));
	}
}

void UMcPlayerDataComponent::ClientCloseWindow_Implementation(int32 Id)
{
	if (Window.IsOpen() && Window.Id == Id)
	{
		Window = FMcWindow();
	}
}

void UMcPlayerDataComponent::ServerSelectSlot_Implementation(int32 Slot)
{
	SelectedSlot = FMath::Clamp(Slot, 0, 8);
}

void UMcPlayerDataComponent::ServerOpenInventory_Implementation()
{
	if (Stats.Health > 0.0f)
	{
		OpenServerWindow(MakeWindow(EMcWindowKind::Inventory, InventoryWindowId, 0), 0);
	}
}

void UMcPlayerDataComponent::ServerClick_Implementation(int32 WindowId, int32 Sequence, int32 Slot, int32 Button, int32 Type)
{
	if (!Window.IsOpen())
	{
		++NumRefused;
		return;
	}
	const McMenuRpc::EResult Result = McMenuRpc::ValidateClick(*Window.Menu, Window.Id, WindowId, Slot, Button, Type, bCreative);
	if (Result != McMenuRpc::EResult::Ok || Stats.Health <= 0.0f)
	{
		++NumRefused;
		UE_LOG(LogMcPlayerData, Warning, TEXT("Refused a click (slot %d, button %d, type %d): %d"), Slot, Button, Type, static_cast<int32>(Result));
		// Put the owner's menu back as it is here.
		SendWindowTo(Sequence);
		return;
	}
	PrepareMenuClick();
	const int32 OutputBefore = Window.Furnace.IsValid() ? Window.Menu->GetStack(FMcFurnace::OutputSlot).Count : 0;
	TArray<FMcItemStack> Dropped;
	Window.Menu->Click(Slot, Button, static_cast<EMcClickType>(Type), Dropped, bCreative);
	for (const FMcItemStack& Stack : Dropped)
	{
		DropFromWindow(Stack);
	}
	if (Window.Furnace.IsValid() && Window.Menu->GetStack(FMcFurnace::OutputSlot).Count < OutputBefore)
	{
		// Taking what was smelted pays out the experience it earned.
		McSurvival::AddXp(Stats, Window.Furnace->TakeExperience(FMath::FRand()));
	}
	McSurvival::AddXp(Stats, Window.Menu->TakeExperience());
	Stats.XpLevel = FMath::Max(0, Stats.XpLevel - Window.Menu->TakeLevelsSpent());
	LastSequence = Sequence;
	if (Window.OnChanged)
	{
		Window.OnChanged();
	}
	SendWindowTo(Sequence);
}

void UMcPlayerDataComponent::ServerCreativeTake_Implementation(int32 WindowId, int32 Sequence, FName Item, int32 Count, bool bToInventory)
{
	if (!Window.IsOpen() || WindowId != Window.Id || !bCreative || Stats.Health <= 0.0f)
	{
		++NumRefused;
		UE_LOG(LogMcPlayerData, Warning, TEXT("Refused a creative take of %s"), *Item.ToString());
		SendWindowTo(Sequence);
		return;
	}
	ApplyCreativeTake(Item, Count, bToInventory);
	LastSequence = Sequence;
	if (Window.OnChanged)
	{
		Window.OnChanged();
	}
	SendWindowTo(Sequence);
}

void UMcPlayerDataComponent::ServerDrag_Implementation(int32 WindowId, int32 Sequence, int32 Button, const TArray<int32>& Slots)
{
	if (!Window.IsOpen())
	{
		++NumRefused;
		return;
	}
	const McMenuRpc::EResult Result = McMenuRpc::ValidateDrag(*Window.Menu, Window.Id, WindowId, Button, Slots, bCreative);
	if (Result != McMenuRpc::EResult::Ok || Stats.Health <= 0.0f)
	{
		++NumRefused;
		UE_LOG(LogMcPlayerData, Warning, TEXT("Refused a drag: %d"), static_cast<int32>(Result));
		SendWindowTo(Sequence);
		return;
	}
	Window.Menu->BeginQuickCraft(Button);
	for (const int32 Slot : Slots)
	{
		Window.Menu->AddQuickCraftSlot(Slot);
	}
	Window.Menu->EndQuickCraft(bCreative);
	LastSequence = Sequence;
	if (Window.OnChanged)
	{
		Window.OnChanged();
	}
	SendWindowTo(Sequence);
}

void UMcPlayerDataComponent::ServerCloseWindow_Implementation(int32 WindowId)
{
	if (Window.IsOpen() && Window.Id == WindowId)
	{
		CloseServerWindow(false);
	}
}
