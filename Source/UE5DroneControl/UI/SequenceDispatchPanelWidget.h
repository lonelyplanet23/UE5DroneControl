#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "Components/ComboBoxString.h"
#include "PathEditor/DronePathSaveLibrary.h"
#include "PathEditor/DronePathActor.h"
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

	/** 全局停止按钮：始终可见，停止并清除全世界所有播放与路径 Actor。 */
	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* GlobalStopButton;

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

	/** 勾选后，本地预演中的 patrol 路径也会参与目标识别；仅用于 UE 场景测试。 */
	UPROPERTY(meta = (BindWidgetOptional))
	class UCheckBox* LocalPatrolSimulationToggle;

	/** 自动分配勾选框：勾选后跳过手动匹配，由后端匈牙利算法分配无人机 */
	UPROPERTY(meta = (BindWidgetOptional))
	class UCheckBox* AutoAssignCheckBox;

	/** 路径编辑模式开关：打开后以选中影子机位置为起点现场编辑路径 */
	UPROPERTY(meta = (BindWidgetOptional))
	class UCheckBox* PathEditToggle;

	/** 保存按钮：把当前临时路径存入内存（仅编辑模式可见） */
	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* SavePathButton;

	/** 播放按钮：弹出三选弹窗（预演/派发/取消），仅编辑模式可见。 */
	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* PlayEditPathButton;

	/** 循环播放勾选框：勾选=所有临时路径循环，仅作用运行副本，不写回磁盘。仅编辑模式可见。 */
	UPROPERTY(meta = (BindWidgetOptional))
	class UCheckBox* EditLoopCheckBox;

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

	// 当前由“本地巡逻模拟”接管的无人机。只在选择 patrol 且勾选开关后填充。
	TSet<int32> LocalPatrolSimulationDroneIds;

	// 在线真机派发：spawn 但未启动的路径，等后端 assembly_complete 后再启动。
	struct FPendingArrivalPath
	{
		TWeakObjectPtr<ADronePathActor> PathActor;
		TWeakObjectPtr<APawn> ShadowPawn;
	};
	TArray<FPendingArrivalPath> PendingArrivalPaths;

	// 集结完成/超时事件早到兜底：事件在路径入队前到达时置位，
	// 路径入队后（StartShadowDronePlayback）若已置位则立即放行，避免路径永久搁浅。
	// 每次派发响应时复位，防止上一批的完成信号误放行下一批。
	bool bAssemblyReleasePending = false;

	// 集结事件订阅句柄（NativeDestruct 中移除）
	FDelegateHandle AssemblyCompleteHandle;
	FDelegateHandle AssemblyTimeoutHandle;

	// 清除上次派发留下的 PathActor
	void ClearActiveDispatchPaths();

	// 本地预演测试通道：把影子机标记为可参与巡逻目标识别，停止/结束时复位。
	void BeginLocalPatrolSimulation(const TMap<int32, FDronePathSaveData>& PreviewMap);
	void EndLocalPatrolSimulation(bool bForceClearAllLocalStates = false);
	bool IsLocalPatrolSimulationEnabled() const;
	bool IsStrictLocalPreviewIsolationEnabled() const;
	// Local preview must start shadow playback immediately, even when telemetry still reports Online.
	// Formal dispatch keeps the existing assembly_complete gate for real drones.
	bool bLocalPreviewPlaybackRequested = false;
	TSet<int32> LocalPreviewPlaybackDroneIds;
	int32 LastLocalPreviewStartedPathCount = 0;
	int32 LastLocalPreviewSkippedNoShadowCount = 0;
	int32 LastLocalPreviewSkippedInsufficientWaypointCount = 0;
	void EndLocalPreviewPlayback();

	// 派发成功后：为每个影子机 spawn DronePathActor 并驱动移动
	void StartShadowDronePlayback();

	// 判定一架无人机是否为"在线真机、需等待到位"：Online 且镜像机已有 GPS 锚点。
	bool IsOnlineRealDrone(int32 DroneId) const;

	// 启动所有待到位路径（解除跟随镜像 + StartMovement），并清空待启动列表。
	void StartPendingArrivalPaths();

	// 后端集结完成 / 超时：放行等待中的路径。
	void OnAssemblyCompleteForDispatch(const FString& ArrayId);
	void OnAssemblyTimeoutForDispatch(const FString& ArrayId, int32 ReadyCount, int32 TotalCount);

	// 派发路径的执行状态变化回调：路径 Completed 时销毁该 Actor 并从跟踪列表移除。
	// 非动态多播委托，用 AddUObject 绑定。
	void OnDispatchPathExecutionStateChanged(ADronePathActor* PathActor, EDronePathExecutionState NewState);

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
	void OnGlobalStopClicked();

	UFUNCTION()
	void OnBackendArrayStopResponse(bool bSuccess, const FString& ResponseBody);

	UFUNCTION()
	void OnDispatchClicked();

	UFUNCTION()
	void OnLocalPreviewClicked();

	UFUNCTION()
	void OnBackClicked();

	UFUNCTION()
	void OnModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	UFUNCTION()
	void OnLocalPatrolSimulationToggleChanged(bool bIsChecked);

	UFUNCTION()
	void OnDispatchResponse(bool bSuccess, const FString& ResponseBody);

	UFUNCTION()
	void OnAutoAssignCheckChanged(bool bIsChecked);

	// ---- 路径编辑模式 ----
	UFUNCTION()
	void OnPathEditToggleChanged(bool bIsChecked);

	UFUNCTION()
	void OnSavePathClicked();

	// 播放按钮：弹出三选弹窗
	UFUNCTION()
	void OnPlayEditPathClicked();

	// 循环勾选框：设置所有临时路径循环状态（仅运行副本）
	UFUNCTION()
	void OnEditLoopCheckChanged(bool bIsChecked);

	// 按编辑模式状态显示/隐藏编辑相关控件（循环框/保存/播放/停止）
	void SetEditControlsVisible(bool bVisible);

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

	// ---- 编队旋转（JSON 路径）----
	// 加载 JSON 后：解析源锚点(编辑系原点)并启动运行锚点 + 旋转环 Gizmo 预览。
	void StartFormationRotatePreview();

	// 停止编队旋转预览（面板关闭/重新加载/预演开始/派发后）。
	void StopFormationRotatePreview();

	// JSON 中源锚点路径的首航点（编辑系坐标）。缺失时退回第一条有效路径首航点。
	FVector GetSourceAnchorEditOrigin() const;

	// 把一条原始路径按"运行锚点平移 + 当前编队旋转角"变换成运行副本（不修改原始 JSON）。
	FDronePathSaveData BuildTransformedPath(const FDronePathSaveData& SourcePath, const FVector& TargetBase, float YawDegrees) const;
};
