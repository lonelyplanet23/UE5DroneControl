#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PathEditor/DronePathSaveLibrary.h"
#include "SequenceDispatchPanelWidget.generated.h"

class UPathFileListItemWidget;
class UPathDroneMatchItemWidget;

UENUM()
enum class ESequencePanelState : uint8
{
	Collapsed,
	FileList,
	Matching
};

UCLASS()
class UE5DRONECONTROL_API USequenceDispatchPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* OpenButton;

	UPROPERTY(meta = (BindWidgetOptional))
	class UCanvasPanel* FileListPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	class UScrollBox* FileListScrollBox;

	UPROPERTY(meta = (BindWidgetOptional))
	class UCanvasPanel* MatchingPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	class UScrollBox* PathListScrollBox;

	UPROPERTY(meta = (BindWidgetOptional))
	class UScrollBox* DroneListScrollBox;

	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* DispatchButton;

	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* BackButton;

	UPROPERTY(meta = (BindWidgetOptional))
	class UTextBlock* StatusText;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SequenceDispatch")
	TSubclassOf<UPathFileListItemWidget> FileListItemClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SequenceDispatch")
	TSubclassOf<UPathDroneMatchItemWidget> MatchItemClass;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	ESequencePanelState CurrentState = ESequencePanelState::Collapsed;
	FDronePathsSaveData LoadedPathsData;
	TMap<int32, int32> MatchedPairs; // PathIndex -> DroneId

	void SetPanelState(ESequencePanelState NewState);
	void ScanDronePathFiles();
	void OnFileSelected(const FString& FilePath);
	void PopulateMatchingView();
	void OnMatchMade(int32 PathIndex, int32 DroneId);
	void UpdateDispatchButtonState();
	void SetStatusMessage(const FString& Message);

	UFUNCTION()
	void OnOpenButtonClicked();

	UFUNCTION()
	void OnDispatchClicked();

	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnDispatchResponse(bool bSuccess, const FString& ResponseBody);
};
