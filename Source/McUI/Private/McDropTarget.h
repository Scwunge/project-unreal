// A widget over the game's viewport that takes files dropped on the window from the desktop.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

DECLARE_DELEGATE_OneParam(FMcOnFilesDropped, const TArray<FString>&);

// Invisible to the mouse except while something is being dragged over the window, so clicks reach the game as usual.
class SMcDropTarget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMcDropTarget) {}
		SLATE_EVENT(FMcOnFilesDropped, OnFiles)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;

private:
	FMcOnFilesDropped OnFiles;
};
