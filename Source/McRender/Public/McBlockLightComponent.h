// Unreal Mode's lamps: real point lights at the blocks that give light nearest the camera (torches, lanterns,
// glowstone, lava and the rest), so they light what is round them per pixel and the nearest cast shadows. A torch's
// flame flickers; soul fire is blue; lava is a deep orange. The chunk's own block light still lights everything else,
// as in the vanilla look; nothing changes there.
//
// The blocks within 16 of the camera are looked over a slice each frame, a full pass every eight frames, and the
// nearest MaxLights get lights. Off (and the lights hidden) in the vanilla look.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "McCore.h"
#include "McBlockLightComponent.generated.h"

class FMcBlockWorld;
class UPointLightComponent;

UCLASS(ClassGroup = (Mc))
class MCRENDER_API UMcBlockLightComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMcBlockLightComponent();

	// Null stops. The world must outlive the component or be unset first.
	void SetWorld(const FMcBlockWorld* InWorld);

	static constexpr int32 MaxLights = 24;
	static constexpr int32 MaxShadowed = 6;
	static constexpr int32 Radius = 16;

	int32 NumLit() const { return NumShown; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	struct FFound
	{
		FIntVector Pos;
		FMcStateId State = 0;
		double DistanceSquared = 0.0;
	};

	void ScanSlice();
	void Place();

	const FMcBlockWorld* World = nullptr;
	TArray<uint8> Emission;  // by state id
	TArray<FLinearColor> Colours;  // by state id
	TArray<uint8> Flickers;  // by state id: a flame

	FIntVector ScanCentre = FIntVector::ZeroValue;
	int32 ScanSliceIndex = 0;
	TArray<FFound> Scanning;
	TArray<FFound> Found;
	int32 NumShown = 0;
	double Time = 0.0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPointLightComponent>> Lights;
};
