#include "McSignMenu.h"
#include "Serialization/Archive.h"

bool FMcSignMenu::SetText(const FString& Text)
{
	Lines = Split(Text);
	return true;
}

void FMcSignMenu::SerializeState(FArchive& Ar)
{
	Ar << bBack << SignKind;
	FString BlockText = Ar.IsSaving() ? Block.ToString() : FString();
	Ar << BlockText;
	Ar << TextColour;
	FString Text = Ar.IsSaving() ? Join(Lines) : FString();
	Ar << Text;
	if (Ar.IsLoading())
	{
		Block = FName(*BlockText);
		Lines = Split(Text);
	}
}

FString FMcSignMenu::Join(const TArray<FString>& Lines)
{
	return FString::Join(Lines, TEXT("\n"));
}

TArray<FString> FMcSignMenu::Split(const FString& Text)
{
	TArray<FString> Out;
	Out.SetNum(NumLines);
	int32 Line = 0;
	for (const TCHAR Char : Text)
	{
		if (Char == TEXT('\n'))
		{
			if (++Line >= NumLines)
			{
				break;
			}
			continue;
		}
		if (Char < 32 || Char == 127 || Char == TEXT('§') || Out[Line].Len() >= MaxLineLength)
		{
			continue;
		}
		Out[Line].AppendChar(Char);
	}
	return Out;
}
