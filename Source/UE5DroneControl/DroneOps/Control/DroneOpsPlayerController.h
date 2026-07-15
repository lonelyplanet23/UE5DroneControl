// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "Components/WidgetComponent.h"
#include "Camera/CameraActor.h"
#include "PathEditor/DroneWaypointActor.h"
#include "PathEditor/DronePathSaveLibrary.h"
#include "DroneOpsPlayerController.generated.h"

class UDroneRegistrySubsystem;
class ADronePathActor;
class ADroneWaypointActor;
class AMultiDroneCharacter;
class UUserWidget;

/**
 * Delegate for when drone info panel is requested by middle click.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnOpenDroneInfoPanelRequested, int32, DroneId);

/**
 * Player controller for drone operations
 * Handles input, selection, camera modes, and command dispatch
 */
UCLASS()
class UE5DRONECONTROL_API ADroneOpsPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ADroneOpsPlayerController();

	/** Manually snap every shadow drone to the current position of its registered mirror drone. */
	UFUNCTION(BlueprintCallable, Category = "DroneOps|Calibration")
	void ResetShadowDronesToMirrors();

	// ---- 预演关卡路径编辑模式（供 SequenceDispatchPanelWidget 调用）----

	/**
	 * 进入路径编辑模式：为每个选中的影子机各 spawn 一条临时 ADronePathActor，
	 * 以影子机当前世界位置为第一航点。无有效选中时返回 false。
	 */
	UFUNCTION(BlueprintCallable, Category = "DroneOps|PathEdit")
	bool BeginPathEditMode(const TArray<int32>& DroneIds);

	/** 退出编辑交互（清 gizmo 选中/拖拽状态），不销毁临时路径。 */
	UFUNCTION(BlueprintCallable, Category = "DroneOps|PathEdit")
	void EndPathEditMode();

	/** 把当前临时路径打包成 DroneId -> FDronePathSaveData（世界坐标）。 */
	TMap<int32, FDronePathSaveData> BuildEditingPathsData() const;

	/** 销毁全部临时路径 Actor（含航点句柄）并清空编辑状态。 */
	UFUNCTION(BlueprintCallable, Category = "DroneOps|PathEdit")
	void ClearEditingPaths();

	UFUNCTION(BlueprintPure, Category = "DroneOps|PathEdit")
	bool IsPathEditMode() const { return bPathEditMode; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaTime) override;

	// Input handlers
	void OnPrimaryClick();
	void OnPrimaryReleased();
	void OnShowInfo();
	void OnFreeCamToggle();
	void OnPauseToggle();
	void OnTopDownToggle();
	void OnSwitchToTopDown();
	void SwitchToNextMultiDroneFollowView(float BlendTime);
	void OnSwitchToRealTimeDrone();
	void OnShiftPressed();
	void OnShiftReleased();
	void OnTestToast();

	// 编辑模式：按住右键旋转视角
	void OnEditLookPressed();
	void OnEditLookReleased();

	/** B 键：返回主菜单关卡 */
	void OnReturnToMainMenu();

	// Click handling
	void HandleMapClick(const FVector& WorldLocation);
	void HandleDroneClick(AActor* ClickedActor);

	// Drone registry reference
	UPROPERTY()
	TObjectPtr<UDroneRegistrySubsystem> DroneRegistry;

	// Current camera mode state
	UPROPERTY(BlueprintReadOnly, Category = "DroneOps")
	FCameraModeState CameraModeState;

	// Currently hovered drone
	UPROPERTY(BlueprintReadOnly, Category = "DroneOps")
	int32 HoveredDroneId = 0;

	UPROPERTY()
	TObjectPtr<AActor> HoveredDroneActor = nullptr;

	// Currently selected drone actor (direct ref for immediate command dispatch)
	UPROPERTY()
	TObjectPtr<AActor> SelectedDroneActor = nullptr;

	// Currently selected drone id
	int32 SelectedDroneId = 0;

	// Helper functions
	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	void SendTargetCommand(int32 DroneId, const FVector& TargetWorldLocation);

	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	AActor* GetActorUnderCursor() const;

	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	bool GetWorldLocationUnderCursor(FVector& OutLocation) const;

