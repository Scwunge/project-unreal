#include "McSignComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Texture2DArray.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "McBlockEntity.h"
#include "McBlockWorld.h"
#include "McCache.h"
#include "McChunk.h"
#include "McFont.h"
#include "McRenderMode.h"
#include "McRenderTextures.h"
#include "McSign.h"
#include "McSkyComponent.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/StrongObjectPtr.h"
#include "McPerf.h"

namespace
{
	enum ESection : int32
	{
		Plain,
		Glowing,
		BlackOutline,
		NearOutline,
		NumSections,
	};

	// The font picture, loaded once for every world.
	UTexture2DArray* GetFont()
	{
		static TStrongObjectPtr<UTexture2DArray> Font;
		if (!Font.IsValid())
		{
			int32 Width = 0, Height = 0;
			Font.Reset(McRenderTextures::LoadPngAsArray(GetTransientPackage(), McCache::Asset(TEXT("textures/font/ascii.png")), Width, Height));
		}
		return Font.Get();
	}

	FColor ToColor(uint32 Rgb)
	{
		return FColor((Rgb >> 16) & 0xFF, (Rgb >> 8) & 0xFF, Rgb & 0xFF, 255);
	}

	uint32 HashSign(const FMcBlockEntity& Sign, FMcStateId State)
	{
		uint32 Hash = GetTypeHash(State);
		for (const FMcSignSide& Side : Sign.SignSides)
		{
			for (const FString& Line : Side.Lines)
			{
				Hash = HashCombine(Hash, GetTypeHash(Line));
			}
			Hash = HashCombine(Hash, HashCombine(GetTypeHash(Side.Color), GetTypeHash(Side.bGlowing)));
		}
		return Hash;
	}

	// One line of one side: each glyph's 8 by 8 cell of the font picture as a quad on the board. Along is the way the
	// line runs and Up the way up, Origin where the line's middle meets the top of its cells, Scale a font pixel, all in
	// blocks; Offset moves every glyph by that many font pixels (for the outline's copies).
	void AddLine(McPlayerModel::FMeshData& Out, const FString& Line, const FVector& Origin, const FVector& Along, const FVector& Up, const FVector& Normal,
		double Scale, FVector2D Offset, FColor Color, float Yaw, const FIntVector& Pos)
	{
		const TArray<uint8>& Widths = McFont::GetGlyphWidths();
		double X = -McFont::GetWidth(Line) / 2.0 + Offset.X;
		for (const TCHAR Char : Line)
		{
			const int32 Code = Char < 256 ? Char : '?';
			if (Code != ' ')
			{
				const FVector2D UV((Code % 16) * 8 / 128.0, (Code / 16) * 8 / 128.0);
				// Top left, bottom left, bottom right, top right, in font pixels from the line's top middle.
				const FVector2D Corners[4] = { { X, Offset.Y }, { X, Offset.Y + 8 }, { X + 8, Offset.Y + 8 }, { X + 8, Offset.Y } };
				const FVector2D UVs[4] = { UV, UV + FVector2D(0, 8 / 128.0), UV + FVector2D(8 / 128.0, 8 / 128.0), UV + FVector2D(8 / 128.0, 0) };
				const int32 First = Out.Positions.Num();
				FVector Placed[4];
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					const FVector Local = Origin + Along * (Corners[Corner].X * Scale) - Up * (Corners[Corner].Y * Scale);
					Placed[Corner] = McSign::Turn(Local, Yaw, Pos) - FVector(Pos);
					Out.Positions.Add(McCoords::BlockToUnreal(Placed[Corner]));
					Out.UV0.Add(UVs[Corner]);
					Out.Colors.Add(Color);
				}
				const FVector TurnedNormal = McSign::Turn(FVector(0.5) + Normal, Yaw, FIntVector::ZeroValue) - McSign::Turn(FVector(0.5), Yaw, FIntVector::ZeroValue);
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					Out.Normals.Add(FVector(TurnedNormal.X, TurnedNormal.Z, TurnedNormal.Y));
				}
				// Counter-clockwise seen from the front in Minecraft's axes, which is how the block quads go.
				const bool bForward = FVector::DotProduct(FVector::CrossProduct(Placed[1] - Placed[0], Placed[2] - Placed[0]), TurnedNormal) > 0.0;
				if (bForward)
				{
					Out.Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
				}
				else
				{
					Out.Triangles.Append({ First, First + 2, First + 1, First, First + 3, First + 2 });
				}
			}
			X += (Widths.IsValidIndex(Code) ? Widths[Code] : 5) + 1;
		}
	}
}

