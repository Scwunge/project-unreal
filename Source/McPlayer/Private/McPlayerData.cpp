#include "McPlayerData.h"
#include "McCraftingMenu.h"
#include "McFurnace.h"
#include "McPlayerMenu.h"
#include "McSurvival.h"
#include "GameFramework/Pawn.h"
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
	case EMcWindowKind::Furnace:
		if (Aux >= 0 && Aux <= static_cast<int32>(EMcFurnaceKind::Smoker))
		{
			Made.Furnace = MakeShared<FMcFurnace>(static_cast<EMcFurnaceKind>(Aux));
			Made.Menu = MakeShared<FMcFurnaceMenu>(Made.Furnace, Inventory);
		}
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
	for (int32 Slot = 0; Slot < Window.Menu->NumSlots(); ++Slot)
	{
		WriteStack(Ar, Window.Menu->GetStack(Slot));
	}
	WriteStack(Ar, Window.Menu->GetCarried());
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
	return true;
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
	Window = FMcWindow();
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

void UMcPlayerDataComponent::OpenBlockWindow(EMcWindowKind Kind, const FIntVector& Pos, const FString& Title, const TSharedPtr<FMcContainer>& Container, const TSharedPtr<FMcFurnace>& Furnace, TFunction<void()> OnChanged)
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
		break;
	case EMcWindowKind::Chest:
		if (!Container.IsValid())
		{
			return;
		}
		Aux = Container->Num();
		Made.Container = Container;
		Made.Menu = MakeShared<FMcContainerMenu>(Container, Inventory);
		break;
	case EMcWindowKind::Furnace:
		if (!Furnace.IsValid())
		{
			return;
		}
		Aux = static_cast<int32>(Furnace->GetKind());
		Made.Furnace = Furnace;
		Made.Menu = MakeShared<FMcFurnaceMenu>(Furnace, Inventory);
		break;
	default:
		return;
	}
	Made.Title = Title;
	Made.Pos = Pos;
	Made.OnChanged = MoveTemp(OnChanged);
	OpenServerWindow(MoveTemp(Made), Aux);
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
		ServerDrop(Stack);
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
