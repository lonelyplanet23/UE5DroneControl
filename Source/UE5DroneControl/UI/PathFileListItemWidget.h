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

	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* SelectButton;

	void SetFileInfo(const FString& InFileName, const FString& InFilePath);

	FOnPathFileItemClicked OnClicked;

protected:
	virtual void NativeConstruct() override;

private:
	FString FilePath;

	UFUNCTION()
	void HandleButtonClicked();
};
