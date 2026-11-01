#include "McChestComponent.h"
#include "McBell.h"
#include "McConduit.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McChestLink.h"
#include "McChunk.h"
#include "McBlockEntity.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "McEntityModel.h"

namespace
{
	bool IsNamed(FMcStateId State, const TCHAR* Name)
	{
		const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
		return Registry.IsValidState(State) && Registry.GetBlockOf(State).Name == FName(Name);
	}

	bool IsEnchantingTable(FMcStateId State)
	{
		return IsNamed(State, TEXT("minecraft:enchanting_table"));
	}
}

void FMcTableBook::Tick(bool bNear, const FVector2D& ToPlayer, FRandomStream& Random)
{
	ORot = Rot;
	OOpen = Open;
	OFlip = Flip;
	++Time;
	// Near, it turns to the player and opens a tenth a tick; away, it shuts as fast and its aim creeps round 0.02 a tick.
	// The pages' target jumps by a whole -3 to 3 pages every tick while it is under half open, else about one tick in 45
	// (42 in 1885 ticks), the sizes counted from 70 jumps.
	if (bNear)
	{
		TRot = static_cast<float>(FMath::Atan2(ToPlayer.Y, ToPlayer.X));
		Open = FMath::Min(Open + 0.1f, 1.0f);
		if (Open < 0.5f || Random.RandRange(0, 44) == 0)
		{
			static const int32 Steps[] = { -3, -2, -1, 1, 2, 3 };
			static const int32 Counts[] = { 4, 10, 14, 27, 11, 4 };
			int32 Pick = Random.RandRange(0, 69);
			int32 Index = 0;
			while (Pick >= Counts[Index])
			{
				Pick -= Counts[Index++];
			}
			FlipT += Steps[Index];
		}
	}
	else
	{
		TRot += 0.02f;
		Open = FMath::Max(Open - 0.1f, 0.0f);
	}
	// Rot closes 0.4 of the (shorter way round) gap a tick; kept within a turn of nought.
	Rot += 0.4f * FMath::FindDeltaAngleRadians(Rot, TRot);
	if (FMath::Abs(Rot) > PI)
	{
		const float Turn = Rot > 0.0f ? -TWO_PI : TWO_PI;
		Rot += Turn;
		ORot += Turn;
		TRot += Turn;
	}
	// The flip springs towards its target, at most 0.2 a tick, its speed keeping a tenth of the last tick's.
	FlipA += (FMath::Clamp((FlipT - Flip) * 0.4f, -0.2f, 0.2f) - FlipA) * 0.9f;
	Flip += FlipA;
}

float FMcTableBook::PageTurn(float Flip, int32 Page)
{
	// Measured from frames of the pages mid-turn (ref_bf_15 to 22 and 67 to 69 against the logged flip): each page goes
	// over in the second part of its own half-offset turn, standing up at three quarters.
	const float Phase = Flip + Page * 0.5f;
	return FMath::Clamp(1.6f * (Phase - FMath::FloorToFloat(Phase)) - 0.7f, 0.0f, 1.0f);
}

UMcChestComponent::UMcChestComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

FName UMcChestComponent::GetChestModel(FMcStateId State, float& OutYaw, FIntVector& OutPartner, bool bChristmas)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!Registry.IsValidState(State))
	{
		return NAME_None;
	}
	FString Name = Registry.GetBlockOf(State).Name.ToString();
	if (!Name.RemoveFromStart(TEXT("minecraft:")) || !Name.EndsWith(TEXT("chest")))
	{
		return NAME_None;
	}
	Name.RemoveFromStart(TEXT("waxed_"));
	FString Picture;
	if (Name == TEXT("chest") || Name == TEXT("trapped_chest"))
	{
		Picture = bChristmas ? TEXT("christmas") : Name == TEXT("chest") ? TEXT("normal") : TEXT("trapped");
	}
	else if (Name == TEXT("ender_chest"))
	{
		Picture = TEXT("ender");
	}
	else if (Name == TEXT("copper_chest"))
	{
		Picture = TEXT("copper");
	}
	else if (Name == TEXT("exposed_copper_chest") || Name == TEXT("weathered_copper_chest") || Name == TEXT("oxidized_copper_chest"))
	{
		FString Age;
		Name.Split(TEXT("_"), &Age, nullptr);
		Picture = TEXT("copper_") + Age;
	}
	else
	{
		return NAME_None;
	}
	// A chest's front is the model's -z, which faces north unturned.
	const FName Facing = Registry.GetPropertyValue(State, TEXT("facing"));
	OutYaw = Facing == TEXT("east") ? 90.0f : Facing == TEXT("south") ? 180.0f : Facing == TEXT("west") ? 270.0f : 0.0f;
	OutPartner = McChestLink::GetPartnerOffset(Facing, Registry.GetPropertyValue(State, TEXT("type")));
	if (OutPartner != FIntVector::ZeroValue)
	{
		// Which side of the turned model the other half is on picks the half's picture.
		const double Radians = FMath::DegreesToRadians(OutYaw);
		const double Side = OutPartner.X * FMath::Cos(Radians) + OutPartner.Z * FMath::Sin(Radians);
		Picture += Side > 0.0 ? TEXT("_left") : TEXT("_right");
	}
	return FName(TEXT("mc:chest/") + Picture);
}

