#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Input/Events.h"
#include "PathDroneMatchItemWidget.generated.h"

class UDragDropOperation;

DECLARE_DELEGATE_TwoParams(FOnMatchCompleted, int32 /*PathIndex*/, int32 /*DroneId*/);

UCLASS()
class UE5DRONECONTROL_API UPathDroneMatchItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidgetOptional))
	class UTextBlock* IdText;

	UPROPERTY(meta = (BindWidgetOptional))
	class UTextBlock* MatchedText;

	UPROPERTY(meta = (BindWidgetOptional))
	class UBorder* HighlightBorder;

	void SetAsPathItem(int32 InPathId, int32 InPathIndex);
	void SetAsDroneItem(int32 InDroneId, const FString& InDroneName);
	void SetMatchedLabel(const FString& Label);
	void ClearMatch();

	bool IsPathItem() const { return bIsPathItem; }
	int32 GetItemId() const { return ItemId; }
	int32 GetPathIndex() const { return PathIndex; }

	FOnMatchCompleted OnMatchCompleted;

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragEnter(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
	bool bIsPathItem = false;
	int32 ItemId = INDEX_NONE;
	int32 PathIndex = INDEX_NONE;
};
