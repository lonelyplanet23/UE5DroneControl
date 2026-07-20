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
		// 选中：高饱和亮青蓝、完全不透明（>1 的分量让它在 HDR/Tonemap 下更"发光"）；
		// 未选中：几乎全黑透明。两者对比拉到最大，一眼可辨。
		HighlightBorder->SetBrushColor(bSelected
			? FLinearColor(0.1f, 0.9f, 2.2f, 1.0f)
			: FLinearColor(0.02f, 0.02f, 0.02f, 0.25f));
	}

	if (FileNameText)
	{
		// 选中时文字纯黑（压在亮蓝底上最清晰）；未选中为暗灰。
		FileNameText->SetColorAndOpacity(bSelected
			? FSlateColor(FLinearColor(0.02f, 0.02f, 0.02f, 1.0f))
			: FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f, 1.0f)));
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
