// The player: walks, sprints, sneaks and jumps with Minecraft's movement, and flies like creative mode after a
// double tap of Space. Movement runs at 20 ticks a second; the camera is drawn between the last two ticks.
// WASD moves, Space jumps, Shift sneaks, Ctrl sprints. The left button breaks blocks, the right places the
// hotbar's selected block, the middle picks the targeted block; 1-9 and the wheel choose a hotbar slot.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "McPhysics.h"
#include "McRaycast.h"
#include "McPlayerPawn.generated.h"

class FMcBlockWorld;
class UCameraComponent;

UCLASS()
class MCPLAYER_API AMcPlayerPawn : public APawn
{
	GENERATED_BODY()

public:
	AMcPlayerPawn();

	// Degrees per mouse count.
	UPROPERTY(EditAnywhere, Category = "Mc")
	float MouseSensitivity = 0.15f;

	// Whether a double tap of Space starts and stops flying, as in creative mode.
	UPROPERTY(EditAnywhere, Category = "Mc")
	bool bMayFly = true;

	UPROPERTY(VisibleAnywhere, Category = "Mc")
	TObjectPtr<UCameraComponent> Camera;

	// The blocks to move through. Until it is set, or while the chunk underfoot isn't loaded, the player
	// stays where it is.
	void SetBlockWorld(const FMcBlockWorld* InWorld) { BlockWorld = InWorld; }
	bool HasBlockWorld() const { return BlockWorld != nullptr; }

	// Puts the eyes at a Minecraft position (x, y, z in blocks), looking along Minecraft's yaw and pitch
	// (yaw 0 faces south, pitch 90 looks straight down), and starts flying so the view stays put.
	void SetView(const FVector& EyePos, float McYaw, float McPitch);

	// Changes blocks for the player. Until the server takes requests (M3's networking), the game mode points
	// this straight at the world actor.
	TFunction<void(const FIntVector& Pos, FMcStateId State)> SetBlockHandler;
	// Swings at a mob in front of the player: the look direction. Set by the controller.
	TFunction<void(const FVector& Look)> AttackHandler;

	static constexpr int32 HotbarSize = 9;
	// Block states in the hotbar; 0 (air) is an empty slot. Creative-style until items and inventories (M7).
	TArray<FMcStateId> Hotbar;
	int32 SelectedSlot = 0;

	// The block under the crosshair, as of the last frame.
	const TOptional<FMcBlockHit>& GetTarget() const { return Target; }

	bool IsFlying() const { return bFlying; }
	const FMcBody& GetBody() const { return Body; }

	// Where the player looks (Minecraft yaw and pitch, degrees) and whether they crouch, for drawing their body on
	// other machines.
	float GetLookYaw() const { return McLookYaw; }
	float GetLookPitch() const { return McLookPitch; }
	bool IsSneakPosed() const { return bSneakPose; }

protected:
	virtual void PossessedBy(AController* NewController) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Where the owning client's player is now, once a tick, in Minecraft block coordinates (feet), and where they look.
	UFUNCTION(Server, Unreliable)
	void ServerMove(FVector FeetPos, float NewYaw, float NewPitch, bool bNewSneaking);

private:
	void GameTick(const FMcMoveInput& Input);
	void UpdateTarget();
	void UseBlocks(APlayerController& Player);
	void FillDefaultHotbar();
	float GetEyeHeight() const;
	void ShowPosition() const;

	const FMcBlockWorld* BlockWorld = nullptr;
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

	// The body drawn for players this machine isn't playing; null on a dedicated server.
	TWeakObjectPtr<class UMcPlayerModelComponent> Model;

	TOptional<FMcBlockHit> Target;
	// Seconds until holding a button breaks or places again.
	float BreakCooldown = 0.0f;
	float PlaceCooldown = 0.0f;
};
