#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "Components/ComboBoxString.h"
#include "PathEditor/DronePathSaveLibrary.h"
#include "PreviewConfirmPopupWidget.h"
#include "SequenceDispatchPanelWidget.generated.h"

class UPathFileListItemWidget;
class UPathDroneMatchItemWidget;
class ADronePathActor;
struct FDronePathAssignment;

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

	/** 自动分配勾选框：勾选后跳过手动匹配，由后端匈牙利算法分配无人机 */
	UPROPERTY(meta = (BindWidgetOptional))
	class UCheckBox* AutoAssignCheckBox;

	/** 路径编辑模式开关：打开后以选中影子机位置为起点现场编辑路径 */
	UPROPERTY(meta = (BindWidgetOptional))
	class UCheckBox* PathEditToggle;

	/** 保存按钮：把当前临时路径存入内存（仅编辑模式可见） */
	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* SavePathButton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SequenceDispatch")
	TSubclassOf<UPathFileListItemWidget> FileListItemClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SequenceDispatch")
	TSubclassOf<UPathDroneMatchItemWidget> MatchItemClass;

	/** 关闭编辑 Toggle 后弹出的三选确认弹窗类（指向 WBP 子类） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SequenceDispatch")
	TSubclassOf<UPreviewConfirmPopupWidget> PreviewConfirmPopupClass;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	ESequencePanelState CurrentState = ESequencePanelState::Collapsed;
	FDronePathsSaveData LoadedPathsData;
	TMap<int32, int32> MatchedPairs; // PathIndex -> DroneId
	int32 SelectedPathIndex = INDEX_NONE; // 当前选中的 path（等待分配 drone）
	EDroneCommandMode DispatchMode = EDroneCommandMode::Scout;

	// 自动分配模式开关（由 AutoAssignCheckBox 驱动）
	bool bAutoAssignEnabled = false;

	// 派发时缓存的重映射路径（DroneId -> PathSaveData），供成功回调后本地驱动影子机使用
	TMap<int32, FDronePathSaveData> PendingRemappedMap;

	// 自动分配派发后缓存的重映射路径（PathId -> PathSaveData），
	// 等收到 assignment_result 后再按分配结果转成 DroneId -> PathSaveData
	TMap<int32, FDronePathSaveData> PendingAutoPathsByPathId;

	// ---- 路径编辑模式状态 ----
	bool bInPathEditMode = false;
	bool bHasSavedPreviewData = false;
	// 已保存到内存的临时路径（DroneId -> PathSaveData）
	TMap<int32, FDronePathSaveData> SavedPreviewData;

	// OnAssignmentResult 订阅句柄（NativeDestruct 中移除）
	FDelegateHandle AssignmentResultHandle;

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

	UFUNCTION()
	void OnAutoAssignCheckChanged(bool bIsChecked);

	// ---- 路径编辑模式 ----
	UFUNCTION()
	void OnPathEditToggleChanged(bool bIsChecked);

	UFUNCTION()
	void OnSavePathClicked();

	UFUNCTION()
	void OnPreviewConfirmChoice(EPreviewConfirmChoice Choice);

	// 编辑模式指令派发的响应回调（不受 AutoAssign 分支影响）
	UFUNCTION()
	void OnEditDispatchResponse(bool bSuccess, const FString& ResponseBody);

	// 拿到 DroneOps 控制器（编辑逻辑宿主）
	class ADroneOpsPlayerController* GetDroneOpsController() const;

	// 显示三选确认弹窗
	void ShowPreviewConfirmPopup();

	// 取当前要用于预演/派发的数据：优先已保存，否则实时构建
	TMap<int32, FDronePathSaveData> ResolvePreviewDispatchMap();

	// 复位编辑状态与 UI
	void ResetPathEditState();

	// 收到后端 assignment_result：同步槽位显示并启动影子机播放
	void HandleAssignmentResult(const FString& ArrayId, const TArray<FDronePathAssignment>& Assignments);

	// 自动分配模式下的派发：drone_id 留空，auto_assign=true
	void DispatchWithAutoAssign(class UDroneNetworkManager* NetMgr, class UDroneRegistrySubsystem* Registry);
};
