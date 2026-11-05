// What a player is: the numbers on the bars and the item containers (minecraft.wiki, "Health", "Hunger",
// "Experience", "Armor", "Inventory"), and the window (inventory, crafting table, chest, furnace) they have open.
// A component on the player's pawn holds it, so the HUD finds it with FindComponentByClass.
//
// The server is the authority. It runs the survival rules (see McSurvival.h) on its copy and sends the owning
// client the stats and the containers when they change; the client's copy is for drawing. Windows work the same
// way: the client applies its clicks to its own menu at once (so the screen feels immediate) and sends them to
// the server, which checks and applies them to its menu and sends back what the window holds, correcting any
// guess that was wrong. On a machine that is both (the host), there is one copy, and clicks need no sending.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "McContainer.h"
#include "McTrading.h"
#include "McPlayerData.generated.h"

class FArchive;
class FMcCraftingMenu;
class FMcFurnace;

// An effect the player has: its name without the namespace, the level less one and the ticks left.
struct MCPLAYER_API FMcStatusEffect
{
	FName Id;
	int32 Amplifier = 0;
	int32 Duration = 0;
};

struct MCPLAYER_API FMcPlayerStats
{
	// Health in half hearts (20 is ten full hearts), and the golden absorption hearts on top of it.
	float Health = 20.0f;
	float MaxHealth = 20.0f;
	float Absorption = 0.0f;
	// Food in half drumsticks, 0 to 20. Saturation is hidden; it is spent before food is.
	int32 Food = 20;
	float Saturation = 5.0f;
	// Breath in ticks: 300 is full; it goes on down to -20 under water, and each time it gets there the player is hurt.
	int32 Air = 300;
	int32 MaxAir = 300;
	// Level, and how far to the next as a fraction.
	int32 XpLevel = 0;
	float XpProgress = 0.0f;
	// Armour points, 0 to 20 (a bar of ten icons, two points each).
	int32 Armor = 0;
	// Ticks left of the hurt flash on the hearts; set to 10 when damage lands.
	int32 HurtTime = 0;
	bool bHardcore = false;

	// ---- What the rules keep between ticks ----
	// Ticks of immunity left after a hit (20 at the hit; within the first 10 only a bigger hit gets through, by the
	// difference), and the damage of the hit that started it.
	int32 InvulnTicks = 0;
	float LastDamage = 0.0f;
	// The kind of the last damage that landed (McSurvival::EDamage): the sound of being hurt, and what a death was
	// killed by (not saved).
	uint8 LastHurtKind = 0;
	// Ticks left burning.
	int32 OnFireTicks = 0;
	// Hunger's hidden bar: it fills as the player works, and a full 4 costs saturation, then food.
	float Exhaustion = 0.0f;
	// Ticks since food last healed or starved the player.
	int32 FoodTimer = 0;
	TArray<FMcStatusEffect> Effects;

	// Reads or writes, by whether the archive is loading. False if a loading archive is malformed.
	bool Serialize(FArchive& Ar);
};

enum class EMcWindowKind : uint8
{
	None,
	// The player's own: 2 x 2 crafting, armour, offhand.
	Inventory,
	CraftingTable,
	Chest,
	Furnace,
	// Aux is the number of bookshelves around it.
	EnchantingTable,
	Stonecutter,
	SmithingTable,
	Grindstone,
	Anvil,
	BrewingStand,
	Hopper,
	// A dispenser or dropper: nine slots in a square.
	Dispenser,
	Loom,
	// The sign editor: the menu is an FMcSignMenu with the lines of the side being written.
	Sign,
	// A lectern's book being read: the menu is an FMcLecternMenu with its pages and the one it is open at.
	Lectern,
	// A villager's or wandering trader's trades; Pos is where the trader stands.
	Trading,
};

// A window one player has open. Menu is the one clicks go to; Container and Furnace are what it shows.
struct MCPLAYER_API FMcWindow
{
	EMcWindowKind Kind = EMcWindowKind::None;
	// Counts up for each window opened, so a click meant for a window that has gone is told from one for the current.
	int32 Id = -1;
	FString Title;
	TSharedPtr<FMcMenu> Menu;
	TSharedPtr<FMcContainer> Container;
	TSharedPtr<FMcFurnace> Furnace;
	TSharedPtr<class FMcBrewingStand> Brewing;
	// The crafting menus' extra, to look at the grid again after the slots were set from outside.
	FMcCraftingMenu* Crafting = nullptr;
	// The enchanting table's, for its offers and the enchanting itself.
	class FMcEnchantMenu* Enchanting = nullptr;
	// Where the block is, for a window on one.
	FIntVector Pos = FIntVector::ZeroValue;
	// Server: told after clicks, to write a chest's items back to the world, and after the menu's own buttons.
	TFunction<void()> OnChanged;