FName UMcChestComponent::GetShulkerModel(FMcStateId State, FQuat& OutRotation)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!Registry.IsValidState(State))
	{
		return NAME_None;
	}
	FString Name = Registry.GetBlockOf(State).Name.ToString();
	if (!Name.RemoveFromStart(TEXT("minecraft:")) || !Name.EndsWith(TEXT("shulker_box")))
	{
		return NAME_None;
	}
	// "red_shulker_box" is drawn with "shulker_red", the plain box with "shulker".
	Name.RemoveFromEnd(TEXT("shulker_box"));
	Name.RemoveFromEnd(TEXT("_"));
	// The lid points the way the box faces: the model's up (Unreal +z) turned onto that side.
	const FName Facing = Registry.GetPropertyValue(State, TEXT("facing"));
	OutRotation = Facing == TEXT("down") ? FQuat(FVector::XAxisVector, PI)
		: Facing == TEXT("north") ? FQuat(FVector::XAxisVector, HALF_PI)
		: Facing == TEXT("south") ? FQuat(FVector::XAxisVector, -HALF_PI)
		: Facing == TEXT("east") ? FQuat(FVector::YAxisVector, HALF_PI)
		: Facing == TEXT("west") ? FQuat(FVector::YAxisVector, -HALF_PI)
		: FQuat::Identity;
	return FName(TEXT("mc:shulker_box/shulker") + (Name.IsEmpty() ? FString() : TEXT("_") + Name));
}

FName UMcChestComponent::GetHeadModel(FMcStateId State, FVector& OutCentre, float& OutYaw)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!Registry.IsValidState(State))
	{
		return NAME_None;
	}
	FString Name = Registry.GetBlockOf(State).Name.ToString();
	if (!Name.RemoveFromStart(TEXT("minecraft:")) || Name == TEXT("piston_head"))
	{
		return NAME_None;
	}
	const bool bWall = Name.Contains(TEXT("_wall_"));
	Name.ReplaceInline(TEXT("_wall_"), TEXT("_"));
	const TCHAR* Kind = Name == TEXT("skeleton_skull") ? TEXT("skeleton") : Name == TEXT("wither_skeleton_skull") ? TEXT("wither_skeleton")
		: Name == TEXT("zombie_head") ? TEXT("zombie") : Name == TEXT("creeper_head") ? TEXT("creeper")
		: Name == TEXT("piglin_head") ? TEXT("piglin") : Name == TEXT("player_head") ? TEXT("player")
		: Name == TEXT("dragon_head") ? TEXT("dragon") : nullptr;
	if (!Kind)
	{
		return NAME_None;
	}
	if (bWall)
	{
		// Its face points out of the wall, its back against it.
		const FName Facing = Registry.GetPropertyValue(State, TEXT("facing"));
		const FVector Out = Facing == TEXT("north") ? FVector(0, 0, -1) : Facing == TEXT("south") ? FVector(0, 0, 1)
			: Facing == TEXT("west") ? FVector(-1, 0, 0) : FVector(1, 0, 0);
		OutCentre = FVector(0.5, 0.25, 0.5) - Out * 0.25;
		OutYaw = Facing == TEXT("west") ? 90.0f : Facing == TEXT("north") ? 180.0f : Facing == TEXT("east") ? 270.0f : 0.0f;
	}
	else
	{
		// Rotation 0 faces north, 4 east, 2 north-east: half a turn from a sign's (measured in vanilla, ref_scene_heads).
		OutCentre = FVector(0.5, 0.0, 0.5);
		OutYaw = FCString::Atoi(*Registry.GetPropertyValue(State, TEXT("rotation")).ToString()) * 22.5f + 180.0f;
	}
	return FName(FString(TEXT("mc:head/")) + Kind);
}

