#include "PathDroneMatchItemWidget.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "InputCoreTypes.h"

void UPathDroneMatchItemWidget::SetAsPathItem(int32 InPathId, int32 InPathIndex)
{
	bIsPathItem = true;
	ItemId = InPathId;
	PathIndex = InPathIndex;
	if (IdText)
	{
		IdText->SetText(FText::FromString(FString::Printf(TEXT("Path %d"), InPathId)));
	}
	if (MatchedText)
	{
		MatchedText->SetText(FText::GetEmpty());
	}
}

void UPathDroneMatchItemWidget::SetAsDroneItem(int32 InDroneId, const FString& InDroneName)
{
	bIsPathItem = false;
	ItemId = InDroneId;
	PathIndex = INDEX_NONE;
	if (IdText)
	{
		IdText->SetText(FText::FromString(FString::Printf(TEXT("[%d] %s"), InDroneId, *InDroneName)));
	}
	if (MatchedText)
	{
		MatchedText->SetText(FText::GetEmpty());
	}
}

void UPathDroneMatchItemWidget::SetMatchedLabel(const FString& Label)
{
	if (MatchedText)
	{
		MatchedText->SetText(FText::FromString(Label));
	}
	if (HighlightBorder)
	{
		HighlightBorder->SetBrushColor(FLinearColor(0.2f, 0.8f, 0.2f, 0.3f));
	}
}

void UPathDroneMatchItemWidget::ClearMatch()
{
	if (MatchedText)
	{
		MatchedText->SetText(FText::GetEmpty());
	}
	if (HighlightBorder)
	{
		HighlightBorder->SetBrushColor(FLinearColor(0.1f, 0.1f, 0.1f, 0.5f));
	}
}

void UPathDroneMatchItemWidget::SetSelected(bool bSelected)
{
	if (HighlightBorder)
	{
		HighlightBorder->SetBrushColor(bSelected
			? FLinearColor(0.3f, 0.6f, 1.0f, 0.5f)
			: FLinearColor(0.1f, 0.1f, 0.1f, 0.5f));
	}
}

FReply UPathDroneMatchItemWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnItemClicked.ExecuteIfBound(bIsPathItem, bIsPathItem ? PathIndex : ItemId);
		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
