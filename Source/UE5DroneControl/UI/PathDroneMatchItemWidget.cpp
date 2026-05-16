#include "PathDroneMatchItemWidget.h"
#include "PathDragDropOperation.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/DragDropOperation.h"

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

FReply UPathDroneMatchItemWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsPathItem && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		FEventReply Reply = UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton);
		return Reply.NativeReply;
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UPathDroneMatchItemWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (!bIsPathItem) return;

	UPathDragDropOperation* DragOp = NewObject<UPathDragDropOperation>();
	DragOp->SourcePathId = ItemId;
	DragOp->SourcePathIndex = PathIndex;
	DragOp->Pivot = EDragPivot::CenterCenter;
	OutOperation = DragOp;
}

bool UPathDroneMatchItemWidget::NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (bIsPathItem) return false;

	UPathDragDropOperation* DragOp = Cast<UPathDragDropOperation>(InOperation);
	if (!DragOp) return false;

	OnMatchCompleted.ExecuteIfBound(DragOp->SourcePathIndex, ItemId);

	if (HighlightBorder)
	{
		HighlightBorder->SetBrushColor(FLinearColor(0.1f, 0.1f, 0.1f, 0.5f));
	}
	return true;
}

void UPathDroneMatchItemWidget::NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (!bIsPathItem && Cast<UPathDragDropOperation>(InOperation))
	{
		if (HighlightBorder)
		{
			HighlightBorder->SetBrushColor(FLinearColor(0.3f, 0.6f, 1.0f, 0.4f));
		}
	}
}

void UPathDroneMatchItemWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (!bIsPathItem)
	{
		if (HighlightBorder)
		{
			HighlightBorder->SetBrushColor(FLinearColor(0.1f, 0.1f, 0.1f, 0.5f));
		}
	}
}
