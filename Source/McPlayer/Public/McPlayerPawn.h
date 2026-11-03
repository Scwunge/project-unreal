// The player: walks, sprints, sneaks and jumps with Minecraft's movement, and flies like creative mode after a
// double tap of Space. Movement runs at 20 ticks a second; the camera is drawn between the last two ticks.
// WASD moves, Space jumps, Shift sneaks, Ctrl sprints. The left button breaks blocks, the right places the
// hotbar's selected block, the middle picks the targeted block; 1-9 and the wheel choose a hotbar slot.
//
// What the player has and how they are (items, health, hunger) is in their UMcPlayerDataComponent. The owning machine
// moves the player and says what it wants (dig this block, place these, use that one, eat); the server decides, runs the
// survival rules every game tick (see McSurvival.h) and tells the owner the result.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "McPhysics.h"
#include "McRaycast.h"
#include "McChunkStreamComponent.h"
#include "McGameRules.h"
#include "McSound.h"
#include "McSurvival.h"
#include "McPlayerPawn.generated.h"

class FMcBlockWorld;
class FMcEntityManager;
class UCameraComponent;
class UMcPlayerDataComponent;

UCLASS()
class MCPLAYER_API AMcPlayerPawn : public APawn
{
	GENERATED_BODY()

public:
	AMcPlayerPawn();

	// Degrees per mouse count.
	UPROPERTY(EditAnywhere, Category = "Mc")
	float MouseSensitivity = 0.15f;

	// Development (-McInput): turns the view by these degrees, as the mouse would. Positive pitch looks up.
	void DevTurn(float DeltaYaw, float DeltaPitch)
	{
		Yaw = FMath::Fmod(Yaw + DeltaYaw, 360.0f);
		Pitch = FMath::Clamp(Pitch + DeltaPitch, -89.9f, 89.9f);
	}

	// Whether a double tap of Space starts and stops flying, as in creative mode.
	UPROPERTY(EditAnywhere, Category = "Mc")
	bool bMayFly = true;

	UPROPERTY(VisibleAnywhere, Category = "Mc")
	TObjectPtr<UCameraComponent> Camera;

	// The blocks to move through. Until it is set, or while the chunk underfoot isn't loaded, the player
	// stays where it is.
	void SetBlockWorld(const FMcBlockWorld* InWorld) { BlockWorld = InWorld; }
	bool HasBlockWorld() const { return BlockWorld != nullptr; }
	const FMcBlockWorld* GetBlockWorld() const { return BlockWorld; }

	// Puts the eyes at a Minecraft position (x, y, z in blocks), looking along Minecraft's yaw and pitch
	// (yaw 0 faces south, pitch 90 looks straight down), and starts flying so the view stays put.
	void SetView(const FVector& EyePos, float McYaw, float McPitch);

	// Changes blocks for the player. Until the server takes requests (M3's networking), the game mode points
	// this straight at the world actor.
	TFunction<void(const FIntVector& Pos, FMcStateId State)> SetBlockHandler;
	// Swings at a mob in front of the player: the look direction. Set by the controller.
	TFunction<void(const FVector& Look)> AttackHandler;
	// How far this tick's opening shulker lids move a body with this box (zero once a lid tick has been asked about,
	// so a frame with two movement steps is not pushed twice): the view's world, set by the controller.
	TFunction<FVector(const FBox& Box, int64& LastLidTick)> LidPush;

	UPROPERTY(VisibleAnywhere, Category = "Mc")
	TObjectPtr<UMcPlayerDataComponent> Data;

