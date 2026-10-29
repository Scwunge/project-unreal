// A placed banner (minecraft.wiki: "Banner"). The block holds nothing the world can draw, so the flag is an entity
// standing where the banner is: it does nothing, is saved with its chunk, and goes when the block does. What it
// holds is McBanner::Encode's line (base colour, standing or on a wall, the patterns).

#pragma once

#include "CoreMinimal.h"
#include "McBanner.h"
#include "McEntityManager.h"

class MCENTITY_API FMcBannerEntity final : public FMcEntity
{
public:
	FMcBannerEntity();
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual bool Serialize(FArchive& Ar) override;

	FString Layers;

	// Stands a flag in the banner block at Pos (what colour it is, whether it is on a wall and which way it faces come from the block),
	// replacing one that is there. Null if the block isn't a banner.
	static FMcBannerEntity* Stand(FMcEntityManager& Manager, const FIntVector& Pos, const TArray<McBanner::FLayer>& Layers);
	// The banner standing in the block at Pos, or null.
	static FMcBannerEntity* FindAt(FMcEntityManager& Manager, const FIntVector& Pos);
};
