#include "McChestComponent.h"
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
		IsChest[Id] = !GetChestModel(static_cast<FMcStateId>(Id), Yaw, Partner).IsNone();
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
		const FName Kind = GetChestModel(Pair.Value, Yaw, Known->Partner, bChristmas);
		TObjectPtr<UMcEntityModelComponent>& Model = Models.FindOrAdd(Pair.Key);
		if (!Model)
		{
			Model = NewObject<UMcEntityModelComponent>(GetOwner());
			Model->SetupAttachment(this);
			Model->SetUsingAbsoluteLocation(true);
			Model->RegisterComponent();
			Model->SetWorldLocation(McCoords::BlockToUnreal(FVector(Pair.Key) + FVector(0.5, 0.0, 0.5)));
		}
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
	for (const auto& Pair : Models)
	{
		if (!Pair.Value)
		{
			continue;
		}
		const FChest* Chest = Chests.Find(Pair.Key);
		const FIntVector Partner = Chest ? Chest->Partner : FIntVector::ZeroValue;
		const bool bOpen = bAllOpen || (IsOpen && (IsOpen(Pair.Key) || (Partner != FIntVector::ZeroValue && IsOpen(Pair.Key + Partner))));
		Pair.Value->SetOpen(bOpen);
	}
}