public:
	/**
	 * Event broadcast when user clicks middle mouse button while hovering a drone.
	 * The HUD should handle this event and open the drone info panel.
	 */
	UPROPERTY(BlueprintAssignable, Category = "DroneOps Events")
	FOnOpenDroneInfoPanelRequested OnOpenDroneInfoPanelRequested;

	/** Class of the main HUD widget to spawn on begin play */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<UUserWidget> DroneOpsHUDWidgetClass;

	/** 框选矩形 Widget 类，在 BP_DroneOpsPlayerController 中赋值 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<UUserWidget> BoxSelectWidgetClass;

	/** 运行时框选矩形 Widget 实例 */
	UPROPERTY()
	UUserWidget* BoxSelectWidgetInstance = nullptr;

	/** Class of the drone info panel widget */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<UUserWidget> DroneInfoPanelWidgetClass;

	/** 主菜单关卡名称，B 键跳转目标 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Navigation")
	FName MainMenuLevelName = FName("MainMenu");

	/** 框选是否正在进行中（供 Widget 蓝图读取驱动矩形显示） */
	UPROPERTY(BlueprintReadOnly, Category = "BoxSelect")
	bool bIsBoxSelectingBP = false;

	/** 框选起点（逻辑像素，viewport-relative，供 Widget 蓝图读取） */
	UPROPERTY(BlueprintReadOnly, Category = "BoxSelect")
	FVector2D BoxSelectStartLogical = FVector2D::ZeroVector;

	/** 框选当前终点（逻辑像素，viewport-relative，供 Widget 蓝图读取） */
	UPROPERTY(BlueprintReadOnly, Category = "BoxSelect")
	FVector2D BoxSelectEndLogical = FVector2D::ZeroVector;