	// ---- The server's hooks into the world, set by the player controller on the server ----
	// Changes a block (the world actor's SetBlock).
	TFunction<void(const FIntVector& Pos, FMcStateId State)> ServerSetBlock;
	// The entities of the dimension, for dropping and picking up items; null if there are none yet.
	TFunction<FMcEntityManager*()> GetEntities;
	// The player used a block (right click): open a crafting table, chest or furnace. Returns true if it did something.
	TFunction<bool(AMcPlayerPawn& Player, const FIntVector& Pos)> OnUseBlock;
	// A survival player started to dig at Pos (a click on it): a note block sounds.
	TFunction<void(AMcPlayerPawn& Player, const FIntVector& Pos)> OnPunchBlock;
	// The player died and their things are on the ground: put them back at the spawn.
	TFunction<void(AMcPlayerPawn& Player)> OnDied;
	// The dead player asked to start again (the death screen's button): the game mode makes a new body.
	TFunction<void(AMcPlayerPawn& Player)> OnRespawnRequest;
	// Called before the server breaks a block for this player, while it is still there (a chest drops its things).
	TFunction<void(const FIntVector& Pos)> OnBeforeBreak;
	// Called after the server put a block down for this player.
	TFunction<void(const FIntVector& Pos)> OnPlaced;
	// The drops of the block about to be broken at Pos, which may still change (a shulker box puts its things in its item).
	TFunction<void(const FIntVector& Pos, TArray<FMcItemStack>& Drops)> OnBreakDrops;
	// An item that carries what it holds was placed at Pos (a shulker box gives its things to the new block).
	TFunction<void(const FIntVector& Pos, const FMcItemStack& Placed)> OnPlacedStack;
	// The player placed a sign at Pos: it gets its text, and they the editor for its front.
	TFunction<void(AMcPlayerPawn& Player, const FIntVector& Pos)> OnPlacedSign;
	// A swing at mobs: the eye, the look direction, the reach and the damage; true if something was hit.
	TFunction<bool(const FVector& Eye, const FVector& Look, double Reach, float Damage, bool bCreative)> MobAttack;
	// The id of the nearest vehicle along the ray (a minecart or boat to get into), or 0.
	TFunction<int32(const FVector& Eye, const FVector& Look, double Reach)> FindVehicle;
	// The player right-clicks the mob their look ray hits (a villager opens its trades): the eye, the look direction and the
	// reach; true if something was done.
	TFunction<bool(AMcPlayerPawn& Player, const FVector& Eye, const FVector& Look, double Reach)> OnUseMob;
	// An item is used on a block (flint and steel makes fire, an eye of ender goes into a frame): Pos is the block clicked, Beside
	// the empty block next to the face clicked. Returns true if something happened.
	TFunction<bool(AMcPlayerPawn& Player, const FIntVector& Pos, const FIntVector& Beside)> OnUseItemOn;
	bool bServerHooked = false;

	// Server: hurts the player (a mob's bite, a blast). Does nothing in creative. Returns the health lost.
	// From is where the blow came from (for the knockback), if it came from somewhere.
	float ServerHurt(float Amount, McSurvival::EDamage Kind, const FVector* From = nullptr, bool bKnock = true);
	// A blast reaches the player: the damage (armour turns some aside), a push added to the velocity (blocks a tick),
	// and the centre it came from for the side the view rolls to.
	void ServerBlast(float Amount, const FVector& Push, const FVector& Center);
	// Server: the owner's swing along Look (Minecraft axes): damage by the held item and how charged the attack is.
	void ServerSwing(const FVector& Look);
	// Pushes the player: the velocity is added to the body's (blocks a tick).
	void ApplyKnockback(const FVector& Velocity);
	// Adds to the velocity (a blast's push), where ApplyKnockback sets it.
	void AddVelocity(const FVector& Push);
	// Server: the points of experience as orbs at the player (a death drops them).
	void ServerDropXp(int32 Points);
	bool IsDead() const { return Data && Data->Stats.Health <= 0.0f; }
	// The experience points the player had when they died: the death screen's "Score".
	int32 GetDeathScore() const { return DeathScore; }
	// Server: puts the player at Feet (block coordinates) looking McYaw, on their own machine too. Their velocity stops.
	void ServerTeleport(const FVector& Feet, float McYaw);
	// Server: where the player's feet are, in block coordinates.
	FVector GetFeetPosition() const { return Body.Position; }
	bool IsServerDead() const { return bServerDead; }
	// The function keys, as this machine's player has set them: F1 hides the HUD and hand, F3 shows the debug text, F5 cycles
	// the view (0 from the eyes, 1 from behind, 2 from the front looking back).
	bool IsHudHidden() const { return bHudHidden; }
	bool IsDebugShown() const { return bDebugShown; }
	int32 GetViewMode() const { return ViewMode; }
	// Lying in a bed. The server keeps the player there (their body at the pillow) until they get up, it is morning or
	// they are hurt; SleepTicks counts the time spent, 100 of which (with every other player in the same state) skip the night.
	bool IsSleeping() const { return bSleeping; }
	int32 GetSleepTicks() const { return SleepTicks; }
	// How dark the screen is from sleeping, 0 to 1: it fades to black as the night goes by and back in when they get up.
	float GetSleepFade() const { return SleepFade; }
	// Server: puts the player to bed in the bed whose head is at Head. The caller has checked the time and the monsters.
	void ServerStartSleep(const FIntVector& Head);
	// Server: gets the player up, to the free spot beside the bed.
	void ServerWakeUp();
	// Server: where the player was put to bed.
	const FIntVector& GetSleepBed() const { return SleepBed; }
	// Server: told each tick a player has slept the whole 100 (by the controller, which knows the other players and the world).
	TFunction<void(AMcPlayerPawn& Player)> OnSleptThrough;
	// The entity id of the minecart or boat the player sits in, or 0.
	int32 GetRidingId() const { return RidingId; }
	// Whether the player's fishing bobber is out (their rod is drawn cast).
	bool IsRodCast() const { return bRodCast; }
	// Server: sits the player in the vehicle (any they were in is left first), or takes them out of it.
	void ServerRide(int32 VehicleId);
	void ServerDismount();
	// How far the server lets a player reach for blocks, eyes to the block's middle.
	static constexpr double ServerReach = 6.5;