UMcSignComponent::UMcSignComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Cutout(TEXT("/Game/Mc/Materials/M_McBlockCutout.M_McBlockCutout"));
	BaseMaterial = Cutout.Object;
}

void UMcSignComponent::BuildText(const FMcBlockEntity& Sign, FMcStateId State, FTextMesh& Out)
{
	const McSign::EKind Kind = McSign::GetKind(State);
	if (Kind == McSign::EKind::None || Sign.Kind != EMcBlockEntityKind::Sign)
	{
		return;
	}
	const float Yaw = McSign::GetYaw(State);
	const FVector Centre = McSign::GetBoardCentre(Kind);
	const double Scale = McSign::GetTextScale(Kind);
	const int32 LineHeight = McSign::GetLineHeight(Kind);
	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		const FMcSignSide& Side = Sign.SignSides[SideIndex];
		if (!Side.HasText())
		{
			continue;
		}
		// The front faces south unturned and reads west to east; the back faces north and reads east to west. The text
		// sits a hair off the board so the two don't fight over the same depth.
		const double Facing = SideIndex == 0 ? 1.0 : -1.0;
		const FVector Normal(0.0, 0.0, Facing);
		const FVector Along(Facing, 0.0, 0.0);
		const FVector Up(0.0, 1.0, 0.0);
		const double Face = McSign::GetHalfThickness(Kind) + 0.002;
		const FColor Text = ToColor(McSign::GetDrawColour(Side));
		const FColor Edge = ToColor(McSign::GetOutlineColour(Side));
		const bool bBlack = McSign::GetTextColour(Side.Color) == 0x000000;
		for (int32 Line = 0; Line < FMcSignSide::NumLines; ++Line)
		{
			if (Side.Lines[Line].IsEmpty())
			{
				continue;
			}
			// The four lines are centred on the board: the first's top is two lines above the middle.
			const FVector Origin = Centre + FVector(0.0, (2 * LineHeight - Line * LineHeight) * Scale, 0.0) + Normal * Face;
			if (Side.bGlowing)
			{
				AddLine(Out.Glowing, Side.Lines[Line], Origin + Normal * 0.001, Along, Up, Normal, Scale, FVector2D::ZeroVector, Text, Yaw, FIntVector::ZeroValue);
				// The outline is the line again in the darker colour, a font pixel out each of eight ways, behind it.
				McPlayerModel::FMeshData& Outline = bBlack ? Out.BlackOutline : Out.NearOutline;
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					for (int32 DX = -1; DX <= 1; ++DX)
					{
						if (DX != 0 || DY != 0)
						{
							AddLine(Outline, Side.Lines[Line], Origin, Along, Up, Normal, Scale, FVector2D(DX, DY), Edge, Yaw, FIntVector::ZeroValue);
						}
					}
				}
			}
			else
			{
				AddLine(Out.Plain, Side.Lines[Line], Origin, Along, Up, Normal, Scale, FVector2D::ZeroVector, Text, Yaw, FIntVector::ZeroValue);
			}
		}
	}
}

void UMcSignComponent::SetWorld(FMcBlockWorld* InWorld)
{
	Unhook();
	for (auto& Pair : Meshes)
	{
		if (Pair.Value)
		{
			Pair.Value->DestroyComponent();
		}
	}
	Meshes.Reset();
	Signs.Reset();
	SignsByChunk.Reset();
	World = InWorld;
	if (!World)
	{
		return;
	}
	AddedHandle = World->OnChunkAdded.AddUObject(this, &UMcSignComponent::Scan);
	ChangedHandle = World->OnChunkChanged.AddUObject(this, &UMcSignComponent::Scan);
	RemovedHandle = World->OnChunkRemoved.AddUObject(this, &UMcSignComponent::Clear);
	EntityHandle = World->OnBlockEntityChanged.AddUObject(this, &UMcSignComponent::EntityChanged);
	for (const auto& Pair : World->GetChunks())
	{
		Scan(Pair.Key);
	}
}

