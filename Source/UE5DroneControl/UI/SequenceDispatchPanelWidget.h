#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "Components/ComboBoxString.h"
#include "PathEditor/DronePathSaveLibrary.h"
#include "SequenceDispatchPanelWidget.generated.h"

class UPathFileListItemWidget;
class UPathDroneMatchItemWidget;
class ADronePathActor;

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
	/** 当面板处于交互状态时返回 true，供 PlayerController 判断是否跳过游戏点击 */
	static bool IsPanelInteractive();

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
	class UButton* LocalPreviewButton;

	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* BackButton;

	UPROPERTY(meta = (BindWidgetOptional))
	class UTextBlock* StatusText;

	UPROPERTY(meta = (BindWidgetOptional))
	class UComboBoxString* ModeComboBox;

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
	int32 SelectedPathIndex = INDEX_NONE; // 当前选中的 path（等待分配 drone）
	EDroneCommandMode DispatchMode = EDroneCommandMode::Scout;

	// 派发时缓存的重映射路径（DroneId -> PathSaveData），供成功回调后本地驱动影子机使用
	TMap<int32, FDronePathSaveData> PendingRemappedMap;

	// 当前派发产生的 PathActor（影子机路径），下次派发前清除
	UPROPERTY()
	TArray<TObjectPtr<ADronePathActor>> ActiveDispatchPathActors;

	// 清除上次派发留下的 PathActor
	void ClearActiveDispatchPaths();

	// 派发成功后：为每个影子机 spawn DronePathActor 并驱动移动
	void StartShadowDronePlayback();

	static bool bStaticPanelInteractive;

	void SetPanelState(ESequencePanelState NewState);
	void ScanDronePathFiles();
	void OnFileSelected(const FString& FilePath);
	void PopulateMatchingView();
	void PopulateModeComboBox();
	void OnItemClicked(bool bIsPath, int32 IndexOrId);
	void UpdateMatchVisuals();
	void UpdateDispatchButtonState();
	void SetStatusMessage(const FString& Message);

	UFUNCTION()
	void OnOpenButtonClicked();

	UFUNCTION()
	void OnDispatchClicked();

	UFUNCTION()
	void OnLocalPreviewClicked();

	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnDispatchResponse(bool bSuccess, const FString& ResponseBody);
};