	static constexpr int32 HotbarSize = 9;
	// The hotbar slot chosen, as the owner's keys have it (the data component's SelectedSlot follows it).
	int32 SelectedSlot = 0;

	// The block under the crosshair, as of the last frame.
	const TOptional<FMcBlockHit>& GetTarget() const { return Target; }

	bool IsFlying() const { return bFlying; }
	// For putting a player back as they were left (a saved world).
	void SetFlying(bool bInFlying) { bFlying = bInFlying; }
	const FMcBody& GetBody() const { return Body; }

	// Where the player looks (Minecraft yaw and pitch, degrees) and whether they crouch, for drawing their body on
	// other machines.
	float GetLookYaw() const { return McLookYaw; }
	float GetLookPitch() const { return McLookPitch; }
	bool IsSneakPosed() const { return bSneakPose; }
	bool IsGlidePosed() const { return bGlidePose; }
	// Owner: gliding on elytra.
	bool IsGliding() const { return bGliding; }
	// Owner, development (-McGlide): starts a glide on the spot, heading the way they look, if the wings are on.
	void StartGlide();

	// Server: how hard the world is (hunger's harm, and how much it hurts).
	void SetDifficulty(McSurvival::EDifficulty InDifficulty) { Difficulty = InDifficulty; }
	// Server: the game rules this player lives by (fall, fire and drowning damage, natural regeneration, block drops
	// and keepInventory). Also sets the difficulty they carry.
	void SetGameRules(const FMcGameRules& InRules)
	{
		GameRules = InRules;
		Difficulty = static_cast<McSurvival::EDifficulty>(FMath::Clamp(InRules.Difficulty, 0, 3));
	}
	const FMcGameRules& GetGameRules() const { return GameRules; }

protected:
	virtual void PossessedBy(AController* NewController) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	virtual void BeginPlay() override;

	// Where the owning client's player is now, once a tick, in Minecraft block coordinates (feet), and where they look.
	// Flags: 1 sneaking, 2 on the ground, 4 sprinting, 8 flying, 16 gliding.
	UFUNCTION(Server, Unreliable)
	void ServerMove(FVector FeetPos, float NewYaw, float NewPitch, uint8 Flags);

