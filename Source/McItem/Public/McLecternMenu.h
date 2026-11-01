// The lectern's window (minecraft.wiki, "Lectern"): no slots, only the book's pages and the one it lies open at. The
// player's screen turns the pages and takes the book with the menu's buttons; the server's menu says what was asked,
// and the lectern (McBlockUse) does it: everyone reading it sees the new page, and the book goes to the one who took it.

#pragma once

#include "CoreMinimal.h"
#include "McContainer.h"

class MCITEM_API FMcLecternMenu final : public FMcMenu
{
public:
	// The buttons, as the game numbers them: back a page, on a page, take the book, and 100 + n to open at page n.
	static constexpr int32 PreviousPage = 1;
	static constexpr int32 NextPage = 2;
	static constexpr int32 TakeBook = 3;
	static constexpr int32 GoToPage = 100;
	// A written book holds at most 100 pages.
	static constexpr int32 MaxPages = 100;

	TArray<FString> Pages;
	int32 Page = 0;
	// Server: the player pressed Take Book.
	bool bTakeAsked = false;

	// True if the button did anything (a page past either end does nothing).
	virtual bool ClickButton(int32 Id) override;
	virtual void SerializeState(FArchive& Ar) override;
};
