#include "McLecternMenu.h"
#include "Serialization/Archive.h"

namespace
{
	// A page holds a few hundred characters; anything far longer in a message is damage.
	constexpr int32 MaxPageLength = 1024;
}

bool FMcLecternMenu::ClickButton(int32 Id)
{
	int32 To = Page;
	if (Id == PreviousPage)
	{
		To = Page - 1;
	}
	else if (Id == NextPage)
	{
		To = Page + 1;
	}
	else if (Id == TakeBook)
	{
		bTakeAsked = true;
		return true;
	}
	else if (Id >= GoToPage)
	{
		To = Id - GoToPage;
	}
	else
	{
		return false;
	}
	if (To == Page || !Pages.IsValidIndex(To))
	{
		return false;
	}
	Page = To;
	return true;
}

void FMcLecternMenu::SerializeState(FArchive& Ar)
{
	int32 NumPages = Pages.Num();
	Ar << NumPages;
	if (Ar.IsLoading())
	{
		if (NumPages < 0 || NumPages > MaxPages)
		{
			Ar.SetError();
			return;
		}
		Pages.SetNum(NumPages);
	}
	for (FString& Text : Pages)
	{
		Ar << Text;
		if (Ar.IsLoading() && Text.Len() > MaxPageLength)
		{
			Ar.SetError();
			return;
		}
	}
	Ar << Page;
	if (Ar.IsLoading())
	{
		Page = Pages.IsEmpty() ? 0 : FMath::Clamp(Page, 0, Pages.Num() - 1);
	}
}