	// The owner holds the dig button on a block (bStart), or lets go (the position is then ignored).
	UFUNCTION(Server, Reliable)
	void ServerDig(FIntVector Pos, bool bStart);
	// The owner places blocks (a door is two): the server checks them against the selected item and the world.
	UFUNCTION(Server, Reliable)
	void ServerPlace(const TArray<FMcNetBlockChange>& Placed);
	// A creative owner placed the banner they hold at Pos (the block itself goes by the chunk stream): the placed flag takes
	// its patterns.
	UFUNCTION(Server, Reliable)
	void ServerAttachBanner(FIntVector Pos);
	UFUNCTION(Server, Reliable)
	void ServerUseBlock(FIntVector Pos);
	// The owner right-clicks a mob in front of them, looking along Look (Minecraft axes).
	UFUNCTION(Server, Reliable)
	void ServerUseMob(FVector Look);
	// The owner uses the held item on the face (0 to 5, as FMcBlockHit) of a block.
	UFUNCTION(Server, Reliable)
	void ServerUseItemOn(FIntVector Pos, int32 Face);
	// The owner throws the snowball, egg or ender pearl they hold, along where they look.
	UFUNCTION(Server, Reliable)
	void ServerThrow();
	// The server moved the owner: they move too, and say so, since until then the server ignores where they say they are.
	UFUNCTION(Client, Reliable)
	void ClientTeleport(FVector Feet, float McYaw);
	UFUNCTION(Server, Reliable)
	void ServerTeleported();
	// The owner uses the held item on nothing in particular: a fishing rod casts, or reels in.
	UFUNCTION(Server, Reliable)
	void ServerUseItem();
	// The owner holds the use button on a food (eating takes a while).
	UFUNCTION(Server, Reliable)
	void ServerSetUsing(bool bUsing);
	// Creative: choose a block's item for the selected slot.
	UFUNCTION(Server, Reliable)
	void ServerPickItem(FName Item);
	// The dead owner presses Respawn.
	UFUNCTION(Server, Reliable)
	void ServerRespawn();
	// The server hit the owner: a push for their own body, which they simulate.
	UFUNCTION(Client, Reliable)
	void ClientKnockback(FVector Velocity);
	UFUNCTION(Client, Reliable)
	void ClientAddVelocity(FVector Push);
	// The server hit the owner from this side (-1 to 1): the view rolls for it.
	UFUNCTION(Client, Reliable)
	void ClientHurtSide(float Side);
	// The owner clicks on a vehicle along Look (Minecraft axes) to get in.
	UFUNCTION(Server, Reliable)
	void ServerTryRide(FVector Look);
	// A riding owner's keys (forward and strafe, as in FMcMoveInput) and where they look (the camera yaw), once a tick.
	UFUNCTION(Server, Unreliable)
	void ServerRideInput(float Forward, float Strafe, float LookYaw);
	// A riding owner wants out.
	UFUNCTION(Server, Reliable)
	void ServerWantDismount();
	// A sleeping owner presses to get up.
	UFUNCTION(Server, Reliable)
	void ServerWantWake();
	// The owner presses Q (one of the selected item) or Ctrl+Q (the whole stack): it is thrown from the eyes along the look.
	UFUNCTION(Server, Reliable)
	void ServerDropSelected(bool bWholeStack);
	// Where the vehicle seats a riding owner whose body the server simulates.
	UFUNCTION(Client, Unreliable)
	void ClientRideSeat(FVector Feet);

public:
	// Asks the server for a new body; the death screen's button.
	void RequestRespawn() { ServerRespawn(); }
	// Right-clicks the block at Pos as the hand does (the -McUse development flag).
	void RequestUseBlock(const FIntVector& Pos) { ServerUseBlock(Pos); }
	// Uses the held item at nothing (the -McCast development flag casts a rod this way).
	void RequestUseItem() { ServerUseItem(); }
	// Right-clicks the mob in front, looking along Look in Minecraft axes (the -McTrade development flag).
	void RequestUseMob(const FVector& Look) { ServerUseMob(Look); }

protected:

private:
	void GameTick(const FMcMoveInput& Input);
	void UpdateTarget();
	void UseBlocks(APlayerController& Player);
	void ServerGameTick();
	void ApplyTeleport(const FVector& Feet, float McYaw);
	bool bServerAwaitingTeleport = false;
	void ServerBreakBlock(const FIntVector& Pos);
public:
	// Development: puts the held block at Pos the way a placing hand does (-McPlaceHeld).
	void PlaceHeldAt(const FIntVector& Pos);
private:
	// Stands a flag entity in the banner block at Pos with the patterns of Banner (the item); false if the block isn't a banner.
	bool AttachBanner(const FIntVector& Pos, const FMcItemStack& Banner);
	// Whether placing the item gives the block something from the item (a banner's patterns, a shulker box's things).
	static bool CarriesPlacedData(FName Item);
	// A creative placement waiting for its block to arrive.
	FIntVector PendingBannerPos = FIntVector::ZeroValue;
	FMcItemStack PendingBanner;
	int32 PendingBannerTicks = 0;
	void ServerDie();
	// Plays the hurt (or death) sound if health has dropped since the last call.
	void ServerHurtSound();
	// A tool or weapon wore out: the crack of it breaking.
	void ServerItemBroke();
	void StopDigging();
	float GetEyeHeight() const;
	void ShowPosition() const;

