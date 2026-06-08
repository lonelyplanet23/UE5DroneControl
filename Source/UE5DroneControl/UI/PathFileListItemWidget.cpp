#include "PathFileListItemWidget.h"
#include "Components/Border.h"
#include "Components/TextBlock.h"

void UPathFileListItemWidget::SetFileInfo(const FString& InFileName, const FString& InFilePath)
{
	FilePath = InFilePath;
	if (FileNameText)
	{
		FileNameText->SetText(FText::FromString(InFileName));
	}
	SetSelected(false);
}

void UPathFileListItemWidget::SetSelected(bool bSelected)
{
	if (HighlightBorder)
	{
		HighlightBorder->SetBrushColor(bSelected
			? FLinearColor(0.3f, 0.6f, 1.0f, 0.5f)
			: FLinearColor(0.1f, 0.1f, 0.1f, 0.5f));
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
