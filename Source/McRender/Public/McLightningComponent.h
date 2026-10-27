// A lightning bolt (minecraft.wiki: "Lightning"): three jagged strands of eight segments each, the longest reaching the
// ground where it struck and the others forking off it, each segment four nested see-through prisms of pale blue-white
// light. It flickers for a moment and is gone; the component destroys itself.

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"
#include "McLightningComponent.generated.h"

class UMaterialInterface;

namespace McLightning
{
	// One segment of a strand, in blocks from the strike: the middle of its bottom and of its top.
	struct FSegment
	{
		FVector Bottom;
		FVector Top;
		// 0 for the main strand, 1 and 2 for the forks.
		int32 Strand = 0;
	};
	// The bolt Seed makes. The main strand's first segment ends at the origin.
	MCRENDER_API TArray<FSegment> BuildBolt(int32 Seed);

	constexpr int32 SegmentsPerStrand = 8;
	constexpr float SegmentHeight = 16.0f;
}

UCLASS(ClassGroup = (Mc))
class MCRENDER_API UMcLightningComponent : public UProceduralMeshComponent
{
	GENERATED_BODY()

public:
	UMcLightningComponent(const FObjectInitializer& ObjectInitializer);

	// Builds the bolt at Strike (block coordinates). Call before RegisterComponent.
	void SetBolt(const FVector& Strike, int32 Seed);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// How long a bolt is seen, in seconds.
	static constexpr float Lifetime = 0.4f;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TObjectPtr<UMaterialInterface> AdditiveMaterial;
	FVector StrikePos = FVector::ZeroVector;
	int32 BoltSeed = 0;
	float Age = 0.0f;
};