private:
	AActor* GetSelectableDroneUnderCursor(FVector* OutFallbackWorldLocation = nullptr) const;
	AActor* FindNearestSelectableDroneOnScreen(float MaxScreenDistance) const;
	AActor* ResolveDroneActorById(int32 DroneId) const;
	AActor* ResolveFollowViewTargetByDroneId(int32 DroneId) const;
	void ApplyFollowViewTarget(int32 DroneId);

	// ---- 路径编辑模式内部实现（移植自 ADroneRuntimeInteractionPlayerController）----
	void HandleEditModePressed();
	void HandleEditModeReleased();
	void UpdateDraggedEditWaypoint();
	float ResolveEditAxisDragDelta();
	void SetEditSelectedWaypoint(ADroneWaypointActor* NewWaypoint);
	void SetEditActiveAxis(EGizmoAxis NewAxis);
	bool CanInteractWithEditWaypoint(const ADroneWaypointActor* WaypointActor) const;
	void AddWaypointToAllEditingPaths(const FVector& WorldLocation);

	// 编辑模式相机：进入时切自由相机（WASD 移动 + 按住右键转视角），退出时恢复
	void EnterEditCamera();
	void ExitEditCamera();
	void TickEditCamera(float DeltaTime);

	// 编辑模式航点增删
	void RemoveSelectedEditWaypoint();   // Delete：删除选中航点（首航点禁止）
	void UndoLastEditWaypoint();         // Ctrl+Z：移除最近一次添加的航点
	void HandleDeleteWaypointKey();
	void HandleUndoWaypointKey();

	// 编辑模式状态
	bool bPathEditMode = false;

	// 进入编辑相机前的相机/视角状态，退出时还原
	EDroneCameraMode EditPrevCameraMode = EDroneCameraMode::Follow;
	UPROPERTY()
	TObjectPtr<AActor> EditPrevViewTarget = nullptr;
	// 按住右键才旋转视角，避免与左键点选/拖拽冲突
	bool bEditCameraLooking = false;
	// 编辑相机当前是否已启用（保证 Enter/Exit 幂等）
	bool bEditCameraActive = false;

	UPROPERTY()
	TArray<TObjectPtr<ADronePathActor>> EditingPaths;

	TArray<int32> EditingDroneIds;

	// 每条路径首航点的世界坐标（编队平移基准），与 EditingPaths 同序
	TArray<FVector> EditingPathOrigins;

	// 编队参考原点（第一架影子机的首航点世界坐标）
	FVector EditFormationRefOrigin = FVector::ZeroVector;

	UPROPERTY()
	TObjectPtr<ADroneWaypointActor> EditSelectedWaypoint = nullptr;

	EGizmoAxis EditActiveAxis = EGizmoAxis::None;
	bool bEditDraggingWaypoint = false;
	FVector2D EditLastMouseScreenPos = FVector2D::ZeroVector;

	// 编辑时地图点击加点的默认段速度（m/s）
	UPROPERTY(EditAnywhere, Category = "PathEdit")
	float EditDefaultSegmentSpeed = 5.0f;

	// gizmo 拖拽灵敏度
	UPROPERTY(EditAnywhere, Category = "PathEdit")
	float EditGizmoDragSensitivity = 1.0f;

	// FR-04: Free camera actor (spawned at runtime)
	UPROPERTY()
	TObjectPtr<ACameraActor> FreeCamActor;

	// Top-down camera actor (spawned at runtime, toggled by Space)
	UPROPERTY()
	TObjectPtr<ACameraActor> TopDownCamActor;

	// Previous camera mode before entering TopDown, so Space can toggle back
	EDroneCameraMode PreTopDownMode = EDroneCameraMode::Follow;

	// ViewTarget active right before entering TopDown — restored verbatim on exit
	UPROPERTY()
	TObjectPtr<AActor> PreTopDownViewTarget = nullptr;

	// Height (cm) of the top-down camera above the focused drone
	UPROPERTY(EditAnywhere, Category = "TopDownCam")
	float TopDownHeightCm = 5000.0f;

	// FR-04: Cached free camera rotation for smooth control
	FRotator FreeCamRotation = FRotator::ZeroRotator;

	// FR-04: Free camera movement speed (cm/s)
	UPROPERTY(EditAnywhere, Category = "FreeCam")
	float FreeCamMoveSpeed = 2000.0f;

	// FR-04: Free camera mouse sensitivity
	UPROPERTY(EditAnywhere, Category = "FreeCam")
	float FreeCamMouseSensitivity = 0.05f;

	/** Current open info panel, if any */
	UPROPERTY()
	UUserWidget* CurrentDroneInfoPanel = nullptr;

	/** Drone currently bound to the open information panel. */
	int32 CurrentDroneInfoDroneId = 0;

	/** Information panel refresh period. Default 0.2 seconds = 5 Hz. */
	UPROPERTY(EditAnywhere, Category = "HUD", meta = (ClampMin = "0.05"))
	float DroneInfoPanelRefreshIntervalSec = 0.2f;

	FTimerHandle DroneInfoRefreshTimerHandle;

	/** Shadow drones that received vertical input on the previous controller tick. */
	TArray<TWeakObjectPtr<AMultiDroneCharacter>> VerticallyControlledDrones;

	/** Last non-free camera target so F can restore it */
	UPROPERTY()
	TObjectPtr<AActor> LastFollowViewTarget = nullptr;

	/** Tracks which drones are currently paused (toggled by P key) */
	TSet<int32> PausedDroneIds;

	/** True = Follow视角当前处于纯俯视(-90°)，false = 斜视(-60°) */
	bool bFollowTopDownPitch = false;

	/** SpringArm俯仰角插值目标值 */
	float FollowTargetPitch = -60.f;

	/** SpringArm俯仰角插值速度（度/秒） */
	UPROPERTY(EditAnywhere, Category = "Camera")
	float CameraPitchInterpSpeed = 5.f;

	/** Whether Shift is held — used for multi-select on click */
	bool bShiftHeld = false;

	// ---- 框选状态 ----
	/** Shift+左键按下时记录起始屏幕坐标，等待 OnPrimaryReleased 决定是框选还是短点击 */
	bool bIsBoxSelecting = false;
	FVector2D BoxSelectStartScreen = FVector2D::ZeroVector;
	/** 框选拖拽期间每帧更新的当前鼠标位置，提交时用此值而非释放后重新读取 */
	FVector2D BoxSelectCurrentScreen = FVector2D::ZeroVector;
	/** 拖拽超过此像素距离后才触发框选，否则视为短点击 */
	static constexpr float BoxSelectThresholdPx = 5.0f;

	/** 根据起止屏幕矩形对场景中所有影子机做一次切换并提交最终选中集合 */
	void CommitBoxSelect(FVector2D StartScreen, FVector2D EndScreen);

	/** 框选完成逻辑（由 OnPrimaryReleased 和 Tick 共同调用，内置幂等保护） */
	void FinishBoxSelectIfPending();

	/** 通知框选 Widget 显示/更新/隐藏（内部通过反射调用蓝图函数） */
	void NotifyBoxSelectShow();
	void NotifyBoxSelectUpdate(FVector2D Start, FVector2D End);
	void NotifyBoxSelectHide();

	/** Last drone ID the user explicitly switched to (key 0/1 or click). F restores this. -1 = never set. */
	int32 LastFollowedDroneId = -1;

	/** Current index for cycling through AMultiDroneCharacter actors (key 0) */
	int32 MultiDroneCharacterIndex = 0;

	/** Current index for cycling through ARealTimeDroneReceiver actors (key 1) */
	int32 RealTimeDroneIndex = 0;

	/** Cached RealTimeDroneReceiver for 1-key camera switch */
	UPROPERTY()
	TObjectPtr<AActor> CachedRealTimeDrone = nullptr;

	/** Open drone info panel for given DroneId (C++ implementation) */
	void OpenDroneInfoPanel(int32 DroneId);
	void RefreshDroneInfoPanel();
	void CloseCurrentDroneInfoPanel();
	void UpdateSelectedDroneVerticalControl();
	void StopSelectedDroneVerticalControl(bool bSendFinalCommand = true);

	UFUNCTION(Exec)
	void TestSendArrayTask();

	UFUNCTION()
	void OnTestArrayTaskComplete(bool bSuccess, const FString& ResponseBody);
};
