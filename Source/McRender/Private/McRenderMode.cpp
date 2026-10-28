#include "McRenderMode.h"
#include "McRender.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	bool GEnhanced = false;
	uint32 GGeneration = 1;

	// Vanilla material -> its lit twin (null when it has none). The twins are assets, kept loaded once found.
	TMap<TObjectKey<UMaterialInterface>, TWeakObjectPtr<UMaterialInterface>>& GetTwins()
	{
		static TMap<TObjectKey<UMaterialInterface>, TWeakObjectPtr<UMaterialInterface>> Twins;
		return Twins;
	}

	void SetConsoleVariable(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
	}
}

bool McRenderMode::IsEnhanced()
{
	return GEnhanced;
}

void McRenderMode::SetEnhanced(bool bEnhanced)
{
	if (bEnhanced == GEnhanced)
	{
		return;
	}
	GEnhanced = bEnhanced;
	++GGeneration;
	// The project turns virtual shadow maps off for the vanilla look, which casts no shadows at all.
	SetConsoleVariable(TEXT("r.Shadow.Virtual.Enable"), bEnhanced ? 1 : 0);
	UE_LOG(LogMcRender, Log, TEXT("Graphics: %s"), bEnhanced ? TEXT("enhanced") : TEXT("vanilla"));
}

uint32 McRenderMode::GetGeneration()
{
	return GGeneration;
}

UMaterialInterface* McRenderMode::Pick(UMaterialInterface* Vanilla)
{
	if (!GEnhanced || !Vanilla)
	{
		return Vanilla;
	}
	TWeakObjectPtr<UMaterialInterface>* Found = GetTwins().Find(Vanilla);
	if (!Found)
	{
		const FString Path = FString::Printf(TEXT("%sLit.%sLit"), *Vanilla->GetOutermost()->GetName(), *Vanilla->GetName());
		UMaterialInterface* Twin = Vanilla->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))
			? LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet)
			: nullptr;
		if (Twin)
		{
			Twin->AddToRoot();
		}
		else
		{
			UE_LOG(LogMcRender, Verbose, TEXT("%s has no lit twin; enhanced mode draws it as it is"), *Vanilla->GetPathName());
		}
		Found = &GetTwins().Add(Vanilla, TWeakObjectPtr<UMaterialInterface>(Twin));
	}
	return Found->IsValid() ? Found->Get() : Vanilla;
}