FQuat UMcChestComponent::RotationFromAxes(const FVector& X, const FVector& Y, const FVector& Z)
{
	// McEntityModel puts model (x, y, z) at Unreal (x, z, y), as McCoords does blocks.
	const auto ToUnreal = [](const FVector& V) { return FVector(V.X, V.Z, V.Y); };
	return FQuat(FMatrix(ToUnreal(X), ToUnreal(Z), ToUnreal(Y), FVector::ZeroVector)).GetNormalized();
}

bool UMcChestComponent::GetLecternBook(FMcStateId State, FVector& OutCentre, FQuat& OutRotation)
{
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	if (!Registry.IsValidState(State) || Registry.GetBlockOf(State).Name != FName(TEXT("minecraft:lectern"))
		|| Registry.GetPropertyValue(State, TEXT("has_book")) != FName(TEXT("true")))
	{
		return false;
	}
	// Facing north, the lectern's top (16 by 13 pixels, its top face 16 up, tipped 22.5 degrees about the block's middle)
	// rises to the south, so whoever reads stands north. The book lies on it, its spine up the slope and its right half
	// on the reader's right (west).
	const double Sin = FMath::Sin(FMath::DegreesToRadians(22.5));
	const double Cos = FMath::Cos(FMath::DegreesToRadians(22.5));
	const FVector Up(0.0, Cos, -Sin);
	FVector Axes[3] = { FVector(-1.0, 0.0, 0.0), FVector(0.0, Sin, Cos), Up };
	// The middle of the top face, turned with the top, and a hair above it.
	FVector Centre = FVector(8.0, 8.0 + 8.0 * Cos + 1.5 * Sin, 8.0 - 8.0 * Sin + 1.5 * Cos) + Up * 0.05;
	const FName Facing = Registry.GetPropertyValue(State, TEXT("facing"));
	const int32 Quarters = Facing == TEXT("east") ? 1 : Facing == TEXT("south") ? 2 : Facing == TEXT("west") ? 3 : 0;
	// Each quarter turns north to east about the block's middle.
	const auto Turn = [](const FVector& V) { return FVector(-V.Z, V.Y, V.X); };
	for (int32 Quarter = 0; Quarter < Quarters; ++Quarter)
	{
		for (FVector& Axis : Axes)
		{
			Axis = Turn(Axis);
		}
		Centre = Turn(Centre - FVector(8.0, 0.0, 8.0)) + FVector(8.0, 0.0, 8.0);
	}
	OutCentre = Centre / 16.0;
	OutRotation = RotationFromAxes(Axes[0], Axes[1], Axes[2]);
	return true;
}

void UMcChestComponent::RingBell(const FIntVector& Pos, int32 Side)
{
	FChest* Bell = Chests.Find(Pos);
	if (Bell && McBell::IsBell(Bell->State))
	{
		Bell->StruckSide = Side;
		Bell->SinceStruck = 0.0f;
	}
}

FVector2D UMcChestComponent::GetBellSwing(const FIntVector& Pos) const
{
	const FChest* Bell = Chests.Find(Pos);
	if (!Bell || Bell->SinceStruck < 0.0f)
	{
		return FVector2D::ZeroVector;
	}
	// Away from the blow: struck on the north its bottom goes south (negative pitch), struck on the west it goes east.
	const float Swing = McBell::GetSwing(Bell->SinceStruck * 20.0f);
	switch (Bell->StruckSide)
	{
	case 0: return FVector2D(-Swing, 0.0f);
	case 1: return FVector2D(Swing, 0.0f);
	case 2: return FVector2D(0.0f, Swing);
	default: return FVector2D(0.0f, -Swing);
	}
}

