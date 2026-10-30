#include "McChestComponent.h"
#include "McBell.h"
#include "McConduit.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McChestLink.h"
#include "McChunk.h"
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
			|| IsNamed(static_cast<FMcStateId>(Id), TEXT("minecraft:decorated_pot")) || McConduit::IsConduit(static_cast<FMcStateId>(Id));
	}
	AddedHandle = World->OnChunkAdded.AddUObject(this, &UMcChestComponent::Scan);
	ChangedHandle = World->OnChunkChanged.AddUObject(this, &UMcChestComponent::Scan);
	RemovedHandle = World->OnChunkRemoved.AddUObject(this, &UMcChestComponent::Clear);
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
		Model->SetKind(Kind, NAME_None, 0);
		Model->SetLook(Yaw, Yaw, 0.0f);
	}
	if (Listed.IsEmpty())
	{
		ChestsByChunk.Remove(ChunkPos);
	}
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
				if (bAwake != Conduit->bAwake)
				{
					Conduit->bAwake = bAwake;
					Pair.Value->SetKind(FName(bAwake ? TEXT("mc:conduit/cage") : TEXT("mc:conduit/shell")), NAME_None, 0);
				}
			}
			// The eye looks at the player.
			const FVector Towards = Viewer - (FVector(Pair.Key) + FVector(0.5));
			const float EyeYaw = FMath::RadiansToDegrees(FMath::Atan2(-Towards.X, Towards.Z));
			const float EyePitch = -FMath::RadiansToDegrees(FMath::Atan2(Towards.Y, FVector2D(Towards.X, Towards.Z).Size()));
			Pair.Value->SetLook(0.0f, EyeYaw, EyePitch);
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
			Book->BookAge += DeltaTime;
			// Its middle about 0.91 up (measured, ref_book_level and ref_book_above); the bob is not measured yet.
			const FVector Centre = FVector(Pair.Key) + FVector(0.5, 0.91 + 0.05 * FMath::Sin(Book->BookAge * 2.0f), 0.5);
			const FVector Towards = Viewer - Centre;
			const bool bNear = Towards.SizeSquared() < 9.0;
			if (bNear)
			{
				const float Target = FMath::Atan2(-Towards.X, Towards.Z);
				Book->BookYaw += FMath::FindDeltaAngleRadians(Book->BookYaw, Target) * FMath::Min(1.0f, DeltaTime * 4.0f);
			}
			else
			{
				Book->BookYaw = FMath::UnwindRadians(Book->BookYaw + DeltaTime * 0.4f);
			}
			// Open, its covers lie about 10 degrees short of flat (measured in vanilla, ref_book_level and ref_book_above).
			Book->BookOpen = FMath::FInterpConstantTo(Book->BookOpen, bNear ? 0.89f : 0.0f, DeltaTime, 1.5f);
			for (int32 Page = 0; Page < 2; ++Page)
			{
				Book->BookFlip[Page] = 0.5f + 0.5f * FMath::Sin(Book->BookAge * (Page == 0 ? 1.3f : 0.8f));
			}
			// It lies tipped back, its pages up and towards the way it turns, its spine 11 degrees over level and running away
			// (measured in vanilla from a player 2.5 blocks off, ref_book_level and ref_book_above, the two fitting each other).
			const double Lean = FMath::DegreesToRadians(79.0);
			const FVector Facing(-FMath::Sin(Book->BookYaw), 0.0, FMath::Cos(Book->BookYaw));
			const FVector Up(0.0, 1.0, 0.0);
			const FVector Pages = Facing * FMath::Cos(Lean) + Up * FMath::Sin(Lean);
			const FVector Spine = Up * FMath::Cos(Lean) - Facing * FMath::Sin(Lean);
			Pair.Value->SetWorldLocationAndRotation(McCoords::BlockToUnreal(Centre), RotationFromAxes(FVector::CrossProduct(Spine, Pages), Spine, Pages));
			Pair.Value->SetBook(Book->BookOpen, Book->BookFlip[0], Book->BookFlip[1]);
			continue;
		}
		const FChest* Chest = Chests.Find(Pair.Key);
		const FIntVector Partner = Chest ? Chest->Partner : FIntVector::ZeroValue;
		const bool bOpen = bAllOpen || (IsOpen && (IsOpen(Pair.Key) || (Partner != FIntVector::ZeroValue && IsOpen(Pair.Key + Partner))));
		Pair.Value->SetOpen(bOpen);
	}
}
