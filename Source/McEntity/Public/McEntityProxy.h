// How the server's entities reach players. Each server-side entity (FMcEntity, plain C++) gets a replicated actor
// that shows it; the actor class implements this so the world can tell it what the entity is doing without McServer
// knowing about rendering. Unreal's replication moves the actor itself; the proxy carries the rest.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "McEntityProxy.generated.h"

class FMcEntity;

UINTERFACE(MinimalAPI)
class UMcEntityProxy : public UInterface
{
	GENERATED_BODY()
};

class MCENTITY_API IMcEntityProxy
{
	GENERATED_BODY()

public:
	// Server, every tick: place the actor where the entity is and copy what players need to see (the kind of
	// entity, where it looks, the item it holds, whether it was just hurt).
	virtual void SyncFromEntity(const FMcEntity& Entity) = 0;
};
