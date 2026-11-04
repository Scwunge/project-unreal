#include "McBook.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace McBook
{
	bool IsLecternBook(FName Item)
	{
		return Item == TEXT("minecraft:writable_book") || Item == TEXT("minecraft:written_book");
	}

	FString PlainText(const TSharedPtr<FJsonValue>& Component)
	{
		FString Text;
		if (!Component.IsValid())
		{
			return Text;
		}
		if (Component->TryGetString(Text))
		{
			return Text;
		}
		if (Component->Type == EJson::Array)
		{
			for (const TSharedPtr<FJsonValue>& Part : Component->AsArray())
			{
				Text += PlainText(Part);
			}
			return Text;
		}
		if (Component->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject>& Object = Component->AsObject();
			Object->TryGetStringField(TEXT("text"), Text);
			const TArray<TSharedPtr<FJsonValue>>* Extra = nullptr;
			if (Object->TryGetArrayField(TEXT("extra"), Extra))
			{
				for (const TSharedPtr<FJsonValue>& Part : *Extra)
				{
					Text += PlainText(Part);
				}
			}
		}
		return Text;
	}

	TArray<FString> GetPages(const FMcItemStack& Book)
	{
		TArray<FString> Pages;
		TSharedPtr<FJsonValue> Content = Book.Get(TEXT("minecraft:written_book_content"));
		if (!Content.IsValid())
		{
			Content = Book.Get(TEXT("minecraft:writable_book_content"));
		}
		const TArray<TSharedPtr<FJsonValue>>* List = nullptr;
		if (Content.IsValid() && Content->Type == EJson::Object && Content->AsObject()->TryGetArrayField(TEXT("pages"), List))
		{
			for (const TSharedPtr<FJsonValue>& Page : *List)
			{
				// {"raw": ..., "filtered": ...} or the page itself.
				if (Page.IsValid() && Page->Type == EJson::Object && Page->AsObject()->HasField(TEXT("raw")))
				{
					Pages.Add(PlainText(Page->AsObject()->TryGetField(TEXT("raw"))));
				}
				else
				{
					Pages.Add(PlainText(Page));
				}
			}
		}
		if (Pages.IsEmpty())
		{
			Pages.AddDefaulted();
		}
		return Pages;
	}

	int32 GetComparatorSignal(int32 Page, int32 NumPages)
	{
		if (NumPages <= 1)
		{
			return 15;
		}
		const int32 Clamped = FMath::Clamp(Page, 0, NumPages - 1);
		return 1 + (14 * Clamped) / (NumPages - 1);
	}
}
