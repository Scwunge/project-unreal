#include "McDropTarget.h"

#include "Framework/Application/SlateApplication.h"
#include "Input/DragAndDrop.h"

void SMcDropTarget::Construct(const FArguments& InArgs)
{
	OnFiles = InArgs._OnFiles;
	SetVisibility(TAttribute<EVisibility>::CreateLambda([]()
	{
		return FSlateApplication::IsInitialized() && FSlateApplication::Get().IsDragDropping() ? EVisibility::Visible : EVisibility::HitTestInvisible;
	}));
	ChildSlot[SNullWidget::NullWidget];
}

FReply SMcDropTarget::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	const TSharedPtr<FExternalDragOperation> Operation = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	return Operation.IsValid() && Operation->HasFiles() ? FReply::Handled() : FReply::Unhandled();
}

FReply SMcDropTarget::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	const TSharedPtr<FExternalDragOperation> Operation = DragDropEvent.GetOperationAs<FExternalDragOperation>();
	if (!Operation.IsValid() || !Operation->HasFiles())
	{
		return FReply::Unhandled();
	}
	OnFiles.ExecuteIfBound(Operation->GetFiles());
	return FReply::Handled();
}
