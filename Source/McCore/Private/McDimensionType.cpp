#include "McDimensionType.h"
#include "McCache.h"
#include "McCore.h"

bool FMcDimensionType::Load(const FString& Id, FMcDimensionType& Out)
{
	const FString Path = McCache::Data(TEXT("dimension_type/") + McCache::PathOf(Id) + TEXT(".json"));
	const TSharedPtr<FJsonObject> Json = McCache::LoadJsonObject(Path);
	if (!Json.IsValid())
	{
		return false;
	}

	FMcDimensionType Result;
	Result.Name = FName(*McCache::Namespaced(Id));
	if (!Json->TryGetNumberField(TEXT("min_y"), Result.MinY) || !Json->TryGetNumberField(TEXT("height"), Result.Height))
	{
		UE_LOG(LogMcCore, Error, TEXT("%s has no min_y or height"), *Path);
		return false;
	}
	Json->TryGetBoolField(TEXT("has_skylight"), Result.bHasSkylight);
	Json->TryGetBoolField(TEXT("has_ceiling"), Result.bHasCeiling);

	if (Result.MinY % 16 != 0 || Result.Height % 16 != 0 || Result.Height <= 0)
	{
		UE_LOG(LogMcCore, Error, TEXT("%s: min_y %d and height %d must be positive multiples of 16"), *Path, Result.MinY, Result.Height);
		return false;
	}

	Out = Result;
	return true;
}
