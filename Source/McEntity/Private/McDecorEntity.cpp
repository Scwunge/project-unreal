#include "McDecorEntity.h"

const FName& FMcDecorEntity::StaticType()
{
	static const FName Name(TEXT("mc:decor"));
	return Name;
}

FMcDecorEntity::FMcDecorEntity()
{
	Body.Width = 0.5f;
	Body.Height = 0.5f;
	Body.StepHeight = 0.0f;
}

bool FMcDecorEntity::Serialize(FArchive& Ar)
{
	return FMcEntity::Serialize(Ar) && (Ar << Look, !Ar.IsError()) && (Ar.IsSaving() || !Look.IsEmpty());
}
