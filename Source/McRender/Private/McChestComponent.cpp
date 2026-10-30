#include "McChestComponent.h"
#include "McBell.h"
#include "McBlockRegistry.h"
#include "McBlockWorld.h"
#include "McChestLink.h"
#include "McChunk.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "McEntityModel.h"

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
		IsChest[Id] = !GetChestModel(static_cast<FMcStateId>(Id), Yaw, Partner).IsNone() || !GetShulkerModel(static_cast<FMcStateId>(Id), Rotation).IsNone()
			|| McBell::IsBell(static_cast<FMcStateId>(Id));
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
		// frame, unturned.
		const bool bBell = McBell::IsBell(Pair.Value);
		const bool bShulker = Kind.IsNone() && !bBell;
		if (bBell)
		{
			Kind = FName(TEXT("mc:bell"));
		}
		else if (bShulker)
		{
			Kind = GetShulkerModel(Pair.Value, Rotation);
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
		Model->SetWorldLocationAndRotation(McCoords::BlockToUnreal(FVector(Pair.Key) + FVector(0.5, bShulker ? 0.5 : 0.0, 0.5)), Rotation);
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
	for (const auto& Pair : Models)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const FVector2D Swing = GetBellSwing(Pair.Key);
		Pair.Value->SetSwing(Swing.X, Swing.Y);
		const FChest* Chest = Chests.Find(Pair.Key);
		const FIntVector Partner = Chest ? Chest->Partner : FIntVector::ZeroValue;
		const bool bOpen = bAllOpen || (IsOpen && (IsOpen(Pair.Key) || (Partner != FIntVector::ZeroValue && IsOpen(Pair.Key + Partner))));
		Pair.Value->SetOpen(bOpen);
	}
}