void UMcChestComponent::SetWorld(FMcBlockWorld* InWorld)
{
	Unhook();
	for (auto& Pair : Models)
	{
		if (Pair.Value)
		{
			Pair.Value->DestroyComponent();
		}
	}
	Models.Reset();
	Chests.Reset();
	ChestsByChunk.Reset();
	World = InWorld;
	if (!World)
	{
		return;
	}
	const FMcBlockRegistry& Registry = FMcBlockRegistry::Get();
	IsChest.Init(false, Registry.NumStates());
	for (int32 Id = 0; Id < Registry.NumStates(); ++Id)
	{
		float Yaw;
		FIntVector Partner;
		FQuat Rotation;
		FVector Centre;
		IsChest[Id] = !GetChestModel(static_cast<FMcStateId>(Id), Yaw, Partner).IsNone() || !GetShulkerModel(static_cast<FMcStateId>(Id), Rotation).IsNone()
			|| McBell::IsBell(static_cast<FMcStateId>(Id)) || IsEnchantingTable(static_cast<FMcStateId>(Id))
			|| GetLecternBook(static_cast<FMcStateId>(Id), Centre, Rotation) || !GetHeadModel(static_cast<FMcStateId>(Id), Centre, Yaw).IsNone()
			|| IsNamed(static_cast<FMcStateId>(Id), TEXT("minecraft:decorated_pot")) || McConduit::IsConduit(static_cast<FMcStateId>(Id))
			|| IsNamed(static_cast<FMcStateId>(Id), TEXT("minecraft:spawner"));
	}
	AddedHandle = World->OnChunkAdded.AddUObject(this, &UMcChestComponent::Scan);
	ChangedHandle = World->OnChunkChanged.AddUObject(this, &UMcChestComponent::Scan);
	RemovedHandle = World->OnChunkRemoved.AddUObject(this, &UMcChestComponent::Clear);
	EntityHandle = World->OnBlockEntityChanged.AddUObject(this, &UMcChestComponent::EntityChanged);
	for (const auto& Pair : World->GetChunks())
	{
		Scan(Pair.Key);
	}
}

void UMcChestComponent::Unhook()
{
	if (World)
	{
		World->OnChunkAdded.Remove(AddedHandle);
		World->OnChunkChanged.Remove(ChangedHandle);
		World->OnChunkRemoved.Remove(RemovedHandle);
		World->OnBlockEntityChanged.Remove(EntityHandle);
	}
}

FName UMcChestComponent::GetPotSides(const FIntVector& Pos) const
{
	const FMcChunk* Chunk = World ? World->FindChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)) : nullptr;
	const FMcBlockEntity* Entity = Chunk ? Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15) : nullptr;
	if (!Entity || Entity->Kind != EMcBlockEntityKind::Pot)
	{
		return NAME_None;
	}
	TArray<FString> Sides;
	for (int32 Side = 0; Side < 4; ++Side)
	{
		Sides.Add(Entity->Slots.IsValidIndex(Side) && !Entity->Slots[Side].IsEmpty() ? Entity->Slots[Side].Item.ToString() : FString(TEXT("minecraft:brick")));
	}
	return FName(FString::Join(Sides, TEXT(",")));
}

FName UMcChestComponent::GetSpawnerMob(const FIntVector& Pos) const
{
	const FMcChunk* Chunk = World ? World->FindChunk(FIntPoint(Pos.X >> 4, Pos.Z >> 4)) : nullptr;
	const FMcBlockEntity* Entity = Chunk ? Chunk->FindBlockEntity(Pos.X & 15, Pos.Y, Pos.Z & 15) : nullptr;
	return Entity && Entity->Kind == EMcBlockEntityKind::Spawner ? Entity->EntityId : NAME_None;
}

void UMcChestComponent::EntityChanged(const FIntVector& Pos)
{
	const FChest* Known = Chests.Find(Pos);
	const TObjectPtr<UMcEntityModelComponent>* Model = Models.Find(Pos);
	if (Known && Known->Shape == EShape::Pot && Model && *Model)
	{
		(*Model)->SetKind(FName(TEXT("mc:decorated_pot")), GetPotSides(Pos), 0);
	}
	if (Known && Known->Shape == EShape::Spawner && Model && *Model)
	{
		(*Model)->SetKind(GetSpawnerMob(Pos), NAME_None, 0);
	}
}

void UMcChestComponent::SpawnerCycled(const FIntVector& Pos)
{
	if (FChest* Spawner = Chests.Find(Pos); Spawner && Spawner->Shape == EShape::Spawner)
	{
		Spawner->SpawnerDelay = 200;
	}
}

void UMcChestComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetWorld(nullptr);
	Super::EndPlay(EndPlayReason);
}

void UMcChestComponent::Clear(FIntPoint ChunkPos)
{
	TArray<FIntVector> Gone;
	ChestsByChunk.RemoveAndCopyValue(ChunkPos, Gone);
	for (const FIntVector& Pos : Gone)
	{
		Chests.Remove(Pos);
		TObjectPtr<UMcEntityModelComponent> Model;
		if (Models.RemoveAndCopyValue(Pos, Model) && Model)
		{
			Model->DestroyComponent();
		}
	}
}