	const FMcBlockWorld* BlockWorld = nullptr;
	int64 LastLidTick = 0;
	FMcBody Body;
	FVector PreviousPosition = FVector::ZeroVector;
	float PreviousEyeHeight = 1.62f;
	// Where Tick last put the actor; anything else moving it (spawning, teleports) is taken as a teleport.
	FVector LastLocation = FVector(TNumericLimits<double>::Max());
	bool bFlying = false;
	bool bSneaking = false;
	bool bSprinting = false;
	// Ticks left in which a second Space press toggles flying.
	int32 FlyToggleTicks = 0;
	bool bJumpWasDown = false;
	// Time not yet used up by whole ticks.
	float TickAccumulator = 0.0f;
	float Yaw = 0.0f;
	float Pitch = 0.0f;

	// Minecraft's yaw (0 faces south) and pitch (down is positive) as the owner has them; everyone else's copy of
	// the player is drawn from these.
	UPROPERTY(Replicated)
	float McLookYaw = 0.0f;
	UPROPERTY(Replicated)
	float McLookPitch = 0.0f;
	UPROPERTY(Replicated)
	bool bSneakPose = false;
	UPROPERTY(Replicated)
	int32 RidingId = 0;
	UPROPERTY(Replicated)
	bool bSleeping = false;
	UPROPERTY(Replicated)
	int32 SleepTicks = 0;
	UPROPERTY(Replicated)
	int32 DeathScore = 0;
	FIntVector SleepBed = FIntVector::ZeroValue;
	bool bSleptThroughSent = false;
	bool bHudHidden = false;
	bool bDebugShown = false;
	bool bF3Combo = false;
	int32 ViewMode = 0;
	void HandleFunctionKeys(APlayerController& Player);
	// Owner side: the dark of the screen, eased towards 1 while sleeping and back to 0 after.
	float SleepFade = 0.0f;
	// Server: the riding owner's keys, as the last RPC (or the local keys) had them.
	float RideForward = 0.0f;
	float RideStrafe = 0.0f;
	void ServerTickRiding();

	// ---- Fishing (McPlayerFishing.cpp) ----
	// Server: the player's bobber, if it is out, as an entity id (0 if none).
	int32 BobberId = 0;
	UPROPERTY(Replicated)
	bool bRodCast = false;
	// Server: keeps the bobber told where the angler is, and takes it away when the rod is put down or left too far behind.
	void ServerTickFishing(FMcEntityManager& Entities);
	// Server: winds the bobber in: the catch thrown to the player, the rod worn.
	void ServerReel(FMcEntityManager& Entities, class FMcFishingBobber& Bobber);
	class FMcFishingBobber* FindBobber(FMcEntityManager& Entities) const;

	// ---- Elytra (McPlayerElytra.cpp) ----
	// Owner: gliding, and the ticks a firework rocket still pulls.
	bool bGliding = false;
	int32 BoostTicks = 0;
	// Owner: one tick of the glide in place of walking: starts it on a jump in the air, ends it on landing, in water or
	// lava, on a ladder or vine, or with the wings worn out. True when the glide moved the body this tick.
	bool TickGlide(const FMcMoveInput& Input, bool bJumpPressed);
	// What everyone sees: the glider's pose, and elytra on the back.
	UPROPERTY(Replicated)
	bool bGlidePose = false;
	UPROPERTY(Replicated)
	bool bWearsElytra = false;
	// The item in the head slot, for a head or carved pumpkin drawn on the head.
	UPROPERTY(Replicated)
	FName WornHead;
	// Server: ticks of gliding, for the wings' wear.
	int32 ServerGlideTicks = 0;
	bool bServerGliding = false;
	// Server: wears the wings while gliding.
	void ServerTickElytra();
	// Server: a rocket used while gliding: used up, and the pull starts on the owner's machine.
	bool ServerBoost();
	// The owner flew into a wall this hard.
	UFUNCTION(Server, Reliable)
	void ServerFlewIntoWall(float Damage);
	UFUNCTION(Client, Reliable)
	void ClientBoost(int32 Ticks);

