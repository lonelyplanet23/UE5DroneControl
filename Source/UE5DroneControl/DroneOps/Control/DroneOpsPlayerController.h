// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneOps/Core/GeographicTypes.h"
#include "Components/WidgetComponent.h"
#include "Camera/CameraActor.h"
#include "PathEditor/DroneWaypointActor.h"
#include "PathEditor/DronePathSaveLibrary.h"
#include "DroneOps/Core/HostileTargetActor.h"
#include "DroneOps/Core/HostileTargetManager.h"
#include "UI/PreviewConfirmPopupWidget.h"
#include "DroneOpsPlayerController.generated.h"

class UDroneRegistrySubsystem;
class ADronePathActor;
class ADroneWaypointActor;
class AMultiDroneCharacter;
class UUserWidget;
class UDroneInfoPanelWidget;

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

	/**
	 * Dispatch (or preview) a geographic target for the currently selected drone(s).
	 *
	 * The primary selected drone is placed exactly at the input lon/lat/alt point; any other
 * multi-selected drones are arranged around it on the existing 1 m square spiral, ordered
 * by ascending DroneId for a stable layout. Shadow drones always move locally; when the backend
 * is connected and a drone has a GPS anchor, one corresponding command is also sent. Altitude is
 * treated as height above mean sea level and converted to
	 * ellipsoid height via UDroneNetworkManager::GeoidSeparationMeters before the coordinate
	 * transform.
	 *
	 * @param CoordinateSystem  Coordinate system of the input (currently only WGS84).
	 * @param Longitude  Longitude in degrees, range [-180, 180].
	 * @param Latitude   Latitude in degrees, range [-90, 90].
	 * @param AltitudeMslMeters  Altitude in metres above mean sea level.
	 * @param bPreviewOnly  When true, only draws preview markers and sends no command.
	 * @return Result describing success, dispatched count, and a status message.
	 */
	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	FGeographicDispatchResult DispatchGeographicTarget(
		EGeographicCoordinateSystem CoordinateSystem,
		double Longitude,
		double Latitude,
		double AltitudeMslMeters,
		bool bPreviewOnly);

	/**
 * Validate the current selection and a geographic target without drawing or sending anything.
 * bForDispatch=true validates local dispatch prerequisites only; backend connection and GPS
 * anchors affect command delivery status but never block the local shadow-drone dispatch.
	 */
	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	FGeographicDispatchResult ValidateGeographicTarget(
		EGeographicCoordinateSystem CoordinateSystem,
		double Longitude,
		double Latitude,
		double AltitudeMslMeters,
		bool bForDispatch) const;

	/** Number of drones currently selected for dispatch (multi-selection, or 0). */
	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	int32 GetSelectedDroneCountForDispatch() const;

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

	/**
	 * 编辑模式下把一个 WGS84 经纬高目标点加为所有在编路径的航点（与地图点击加点同样走编队平移）。
	 * 非编辑模式或无在编路径时返回失败。用于右上角坐标输入面板在编辑模式下的行为。
	 */
	UFUNCTION(BlueprintCallable, Category = "DroneOps|PathEdit")
	FGeographicDispatchResult AddGeographicWaypointInEditMode(
		EGeographicCoordinateSystem CoordinateSystem,
		double Longitude,
		double Latitude,
		double AltitudeMslMeters);

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

	// ===== 敌对目标点管理 =====

	/** 在鼠标位置放置敌对目标点（Ctrl+Shift+左键，避免占用框选快捷键） */
	UFUNCTION(BlueprintCallable, Category = "HostileTarget")
	void SpawnHostileTarget();

	/** 删除选中的敌对目标点 */
	UFUNCTION(BlueprintCallable, Category = "HostileTarget")
	void RemoveHostileTarget(AHostileTargetActor* Target);

	/** 获取敌对目标点管理器 */
	UFUNCTION(BlueprintPure, Category = "HostileTarget")
	class UHostileTargetManager* GetHostileTargetManager() const;

	/** 获取当前选中的目标点（高亮） */
	UFUNCTION(BlueprintPure, Category = "HostileTarget")
	AHostileTargetActor* GetSelectedHostileTarget() const { return SelectedHostileTarget; }

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

	// ===== 敌对目标点发现检测 =====
	/** Tick中调用：检测所有巡逻无人机是否发现目标 */
	void CheckHostileTargetDetection();

	/** 处理无人机发现目标的逻辑（弹窗、分配） */
	void HandleTargetDiscovery(int32 DroneId, int32 TargetId);

	/** 获取鼠标下的敌对目标点 */
	AHostileTargetActor* GetHostileTargetUnderCursor() const;

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
	TSubclassOf<UDroneInfoPanelWidget> DroneInfoPanelWidgetClass;

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "HostileTarget")
	TSubclassOf<UPreviewConfirmPopupWidget> PreviewConfirmPopupClass;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FWgs84GeographicDispatchLatentCommand;