void UMcSignComponent::Unhook()
{
	if (World)
	{
		World->OnChunkAdded.Remove(AddedHandle);
		World->OnChunkChanged.Remove(ChangedHandle);
		World->OnChunkRemoved.Remove(RemovedHandle);
		World->OnBlockEntityChanged.Remove(EntityHandle);
	}
}

void UMcSignComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetWorld(nullptr);
	Super::EndPlay(EndPlayReason);
}

void UMcSignComponent::Remove(const FIntVector& Pos)
{
	Signs.Remove(Pos);
	TObjectPtr<UProceduralMeshComponent> Mesh;
	if (Meshes.RemoveAndCopyValue(Pos, Mesh) && Mesh)
	{
		Mesh->DestroyComponent();
	}
}

void UMcSignComponent::Clear(FIntPoint ChunkPos)
{
	TArray<FIntVector> Gone;
	SignsByChunk.RemoveAndCopyValue(ChunkPos, Gone);
	for (const FIntVector& Pos : Gone)
	{
		Remove(Pos);
	}
}

void UMcSignComponent::EntityChanged(const FIntVector& Pos)
{
	Scan(FIntPoint(McCoords::BlockToChunk(Pos.X), McCoords::BlockToChunk(Pos.Z)));
}

void UMcSignComponent::MakeMaterials()
{
	if (PlainMaterial && MaterialGeneration == McRenderMode::GetGeneration())
	{
		return;
	}
	UTexture2DArray* Font = GetFont();
	if (!Font || !BaseMaterial)
	{
		return;
	}
	MaterialGeneration = McRenderMode::GetGeneration();
	PlainMaterial = UMaterialInstanceDynamic::Create(McRenderMode::Pick(BaseMaterial), this);
	PlainMaterial->SetTextureParameterValue(TEXT("Blocks"), Font);
	GlowMaterial = UMaterialInstanceDynamic::Create(McRenderMode::Pick(BaseMaterial), this);
	GlowMaterial->SetTextureParameterValue(TEXT("Blocks"), Font);
	for (const auto& Pair : Meshes)
	{
		if (Pair.Value)
		{
			for (int32 Section = 0; Section < Pair.Value->GetNumSections(); ++Section)
			{
				Pair.Value->SetMaterial(Section, Section == Plain ? PlainMaterial.Get() : GlowMaterial.Get());
			}
		}
	}
}