	bool IsOpen() const { return Menu.IsValid(); }
};

UCLASS(ClassGroup = (Mc))
class MCPLAYER_API UMcPlayerDataComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMcPlayerDataComponent();

	static constexpr int32 InventorySize = 36;
	// Armour slot order: head, chest, legs, feet.
	static constexpr int32 NumArmorSlots = 4;

	FMcPlayerStats Stats;
	// 0 to 8 the hotbar, 9 to 35 the main part (as the game orders them).
	TSharedPtr<FMcContainer> Inventory;
	TSharedPtr<FMcContainer> Armor;
	TSharedPtr<FMcContainer> Offhand;
	// What every ender chest the player opens shows: 3 rows, kept with the player (and across deaths), not with a block.
	static constexpr int32 EnderChestSize = 27;
	TSharedPtr<FMcContainer> EnderChest;
	int32 SelectedSlot = 0;
	// Creative: unlimited blocks, instant breaking, flying, no damage. The server decides; the client is told.
	bool bCreative = false;
	// Server: the bed (either half) the player sleeps in last set as where they come back after dying; kept in the save.
	TOptional<FIntVector> SpawnBed;
	// Server: the advancements made ("minecraft:story/mine_stone"), kept in the save.
	TSet<FName> Advancements;

	const FMcItemStack& GetSelected() const { return Inventory->Get(SelectedSlot); }
	// The selected slot after turning the wheel by Steps (positive is down), wrapping round the hotbar.
	static int32 Scroll(int32 Selected, int32 Steps);

	// The player's own start: nothing in the containers and full bars. In creative, the usual blocks in the hotbar.
	void Reset();
	void FillCreativeHotbar();
	// Armour points and toughness of what is worn, put in Stats.Armor.
	void UpdateArmor();

	// ---- Windows ----
	const FMcWindow& GetWindow() const { return Window; }
	bool HasWindow() const { return Window.IsOpen(); }
	// Opens the player's own window (the E key). A client asks the server to as well.
	void OpenInventoryWindow();
	// Server: opens a window on a block for this player. Chest takes the block's container (its items are what the
	// player moves; OnChanged writes them back), Machine the furnace or brewing stand.
	void OpenBlockWindow(EMcWindowKind Kind, const FIntVector& Pos, const FString& Title, const TSharedPtr<FMcContainer>& Container, const TSharedPtr<class FMcMachine>& Machine, TFunction<void()> OnChanged = nullptr, const TSharedPtr<FMcContainer>& SecondContainer = nullptr);
	// The player closes the window: the cursor stack goes back (or drops), and the other side is told.
	void CloseWindow();
	// The HUD tells of each click or drag it has applied to the window's menu, to be sent to the server (a no-op where
	// this is the server).
	void NotifyClick(int32 Slot, int32 Button, EMcClickType Type);
	void NotifyDrag(int32 Button, const TArray<int32>& Slots);
	// Stacks that a click or the closing of a window threw out, made into items on the ground; only the server does.
	void DropFromWindow(const FMcItemStack& Stack);
	// Creative only, with a window open: Count of Item onto the cursor (no item or a count of 0 empties it, which is how
	// the item list destroys what is dropped on it), or with bToInventory into the inventory instead. The server
	// checks the player is in creative and that the item exists, and keeps the count to a stack.
	void CreativeTake(FName Item, int32 Count, bool bToInventory);
	// Server: opens the editor on one side of the sign at Pos (Block is the sign's block, Kind its McSign::EKind, Colour
	// its dye's text colour), starting from Lines. OnDone gets what the player wrote when the window closes.
	void OpenSignEditor(const FIntVector& Pos, FName Block, uint8 Kind, bool bBack, uint32 Colour, const TArray<FString>& Lines, TFunction<void(const TArray<FString>&)> OnDone);
	// Server: opens the book on the lectern at Pos, at Page of Pages. OnButton is told after each button the player
	// presses (a page turned, or Take Book), with the menu as it is then.
	void OpenLectern(const FIntVector& Pos, const TArray<FString>& Pages, int32 Page, TFunction<void(class FMcLecternMenu&)> OnButton);
	// Server: opens an enchanting table at Pos with Bookshelves around it.
	void OpenEnchantingTable(const FIntVector& Pos, int32 Bookshelves);
	// Server: opens the trading window of a trader at Pos. Offers is what it sells; OnTrade is told of each trade made (the
	// offer's index, the trader's experience from it) to keep the trader's side, and gives back the trader's offers, level
	// and experience as they are then.
	void OpenTrading(const FIntVector& Pos, const FString& Title, const TArray<McTrading::FOffer>& Offers, int32 Reputation, int32 Level, int32 Xp, bool bLevels, TFunction<void(int32 Index, int32 TraderXp, TArray<McTrading::FOffer>& OutOffers, int32& OutLevel, int32& OutXp)> OnTrade);
	// The owner pressed one of the open menu's own buttons (a stonecutter's recipe); the server's menu decides.
	void ClickMenuButton(int32 Id);
	// The owner typed in the open menu's text field (an anvil's name).
	void SetMenuText(const FString& Text);
	// Tells the open menu the player's levels and game mode, before a click (an anvil's result needs them).
	void PrepareMenuClick();
	// The owner chose option Slot (0 to 2) of the enchanting table open; the server enchants if they can pay.
	void Enchant(int32 Slot);
	// What the table offers depends on this, which changes each time the player enchants (minecraft.wiki: Enchanting
	// mechanics). The server's is the one used; it reaches the owner with the window.
	int32 EnchantSeed = 0;

	// ---- Server side ----
	// Makes a stack into an item entity at the player.
	TFunction<void(const FMcItemStack&)> ServerDrop;
	// Whether the player may still have the window open (the block is still there, they are close); asked each tick.
	TFunction<bool(const FMcWindow&)> ServerWindowValid;
	// Plays a sound for what the player did in a window (an enchantment bought), at their position.
	TFunction<void(FName Event, float Volume, float Pitch)> ServerSound;
	// A chest, barrel or shulker box window opened (true) or closed (false) at the position; for the vibration it makes.
	TFunction<void(const FIntVector& Pos, bool bOpen)> ServerContainerEvent;
	// Sends the owner what changed. Called each game tick by the pawn.
	void ServerTick();
	// The stats and containers as bytes, for the owner and for the save.
	TArray<uint8> WriteState() const;
	bool ReadState(TConstArrayView<uint8> Bytes);
	TArray<uint8> WriteInventory() const;
	bool ReadInventory(TConstArrayView<uint8> Bytes);
	bool IsServer() const;

	// ---- Chat ----
	struct FChatLine
	{
		FString Text;
		uint32 Rgb = 0xFFFFFF;
		double Time = 0.0;  // FPlatformTime::Seconds when it arrived
	};
	// What this machine's player has been told, oldest first, at most 100 lines. Kept for the whole run (across
	// respawns and worlds), as the game's chat is.
	static TArray<FChatLine>& GetChatLog();
	static void AddChatLine(const FString& Text, uint32 Rgb);
	// The line above the hotbar (the game's overlay message, the action bar): "Now Playing: ..." from a jukebox. Replaced
	// by the next, and shown for Seconds.
	struct FOverlay
	{
		FString Text;
		// Drawn in colours that run round the rainbow, as a record's title is.
		bool bRainbow = false;
		double Time = -1000.0;  // FPlatformTime::Seconds when it arrived
	};
	static FOverlay& GetOverlay();
	static void ShowOverlay(const FString& Text, bool bRainbow);
	static constexpr double OverlaySeconds = 3.0;

	// The small panels that slide in at the top right (an advancement made). A client list: each has the time it
	// arrived and drops out of it once it has gone.
	struct FToast
	{
		FString Heading;
		uint32 HeadingRgb = 0xFFFF00;
		FString Title;
		FName Icon;
		double Time = 0.0;  // FPlatformTime::Seconds when it arrived
		bool bInSounded = false;
		bool bOutSounded = false;
	};
	static TArray<FToast>& GetToasts();
	static void ShowToast(const FString& Heading, uint32 HeadingRgb, const FString& Title, FName Icon);
	// Server: shows this player a toast.
	void ServerToast(const FString& Heading, int32 HeadingRgb, const FString& Title, FName Icon);
	// Server: shows this player the overlay message.
	void ServerOverlay(const FString& Text, bool bRainbow);
	// The owner typed a line (text or a command); it goes to the server.
	void SendChat(const FString& Text);
	// Server: shows a line to this player.
	void TellOwner(const FString& Text, uint32 Rgb = 0xFFFFFF);
	// Server: what to do with a line the owner typed (the game mode sets it).
	TFunction<void(const FString&)> ServerOnChat;

	// Tells the server the owner chose a hotbar slot.
	void SelectSlot(int32 Slot);

	// Times the server refused a click or a drag, for logging and tests.
	int32 NumRefused = 0;

	// The raid bar at the top of the screen: how full it is (0 to 1, below 0 for none) and its title. The server sets
	// it every second or so; a remote owner is sent it.
	float RaidBarFraction = -1.0f;
	FString RaidBarTitle;
	void SetRaidBar(float Fraction, const FString& Title);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(Client, Reliable)
	void ClientSetStats(const TArray<uint8>& Bytes);
	UFUNCTION(Client, Unreliable)
	void ClientSetRaidBar(float Fraction, const FString& Title);
	UFUNCTION(Client, Reliable)
	void ClientSetInventory(const TArray<uint8>& Bytes);
	UFUNCTION(Client, Reliable)
	void ClientOpenWindow(int32 Id, uint8 Kind, const FString& Title, const FIntVector& Pos, int32 Aux, const TArray<uint8>& Bytes);
	UFUNCTION(Client, Reliable)
	void ClientSyncWindow(int32 Id, int32 Sequence, const TArray<uint8>& Bytes);
	UFUNCTION(Client, Reliable)
	void ClientCloseWindow(int32 Id);

	UFUNCTION(Client, Reliable)
	void ClientChat(const FString& Text, int32 Rgb);
	UFUNCTION(Client, Reliable)
	void ClientOverlay(const FString& Text, bool bRainbow);
	UFUNCTION(Client, Reliable)
	void ClientToast(const FString& Heading, int32 HeadingRgb, const FString& Title, FName Icon);
	UFUNCTION(Server, Reliable)
	void ServerChat(const FString& Text);

	UFUNCTION(Server, Reliable)
	void ServerSelectSlot(int32 Slot);
	UFUNCTION(Server, Reliable)
	void ServerOpenInventory();
	UFUNCTION(Server, Reliable)
	void ServerClick(int32 WindowId, int32 Sequence, int32 Slot, int32 Button, int32 Type);
	UFUNCTION(Server, Reliable)
	void ServerDrag(int32 WindowId, int32 Sequence, int32 Button, const TArray<int32>& Slots);
	UFUNCTION(Server, Reliable)
	void ServerCloseWindow(int32 WindowId);
	UFUNCTION(Server, Reliable)
	void ServerMenuButton(int32 WindowId, int32 Sequence, int32 Id);
	UFUNCTION(Server, Reliable)
	void ServerMenuText(int32 WindowId, int32 Sequence, const FString& Text);
	UFUNCTION(Server, Reliable)
	void ServerEnchant(int32 WindowId, int32 Sequence, int32 Slot);
	UFUNCTION(Server, Reliable)
	void ServerCreativeTake(int32 WindowId, int32 Sequence, FName Item, int32 Count, bool bToInventory);