void UMcChestComponent::Scan(FIntPoint ChunkPos)
{
	const FMcChunk* Chunk = World ? World->FindChunk(ChunkPos) : nullptr;
	if (!Chunk)
	{
		Clear(ChunkPos);
		return;
	}
	// The chests in the chunk now, from the sections that hold any.
	TMap<FIntVector, FMcStateId> Found;
	TArray<FMcStateId> States;
	States.SetNumUninitialized(4096);
	for (int32 Section = 0; Section < Chunk->NumSections(); ++Section)
	{
		const FMcChunkSection& Blocks = Chunk->GetSection(Section);
		if (Blocks.IsEmpty())
		{
			continue;
		}
		Blocks.Blocks.CopyTo(States);
		for (int32 Index = 0; Index < 4096; ++Index)
		{
			const FMcStateId State = States[Index];
			if (IsChest.IsValidIndex(State) && IsChest[State])
			{
				Found.Add(FIntVector(ChunkPos.X * 16 + (Index & 15), Chunk->SectionMinY(Section) + (Index >> 8), ChunkPos.Y * 16 + ((Index >> 4) & 15)), State);
			}
		}
	}
	TArray<FIntVector>& Listed = ChestsByChunk.FindOrAdd(ChunkPos);
	for (int32 Index = Listed.Num() - 1; Index >= 0; --Index)
	{
		if (!Found.Contains(Listed[Index]))
		{
			Chests.Remove(Listed[Index]);
			TObjectPtr<UMcEntityModelComponent> Model;
			if (Models.RemoveAndCopyValue(Listed[Index], Model) && Model)
			{
				Model->DestroyComponent();
			}
			Listed.RemoveAt(Index);
		}
	}
	const FDateTime Now = FDateTime::Now();
	const bool bChristmas = Now.GetMonth() == 12 && Now.GetDay() >= 24 && Now.GetDay() <= 26;
	for (const auto& Pair : Found)
	{
		FChest* Known = Chests.Find(Pair.Key);
		if (Known && Known->State == Pair.Value)
		{
			continue;
		}
		if (!Known)
		{
			Listed.Add(Pair.Key);
			Known = &Chests.Add(Pair.Key);
		}
		Known->State = Pair.Value;
		float Yaw = 0.0f;
		FQuat Rotation = FQuat::Identity;
		FName Kind = GetChestModel(Pair.Value, Yaw, Known->Partner, bChristmas);
		// A shulker box's model is about the block's middle, turned to its facing. A bell's body hangs the same in every
		// frame, unturned. A lectern's book lies on its top; an enchanting table's is placed each frame as it floats.
		FVector Centre(0.5, 0.0, 0.5);
		if (!Kind.IsNone())
		{
			Known->Shape = EShape::Chest;
		}
		else if (McBell::IsBell(Pair.Value))
		{
			Known->Shape = EShape::Bell;
			Kind = FName(TEXT("mc:bell"));
		}
		else if (GetLecternBook(Pair.Value, Centre, Rotation))
		{
			Known->Shape = EShape::LecternBook;
			Kind = FName(TEXT("mc:book"));
		}
		else if (const FName Head = GetHeadModel(Pair.Value, Centre, Yaw); !Head.IsNone())
		{
			Known->Shape = EShape::Head;
			Kind = Head;
		}
		else if (IsNamed(Pair.Value, TEXT("minecraft:decorated_pot")))
		{
			// Its front faces the way it was placed.
			Known->Shape = EShape::Pot;
			Kind = FName(TEXT("mc:decorated_pot"));
			const FName Facing = FMcBlockRegistry::Get().GetPropertyValue(Pair.Value, TEXT("facing"));
			Yaw = Facing == TEXT("west") ? 90.0f : Facing == TEXT("north") ? 180.0f : Facing == TEXT("east") ? 270.0f : 0.0f;
		}
		else if (IsNamed(Pair.Value, TEXT("minecraft:spawner")))
		{
			// The mob of its block entity (none until that comes), turning; a new spawner's wait is 20, as the server's.
			Known->Shape = EShape::Spawner;
			Known->SpawnerDelay = 20;
			Kind = GetSpawnerMob(Pair.Key);
			Centre = FVector(0.5);
		}
		else if (McConduit::IsConduit(Pair.Value))
		{
			// Looked at again straight away, in case it is awake.
			Known->Shape = EShape::Conduit;
			Known->bAwake = false;
			Known->UntilCheck = 0.0f;
			Kind = FName(TEXT("mc:conduit/shell"));
			Centre = FVector(0.5);
		}
		else if (IsEnchantingTable(Pair.Value))
		{
			Known->Shape = EShape::TableBook;
			Kind = FName(TEXT("mc:book"));
			Centre = FVector(0.5, 1.0, 0.5);
		}
		else
		{
			Known->Shape = EShape::Shulker;
			Kind = GetShulkerModel(Pair.Value, Rotation);
			Centre = FVector(0.5);
		}
		TObjectPtr<UMcEntityModelComponent>& Model = Models.FindOrAdd(Pair.Key);
		if (!Model)
		{
			Model = NewObject<UMcEntityModelComponent>(GetOwner());
			Model->SetupAttachment(this);
			Model->SetUsingAbsoluteLocation(true);
			Model->SetUsingAbsoluteRotation(true);
			Model->RegisterComponent();
		}
		Model->SetWorldLocationAndRotation(McCoords::BlockToUnreal(FVector(Pair.Key) + Centre), Rotation);
		Model->SetKind(Kind, Known->Shape == EShape::Pot ? GetPotSides(Pair.Key) : NAME_None, 0);
		Model->SetLook(Yaw, Yaw, 0.0f);
	}
	if (Listed.IsEmpty())
	{
		ChestsByChunk.Remove(ChunkPos);
	}
}