void UMcSignComponent::Scan(FIntPoint ChunkPos)
{
	const FMcChunk* Chunk = World ? World->FindChunk(ChunkPos) : nullptr;
	if (!Chunk)
	{
		Clear(ChunkPos);
		return;
	}
	MakeMaterials();
	// The signs with text in the chunk now.
	TMap<FIntVector, TPair<FMcStateId, const FMcBlockEntity*>> Found;
	for (const TPair<FIntVector, FMcBlockEntity>& Pair : Chunk->GetBlockEntities())
	{
		const FMcBlockEntity& Entity = Pair.Value;
		if (Entity.Kind != EMcBlockEntityKind::Sign || (!Entity.SignSides[0].HasText() && !Entity.SignSides[1].HasText()))
		{
			continue;
		}
		const FIntVector Pos(ChunkPos.X * 16 + Pair.Key.X, Pair.Key.Y, ChunkPos.Y * 16 + Pair.Key.Z);
		const FMcStateId State = World->GetBlock(Pos);
		if (McSign::GetKind(State) != McSign::EKind::None)
		{
			Found.Add(Pos, { State, &Entity });
		}
	}
	TArray<FIntVector>& Listed = SignsByChunk.FindOrAdd(ChunkPos);
	for (int32 Index = Listed.Num() - 1; Index >= 0; --Index)
	{
		if (!Found.Contains(Listed[Index]))
		{
			Remove(Listed[Index]);
			Listed.RemoveAt(Index);
		}
	}
	for (const auto& Pair : Found)
	{
		const uint32 Hash = HashSign(*Pair.Value.Value, Pair.Value.Key);
		FSign* Known = Signs.Find(Pair.Key);
		if (Known && Known->Hash == Hash)
		{
			continue;
		}
		if (!Known)
		{
			Listed.Add(Pair.Key);
			Known = &Signs.Add(Pair.Key);
		}
		Known->State = Pair.Value.Key;
		Known->Hash = Hash;
		Known->bNear = true;
		TObjectPtr<UProceduralMeshComponent>& Mesh = Meshes.FindOrAdd(Pair.Key);
		if (!Mesh)
		{
			Mesh = NewObject<UProceduralMeshComponent>(GetOwner());
			Mesh->SetupAttachment(this);
			Mesh->SetUsingAbsoluteLocation(true);
			Mesh->SetUsingAbsoluteRotation(true);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCastShadow(false);
			Mesh->bUseAsyncCooking = false;
			Mesh->RegisterComponent();
			Mesh->SetWorldLocation(McCoords::BlockToUnreal(FVector(Pair.Key)));
		}
		Mesh->ClearAllMeshSections();
		FTextMesh Text;
		BuildText(*Pair.Value.Value, Pair.Value.Key, Text);
		const McPlayerModel::FMeshData* Parts[NumSections] = { &Text.Plain, &Text.Glowing, &Text.BlackOutline, &Text.NearOutline };
		for (int32 Section = 0; Section < NumSections; ++Section)
		{
			const McPlayerModel::FMeshData& Data = *Parts[Section];
			// Every section is made, empty or not, so each keeps its number.
			TArray<FVector2D> UV1, UV2;
			UV1.Init(FVector2D::ZeroVector, Data.Positions.Num());
			UV2.Init(FVector2D(1.0, 0.0), Data.Positions.Num());
			Mesh->CreateMeshSection(Section, Data.Positions, Data.Triangles, Data.Normals, Data.UV0, UV1, UV2, TArray<FVector2D>(), Data.Colors, TArray<FProcMeshTangent>(), false);
			Mesh->SetMaterial(Section, Section == Plain ? PlainMaterial.Get() : GlowMaterial.Get());
		}
	}
	if (Listed.IsEmpty())
	{
		SignsByChunk.Remove(ChunkPos);
	}
}

void UMcSignComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	MC_PERF_SCOPE(BlockEnts);
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Signs.IsEmpty())
	{
		return;
	}
	MakeMaterials();
	// Plain text is lit as the entities are; glowing text, and its outline, always as if in full light.
	if (PlainMaterial)
	{
		PlainMaterial->SetScalarParameterValue(TEXT("SkyFactor"), McSky::GetSkyLightFactor());
		PlainMaterial->SetScalarParameterValue(TEXT("Ambient"), McRenderMode::GetEntityAmbient());
	}
	if (GlowMaterial)
	{
		GlowMaterial->SetScalarParameterValue(TEXT("SkyFactor"), 1.0f);
		GlowMaterial->SetScalarParameterValue(TEXT("Ambient"), 1.0f);
	}
	const APlayerController* Player = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!Player || !Player->PlayerCameraManager)
	{
		return;
	}
	const FVector Camera = McCoords::UnrealToBlock(Player->PlayerCameraManager->GetCameraLocation());
	for (auto& Pair : Signs)
	{
		const bool bNear = FVector::Dist(Camera, FVector(Pair.Key) + FVector(0.5)) <= 16.0;
		if (bNear != Pair.Value.bNear)
		{
			Pair.Value.bNear = bNear;
			if (const TObjectPtr<UProceduralMeshComponent>* Mesh = Meshes.Find(Pair.Key); Mesh && *Mesh)
			{
				(*Mesh)->SetMeshSectionVisible(NearOutline, bNear);
			}
		}
	}
}
