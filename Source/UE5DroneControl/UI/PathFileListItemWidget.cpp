#include "PathFileListItemWidget.h"
#include "Components/TextBlock.h"

void UPathFileListItemWidget::SetFileInfo(const FString& InFileName, const FString& InFilePath)
{
	FilePath = InFilePath;
	if (FileNameText)
	{
		FileNameText->SetText(FText::FromString(InFileName));
	}
}

FReply UPathFileListItemWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnClicked.ExecuteIfBound(FilePath);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