private:
	// Builds the menu for a kind of window over this player's containers.
	FMcWindow MakeWindow(EMcWindowKind Kind, int32 Id, int32 Aux) const;
	// What the window holds, as bytes (the furnace's state, then every slot and the cursor stack), and back.
	TArray<uint8> WriteWindow() const;
	bool ReadWindow(TConstArrayView<uint8> Bytes);
	void OpenServerWindow(FMcWindow NewWindow, int32 Aux);
	void CloseServerWindow(bool bTellOwner);
	void SendWindowTo(int32 Sequence);
	void ApplyCreativeTake(FName Item, int32 Count, bool bToInventory);
	bool ApplyEnchant(int32 Slot);
	void FlushDropped();
	bool IsOwnerRemote() const;

	FMcWindow Window;
	int32 NextWindowId = 0;
	// Client: clicks sent so far; a correction only counts once it answers the latest.
	int32 ClickSequence = 0;
	// Server: the latest click's sequence, echoed in corrections.
	int32 LastSequence = 0;
	TArray<FMcItemStack> Thrown;
	// Server: what the owner was last sent.
	TArray<uint8> SentStats;
	float SentRaidFraction = -1.0f;
	FString SentRaidTitle;
	TArray<uint8> SentInventory;
	TArray<uint8> SentWindow;
	int32 TicksSinceWindowSync = 0;
};
