// Books with writing in them (minecraft.wiki: "Book and Quill", "Written Book", "Lectern"). A book and quill keeps
// its pages as "minecraft:writable_book_content" ({"pages": [{"raw": "text"}, ...]}), a written book as
// "minecraft:written_book_content" ({"title": ..., "author": ..., "pages": [{"raw": <text component>}, ...]}); a page
// may also be the bare string or component. Only these two go on a lectern.

#pragma once

#include "CoreMinimal.h"
#include "McItemStack.h"

namespace McBook
{
	// A book a lectern takes: a book and quill or a written book.
	MCITEM_API bool IsLecternBook(FName Item);

	// The text of each page, plain (a component's text and its extras run together). A book with no pages has one
	// empty page, as the screen shows it.
	MCITEM_API TArray<FString> GetPages(const FMcItemStack& Book);

	// A text component as plain text: a string, {"text": ..., "extra": [...]}, or a list of them.
	MCITEM_API FString PlainText(const TSharedPtr<FJsonValue>& Component);

	// What a comparator reads from a lectern open at Page (from 0) of NumPages: 1 on the first page up to 15 on the
	// last, floor(1 + 14 * page / (pages - 1)); 15 for a book of one page.
	MCITEM_API int32 GetComparatorSignal(int32 Page, int32 NumPages);
}