	// ---- Totem of Undying (McTotem.cpp) ----
	// Server: a blow that would have killed: a totem in either hand is used up instead (McTotem::TryUse), with its
	// sound and its show for everyone. True if it saved the player.
	bool ServerTryTotem();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastTotem();
	// Every machine: ticks since the totem went off, -1 if not lately; the sparks fly for the first ParticleTicks.
	float TotemAge = -1.0f;
	void TickTotem(float DeltaSeconds);
public:
	// Ticks since this player's totem went off (its flight across their screen), or -1.
	float GetTotemAge() const { return TotemAge; }
private:

	// The body drawn for players this machine isn't playing; null on a dedicated server.
	TWeakObjectPtr<class UMcPlayerModelComponent> Model;
	// The player's own arm and what it holds, in first person.
	TWeakObjectPtr<class UMcHandComponent> Hand;
	// The outline round the aimed block and the cracks on the dug one, in the world.
	TWeakObjectPtr<class UMcBlockHighlightComponent> Highlight;

	TOptional<FMcBlockHit> Target;
	// Seconds until holding a button breaks or places again.
	float BreakCooldown = 0.0f;
	float PlaceCooldown = 0.0f;

	// ---- Owner side ----
	// The block the dig button is held on, so the server is told when it changes.
	TOptional<FIntVector> DigSent;
	bool bUsingSent = false;
	// The local player is eating what is in hand, and how long that takes (for the hand's animation).
	bool bEatingLocal = false;
	float EatSecondsLocal = 1.6f;
	int32 LastSelectedSent = -1;

	// ---- Server side ----
	float ServerAccumulator = 0.0f;
	// Whether a position has come (or the owner has moved themselves) yet; before it Body is at the origin.
	bool bServerPositionKnown = false;
	bool bServerInitialised = false;
	bool bServerDead = false;
	// When the next ender pearl may be thrown (a second's wait after each).
	double PearlReadyTime = 0.0;
	bool bServerSprint = false;
	bool bServerFlying = false;
	bool bServerWasOnGround = true;
	// Server: for the splash on entering water, the swimming sound's count of blocks, and putting a fire out.
	bool bServerWasInWater = false;
	bool bServerWasInBubble = false;
	double ServerSwum = 0.0;
	FVector ServerPreviousPosition = FVector::ZeroVector;
	McSurvival::FFallTracker ServerFall;
	McSurvival::EDifficulty Difficulty = McSurvival::EDifficulty::Normal;
	FMcGameRules GameRules;
	TOptional<FIntVector> DigPos;
	float DigProgress = 0.0f;
	// How far the dig has gone, in tenths (0 to 9, -1 for none), and on what block: the digger's client draws the cracks.
	UPROPERTY(Replicated)
	int32 DigStage = -1;
	UPROPERTY(Replicated)
	FIntVector DigBlockPos = FIntVector::ZeroValue;
	// Ticks spent on the block being dug; it is hit (with its sound) every 4.
	int32 DigTicks = 0;
	int32 EatTicks = 0;
	// Client: ticks spent eating (the crumbs follow the munching), and the colours of the food's sprite they take.
	int32 EatCrumbTicks = 0;
	// The bow: server ticks the draw has been held (and the slot it began in), and the client's seconds drawn and the
	// field of view it had before the zoom of the draw.
	int32 BowTicks = 0;
	int32 BowSlot = -1;
	float BowSecondsLocal = 0.0f;
	bool bBowLocal = false;
	// The side of the last hit on the owner, for the view's roll while the hurt time runs down.
	float HurtSide = 0.0f;
	float BowBaseFov = 0.0f;
	// Lets the arrow go after DrawTicks of drawing.
	void ServerShootBow(int32 DrawTicks);
	FName EatCrumbItem;
	TArray<FColor> EatCrumbColours;
	McSound::FStepCounter ServerSteps;
	// The health at the end of the last tick, for the hurt sound whatever did the damage; below 0 until the first.
	float ServerLastHealth = -1.0f;
	// Ticks since the last swing, for how charged the next is.
	int32 ServerAttackTicks = 100;
	bool bServerUsing = false;
	FRandomStream DropRandom{ 11 };
};
