#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PathDroneMatchItemWidget.generated.h"

DECLARE_DELEGATE_TwoParams(FOnMatchItemClicked, bool /*bIsPath*/, int32 /*Index or Id*/);

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
	void SetSelected(bool bSelected);

	bool IsPathItem() const { return bIsPathItem; }
	int32 GetItemId() const { return ItemId; }
	int32 GetPathIndex() const { return PathIndex; }

	FOnMatchItemClicked OnItemClicked;

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	bool bIsPathItem = false;
	int32 ItemId = INDEX_NONE;
	int32 PathIndex = INDEX_NONE;
};