#endif

	struct FGeographicDispatchSlot
	{
		int32 DroneId = 0;
		FVector WorldTarget = FVector::ZeroVector;
		bool bIsPrimary = false;
	};

	FGeographicDispatchResult BuildGeographicDispatchPlan(
		EGeographicCoordinateSystem CoordinateSystem,
		double Longitude,
		double Latitude,
		double AltitudeMslMeters,
		bool bForDispatch,
		TArray<FGeographicDispatchSlot>& OutSlots) const;

	/**
	 * 把 WGS84 经纬高（海拔 MSL）转换为 UE 世界坐标（cm）。
	 * 供派发规划与编辑模式加航点共用，避免重复坐标转换逻辑。
	 * 失败时返回 false 并写入 OutError（坐标系不支持/服务未就绪/超范围/转换失败）。
	 */
	bool TryConvertGeographicToWorld(
		EGeographicCoordinateSystem CoordinateSystem,
		double Longitude,
		double Latitude,
		double AltitudeMslMeters,
		FVector& OutWorld,
		FString& OutError) const;

	/** Builds the same stable primary-first formation targets for every world-space input path. */
	FGeographicDispatchResult BuildWorldDispatchPlan(
		const FVector& BaseWorldTarget,
		TArray<FGeographicDispatchSlot>& OutSlots) const;

	/** Moves shadow drones locally first; sends backend commands only when they are currently possible. */
	FGeographicDispatchResult ExecuteWorldDispatchPlan(const TArray<FGeographicDispatchSlot>& Slots);

	/** Last preview plan, redrawn every frame until another preview replaces it. */
	TArray<FGeographicDispatchSlot> ActiveGeographicPreviewSlots;

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

	// 编辑模式下点击无人机：活跃则冻结（保留路径），已冻结/未加入则激活（复活或新建路径）。
	// 返回 true 表示本次点击已作为增删处理（不应再当作加航点）。
	bool ToggleDroneInEditMode(int32 DroneId);
	// 激活单架无人机：已有(冻结)路径则复活并接续加点；无则新建一条以影子机当前位置为首航点的路径。
	void AddDroneToEditMode(int32 DroneId);
	// 冻结单架无人机：保留其临时路径可见但不再加点，并移出选中集合（不销毁，退出编辑时才清）。
	void FreezeDroneInEditMode(int32 DroneId);
	// 重算编队参考原点为当前第一条在编路径的起点（增删无人机后调用，避免落点偏移）。
	void RecomputeEditFormationRefOrigin();

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

	// 每条路径当前是否活跃（接收新航点），与 EditingPaths 同序。
	// 取消选中无人机 = 冻结(false)，路径保留可见但不再加点；重新选中 = 复活(true)。
	// 只有退出编辑模式(ClearEditingPaths)才真正销毁所有路径。
	TArray<bool> EditingPathActive;

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
	TObjectPtr<UDroneInfoPanelWidget> CurrentDroneInfoPanel = nullptr;

	/** Drone currently bound to the open information panel. */
	int32 CurrentDroneInfoDroneId = 0;

	/** Information panel refresh period. Default 0.2 seconds = 5 Hz. */
	UPROPERTY(EditAnywhere, Category = "HUD", meta = (ClampMin = "0.05"))
	float DroneInfoPanelRefreshIntervalSec = 0.2f;

	FTimerHandle DroneInfoRefreshTimerHandle;

	void OnDroneInfoPanelClosed();

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

	// ===== 敌对目标点成员 =====

	UPROPERTY()
	TObjectPtr<AHostileTargetActor> SelectedHostileTarget = nullptr;

	UPROPERTY(EditAnywhere, Category = "HostileTarget")
	TSubclassOf<AHostileTargetActor> HostileTargetClass;

	/** 攻击确认弹窗回调（由 PreviewConfirmPopupWidget 的 OnAttackConfirmMade delegate 触发） */
	UFUNCTION()
	void OnAttackConfirmMade(int32 DroneId, int32 TargetId, bool bAttack);
};
