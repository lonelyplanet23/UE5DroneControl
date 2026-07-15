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

	/** 设置某条临时路径的循环开关（预演关卡，每条路径独立）。 */
	UFUNCTION(BlueprintCallable, Category = "DroneOps|PathEdit")
	bool SetEditingPathClosedLoop(int32 DroneId, bool bClosedLoop);

	/** 一次性设置全部临时路径的循环开关。 */
	UFUNCTION(BlueprintCallable, Category = "DroneOps|PathEdit")
	void SetAllEditingPathsClosedLoop(bool bClosedLoop);

	// ---- 编队旋转预览（JSON 路径：运行锚点平移 + 水平旋转 Gizmo）----

	/**
	 * 编队旋转的几何变换：把编辑系节点坐标先相对源锚点旋转（仅水平面绕 Z），再平移到目标基准位置。
	 * 高度(Z)保持不变，节点相对距离不变。预演与派发共用此函数，保证结果完全一致。
	 */
	static FVector ApplyFormationTransform(const FVector& NodeEditLocation, const FVector& RefEditOrigin, const FVector& TargetBase, float YawDegrees);

	/**
	 * 解析运行锚点无人机：优先主选中，其次最小可用 DroneId；都没有则返回 false。
	 * OutAnchorWorld = 该无人机当前世界位置。
	 */
	bool ResolveRunAnchorDrone(int32& OutDroneId, FVector& OutAnchorWorld);

	/**
	 * 开始 JSON 编队旋转预览：生成预览路径可视化 + 锚点旋转环 Gizmo。角度从 0 开始。
	 * 失败（无有效运行锚点）返回 false。
	 */
	bool BeginFormationRotatePreview(const FDronePathsSaveData& PathsData);

	/** 结束编队旋转预览：销毁预览可视化与 Gizmo。 */
	UFUNCTION(BlueprintCallable, Category = "DroneOps|PathEdit")
	void EndFormationRotatePreview();

	bool IsFormationRotateActive() const { return bFormationRotateActive; }
	float GetFormationYawDegrees() const { return FormationYawDegrees; }
	FVector GetFormationRunAnchorWorld() const { return FormationRunAnchorWorld; }
	FVector GetFormationRefEditOrigin() const { return FormationRefEditOrigin; }
	int32 GetFormationRunAnchorDroneId() const { return FormationRunAnchorDroneId; }

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

	/** Class of the drone info panel widget */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HUD")
	TSubclassOf<UUserWidget> DroneInfoPanelWidgetClass;

	/** 主菜单关卡名称，B 键跳转目标 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Navigation")
	FName MainMenuLevelName = FName("MainMenu");

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

	// ---- 编队旋转预览状态 ----
	// 预览路径可视化 Actor（只显示不移动），BeginFormationRotatePreview 生成，End 清理。
	UPROPERTY()
	TArray<TObjectPtr<ADronePathActor>> FormationPreviewActors;

	// 锚点旋转环 Gizmo。
	UPROPERTY()
	TObjectPtr<class AFormationRotationGizmoActor> FormationGizmoActor = nullptr;

	// 当前加载的 JSON 路径数据（原始，只读，不修改）。
	FDronePathsSaveData FormationSourcePaths;

	bool bFormationRotateActive = false;
	// 当前编队水平旋转角（度，绕世界 Z）。
	float FormationYawDegrees = 0.0f;
	// 运行锚点无人机当前世界位置（编队平移目标基准）。
	FVector FormationRunAnchorWorld = FVector::ZeroVector;
	int32 FormationRunAnchorDroneId = INDEX_NONE;
	// 源锚点（JSON 中 AnchorDroneId 路径首航点的编辑系坐标）。
	FVector FormationRefEditOrigin = FVector::ZeroVector;

	// 旋转环拖拽状态。
	bool bDraggingFormationRing = false;
	// 旋转环半径（cm），生成 Gizmo 时缓存，用于平面命中判定。
	float FormationRingRadiusCm = 400.0f;
	// 编队旋转灵敏度：每单位水平鼠标增量对应的偏航角（度）。
	UPROPERTY(EditAnywhere, Category = "PathEdit")
	float FormationRotateDegPerMouseUnit = 4.0f;

	void RefreshFormationPreview();
	void HandleFormationRingPressed();
	void UpdateFormationRingDrag();
	void EndFormationRingDrag();
	// 鼠标反投影到锚点水平面，返回落点离锚点的距离(cm)。用于按下时判定是否点中旋转环。
	bool ComputeCursorRadiusOnAnchorPlane(float& OutRadiusCm) const;
	// 光标是否落在旋转环附近（平面半径判定，命中可靠，不依赖细碰撞体）。
	bool IsCursorOnFormationRing() const;

	UPROPERTY()
	TObjectPtr<ADroneWaypointActor> EditSelectedWaypoint = nullptr;

	EGizmoAxis EditActiveAxis = EGizmoAxis::None;
	bool bEditDraggingWaypoint = false;
	FVector2D EditLastMouseScreenPos = FVector2D::ZeroVector;

	// 编辑时地图点击加点的默认段速度（m/s）
	UPROPERTY(EditAnywhere, Category = "PathEdit")
	float EditDefaultSegmentSpeed = 1.0f;

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
