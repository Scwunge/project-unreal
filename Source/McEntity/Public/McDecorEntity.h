// An item frame, painting or armor stand that came from a Java world (McDecor). Scenery: it does nothing, cannot be hit,
// is saved with its chunk, and is drawn from its McDecor line.

#pragma once

#include "CoreMinimal.h"
#include "McEntityManager.h"

class MCENTITY_API FMcDecorEntity final : public FMcEntity
{
public:
	FMcDecorEntity();
	static const FName& StaticType();
	virtual FName GetType() const override { return StaticType(); }
	virtual bool Serialize(FArchive& Ar) override;

	// McDecor::Encode's line.
	FString Look;
};
