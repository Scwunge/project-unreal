#include "McLightningComponent.h"
#include "McChunk.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

TArray<McLightning::FSegment> McLightning::BuildBolt(int32 Seed)
{
	FRandomStream Random(Seed);
	TArray<FSegment> Segments;
	// The main strand, from the ground up: each segment's top wanders up to 2.5 blocks either way from its bottom.
	TArray<FVector> Joints;
	FVector At = FVector::ZeroVector;
	Joints.Add(At);
	for (int32 Index = 0; Index < SegmentsPerStrand; ++Index)
	{
		const FVector Next = At + FVector(Random.FRandRange(-2.5f, 2.5f), SegmentHeight, Random.FRandRange(-2.5f, 2.5f));
		Segments.Add(FSegment{ At, Next, 0 });
		Joints.Add(Next);
		At = Next;
	}
	// Two forks: each leaves the main strand at a joint part way up and runs down beside it, ending in the air.
	for (int32 Strand = 1; Strand <= 2; ++Strand)
	{
		const int32 From = Random.RandRange(3, SegmentsPerStrand - 2);
		FVector Top = Joints[From];
		const int32 Length = Random.RandRange(2, From - 1);
		for (int32 Index = 0; Index < Length; ++Index)
		{
			const FVector Bottom = Top - FVector(Random.FRandRange(-3.0f, 3.0f), SegmentHeight * 0.75f, Random.FRandRange(-3.0f, 3.0f));
			Segments.Add(FSegment{ Bottom, Top, Strand });
			Top = Bottom;
		}
	}
	return Segments;
}

UMcLightningComponent::UMcLightningComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = true;
	SetUsingAbsoluteLocation(true);
	SetUsingAbsoluteRotation(true);
	SetUsingAbsoluteScale(true);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetCastShadow(false);
	bUseAsyncCooking = false;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Finder(TEXT("/Game/Mc/Materials/M_McSkyAdditive.M_McSkyAdditive"));
	AdditiveMaterial = Finder.Object;
}

void UMcLightningComponent::SetBolt(const FVector& Strike, int32 Seed)
{
	StrikePos = Strike;
	BoltSeed = Seed;
}

void UMcLightningComponent::BeginPlay()
{
	Super::BeginPlay();
	SetWorldLocation(McCoords::BlockToUnreal(StrikePos));

	TArray<FVector> Positions;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FColor> Colours;
	// Four nested prisms per segment, open at the ends, each adding a little pale blue light (both its sides do, seen
	// through): brightest at the core, a soft glow at the edge.
	const FColor Glow(80, 80, 96, 255);
	for (const McLightning::FSegment& Segment : McLightning::BuildBolt(BoltSeed))
	{
		const float Thin = Segment.Strand == 0 ? 1.0f : 0.6f;
		for (int32 Layer = 0; Layer < 4; ++Layer)
		{
			const float Half = (0.1f + 0.1f * Layer) * Thin;
			const FVector Corners[4] = { FVector(-Half, 0, -Half), FVector(Half, 0, -Half), FVector(Half, 0, Half), FVector(-Half, 0, Half) };
			for (int32 Side = 0; Side < 4; ++Side)
			{
				const FVector& A = Corners[Side];
				const FVector& B = Corners[(Side + 1) % 4];
				const int32 First = Positions.Num();
				Positions.Add(McCoords::BlockToUnreal(Segment.Bottom + A));
				Positions.Add(McCoords::BlockToUnreal(Segment.Bottom + B));
				Positions.Add(McCoords::BlockToUnreal(Segment.Top + B));
				Positions.Add(McCoords::BlockToUnreal(Segment.Top + A));
				const FVector Out = McCoords::BlockToUnreal(A + B).GetSafeNormal();
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					Normals.Add(Out);
					UV0.Add(FVector2D::ZeroVector);
					Colours.Add(Glow);
				}
				Triangles.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
			}
		}
	}
	TArray<FVector2D> Zero;
	Zero.Init(FVector2D::ZeroVector, Positions.Num());
	CreateMeshSection(0, Positions, Triangles, Normals, UV0, Zero, Zero, Zero, Colours, TArray<FProcMeshTangent>(), false);
	if (AdditiveMaterial)
	{
		// Solid: white instead of a texture, coloured by the vertices.
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(AdditiveMaterial, this);
		Material->SetScalarParameterValue(TEXT("Solid"), 1.0f);
		SetMaterial(0, Material);
	}
}

void UMcLightningComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	Age += DeltaTime;
	if (Age >= Lifetime)
	{
		DestroyComponent();
		return;
	}
	// It flickers: off for a moment half way.
	SetVisibility(Age < Lifetime * 0.45f || Age > Lifetime * 0.6f);
}
