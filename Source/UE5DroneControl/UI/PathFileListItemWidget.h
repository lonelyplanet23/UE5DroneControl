#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PathFileListItemWidget.generated.h"

DECLARE_DELEGATE_OneParam(FOnPathFileItemClicked, const FString& /*FilePath*/);

UCLASS()
class UE5DRONECONTROL_API UPathFileListItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidgetOptional))
	class UTextBlock* FileNameText;

	void SetFileInfo(const FString& InFileName, const FString& InFilePath);

	FOnPathFileItemClicked OnClicked;

protected:
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	FString FilePath;
};