void UMcChestComponent::TickSpawner(const FIntVector& Pos, FChest& Spawner, UMcEntityModelComponent& Model, const FVector& Viewer, float DeltaTime)
{
	// Development: -McSpawnerSpin=<degrees> holds every spawner's mob at that spin (for a -McShot of it);
	// -McSpawnerPose=<size>,<nose up>,<pivot>,<drop> tries another pose (see below).
	static float HeldSpin = -1.0f;
	static const bool bHeld = FParse::Value(FCommandLine::Get(), TEXT("-McSpawnerSpin="), HeldSpin);
	static float Size = 0.5f / 0.9f, NoseUp = 40.0f, Pivot = 0.5f, Drop = 0.27f;
	static const bool bPosed = [&]()
	{
		FString Text;
		TArray<FString> Parts;
		if (FParse::Value(FCommandLine::Get(), TEXT("-McSpawnerPose="), Text, /*bShouldStopOnSeparator=*/ false) && Text.ParseIntoArray(Parts, TEXT(",")) == 4)
		{
			Size = FCString::Atof(*Parts[0]);
			NoseUp = FCString::Atof(*Parts[1]);
			Pivot = FCString::Atof(*Parts[2]);
			Drop = FCString::Atof(*Parts[3]);
		}
		return true;
	}();
	(void)bPosed;

	// 20 times a second, while this machine's player is within 16 blocks of its middle (vanilla stood still with the
	// player 17 off), the client's wait counts down to 0 and the mob turns 1000 / (wait + 200) degrees: 2.5 a tick just
	// after a cycle (the wait starts again at 200), 5 a tick once it is over (mc-reference spawner_log.txt, every tick
	// of 3400; spawner_rule.txt: a new spawner's client wait starts at 20, as the server's).
	const FVector Middle = FVector(Pos) + FVector(0.5);
	const bool bNear = FVector::DistSquared(Viewer - FVector(0.0, 1.62, 0.0), Middle) <= 16.0 * 16.0;
	for (Spawner.SpinTickPart = FMath::Min(Spawner.SpinTickPart + DeltaTime * 20.0f, 20.0f); Spawner.SpinTickPart >= 1.0f; Spawner.SpinTickPart -= 1.0f)
	{
		Spawner.OSpin = Spawner.Spin;
		if (bNear)
		{
			Spawner.SpawnerDelay = FMath::Max(0, Spawner.SpawnerDelay - 1);
			Spawner.Spin = FMath::Fmod(Spawner.Spin + 1000.0f / (Spawner.SpawnerDelay + 200.0f), 360.0f);
		}
	}
	float Spin = Spawner.OSpin + FMath::FindDeltaAngleDegrees(Spawner.OSpin, Spawner.Spin) * FMath::Min(Spawner.SpinTickPart, 1.0f);
	if (bHeld)
	{
		Spin = HeldSpin;
	}

	// The mob drawn at Size (0.556 of its own size, nose up 40: ref_spawner_pig_south/top, the pig's bigger side 0.5 of
	// a block against 0.5 to 0.56 and noses 30 to 50 at the logged spin), and shrunk further only when its bigger
	// side is over a block: a small mob isn't blown up to fill the cage (ref_display_chicken: the chicken's beak,
	// 4 pixels, is 9.6 px across about 2 blocks from the eye, about 0.55 of its size, not the 0.71 that a bigger
	// side of 0.5 would make it). Turned by the spin about the upright through the block's middle, tipped nose up,
	// its feet Pivot over the block's floor (less Drop along its own up).
	const FVector2D Hitbox = GetMobSize ? GetMobSize(Model.GetKind()) : FVector2D(1.0, 1.0);
	const float Scale = Size / FMath::Max3(Hitbox.X, Hitbox.Y, 1.0);
	// The mob faces east at spin 0 and turns ten times the spin toward the south: at the logged spin 0
	// (spawner_mob.txt, ref_spawner_mob_pig) vanilla's pig is side on from the south, head to the east; at 256.2643
	// (spawner_pig.txt, mob yrot, xrot, headrot and bodyrot all 0) it faces south-east, mirror alike from the south and
	// the east (ref_spawner_pig_south/east/top), which neither 256.26 nor -256.26 degrees gives.
	const double Yaw = FMath::DegreesToRadians(10.0 * Spin - 90.0);
	const double Tip = FMath::DegreesToRadians(NoseUp);
	// The model's own z runs from its face to its tail, so it faces along -z: at spin 0, south.
	const FVector Side(-FMath::Cos(Yaw), 0.0, -FMath::Sin(Yaw));
	const FVector Facing(-FMath::Sin(Yaw), 0.0, FMath::Cos(Yaw));
	const FVector Up(0.0, 1.0, 0.0);
	const FVector Forward = Facing * FMath::Cos(Tip) + Up * FMath::Sin(Tip);
	const FVector Over = Up * FMath::Cos(Tip) - Facing * FMath::Sin(Tip);
	const FVector Feet = FVector(Pos) + FVector(0.5, Pivot, 0.5) - Over * Drop;
	Model.SetWorldLocationAndRotation(McCoords::BlockToUnreal(Feet), RotationFromAxes(Side, Over, -Forward));
	Model.SetModelScale(Scale);
	Model.SetLook(0.0f, 0.0f, 0.0f);
}

void UMcChestComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// Development: -McChestsOpen opens every chest (for a -McShot of their lids).
	static const bool bAllOpen = FParse::Param(FCommandLine::Get(), TEXT("McChestsOpen"));
	// Development: -McBellsSwung holds every bell at the top of a swing from the north (for a -McShot of it).
	static const bool bAllSwung = FParse::Param(FCommandLine::Get(), TEXT("McBellsSwung"));
	for (auto& Pair : Chests)
	{
		if (Pair.Value.SinceStruck >= 0.0f)
		{
			Pair.Value.SinceStruck += DeltaTime;
			if (Pair.Value.SinceStruck * 20.0f >= McBell::RingTicks)
			{
				Pair.Value.SinceStruck = -1.0f;
			}
		}
		if (bAllSwung && McBell::IsBell(Pair.Value.State))
		{
			Pair.Value.StruckSide = 0;
			Pair.Value.SinceStruck = 4.0f / 20.0f;
		}
	}

	// Where this machine's player looks from, for the enchanting tables' books.
	FVector Viewer(TNumericLimits<float>::Max());
	if (const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		FVector Location;
		FRotator Rotation;
		Player->GetPlayerViewPoint(Location, Rotation);
		Viewer = McCoords::UnrealToBlock(Location);
	}
	for (const auto& Pair : Models)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const FVector2D Swing = GetBellSwing(Pair.Key);
		Pair.Value->SetSwing(Swing.X, Swing.Y);
		if (FChest* Conduit = Chests.Find(Pair.Key); Conduit && Conduit->Shape == EShape::Conduit)
		{
			// Whether it is awake is looked at twice a second (its frame or water may change without it changing).
			Conduit->UntilCheck -= DeltaTime;
			if (Conduit->UntilCheck <= 0.0f && World)
			{
				Conduit->UntilCheck = 0.5f;
				const bool bAwake = McConduit::IsActive(*World, Pair.Key);
				const bool bHunting = bAwake && McConduit::CountFrame(*World, Pair.Key) == McConduit::GetFramePlaces().Num();
				if (bAwake != Conduit->bAwake || bHunting != Conduit->bHunting)
				{
					Conduit->bAwake = bAwake;
					Conduit->bHunting = bHunting;
					Pair.Value->SetKind(FName(bHunting ? TEXT("mc:conduit/hunting") : bAwake ? TEXT("mc:conduit/cage") : TEXT("mc:conduit/shell")), NAME_None, 0);
				}
			}
			// The eye looks at the player.
			const FVector Towards = Viewer - (FVector(Pair.Key) + FVector(0.5));
			const float EyeYaw = FMath::RadiansToDegrees(FMath::Atan2(-Towards.X, Towards.Z));
			const float EyePitch = -FMath::RadiansToDegrees(FMath::Atan2(Towards.Y, FVector2D(Towards.X, Towards.Z).Size()));
			Pair.Value->SetLook(0.0f, EyeYaw, EyePitch);
			continue;
		}
		if (FChest* Spawner = Chests.Find(Pair.Key); Spawner && Spawner->Shape == EShape::Spawner)
		{
			TickSpawner(Pair.Key, *Spawner, *Pair.Value, Viewer, DeltaTime);
			continue;
		}
		if (FChest* Book = Chests.Find(Pair.Key); Book && (Book->Shape == EShape::TableBook || Book->Shape == EShape::LecternBook))
		{
			if (Book->Shape == EShape::LecternBook)
			{
				// Open, each cover folded about 28 degrees up from flat, so the pages meet at the spine (measured in vanilla:
				// the near half's edge stands 3.8 pixels off the slope side on, ref_scene_lectern_side and _top).
				Pair.Value->SetBook(0.69f, 0.0f, 0.0f);
				continue;
			}
			// Over an enchanting table it bobs; with a player within 3 blocks it turns to them and opens, its loose pages
			// waving over and back, and without one it shuts and slowly turns round.
			// Near is a player's feet within 3 blocks of the table's middle (vanilla opens at 2.9 blocks level with the
			// table and not at 3.0, table_book_run2).
			const FVector Middle = FVector(Pair.Key) + FVector(0.5);
			const FVector Feet = Viewer - FVector(0.0, 1.62, 0.0);
			FMcTableBook& State = Book->Book;
			if (State.Time == 0)
			{
				Book->BookRandom.Initialize(GetTypeHash(Pair.Key));
			}
			// A long frame (a hitch) steps at most a second's ticks.
			for (Book->BookTickPart = FMath::Min(Book->BookTickPart + DeltaTime * 20.0f, 20.0f); Book->BookTickPart >= 1.0f; Book->BookTickPart -= 1.0f)
			{
				const FVector Towards = Feet - Middle;
				State.Tick(Towards.SizeSquared() < 9.0, FVector2D(Towards.X, Towards.Z), Book->BookRandom);
			}
			const float Part = FMath::Min(Book->BookTickPart, 1.0f);
			const float Rot = State.ORot + FMath::FindDeltaAngleRadians(State.ORot, State.Rot) * Part;
			const float Open = FMath::Lerp(State.OOpen, State.Open, Part);
			const float Flip = FMath::Lerp(State.OFlip, State.Flip, Part);
			const float Time = State.Time + Part;
			// Open, it lies tipped back, its pages up and towards the way it turns, its spine 10.4 degrees over level and
			// running away, each cover 37 degrees short of flat and the spine's middle 0.735 up, just under the table's top:
			// our model's gold corners fitted to vanilla's in ref_bf_05 to 14 and ref_book_above together, to 2 pixels (the
			// frames' bob, -0.02, taken off). Part open, the covers fold further as the book shuts, as our model's do (gold
			// corner fits of frames ref_bf_02 to 05). It bobs by two waves of its age in ticks: 0.010 blocks at 0.1 a tick
			// and 0.026 at 0.02 (fitted to the gold corners' height in 90 frames 14 ticks apart, to 0.1 pixel,
			// E:/mc-reference/scripts/table_book_bob.txt).
			const FVector Centre = Middle + FVector(0.0, 0.235 + 0.010 * FMath::Sin(0.1f * Time) - 0.026 * FMath::Sin(0.02f * Time), 0.0);
			const double Lean = FMath::DegreesToRadians(79.6);
			const float BookYaw = Rot - HALF_PI;
			const FVector Facing(-FMath::Sin(BookYaw), 0.0, FMath::Cos(BookYaw));
			const FVector Up(0.0, 1.0, 0.0);
			const FVector Pages = Facing * FMath::Cos(Lean) + Up * FMath::Sin(Lean);
			const FVector Spine = Up * FMath::Cos(Lean) - Facing * FMath::Sin(Lean);
			Pair.Value->SetWorldLocationAndRotation(McCoords::BlockToUnreal(Centre), RotationFromAxes(FVector::CrossProduct(Spine, Pages), Spine, Pages));
			Pair.Value->SetBook(0.59f * Open, FMcTableBook::PageTurn(Flip, 0), FMcTableBook::PageTurn(Flip, 1));
			continue;
		}
		const FChest* Chest = Chests.Find(Pair.Key);
		const FIntVector Partner = Chest ? Chest->Partner : FIntVector::ZeroValue;
		const bool bOpen = bAllOpen || (IsOpen && (IsOpen(Pair.Key) || (Partner != FIntVector::ZeroValue && IsOpen(Pair.Key + Partner))));
		Pair.Value->SetOpen(bOpen);
	}
}
