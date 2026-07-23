// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOpsPlayerController.h"
#include "InputCoreTypes.h"
#include "Camera/CameraActor.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/ICoordinateService.h"
#include "DroneOps/Core/HostileTargetActor.h"
#include "DroneOps/Core/HostileTargetManager.h"
#include "DroneOps/Interfaces/DroneSelectableInterface.h"
#include "DroneOps/Drone/DroneSelectionComponent.h"
#include "DroneOps/Drone/DroneCommandSenderComponent.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "MultiDroneCharacter.h"
#include "RealTimeDroneReceiver.h"
#include "MultiDroneManager.h"
#include "PathEditor/DronePathActor.h"
#include "PathEditor/DroneWaypointActor.h"
#include "PathEditor/DronePathSaveLibrary.h"
#include "PathEditor/FormationRotationGizmoActor.h"
#include "UE5DroneControlCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "UI/UIManagerBlueprintLibrary.h"
#include "UI/DroneInfoPanelWidget.h"
#include "UI/DroneVideoWindowManager.h"
#include "UI/DroneVideoWindowWidget.h"
#include "UI/GeographicTargetPanelWidget.h"
#include "UI/LocalPreviewIsolationToggleWidget.h"
#include "UI/SequenceDispatchPanelWidget.h"
#include "UI/DroneInfoPanelWidget.h"
#include "UI/GeographicTargetPanelWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/MeshComponent.h"

namespace
{
	bool IsFr2ControllableDroneActor(const AActor* Actor)
	{
		return IsValid(Actor) && Actor->IsA(AMultiDroneCharacter::StaticClass());
	}

	AActor* FindShadowDroneById(const UWorld* World, int32 DroneId)
	{
		if (!World || DroneId <= 0)
		{
			return nullptr;
		}

		for (TActorIterator<AMultiDroneCharacter> It(World); It; ++It)
		{
			AMultiDroneCharacter* Candidate = *It;
			if (IsValid(Candidate) && Candidate->DroneId == DroneId)
			{
				return Candidate;
			}
		}

		return nullptr;
	}

	int32 ResolveDroneIdFromActor(const AActor* Actor)
	{
		if (!Actor)
		{
			return 0;
		}

		if (Actor->GetClass()->ImplementsInterface(UDroneSelectableInterface::StaticClass()))
		{
			return IDroneSelectableInterface::Execute_GetDroneId(const_cast<AActor*>(Actor));
		}

		if (const UDroneSelectionComponent* SelectionComp = Actor->FindComponentByClass<UDroneSelectionComponent>())
		{
			return SelectionComp->DroneId;
		}

		return 0;
	}

	void SetDroneHoveredState(AActor* Actor, bool bHovered)
	{
		if (!Actor)
		{
			return;
		}

		if (Actor->GetClass()->ImplementsInterface(UDroneSelectableInterface::StaticClass()))
		{
			IDroneSelectableInterface::Execute_OnHoveredChanged(Actor, bHovered);
			return;
		}

		if (UDroneSelectionComponent* SelectionComp = Actor->FindComponentByClass<UDroneSelectionComponent>())
		{
			SelectionComp->SetHovered(bHovered);
		}
	}

	void SetDronePrimarySelectedState(AActor* Actor, bool bSelected)
	{
		if (!Actor)
		{
			return;
		}

		if (Actor->GetClass()->ImplementsInterface(UDroneSelectableInterface::StaticClass()))
		{
			if (bSelected)
			{
				IDroneSelectableInterface::Execute_OnPrimarySelected(Actor);
			}
			else
			{
				IDroneSelectableInterface::Execute_OnDeselected(Actor);
			}
			return;
		}

		if (UDroneSelectionComponent* SelectionComp = Actor->FindComponentByClass<UDroneSelectionComponent>())
		{
			SelectionComp->SetPrimarySelected(bSelected);
			if (!bSelected)
			{
				SelectionComp->SetSecondarySelected(false);
			}
		}
	}

	// 多机派发时为每架无人机计算一个不重叠的偏移点（方形螺旋，中心为 index 0）。
	FVector ComputeMultiDispatchOffset(int32 Index, float SpacingCm)
	{
		if (Index <= 0)
		{
			return FVector::ZeroVector;
		}

		int32 Ring = 1;
		while (((2 * Ring + 1) * (2 * Ring + 1)) <= Index)
		{
			++Ring;
		}

		const int32 RingStart = (2 * Ring - 1) * (2 * Ring - 1);
		const int32 RingOffset = Index - RingStart;
		const int32 SideLen = 2 * Ring;
		const int32 Side = RingOffset / SideLen;
		const int32 PosOnSide = RingOffset % SideLen;

		int32 GX = 0;
		int32 GY = 0;
		switch (Side)
		{
			case 0: GX =  Ring;             GY = -Ring + PosOnSide; break;
			case 1: GX =  Ring - PosOnSide; GY =  Ring;             break;
			case 2: GX = -Ring;             GY =  Ring - PosOnSide; break;
			case 3: GX = -Ring + PosOnSide; GY = -Ring;             break;
			default: break;
		}

		return FVector(static_cast<float>(GX) * SpacingCm, static_cast<float>(GY) * SpacingCm, 0.0f);
	}
}

ADroneOpsPlayerController::ADroneOpsPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	PrimaryActorTick.bCanEverTick = true;
	// A hostile target must always have a visible native fallback. Blueprint overrides are optional.
	HostileTargetClass = AHostileTargetActor::StaticClass();
	PreviewConfirmPopupClass = UPreviewConfirmPopupWidget::StaticClass();

	// Keep the Blueprint property overridable, but provide the project panel as
	// the C++ fallback so middle-click works when a controller BP has no value.
	static ConstructorHelpers::FClassFinder<UDroneInfoPanelWidget> DroneInfoPanelClass(
		TEXT("/Game/DroneOps/UI/WBP_DroneInfoPanel"));
	if (DroneInfoPanelClass.Succeeded())
	{
		DroneInfoPanelWidgetClass = DroneInfoPanelClass.Class;
	}

	static ConstructorHelpers::FClassFinder<UDroneVideoWindowWidget> DroneVideoWindowClass(
		TEXT("/Game/DroneOps/UI/WBP_DroneVideoWindow"));
	if (DroneVideoWindowClass.Succeeded())
	{
		DroneVideoWindowWidgetClass = DroneVideoWindowClass.Class;
	}
}

void ADroneOpsPlayerController::BeginPlay()
{
	Super::BeginPlay();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Green, TEXT(">>> DroneOpsPlayerController ACTIVE <<<"));
	}
	UE_LOG(LogTemp, Warning, TEXT("=== DroneOpsPlayerController::BeginPlay CALLED ==="));

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		DroneRegistry = GameInstance->GetSubsystem<UDroneRegistrySubsystem>();
		if (DroneRegistry)
		{
			UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: Connected to DroneRegistry"));
			// 订阅多选变更委托，使 UI 面板的选中操作同步刷新场景高亮
			DroneRegistry->OnMultiSelectionChanged.AddDynamic(
				this, &ADroneOpsPlayerController::OnRegistryMultiSelectionChanged);
		}
	}

	CameraModeState.CameraMode = EDroneCameraMode::Follow;
	CameraModeState.FollowDroneId = DroneRegistry ? DroneRegistry->GetPrimarySelectedDrone() : 0;
	LastFollowViewTarget = GetViewTarget();
	if (AActor* InitialFollowTarget = ResolveFollowViewTargetByDroneId(CameraModeState.FollowDroneId))
	{
		LastFollowViewTarget = InitialFollowTarget;
	}

	// FR-04: Spawn free camera actor
	FreeCamActor = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);

	// Create and add main HUD widget to viewport
	if (DroneOpsHUDWidgetClass && IsLocalController())
	{
		UUserWidget* HUDWidget = CreateWidget(this, DroneOpsHUDWidgetClass);
		if (HUDWidget)
		{
			// HUD 放在框选层(ZOrder=10)等临时覆盖层之上，否则那些全屏覆盖层会挡住 HUD 上按钮的点击。
			HUDWidget->AddToViewport(50);
			UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: HUD widget created and added to viewport"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("DroneOpsPlayerController: Failed to create HUD widget"));
		}
	}

	// 创建框选矩形 Widget，默认隐藏，框选期间由控制器调用蓝图函数驱动显示
	if (BoxSelectWidgetClass && IsLocalController())
	{
		BoxSelectWidgetInstance = CreateWidget(this, BoxSelectWidgetClass);
		if (BoxSelectWidgetInstance)
		{
			BoxSelectWidgetInstance->AddToViewport(10);
			UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: BoxSelectWidget created OK, class=%s"), *BoxSelectWidgetClass->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("DroneOpsPlayerController: BoxSelectWidget CreateWidget FAILED"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneOpsPlayerController: BoxSelectWidgetClass is NULL - not assigned in BP"));
	}

	// 纯本地预演隔离 Toggle（右上角，高 ZOrder 确保不被遮挡）
	{
		ULocalPreviewIsolationToggleWidget* IsolationToggle =
			CreateWidget<ULocalPreviewIsolationToggleWidget>(this, ULocalPreviewIsolationToggleWidget::StaticClass());
		if (IsolationToggle)
		{
			IsolationToggle->AddToViewport(50); // ZOrder 50：高于普通面板，低于弹窗
			UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: LocalPreviewIsolationToggle created"));
		}
	}

	// Create Sequence Dispatch Panel (persistent, bottom-right)
	UUIManagerBlueprintLibrary::ShowSequenceDispatchPanel(this);

	// Create Geographic Target Panel (persistent; coordinate icon button toggles the body)
	UUIManagerBlueprintLibrary::ShowGeographicTargetPanel(this);

	// CesiumWorld 也需要常驻的无人机态势面板。具体锚点由 DroneListWidget
	// 根据当前地图决定：CesiumWorld 左下角，MainMenu 右上角。
	UUIManagerBlueprintLibrary::ShowDroneList(this);

	FTimerHandle InitialFollowViewTimer;
	GetWorldTimerManager().SetTimer(
		InitialFollowViewTimer,
		[this]()
		{
			SwitchToNextMultiDroneFollowView(0.0f);
		},
		0.2f,
		false);
}

void ADroneOpsPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSelectedDroneVerticalControl(false);
	CloseCurrentDroneInfoPanel();
	CloseAllDroneVideoWindows();
	EndFormationRotatePreview();
	Super::EndPlay(EndPlayReason);
}

void ADroneOpsPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ADroneOpsPlayerController::OnPrimaryClick);
		InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ADroneOpsPlayerController::OnPrimaryReleased);
		InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &ADroneOpsPlayerController::OnShowInfo);

		// 编辑模式：按住右键转视角、Delete 删除航点、Ctrl+Z 撤销
		InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ADroneOpsPlayerController::OnEditLookPressed);
		InputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &ADroneOpsPlayerController::OnEditLookReleased);
		InputComponent->BindKey(EKeys::Delete, IE_Pressed, this, &ADroneOpsPlayerController::HandleDeleteWaypointKey);
		FInputChord UndoChord(EKeys::Z);
		UndoChord.bCtrl = true;
		InputComponent->BindKey(UndoChord, IE_Pressed, this, &ADroneOpsPlayerController::HandleUndoWaypointKey);
		// Ctrl+A：编辑模式下全选所有可编辑航点
		FInputChord SelectAllChord(EKeys::A);
		SelectAllChord.bCtrl = true;
		InputComponent->BindKey(SelectAllChord, IE_Pressed, this, &ADroneOpsPlayerController::HandleSelectAllWaypointsKey);
		InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ADroneOpsPlayerController::OnFreeCamToggle);
		InputComponent->BindKey(EKeys::P, IE_Pressed, this, &ADroneOpsPlayerController::OnPauseToggle);
		InputComponent->BindKey(EKeys::Zero, IE_Pressed, this, &ADroneOpsPlayerController::OnSwitchToTopDown);
		InputComponent->BindKey(EKeys::One, IE_Pressed, this, &ADroneOpsPlayerController::OnSwitchToRealTimeDrone);
		InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ADroneOpsPlayerController::ResetShadowDronesToMirrors);
		InputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &ADroneOpsPlayerController::OnShiftPressed);
		InputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &ADroneOpsPlayerController::OnShiftReleased);
		InputComponent->BindKey(EKeys::RightShift, IE_Pressed, this, &ADroneOpsPlayerController::OnShiftPressed);
		InputComponent->BindKey(EKeys::RightShift, IE_Released, this, &ADroneOpsPlayerController::OnShiftReleased);
		InputComponent->BindKey(EKeys::B, IE_Pressed, this, &ADroneOpsPlayerController::OnReturnToMainMenu);
		UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: Input bindings installed"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DroneOpsPlayerController: InputComponent is null"));
		InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ADroneOpsPlayerController::OnTestToast);
	}
}

void ADroneOpsPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 编队旋转预览：拖拽旋转环时每帧更新角度（跟随光标）。
	// 结束由 OnPrimaryReleased 驱动；不用 IsInputKeyDown 判断——GameAndUI 输入模式下它读不到
	// 左键按住状态，会导致每帧误判为松开、拖拽被立刻清掉（yaw 永远为 0）。
	if (bFormationRotateActive && bDraggingFormationRing)
	{
		UpdateFormationRingDrag();
	}

	// 路径编辑模式下：驱动自由相机 + gizmo 拖拽，跳过常规相机/悬停逻辑
	if (bPathEditMode)
	{
		// 剔除已失效的选中航点（可能被删除/路径重建），并同步主选中。
		PruneInvalidSelection();

		if (bEditDraggingWaypoint)
		{
			UpdateDraggedEditWaypoint();
		}
		else
		{
			// 拖拽航点时不移动相机，避免冲突
			TickEditCamera(DeltaTime);
		}
		return;
	}

	// SpaceBar: 单选时在Follow模式下切换SpringArm俯仰角(-60° <-> -90°)；多选时忽略。
	// Polled here (not BindKey) because BindKey misses some edges depending on InputMode.
	// 编辑模式下禁用：编辑相机自成一套，空格切俯仰角会与之冲突。
	if (!bPathEditMode && WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		if (CameraModeState.CameraMode == EDroneCameraMode::Follow)
		{
			const TArray<int32> MultiSelected = DroneRegistry ? DroneRegistry->GetMultiSelectedDrones() : TArray<int32>();
			const bool bSingleSelect = MultiSelected.Num() <= 1;

			if (bSingleSelect)
			{
				bFollowTopDownPitch = !bFollowTopDownPitch;
				FollowTargetPitch = bFollowTopDownPitch ? -90.f : -60.f;
				UE_LOG(LogTemp, Log, TEXT("SpaceBar: Follow pitch target -> %.0f"), FollowTargetPitch);
			}
			// 多选时不做任何操作，视角继续跟随当前无人机
		}
	}

	// Follow模式下平滑插值SpringArm俯仰角
	if (CameraModeState.CameraMode == EDroneCameraMode::Follow)
	{
		AActor* ViewTarget = GetViewTarget();
		if (AUE5DroneControlCharacter* DroneChar = Cast<AUE5DroneControlCharacter>(ViewTarget))
		{
			if (USpringArmComponent* Boom = DroneChar->GetCameraBoom())
			{
				const float CurrentPitch = Boom->GetRelativeRotation().Pitch;
				const float NewPitch = FMath::FInterpTo(CurrentPitch, FollowTargetPitch, DeltaTime, CameraPitchInterpSpeed);
				if (!FMath::IsNearlyEqual(CurrentPitch, NewPitch, 0.01f))
				{
					Boom->SetRelativeRotation(FRotator(NewPitch, 0.f, 0.f));
				}
			}
		}
	}

	// FR-04: Free camera control
	if (CameraModeState.CameraMode == EDroneCameraMode::Free && FreeCamActor && IsValid(FreeCamActor))
	{
		// 按住Shift时冻结视角，显示鼠标用于点选无人机
		if (!bShiftHeld)
		{
			// 鼠标旋转视角
			float MouseX = 0.0f, MouseY = 0.0f;
			GetInputMouseDelta(MouseX, MouseY);
			if (MouseX != 0.0f || MouseY != 0.0f)
			{
				FreeCamRotation.Yaw += MouseX * FreeCamMouseSensitivity * 100.0f;
				FreeCamRotation.Pitch = FMath::Clamp(FreeCamRotation.Pitch - MouseY * FreeCamMouseSensitivity * 100.0f, -89.0f, 89.0f);
				FreeCamActor->SetActorRotation(FreeCamRotation);
			}
		}

		// WASD/QE 移动（相对于当前视角方向）
		FVector MoveDir = FVector::ZeroVector;
		const FRotationMatrix RotMat(FreeCamRotation);
		if (IsInputKeyDown(EKeys::W)) MoveDir += RotMat.GetScaledAxis(EAxis::X);
		if (IsInputKeyDown(EKeys::S)) MoveDir -= RotMat.GetScaledAxis(EAxis::X);
		if (IsInputKeyDown(EKeys::A)) MoveDir -= RotMat.GetScaledAxis(EAxis::Y);
		if (IsInputKeyDown(EKeys::D)) MoveDir += RotMat.GetScaledAxis(EAxis::Y);
		if (IsInputKeyDown(EKeys::Q)) MoveDir -= FVector::UpVector;
		if (IsInputKeyDown(EKeys::E)) MoveDir += FVector::UpVector;

		if (!MoveDir.IsNearlyZero())
		{
			MoveDir.Normalize();
			FreeCamActor->AddActorWorldOffset(MoveDir * FreeCamMoveSpeed * DeltaTime);
		}
	}

	// Q/E is reserved for the free camera in Free mode. In preview camera modes
	// it controls the local shadow drones represented by the registry selection.
	if (CameraModeState.CameraMode == EDroneCameraMode::Free)
	{
		StopSelectedDroneVerticalControl();
	}
	else
	{
		UpdateSelectedDroneVerticalControl();
	}

	// Top-down camera: lock XY to the drone we entered TopDown on, fixed height, looking straight down
	if (CameraModeState.CameraMode == EDroneCameraMode::TopDown && TopDownCamActor && IsValid(TopDownCamActor))
	{
		AActor* Focus = IsValid(PreTopDownViewTarget) ? PreTopDownViewTarget.Get() : nullptr;
		if (!Focus)
		{
			Focus = ResolveFollowViewTargetByDroneId(CameraModeState.FollowDroneId);
		}
		if (IsValid(Focus))
		{
			const FVector FocusLoc = Focus->GetActorLocation();
			TopDownCamActor->SetActorLocation(FVector(FocusLoc.X, FocusLoc.Y, FocusLoc.Z + TopDownHeightCm));
		}
	}

	// Hover detection
	int32 NewHoveredId = 0;
	AActor* HoveredActor = GetSelectableDroneUnderCursor();
	if (HoveredActor)
	{
		// Debug: log what actor we hit
		UE_LOG(LogTemp, Verbose, TEXT("[FR-03] Hovered actor: %s (class: %s)"),
			*HoveredActor->GetName(), *HoveredActor->GetClass()->GetName());

		// Check if actor implements IDroneSelectable (which means it's a drone)
		if (Cast<IDroneSelectableInterface>(HoveredActor) != nullptr)
		{
			// For BlueprintNativeEvent functions, must use Execute_* static call
			HoveredDroneId = IDroneSelectableInterface::Execute_GetDroneId(HoveredActor);
			UE_LOG(LogTemp, Log, TEXT("[FR-03] Hovered drone detected, DroneId: %d"), HoveredDroneId);
		}
		else
		{
			HoveredDroneId = 0;
			UE_LOG(LogTemp, Verbose, TEXT("[FR-03] Hovered actor doesn't implement IDroneSelectable: %s"),
				*HoveredActor->GetName());
		}
	}
	else
	{
		HoveredDroneId = 0;
	}

	// Keep the last geographic preview visible without spawning actors or sending commands.
	for (const FGeographicDispatchSlot& Slot : ActiveGeographicPreviewSlots)
	{
		const FColor Color = Slot.bIsPrimary ? FColor::Green : FColor::Cyan;
		DrawDebugSphere(GetWorld(), Slot.WorldTarget, 60.0f, 16, Color, false, 0.0f, 0, 2.0f);
	}

	// 自由视角下 IE_Released 事件可能不稳定，用轮询兜底：左键已抬起但框选未提交时主动完成
	if (bIsBoxSelecting)
	{
		// 每帧持续记录鼠标位置——GetMousePositionOnViewport 返回 viewport-relative 逻辑像素，
		// 乘以 DPI scale 转为物理像素，与 ProjectWorldLocationToScreen(true) 坐标系一致
		const float DPI = UWidgetLayoutLibrary::GetViewportScale(this);
		FVector2D LogicalPos = UWidgetLayoutLibrary::GetMousePositionOnViewport(this);
		BoxSelectCurrentScreen = LogicalPos * DPI;

		// 同步蓝图可读变量，供 Widget Tick 驱动矩形显示
		bIsBoxSelectingBP = true;
		BoxSelectStartLogical = BoxSelectStartScreen / DPI;
		BoxSelectEndLogical = LogicalPos;

		// 实时通知 Widget 更新矩形位置，传入逻辑像素（Widget 坐标系）
		NotifyBoxSelectUpdate(BoxSelectStartScreen / DPI, LogicalPos);

		if (!IsInputKeyDown(EKeys::LeftMouseButton))
		{
			FinishBoxSelectIfPending();
		}
	}

	// 敌对目标发现检测
	if (!bPathEditMode)
	{
		CheckHostileTargetDetection();
		if (UHostileTargetManager* Manager = GetHostileTargetManager())
		{
			Manager->ResetUndetectedTargets();
		}
	}
}

void ADroneOpsPlayerController::UpdateSelectedDroneVerticalControl()
{
	if (!DroneRegistry || USequenceDispatchPanelWidget::IsPanelInteractive() || UGeographicTargetPanelWidget::IsPanelInteractive())
	{
		StopSelectedDroneVerticalControl();
		return;
	}

	const float Direction = (IsInputKeyDown(EKeys::E) ? 1.0f : 0.0f)
		- (IsInputKeyDown(EKeys::Q) ? 1.0f : 0.0f);
	if (FMath::IsNearlyZero(Direction))
	{
		StopSelectedDroneVerticalControl();
		return;
	}

	TArray<TWeakObjectPtr<AMultiDroneCharacter>> CurrentControlledDrones;
	for (const int32 DroneId : DroneRegistry->GetMultiSelectedDrones())
	{
		EDroneControlLockReason LockReason = EDroneControlLockReason::None;
		if (DroneRegistry->IsControlLocked(DroneId, LockReason)) continue;
		AMultiDroneCharacter* ShadowDrone = Cast<AMultiDroneCharacter>(DroneRegistry->GetSenderPawn(DroneId));
		if (!IsValid(ShadowDrone)) continue;
		ShadowDrone->SetVerticalMoveInput(Direction);
		CurrentControlledDrones.AddUnique(TWeakObjectPtr<AMultiDroneCharacter>(ShadowDrone));
	}

	for (const TWeakObjectPtr<AMultiDroneCharacter>& PreviousDrone : VerticallyControlledDrones)
	{
		if (AMultiDroneCharacter* ShadowDrone = PreviousDrone.Get())
		{
			if (!CurrentControlledDrones.Contains(PreviousDrone)) ShadowDrone->StopVerticalMove(false);
		}
	}
	VerticallyControlledDrones = MoveTemp(CurrentControlledDrones);
}

void ADroneOpsPlayerController::StopSelectedDroneVerticalControl(bool bSendFinalCommand)
{
	for (const TWeakObjectPtr<AMultiDroneCharacter>& ControlledDrone : VerticallyControlledDrones)
	{
		if (AMultiDroneCharacter* ShadowDrone = ControlledDrone.Get()) ShadowDrone->StopVerticalMove(bSendFinalCommand);
	}
	VerticallyControlledDrones.Empty();
}

void ADroneOpsPlayerController::OnPrimaryClick()
{
	// 编队旋转预览：左键落在旋转环附近则进入旋转拖拽，优先处理。
	// 用"锚点水平面 + 半径"判定命中，比点击细圆柱碰撞体可靠得多。
	if (bFormationRotateActive && IsValid(FormationGizmoActor))
	{
		if (IsCursorOnFormationRing())
		{
			HandleFormationRingPressed();
			return;
		}
	}

	// 路径编辑模式：地图点击加点 / gizmo 拖拽，优先于常规选择与派发
	if (bPathEditMode)
	{
		HandleEditModePressed();
		return;
	}

	// 面板处于交互状态时不处理游戏点击
	if (USequenceDispatchPanelWidget::IsPanelInteractive() || UGeographicTargetPanelWidget::IsPanelInteractive())
	{
		return;
	}

	// Shift remains the established box-select modifier. Add Ctrl for hostile-target placement so
	// the two interaction modes can coexist without silently changing box selection behaviour.
	if (bShiftHeld && (IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl)))
	{
		SpawnHostileTarget();
		return;
	}

	// Shift 按下：记录起始坐标，推迟到 OnPrimaryReleased 决定是框选还是短点击；
	// 此时不立即执行选中或派发，避免误触发。
	if (bShiftHeld)
	{
		// GetMousePositionOnViewport 返回 viewport-relative 逻辑像素，乘以 DPI = 物理像素，
		// 与 ProjectWorldLocationToScreen(true) 坐标系一致
		const float DPI = UWidgetLayoutLibrary::GetViewportScale(this);
		FVector2D LogicalPos = UWidgetLayoutLibrary::GetMousePositionOnViewport(this);
		BoxSelectStartScreen = LogicalPos * DPI;
		BoxSelectCurrentScreen = BoxSelectStartScreen;
		bIsBoxSelecting = true;
		NotifyBoxSelectShow();
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: OnPrimaryClick"));
	FVector WorldLocation = FVector::ZeroVector;

	// 左键可命中影子机或镜像机，但最终统一选中同 DroneId 的影子机。
	AActor* DroneActor = GetSelectableDroneUnderCursor(&WorldLocation);

	if (GEngine)
	{
		if (DroneActor)
		{
			const int32 Id = IDroneSelectableInterface::Execute_GetDroneId(DroneActor);
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green,
				FString::Printf(TEXT("DRONE FOUND: %s (ID=%d)"), *DroneActor->GetName(), Id));
		}
		else
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
				FString::Printf(TEXT("Map click at (%.0f, %.0f, %.0f) - no drone nearby"),
					WorldLocation.X, WorldLocation.Y, WorldLocation.Z));
		}
	}

	if (DroneActor)
	{
		HandleDroneClick(DroneActor);
		return;
	}

	// 自由视角旋转模式下（未按Shift、鼠标不可见），禁止派发目标点
	if (CameraModeState.CameraMode == EDroneCameraMode::Free && !bShiftHeld)
	{
		return;
	}

	if (WorldLocation.IsNearlyZero() && !GetWorldLocationUnderCursor(WorldLocation))
	{
		UE_LOG(LogTemp, Warning, TEXT("OnPrimaryClick: Ray miss - no hit under cursor"));
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("Ray miss - no hit under cursor"));
		}
		return;
	}

	HandleMapClick(WorldLocation);
}

void ADroneOpsPlayerController::OnPrimaryReleased()
{
	if (bDraggingFormationRing)
	{
		EndFormationRingDrag();
	}

	if (bPathEditMode)
	{
		bIsBoxSelecting = false;
		HandleEditModeReleased();
		return;
	}

	FinishBoxSelectIfPending();
}

void ADroneOpsPlayerController::FinishBoxSelectIfPending()
{
	if (!bIsBoxSelecting)
	{
		return;
	}

	bIsBoxSelecting = false; // 先清标志，防止 Tick 与本函数同帧重复处理
	bIsBoxSelectingBP = false;
	NotifyBoxSelectHide();   // 隐藏框选矩形

	if (USequenceDispatchPanelWidget::IsPanelInteractive())
	{
		return;
	}

	// 用拖拽期间持续记录的位置（不在释放后重新读，避免鼠标移动导致坐标偏差）
	const FVector2D EndScreen = BoxSelectCurrentScreen;
	const float Dist = FVector2D::Distance(BoxSelectStartScreen, EndScreen);

	if (Dist <= BoxSelectThresholdPx)
	{
		// 短点击：按单机切换规则处理；不派发地图目标点
		AActor* DroneActor = GetSelectableDroneUnderCursor();
		if (DroneActor)
		{
			HandleDroneClick(DroneActor);
		}
	}
	else
	{
		CommitBoxSelect(BoxSelectStartScreen, EndScreen);
	}
}

void ADroneOpsPlayerController::CommitBoxSelect(FVector2D StartScreen, FVector2D EndScreen)
{
	if (!DroneRegistry)
	{
		return;
	}

	// 获取视口尺寸（用于 debug 输出）
	int32 ViewportSizeX = 0, ViewportSizeY = 0;
	GetViewportSize(ViewportSizeX, ViewportSizeY);

	// 鼠标坐标已在 OnPrimaryClick/Tick 中转换为 viewport-relative 物理像素
	// （GetMousePositionOnViewport × DPI），直接规范化为左上/右下即可。
	const FVector2D BoxMin(FMath::Min(StartScreen.X, EndScreen.X), FMath::Min(StartScreen.Y, EndScreen.Y));
	const FVector2D BoxMax(FMath::Max(StartScreen.X, EndScreen.X), FMath::Max(StartScreen.Y, EndScreen.Y));

	const float DPIScale = UWidgetLayoutLibrary::GetViewportScale(this);
	UE_LOG(LogTemp, Log, TEXT("CommitBoxSelect: Viewport=(%d,%d) DPI=%.2f Box=(%.0f,%.0f)-(%.0f,%.0f)"),
		ViewportSizeX, ViewportSizeY, DPIScale, BoxMin.X, BoxMin.Y, BoxMax.X, BoxMax.Y);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Cyan,
			FString::Printf(TEXT("BoxSelect rect: (%.0f,%.0f)-(%.0f,%.0f) vp=(%d,%d) dpi=%.2f"),
				BoxMin.X, BoxMin.Y, BoxMax.X, BoxMax.Y, ViewportSizeX, ViewportSizeY, DPIScale));
	}

	// 在当前选中集合基础上切换框内无人机
	TArray<int32> NewSelection = DroneRegistry->GetMultiSelectedDrones();
	bool bAnyToggled = false;

	// 遍历所有影子机（离线与在线均参与框选）
	for (TActorIterator<AMultiDroneCharacter> It(GetWorld()); It; ++It)
	{
		AMultiDroneCharacter* ShadowDrone = *It;
		if (!IsValid(ShadowDrone) || ShadowDrone->DroneId <= 0)
		{
			continue;
		}

		// bPlayerViewportRelative=true：返回相对于游戏视口左上角的物理像素坐标，
		// 与 GetMousePositionOnViewport * DPI 的坐标系一致
		FVector2D ScreenPos;
		if (!ProjectWorldLocationToScreen(ShadowDrone->GetActorLocation(), ScreenPos, true))
		{
			UE_LOG(LogTemp, Log, TEXT("CommitBoxSelect: Drone %d project failed (behind camera?)"), ShadowDrone->DroneId);
			continue;
		}

		UE_LOG(LogTemp, Log, TEXT("CommitBoxSelect: Drone %d screen=(%.0f,%.0f)"),
			ShadowDrone->DroneId, ScreenPos.X, ScreenPos.Y);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Yellow,
				FString::Printf(TEXT("Drone %d screen=(%.0f,%.0f)"),
					ShadowDrone->DroneId, ScreenPos.X, ScreenPos.Y));
		}

		// 直接像素坐标边界判定
		if (ScreenPos.X < BoxMin.X || ScreenPos.X > BoxMax.X ||
			ScreenPos.Y < BoxMin.Y || ScreenPos.Y > BoxMax.Y)
		{
			continue;
		}

		// 切换选中状态
		if (NewSelection.Contains(ShadowDrone->DroneId))
		{
			NewSelection.Remove(ShadowDrone->DroneId);
		}
		else
		{
			NewSelection.AddUnique(ShadowDrone->DroneId);
		}
		bAnyToggled = true;
	}

	if (!bAnyToggled)
	{
		// 框内无可选影子机，保留原选中集合不变
		UE_LOG(LogTemp, Log, TEXT("CommitBoxSelect: no drones in box, selection unchanged"));
		return;
	}

	// 提交最终选中集合
	if (NewSelection.IsEmpty())
	{
		DroneRegistry->ClearSelection();
		SelectedDroneId = 0;
		SelectedDroneActor = nullptr;
	}
	else
	{
		// 优先保留当前主选中机；若不在新集合中则使用集合首项
		const int32 CurrentPrimary = DroneRegistry->GetPrimarySelectedDrone();
		const int32 NewPrimary = NewSelection.Contains(CurrentPrimary) ? CurrentPrimary : NewSelection[0];

		// 先设置主选中（会把 multi 临时清为 [NewPrimary]），再设置完整多选集合
		// （此时 NewPrimary 已在列表中，SetMultiSelectedDrones 不会再次调用 SetPrimarySelectedDrone，
		//  从而保留完整的多选集合。）
		DroneRegistry->SetPrimarySelectedDrone(NewPrimary);
		DroneRegistry->SetMultiSelectedDrones(NewSelection);

		SelectedDroneId = NewPrimary;
		SelectedDroneActor = FindShadowDroneById(GetWorld(), NewPrimary);
	}

	// 同步所有影子机的高亮显示
	for (TActorIterator<AMultiDroneCharacter> It(GetWorld()); It; ++It)
	{
		AMultiDroneCharacter* ShadowDrone = *It;
		if (!IsValid(ShadowDrone))
		{
			continue;
		}
		const bool bSelected = NewSelection.Contains(ShadowDrone->DroneId);
		SetDronePrimarySelectedState(ShadowDrone, bSelected);
	}

	UE_LOG(LogTemp, Log, TEXT("CommitBoxSelect: %d drone(s) selected, primary=%d"),
		NewSelection.Num(), SelectedDroneId);
}

void ADroneOpsPlayerController::OnShowInfo()
{
	// Debug: re-check hover state immediately when middle click is pressed
	AActor* HoveredActor = GetActorUnderCursor();
	if (!HoveredActor)
	{
		UE_LOG(LogTemp, Log, TEXT("[FR-03] Middle click - GetActorUnderCursor returned nullptr, cached HoveredDroneId=%d"), HoveredDroneId);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[FR-03] Middle click - Hit actor: %s, class: %s"), *HoveredActor->GetName(), *HoveredActor->GetClass()->GetName());

		bool bImplements = HoveredActor->Implements<UDroneSelectableInterface>();
		if (!bImplements)
		{
			UE_LOG(LogTemp, Log, TEXT("[FR-03] Actor doesn't implement UDroneSelectableInterface"));
		}
		else
		{
			int32 CurrentDroneId = IDroneSelectableInterface::Execute_GetDroneId(HoveredActor);
			UE_LOG(LogTemp, Log, TEXT("[FR-03] Got DroneId: %d from %s"), CurrentDroneId, *HoveredActor->GetName());
			if (CurrentDroneId > 0)
			{
				OpenDroneInfoPanel(CurrentDroneId);
				return;
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[FR-03] Middle click - no drone under cursor, cached HoveredDroneId=%d"), HoveredDroneId);
}

void ADroneOpsPlayerController::OpenDroneInfoPanel(int32 DroneId)
{
	if (!DroneRegistry || !DroneInfoPanelWidgetClass || DroneId <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[FR-03] OpenDroneInfoPanel: DroneRegistry=%p, WidgetClass=%p"),
			DroneRegistry.Get(), DroneInfoPanelWidgetClass.Get());
		return;
	}

	CloseCurrentDroneInfoPanel();

	UDroneInfoPanelWidget* NewPanel = CreateWidget<UDroneInfoPanelWidget>(this, DroneInfoPanelWidgetClass);
	if (!NewPanel)
	{
		UE_LOG(LogTemp, Error, TEXT("[FR-03] Failed to create info panel widget"));
		return;
	}

	NewPanel->AddToViewport();
	NewPanel->SetPositionInViewport(FVector2D(10, 10));
	CurrentDroneInfoPanel = NewPanel;
	CurrentDroneInfoDroneId = DroneId;
	NewPanel->OnPanelClosed.AddUObject(this, &ADroneOpsPlayerController::OnDroneInfoPanelClosed);

	FDroneDescriptor Descriptor;
	DroneRegistry->GetDroneDescriptor(DroneId, Descriptor);
	NewPanel->SetDroneContext(DroneId, Descriptor.VideoUrl);

	RefreshDroneInfoPanel();
	if (!IsValid(CurrentDroneInfoPanel) || CurrentDroneInfoDroneId != DroneId)
	{
		return;
	}
	GetWorldTimerManager().SetTimer(
		DroneInfoRefreshTimerHandle,
		this,
		&ADroneOpsPlayerController::RefreshDroneInfoPanel,
		FMath::Max(DroneInfoPanelRefreshIntervalSec, 0.05f),
		true);

	UE_LOG(LogTemp, Log, TEXT("[FR-03] Info panel opened successfully for DroneId=%d"), DroneId);
}

void ADroneOpsPlayerController::RefreshDroneInfoPanel()
{
	if (!DroneRegistry || CurrentDroneInfoDroneId <= 0 ||
		!IsValid(CurrentDroneInfoPanel) || !CurrentDroneInfoPanel->IsInViewport())
	{
		CloseCurrentDroneInfoPanel();
		return;
	}

	FDroneTelemetrySnapshot Snapshot;
	if (!DroneRegistry->GetTelemetry(CurrentDroneInfoDroneId, Snapshot))
	{
		// Do not manufacture zero values. The widget retains its last valid
		// snapshot and presents an explicit offline/no-data state instead.
		Snapshot.Availability = EDroneAvailability::Offline;
	}
	Snapshot.DroneId = CurrentDroneInfoDroneId;

	FString DroneName = FString::Printf(TEXT("Drone-%d"), CurrentDroneInfoDroneId);
	FDroneDescriptor Descriptor;
	if (DroneRegistry->GetDroneDescriptor(CurrentDroneInfoDroneId, Descriptor) && !Descriptor.Name.IsEmpty())
	{
		DroneName = Descriptor.Name;
	}

	FDroneTaskStateSnapshot TaskState;
	DroneRegistry->GetTaskState(CurrentDroneInfoDroneId, TaskState);
	CurrentDroneInfoPanel->UpdateFromSnapshot(Snapshot, DroneName, TaskState);
}

void ADroneOpsPlayerController::CloseCurrentDroneInfoPanel()
{
	GetWorldTimerManager().ClearTimer(DroneInfoRefreshTimerHandle);
	UDroneInfoPanelWidget* PanelToClose = CurrentDroneInfoPanel;
	CurrentDroneInfoPanel = nullptr;
	CurrentDroneInfoDroneId = 0;
	if (IsValid(PanelToClose))
	{
		PanelToClose->ShutdownPanel();
		if (PanelToClose->IsInViewport())
		{
			PanelToClose->RemoveFromParent();
		}
	}
}

void ADroneOpsPlayerController::OnDroneInfoPanelClosed()
{
	GetWorldTimerManager().ClearTimer(DroneInfoRefreshTimerHandle);
	CurrentDroneInfoPanel = nullptr;
	CurrentDroneInfoDroneId = 0;
}

UDroneVideoWindowManager* ADroneOpsPlayerController::EnsureVideoWindowManager()
{
	if (!VideoWindowManager)
	{
		VideoWindowManager = NewObject<UDroneVideoWindowManager>(this);
		VideoWindowManager->SetWindowContentClass(DroneVideoWindowWidgetClass);
		// Any close path funnels through the manager's CloseVideoWindow; forward that out to
		// Blueprint so the video-stream row can reset its checkbox.
		VideoWindowManager->OnWindowClosed.AddUObject(this, &ADroneOpsPlayerController::HandleVideoWindowClosed);
	}
	return VideoWindowManager;
}

void ADroneOpsPlayerController::HandleVideoWindowClosed(int32 DroneId)
{
	OnDroneVideoWindowClosed.Broadcast(DroneId);
}

bool ADroneOpsPlayerController::OpenDroneVideoWindow(int32 DroneId)
{
	if (DroneId <= 0)
	{
		return false;
	}
	if (!DroneVideoWindowWidgetClass)
	{
		UUIManagerBlueprintLibrary::ShowToast(this, TEXT("视频窗口控件未配置"), 3.0f);
		return false;
	}

	// 严格本地预演：只读缓存注册表，不触碰 DroneBackend。
	FString DroneName = FString::Printf(TEXT("Drone-%d"), DroneId);
	FString VideoUrl;
	if (DroneRegistry)
	{
		FDroneDescriptor Descriptor;
		if (DroneRegistry->GetDroneDescriptor(DroneId, Descriptor))
		{
			if (!Descriptor.Name.IsEmpty()) DroneName = Descriptor.Name;
			VideoUrl = Descriptor.VideoUrl;
		}
	}

	FString Error;
	const bool bOpen = EnsureVideoWindowManager()->OpenVideoWindow(DroneId, DroneName, VideoUrl, Error);
	if (!bOpen && !Error.IsEmpty())
	{
		UUIManagerBlueprintLibrary::ShowToast(this, Error, 3.0f);
	}
	return bOpen;
}

void ADroneOpsPlayerController::CloseDroneVideoWindow(int32 DroneId)
{
	if (VideoWindowManager)
	{
		VideoWindowManager->CloseVideoWindow(DroneId);
	}
}

void ADroneOpsPlayerController::CloseAllDroneVideoWindows()
{
	if (VideoWindowManager)
	{
		VideoWindowManager->CloseAllVideoWindows();
	}
}

bool ADroneOpsPlayerController::HasDroneVideoWindow(int32 DroneId) const
{
	return VideoWindowManager && VideoWindowManager->HasWindow(DroneId);
}

void ADroneOpsPlayerController::OnFreeCamToggle()
{
	// 编辑模式下禁用 F：编辑已切到专用自由相机，再切会打乱编辑视角与状态。
	if (bPathEditMode)
	{
		return;
	}

	if (CameraModeState.CameraMode == EDroneCameraMode::Follow)
	{
		// 保存当前跟随目标
		if (SelectedDroneId > 0)
			CameraModeState.FollowDroneId = SelectedDroneId;
		else if (DroneRegistry)
			CameraModeState.FollowDroneId = DroneRegistry->GetPrimarySelectedDrone();

		// 将 FreeCamActor 移动到当前相机位置
		if (FreeCamActor && IsValid(FreeCamActor) && PlayerCameraManager)
		{
			const FVector CamLoc = PlayerCameraManager->GetCameraLocation();
			const FRotator CamRot = PlayerCameraManager->GetCameraRotation();
			FreeCamActor->SetActorLocationAndRotation(CamLoc, CamRot);
			FreeCamRotation = CamRot;
		}

		CameraModeState.CameraMode = EDroneCameraMode::Free;
		SetViewTargetWithBlend(FreeCamActor, 0.0f);

		// 禁用 Pawn 输入，防止 WASD 继续控制无人机
		if (APawn* ControlledPawn = GetPawn())
		{
			ControlledPawn->DisableInput(this);
		}

		// 隐藏光标，捕获鼠标用于视角旋转
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());

		UE_LOG(LogTemp, Log, TEXT("FR-04: Switched to Free Camera (FollowDroneId=%d)"), CameraModeState.FollowDroneId);
	}
	else
	{
		CameraModeState.CameraMode = EDroneCameraMode::Follow;

		// 恢复 Pawn 输入
		if (APawn* ControlledPawn = GetPawn())
		{
			ControlledPawn->EnableInput(this);
		}

		// 恢复鼠标光标
		bShowMouseCursor = true;
		SetInputMode(FInputModeGameAndUI());

		// 优先恢复用户上次手动选择的无人机；若从未手动选择则 fallback
		const int32 RestoreId = (LastFollowedDroneId > 0) ? LastFollowedDroneId : CameraModeState.FollowDroneId;
		AActor* FollowTarget = ResolveFollowViewTargetByDroneId(RestoreId);
		if (!IsValid(FollowTarget) && IsValid(LastFollowViewTarget))
		{
			FollowTarget = LastFollowViewTarget.Get();
		}
		if (!IsValid(FollowTarget) && IsValid(CachedRealTimeDrone))
		{
			FollowTarget = CachedRealTimeDrone;
		}
		if (IsValid(FollowTarget))
		{
			SetViewTargetWithBlend(FollowTarget, 0.35f);
		}

		UE_LOG(LogTemp, Log, TEXT("FR-04: Switched back to Follow Camera (LastFollowedDroneId=%d, RestoreId=%d)"), LastFollowedDroneId, RestoreId);
	}
}

void ADroneOpsPlayerController::OnShiftPressed()
{
	bShiftHeld = true;

	if (CameraModeState.CameraMode == EDroneCameraMode::Free)
	{
		bShowMouseCursor = true;
	}

	// 无论何种相机模式，Shift 按住期间左键按下（框选拖拽）不得触发鼠标捕获隐藏光标
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
}

void ADroneOpsPlayerController::OnShiftReleased()
{
	bShiftHeld = false;

	if (CameraModeState.CameraMode == EDroneCameraMode::Free)
	{
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());
	}
	else
	{
		// 恢复默认的 GameAndUI 输入模式（允许鼠标捕获时隐藏光标）
		SetInputMode(FInputModeGameAndUI());
	}
}

void ADroneOpsPlayerController::HandleMapClick(const FVector& WorldLocation)
{
	TArray<FGeographicDispatchSlot> Slots;
	FGeographicDispatchResult Result = BuildWorldDispatchPlan(WorldLocation, Slots);
	if (!Result.bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleMapClick: %s"), *Result.Message);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange, Result.Message);
		}
		return;
	}

	// A map hit represents only the requested horizontal location. Its Z is usually the
	// terrain height, so do not let it alter a drone's current flight altitude. Each selected
	// drone keeps its own height; this is deliberately different from the geographic panel,
	// whose input altitude is converted to World Z in BuildGeographicDispatchPlan.
	for (FGeographicDispatchSlot& Slot : Slots)
	{
		if (const AActor* DroneActor = ResolveDroneActorById(Slot.DroneId))
		{
			Slot.WorldTarget.Z = DroneActor->GetActorLocation().Z;
		}
	}

	Result = ExecuteWorldDispatchPlan(Slots);
	UE_LOG(LogTemp, Log, TEXT("HandleMapClick: %s -> clicked=(%.0f, %.0f, %.0f), preserving each drone altitude"),
		*Result.Message, WorldLocation.X, WorldLocation.Y, WorldLocation.Z);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, Result.bSuccess ? FColor::Green : FColor::Orange, Result.Message);
	}
	DrawDebugSphere(GetWorld(), WorldLocation, 50.0f, 12, FColor::Green, false, 3.0f);
}

int32 ADroneOpsPlayerController::GetSelectedDroneCountForDispatch() const
{
	return GetSelectedDroneIdsForDispatch().Num();
}

FGeographicDispatchResult ADroneOpsPlayerController::BuildWorldDispatchPlan(
	const FVector& BaseWorldTarget,
	TArray<FGeographicDispatchSlot>& OutSlots) const
{
	FGeographicDispatchResult Result;
	OutSlots.Reset();

	if (!DroneRegistry)
	{
		Result.Message = TEXT("无人机注册表不可用");
		return Result;
	}

	TArray<int32> SelectedIds = GetSelectedDroneIdsForDispatch();

	int32 PrimaryId = DroneRegistry->GetPrimarySelectedDrone();
	if (SelectedIds.IsEmpty())
	{
		Result.Message = TEXT("请先选择无人机");
		return Result;
	}
	if (!SelectedIds.Contains(PrimaryId))
	{
		SelectedIds.Sort();
		PrimaryId = SelectedIds[0];
	}

	TArray<int32> OrderedIds;
	OrderedIds.Add(PrimaryId);
	TArray<int32> Others = SelectedIds;
	Others.Remove(PrimaryId);
	Others.Sort();
	OrderedIds.Append(Others);

	constexpr float SpacingCm = 100.0f;
	for (int32 Index = 0; Index < OrderedIds.Num(); ++Index)
	{
		const int32 DroneId = OrderedIds[Index];
		if (!ValidateDispatchDrone(DroneId, Result.Message))
		{
			return Result;
		}

		FGeographicDispatchSlot Slot;
		Slot.DroneId = DroneId;
		Slot.bIsPrimary = Index == 0;
		Slot.WorldTarget = BaseWorldTarget + ComputeMultiDispatchOffset(Index, SpacingCm);
		OutSlots.Add(Slot);
	}

	Result.bSuccess = true;
	Result.DispatchedCount = OutSlots.Num();
	return Result;
}

FGeographicDispatchResult ADroneOpsPlayerController::ExecuteWorldDispatchPlan(
	const TArray<FGeographicDispatchSlot>& Slots)
{
	FGeographicDispatchResult Result;
	if (!DroneRegistry || Slots.IsEmpty())
	{
		Result.Message = TEXT("没有可执行的目标点");
		return Result;
	}
	// A committed dispatch supersedes any older coordinate preview, regardless of input method.
	ActiveGeographicPreviewSlots.Reset();

	UDroneNetworkManager* NetworkManager = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDroneNetworkManager>()
		: nullptr;
	const bool bBackendConnected = NetworkManager && NetworkManager->CanSendToBackend()
		&& NetworkManager->GetWebSocketClient()
		&& NetworkManager->GetWebSocketClient()->IsConnected();

	int32 LocalMoveCount = 0;
	int32 BackendSendCount = 0;
	int32 MissingAnchorCount = 0;
	for (const FGeographicDispatchSlot& Slot : Slots)
	{
		if (AMultiDroneCharacter* Shadow = Cast<AMultiDroneCharacter>(ResolveDroneActorById(Slot.DroneId)))
		{
			// Both map-click and geographic input use this 3D local movement path.
			Shadow->MoveToTarget3D(Slot.WorldTarget);
			++LocalMoveCount;
		}

		if (!bBackendConnected)
		{
			continue;
		}

		const ARealTimeDroneReceiver* Receiver = Cast<ARealTimeDroneReceiver>(
			DroneRegistry->GetReceiverActor(Slot.DroneId));
		if (!Receiver || !Receiver->bHasGpsAnchor)
		{
			++MissingAnchorCount;
			continue;
		}

		const FVector SendLocation = Slot.WorldTarget - Receiver->AnchorWorldLocation;
		NetworkManager->SendMoveCommand(
			Slot.DroneId, SendLocation, DroneRegistry->GetDroneCommandMode(Slot.DroneId));
		++BackendSendCount;
	}

	Result.bSuccess = LocalMoveCount > 0;
	Result.DispatchedCount = LocalMoveCount;
	if (!Result.bSuccess)
	{
		Result.Message = TEXT("影子机不可用，未执行派发");
		return Result;
	}
	if (!bBackendConnected)
	{
		Result.Message = FString::Printf(TEXT("已在UE内派发 %d 架；后端未连接，暂未发送指令"), LocalMoveCount);
	}
	else if (MissingAnchorCount > 0)
	{
		Result.Message = FString::Printf(TEXT("已在UE内派发 %d 架；已发送后端 %d 架，%d 架等待GPS锚点"),
			LocalMoveCount, BackendSendCount, MissingAnchorCount);
	}
	else
	{
		Result.Message = FString::Printf(TEXT("已派发 %d 架，并已发送后端"), LocalMoveCount);
	}
	return Result;
}

bool ADroneOpsPlayerController::TryBuildGeographicBackendTarget(
	int32 DroneId,
	const FGeographicCoordinate3D& TargetCoordinate,
	const FVector& AdditionalLocalUeOffset,
	const UDroneNetworkManager* NetworkManager,
	FVector& OutBackendRelativeTarget,
	FString& OutError) const
{
	OutBackendRelativeTarget = FVector::ZeroVector;
	if (!DroneRegistry || !NetworkManager)
	{
		OutError = TEXT("后端连接未就绪");
		return false;
	}

	const ARealTimeDroneReceiver* Receiver = Cast<ARealTimeDroneReceiver>(
		DroneRegistry->GetReceiverActor(DroneId));
	if (!Receiver || !Receiver->bHasGpsAnchor)
	{
		OutError = FString::Printf(TEXT("无人机 %d 尚未获得GPS锚点，请等待上电信息"), DroneId);
		return false;
	}

	double AnchorLatitude = 0.0;
	double AnchorLongitude = 0.0;
	double AnchorAltitudeMslMeters = 0.0;
	if (!NetworkManager->GetCachedGpsAnchor(
		DroneId, AnchorLatitude, AnchorLongitude, AnchorAltitudeMslMeters)
		|| !FMath::IsFinite(AnchorLatitude)
		|| AnchorLatitude < -90.0 || AnchorLatitude > 90.0
		|| !FMath::IsFinite(AnchorLongitude)
		|| AnchorLongitude < -180.0 || AnchorLongitude > 180.0
		|| !FMath::IsFinite(AnchorAltitudeMslMeters))
	{
		OutError = FString::Printf(TEXT("无人机 %d 的GPS锚点无效"), DroneId);
		return false;
	}

	FDroneTelemetrySnapshot Telemetry;
	if (!DroneRegistry->GetTelemetry(DroneId, Telemetry)
		|| Telemetry.Availability != EDroneAvailability::Online)
	{
		OutError = FString::Printf(TEXT("无人机 %d 尚无可用遥测"), DroneId);
		return false;
	}
	if (!Telemetry.bLocalPositionValid)
	{
		OutError = FString::Printf(TEXT("无人机 %d 本地NED位置尚未就绪"), DroneId);
		return false;
	}
	if (!Telemetry.bGpsFix
		|| !FMath::IsFinite(Telemetry.GpsLatitude)
		|| Telemetry.GpsLatitude < -90.0 || Telemetry.GpsLatitude > 90.0
		|| !FMath::IsFinite(Telemetry.GpsLongitude)
		|| Telemetry.GpsLongitude < -180.0 || Telemetry.GpsLongitude > 180.0
		|| !FMath::IsFinite(Telemetry.GpsAltitude))
	{
		OutError = FString::Printf(TEXT("无人机 %d 当前GPS数据无效"), DroneId);
		return false;
	}

	constexpr double MaxTelemetryAgeSeconds = 3.0;
	const double TelemetryAgeSeconds = FPlatformTime::Seconds() - Telemetry.LastUpdateTime;
	if (Telemetry.LastUpdateTime <= 0.0
		|| !FMath::IsFinite(TelemetryAgeSeconds)
		|| TelemetryAgeSeconds < 0.0
		|| TelemetryAgeSeconds > MaxTelemetryAgeSeconds)
	{
		OutError = FString::Printf(TEXT("无人机 %d 遥测已过期，请等待数据恢复"), DroneId);
		return false;
	}

	FGeographicCoordinate3D CurrentCoordinate;
	CurrentCoordinate.Longitude = Telemetry.GpsLongitude;
	CurrentCoordinate.Latitude = Telemetry.GpsLatitude;
	CurrentCoordinate.AltitudeMslMeters = Telemetry.GpsAltitude;
	const FVector CurrentLocalUeOffset(
		Telemetry.NedLocation.X * 100.0,
		Telemetry.NedLocation.Y * 100.0,
		-Telemetry.NedLocation.Z * 100.0);
	if (!FGeographicDispatchOffsetCalculator::CalculateBackendRelativeOffset(
		TargetCoordinate,
		CurrentCoordinate,
		CurrentLocalUeOffset,
		AdditionalLocalUeOffset,
		OutBackendRelativeTarget))
	{
		OutError = FString::Printf(TEXT("无人机 %d 的目标偏移计算失败"), DroneId);
		return false;
	}

	// Match Jetson's existing default absolute local-NED guard. Reject here so
	// the UI cannot report success for a command the flight endpoint will drop.
	constexpr double MaxAbsoluteLocalTargetCm = 5000.0 * 100.0;
	if (FMath::Abs(OutBackendRelativeTarget.X) > MaxAbsoluteLocalTargetCm
		|| FMath::Abs(OutBackendRelativeTarget.Y) > MaxAbsoluteLocalTargetCm
		|| FMath::Abs(OutBackendRelativeTarget.Z) > MaxAbsoluteLocalTargetCm)
	{
		OutBackendRelativeTarget = FVector::ZeroVector;
		OutError = FString::Printf(TEXT("无人机 %d 的目标超出本地NED范围（5000 m）"), DroneId);
		return false;
	}
	return true;
}

bool ADroneOpsPlayerController::TryConvertGeographicToWorld(
	EGeographicCoordinateSystem CoordinateSystem,
	double Longitude,
	double Latitude,
	double AltitudeMslMeters,
	FVector& OutWorld,
	FString& OutError) const
{
	if (!DroneRegistry)
	{
		OutError = TEXT("无人机注册表不可用");
		return false;
	}

	if (CoordinateSystem != EGeographicCoordinateSystem::WGS84)
	{
		OutError = TEXT("当前仅支持 WGS84 坐标系");
		return false;
	}

	if (!FMath::IsFinite(Longitude) || !FMath::IsFinite(Latitude) || !FMath::IsFinite(AltitudeMslMeters))
	{
		OutError = TEXT("请输入有效的经度、纬度和海拔");
		return false;
	}

	if (Longitude < -180.0 || Longitude > 180.0 || Latitude < -90.0 || Latitude > 90.0)
	{
		OutError = TEXT("经纬度超出合法范围");
		return false;
	}

	// Coordinate service must be ready and support geographic conversion.
	TScriptInterface<ICoordinateService> CoordService = DroneRegistry->GetCoordinateService();
	UObject* CoordObject = CoordService.GetObject();
	if (!CoordObject
		|| !ICoordinateService::Execute_IsCoordinateSystemReady(CoordObject)
		|| !ICoordinateService::Execute_IsGeographicSupported(CoordObject))
	{
		OutError = TEXT("坐标服务未就绪");
		return false;
	}

	// Cesium consumes WGS84 ellipsoid height while the UI and PX4 anchor altitude are AMSL.
	// Use the same configured geoid separation for both target and power-on anchor conversion.
	UDroneNetworkManager* NetworkManager = nullptr;
	if (UGameInstance* GI = GetGameInstance())
	{
		NetworkManager = GI->GetSubsystem<UDroneNetworkManager>();
	}
	const double EllipsoidAltMeters = NetworkManager
		? NetworkManager->ConvertMslToWgs84EllipsoidHeight(AltitudeMslMeters)
		: AltitudeMslMeters;

	const FVector Converted = ICoordinateService::Execute_GeographicToWorld(
		CoordObject, Latitude, Longitude, EllipsoidAltMeters);
	if (!FMath::IsFinite(Converted.X) || !FMath::IsFinite(Converted.Y) || !FMath::IsFinite(Converted.Z))
	{
		OutError = TEXT("坐标转换失败");
		return false;
	}

	OutWorld = Converted;
	return true;
}

FGeographicDispatchResult ADroneOpsPlayerController::AddGeographicWaypointInEditMode(
	EGeographicCoordinateSystem CoordinateSystem,
	double Longitude,
	double Latitude,
	double AltitudeMslMeters)
{
	FGeographicDispatchResult Result;

	if (!bPathEditMode || EditingPaths.IsEmpty())
	{
		Result.Message = TEXT("当前不在编辑模式，无法加航点");
		return Result;
	}

	FVector WorldTarget = FVector::ZeroVector;
	FString ConvertError;
	if (!TryConvertGeographicToWorld(CoordinateSystem, Longitude, Latitude, AltitudeMslMeters, WorldTarget, ConvertError))
	{
		Result.Message = ConvertError;
		return Result;
	}

	// 与地图点击加点完全一致：以坐标点为编队参考落点，给所有在编路径叠加相同偏移。
	AddWaypointToAllEditingPaths(WorldTarget);

	Result.bSuccess = true;
	Result.DispatchedCount = EditingPaths.Num();
	Result.Message = FString::Printf(TEXT("已在 %d 条路径上添加航点"), EditingPaths.Num());
	return Result;
}

FGeographicDispatchResult ADroneOpsPlayerController::BuildGeographicDispatchPlan(
	EGeographicCoordinateSystem CoordinateSystem,
	double Longitude,
	double Latitude,
	double AltitudeMslMeters,
	bool bForDispatch,
	TArray<FGeographicDispatchSlot>& OutSlots) const
{
	FGeographicDispatchResult Result;
	OutSlots.Reset();

	if (!DroneRegistry)
	{
		Result.Message = TEXT("无人机注册表不可用");
		return Result;
	}

	if (CoordinateSystem != EGeographicCoordinateSystem::WGS84)
	{
		Result.Message = TEXT("当前仅支持 WGS84 坐标系");
		return Result;
	}

	if (!FMath::IsFinite(Longitude) || !FMath::IsFinite(Latitude) || !FMath::IsFinite(AltitudeMslMeters))
	{
		Result.Message = TEXT("请输入有效的经度、纬度和海拔");
		return Result;
	}

	if (Longitude < -180.0 || Longitude > 180.0 || Latitude < -90.0 || Latitude > 90.0)
	{
		Result.Message = TEXT("经纬度超出合法范围");
		return Result;
	}

	// Build a unique ordered selection: primary first, all remaining drones by ascending DroneId.
	TArray<int32> SelectedIds = DroneRegistry->GetMultiSelectedDrones();
	TSet<int32> SeenIds;
	SelectedIds.RemoveAll([&SeenIds](const int32 DroneId)
	{
		if (DroneId <= 0 || SeenIds.Contains(DroneId))
		{
			return true;
		}
		SeenIds.Add(DroneId);
		return false;
	});

	int32 PrimaryId = DroneRegistry->GetPrimarySelectedDrone();
	if (SelectedIds.IsEmpty())
	{
		if (PrimaryId > 0)
		{
			SelectedIds.Add(PrimaryId);
		}
	}

	if (SelectedIds.IsEmpty())
	{
		Result.Message = TEXT("请先选择无人机");
		return Result;
	}

	if (!SelectedIds.Contains(PrimaryId))
	{
		// Registry selection should always contain its primary. Keep a deterministic fallback for
		// direct Blueprint manipulation or a transient selection update in the same frame.
		SelectedIds.Sort();
		PrimaryId = SelectedIds[0];
	}

	TArray<int32> OrderedIds;
	OrderedIds.Add(PrimaryId);
	{
		TArray<int32> Others = SelectedIds;
		Others.Remove(PrimaryId);
		Others.Sort(); // ascending DroneId → stable spiral layout
		OrderedIds.Append(Others);
	}

	// Coordinate service must be ready and support geographic conversion.
	TScriptInterface<ICoordinateService> CoordService = DroneRegistry->GetCoordinateService();
	UObject* CoordObject = CoordService.GetObject();
	if (!CoordObject
		|| !ICoordinateService::Execute_IsCoordinateSystemReady(CoordObject)
		|| !ICoordinateService::Execute_IsGeographicSupported(CoordObject))
	{
		Result.Message = TEXT("坐标服务未就绪");
		return Result;
	}

	// Cesium consumes WGS84 ellipsoid height while the UI and PX4 anchor altitude are AMSL.
	// Use the same configured geoid separation for both target and power-on anchor conversion.
	UDroneNetworkManager* NetworkManager = nullptr;
	if (UGameInstance* GI = GetGameInstance())
	{
		NetworkManager = GI->GetSubsystem<UDroneNetworkManager>();
	}
	const double EllipsoidAltMeters = NetworkManager
		? NetworkManager->ConvertMslToWgs84EllipsoidHeight(AltitudeMslMeters)
		: AltitudeMslMeters;

	const FVector BaseTarget = ICoordinateService::Execute_GeographicToWorld(
		CoordObject, Latitude, Longitude, EllipsoidAltMeters);
	if (!FMath::IsFinite(BaseTarget.X) || !FMath::IsFinite(BaseTarget.Y) || !FMath::IsFinite(BaseTarget.Z))
	{
		Result.Message = TEXT("坐标转换失败");
		return Result;
	}
	FGeographicCoordinate3D GeographicTarget;
	GeographicTarget.Longitude = Longitude;
	GeographicTarget.Latitude = Latitude;
	GeographicTarget.AltitudeMslMeters = AltitudeMslMeters;

	// Dispatch is atomic from the UI's perspective: validate every selected drone before sending
	// the first command. Never silently skip a locked or unanchored secondary drone because doing so
	// would change the stable DroneId-to-spiral-slot mapping.
	if (bForDispatch)
	{
		if (!NetworkManager || !NetworkManager->CanSendToBackend()
			|| !NetworkManager->GetWebSocketClient()
			|| !NetworkManager->GetWebSocketClient()->IsConnected())
		{
			Result.Message = TEXT("后端连接未就绪");
			return Result;
		}

		for (const int32 DroneId : OrderedIds)
		{
			EDroneControlLockReason LockReason = EDroneControlLockReason::None;
			if (DroneRegistry->IsControlLocked(DroneId, LockReason))
			{
				Result.Message = FString::Printf(TEXT("无人机 %d 当前不可控，无法派发"), DroneId);
				return Result;
			}

			const ARealTimeDroneReceiver* Receiver = Cast<ARealTimeDroneReceiver>(
				DroneRegistry->GetReceiverActor(DroneId));
			if (!Receiver || !Receiver->bHasGpsAnchor)
			{
				Result.Message = FString::Printf(TEXT("无人机 %d 尚未获得GPS锚点，请等待上电信息"), DroneId);
				return Result;
			}
		}
	}

	constexpr float SpacingCm = 100.0f; // 1 m square spiral
	int32 SlotIndex = 0;
	for (int32 i = 0; i < OrderedIds.Num(); ++i)
	{
		const int32 Id = OrderedIds[i];
		const FVector FormationOffset = ComputeMultiDispatchOffset(SlotIndex, SpacingCm);
		FGeographicDispatchSlot Slot;
		Slot.DroneId = Id;
		Slot.bIsPrimary = (i == 0);
		Slot.WorldTarget = BaseTarget + FormationOffset;
		if (bForDispatch)
		{
			if (!TryBuildGeographicBackendTarget(
				Id,
				GeographicTarget,
				FormationOffset,
				NetworkManager,
				Slot.BackendRelativeTarget,
				Result.Message))
			{
				OutSlots.Reset();
				return Result;
			}
			Slot.bHasBackendRelativeTarget = true;
		}
		OutSlots.Add(Slot);
		++SlotIndex;
	}

	Result.bSuccess = true;
	Result.DispatchedCount = OutSlots.Num();
	Result.Message = bForDispatch ? TEXT("可以派发") : TEXT("可以预览");
	return Result;
}

FGeographicDispatchResult ADroneOpsPlayerController::ValidateGeographicTarget(
	EGeographicCoordinateSystem CoordinateSystem,
	double Longitude,
	double Latitude,
	double AltitudeMslMeters,
	bool bForDispatch) const
{
	TArray<FGeographicDispatchSlot> Slots;
	return BuildGeographicDispatchPlan(
		CoordinateSystem, Longitude, Latitude, AltitudeMslMeters, bForDispatch, Slots);
}

FGeographicDispatchResult ADroneOpsPlayerController::DispatchGeographicTarget(
	EGeographicCoordinateSystem CoordinateSystem,
	double Longitude,
	double Latitude,
	double AltitudeMslMeters,
	bool bPreviewOnly)
{
	TArray<FGeographicDispatchSlot> Slots;
	FGeographicDispatchResult Result = BuildGeographicDispatchPlan(
		CoordinateSystem, Longitude, Latitude, AltitudeMslMeters, !bPreviewOnly, Slots);
	if (!Result.bSuccess)
	{
		return Result;
	}

	// Preview: draw markers only, send nothing.
	if (bPreviewOnly)
	{
		ActiveGeographicPreviewSlots = Slots;
		Result.Message = FString::Printf(TEXT("已预览 %d 个目标点"), Slots.Num());
		return Result;
	}

	UDroneNetworkManager* NetworkManager = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDroneNetworkManager>()
		: nullptr;

	// All prerequisites were checked for the complete selection above. Send exactly one frontend
	// command per drone; the backend heartbeat owns reliable resend and queue consumption.
	for (const FGeographicDispatchSlot& Slot : Slots)
	{
		check(Slot.bHasBackendRelativeTarget);
		const FVector SendLocation = Slot.BackendRelativeTarget;
		const EDroneCommandMode CommandMode = DroneRegistry->GetDroneCommandMode(Slot.DroneId);
		NetworkManager->SendMoveCommand(Slot.DroneId, SendLocation, CommandMode);

		// Local visual: move the shadow drone toward the full 3D world target (honouring Z).
		if (AActor* DroneActor = ResolveDroneActorById(Slot.DroneId))
		{
			if (AMultiDroneCharacter* Shadow = Cast<AMultiDroneCharacter>(DroneActor))
			{
				Shadow->MoveToTarget3D(Slot.WorldTarget);
			}
		}
	}

	const double GeoidSeparationMeters = NetworkManager ? NetworkManager->GeoidSeparationMeters : 0.0;
	const FVector BaseTarget = Slots[0].WorldTarget;
	UE_LOG(LogTemp, Log,
		TEXT("DispatchGeographicTarget: lon=%.7f lat=%.7f mslAlt=%.2f geoid=%.2f -> world=(%.1f, %.1f, %.1f), %d drones"),
		Longitude, Latitude, AltitudeMslMeters, GeoidSeparationMeters,
		BaseTarget.X, BaseTarget.Y, BaseTarget.Z, Slots.Num());

	Result.Message = FString::Printf(TEXT("已派发 %d 架无人机"), Slots.Num());
	return Result;
}

void ADroneOpsPlayerController::HandleDroneClick(AActor* ClickedActor)
{
	if (!ClickedActor || !IsFr2ControllableDroneActor(ClickedActor))
	{
		return;
	}

	if (!DroneRegistry)
	{
		return;
	}

	const int32 ClickedDroneId = ResolveDroneIdFromActor(ClickedActor);
	if (ClickedDroneId <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleDroneClick: invalid DroneId for %s"), *ClickedActor->GetName());
		return;
	}

	TArray<int32> CurrentMulti = DroneRegistry->GetMultiSelectedDrones();

	if (CurrentMulti.Contains(ClickedDroneId))
	{
		// 已选中 → 取消选中
		CurrentMulti.Remove(ClickedDroneId);
		SetDronePrimarySelectedState(ClickedActor, false);

		if (CurrentMulti.IsEmpty())
		{
			// 集合清空
			DroneRegistry->ClearSelection();
			SelectedDroneId = 0;
			SelectedDroneActor = nullptr;
			UE_LOG(LogTemp, Log, TEXT("HandleDroneClick: deselected drone %d, selection empty"), ClickedDroneId);
		}
		else
		{
			// 若取消的是主选中机，把集合首项设为新主选中机
			const int32 CurrentPrimary = DroneRegistry->GetPrimarySelectedDrone();
			const int32 NewPrimary = (CurrentPrimary == ClickedDroneId) ? CurrentMulti[0] : CurrentPrimary;
			// 先设主选中（primary 仍在旧 multi 中，不会触发 multi 清空），再更新 multi
			DroneRegistry->SetPrimarySelectedDrone(NewPrimary);
			DroneRegistry->SetMultiSelectedDrones(CurrentMulti);
			SelectedDroneId = NewPrimary;
			SelectedDroneActor = FindShadowDroneById(GetWorld(), NewPrimary);
			UE_LOG(LogTemp, Log, TEXT("HandleDroneClick: deselected drone %d, new primary=%d, remaining=%d"),
				ClickedDroneId, NewPrimary, CurrentMulti.Num());
		}
	}
	else
	{
		// 未选中 → 加入选中集合（离线/失联无人机同样允许选中）
		CurrentMulti.AddUnique(ClickedDroneId);
		SetDronePrimarySelectedState(ClickedActor, true);

		// 新加入的无人机成为主选中机；先设主选中再设完整 multi，避免 multi 被清空
		DroneRegistry->SetPrimarySelectedDrone(ClickedDroneId);
		DroneRegistry->SetMultiSelectedDrones(CurrentMulti);
		SelectedDroneId = ClickedDroneId;
		SelectedDroneActor = ClickedActor;

		// 跟随视角切换到刚点击的无人机
		if (CameraModeState.CameraMode == EDroneCameraMode::Follow)
		{
			LastFollowViewTarget = ClickedActor;
			CameraModeState.FollowDroneId = ClickedDroneId;
			LastFollowedDroneId = ClickedDroneId;
			CameraModeState.LastFollowLocation = ClickedActor->GetActorLocation();
			CameraModeState.LastFollowRotation = ClickedActor->GetActorRotation();
			SetViewTargetWithBlend(ClickedActor, 0.35f);
		}

		FDroneDescriptor Desc;
		FString DisplayName = FString::Printf(TEXT("Drone-%d"), ClickedDroneId);
		if (DroneRegistry->GetDroneDescriptor(ClickedDroneId, Desc))
		{
			DisplayName = Desc.Name;
		}
		UE_LOG(LogTemp, Log, TEXT("HandleDroneClick: selected %s (ID=%d), total=%d"),
			*DisplayName, ClickedDroneId, CurrentMulti.Num());
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow,
			FString::Printf(TEXT("选中: %d 架  主选中: %d"), DroneRegistry->GetMultiSelectedDrones().Num(), SelectedDroneId));
	}
}

void ADroneOpsPlayerController::OnRegistryMultiSelectionChanged()
{
	if (!DroneRegistry)
	{
		return;
	}

	const TArray<int32> CurrentMulti = DroneRegistry->GetMultiSelectedDrones();
	const int32 PrimaryId = DroneRegistry->GetPrimarySelectedDrone();

	// 同步 PlayerController 本地选中状态，使指令派发、跟随相机等系统感知到面板选中的变化
	SelectedDroneId = PrimaryId;
	SelectedDroneActor = (PrimaryId > 0) ? FindShadowDroneById(GetWorld(), PrimaryId) : nullptr;

	// 遍历场景所有影子机，更新高亮显示
	for (TActorIterator<AMultiDroneCharacter> It(GetWorld()); It; ++It)
	{
		AMultiDroneCharacter* ShadowDrone = *It;
		if (IsValid(ShadowDrone))
		{
			SetDronePrimarySelectedState(ShadowDrone, CurrentMulti.Contains(ShadowDrone->DroneId));
		}
	}
}

void ADroneOpsPlayerController::SendTargetCommand(int32 DroneId, const FVector& TargetWorldLocation)
{
	if (!DroneRegistry)
	{
		return;
	}

	// WebSocket path: send move command via DroneNetworkManager
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UDroneNetworkManager* NetworkManager = GI->GetSubsystem<UDroneNetworkManager>())
		{
			if (NetworkManager->GetWebSocketClient() && NetworkManager->GetWebSocketClient()->IsConnected())
			{
				const EDroneCommandMode CommandMode = DroneRegistry->GetDroneCommandMode(DroneId);

				// 协议要求发送相对 GPS 锚点的 UE 偏移量（厘米）
				// 锚点由 power_on 事件建立，存储在对应的镜像机 ARealTimeDroneReceiver 上
				FVector SendLocation = TargetWorldLocation;
				if (AActor* ReceiverActor = DroneRegistry->GetReceiverActor(DroneId))
				{
					if (ARealTimeDroneReceiver* Receiver = Cast<ARealTimeDroneReceiver>(ReceiverActor))
					{
						if (Receiver->bHasGpsAnchor)
						{
							SendLocation = TargetWorldLocation - Receiver->AnchorWorldLocation;
						}
						else
						{
							UE_LOG(LogTemp, Warning, TEXT("SendTargetCommand: Drone %d has no GPS anchor yet (power_on not received), command may be incorrect"), DroneId);
						}
					}
				}

				NetworkManager->SendMoveCommand(DroneId, SendLocation, CommandMode);
				UE_LOG(LogTemp, Log, TEXT("Move command sent via WebSocket for Drone %d: mode=%s offset=(%.1f, %.1f, %.1f)"),
					DroneId, *DroneCommandModeToProtocolString(CommandMode),
					SendLocation.X, SendLocation.Y, SendLocation.Z);
				return;
			}
		}
	}

	// Fallback: UDP path via DroneCommandSenderComponent
	TScriptInterface<ICoordinateService> CoordService = DroneRegistry->GetCoordinateService();
	if (!CoordService.GetObject())
	{
		UE_LOG(LogTemp, Warning, TEXT("SendTargetCommand: No coordinate service and no WebSocket, command dropped"));
		return;
	}

	const FVector NedLocation = ICoordinateService::Execute_WorldToNed(CoordService.GetObject(), TargetWorldLocation);

	UDroneCommandSenderComponent* CommandSender = nullptr;
	TArray<AActor*> Managers;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AMultiDroneManager::StaticClass(), Managers);
	if (Managers.Num() > 0)
	{
		CommandSender = Managers[0]->FindComponentByClass<UDroneCommandSenderComponent>();
	}

	if (CommandSender)
	{
		CommandSender->SendSingleDroneCommand(DroneId, NedLocation, 1);
		UE_LOG(LogTemp, Log, TEXT("Move command sent via UDP for Drone %d"), DroneId);
	}
}

AActor* ADroneOpsPlayerController::GetActorUnderCursor() const
{
	FHitResult HitResult;
	if (GetHitResultUnderCursor(ECC_Visibility, true, HitResult))
	{
		UE_LOG(LogTemp, Verbose, TEXT("[FR-03] Ray trace hit: %s at (%.1f, %.1f, %.1f)"),
			HitResult.GetActor() ? *HitResult.GetActor()->GetName() : TEXT("NULL"),
			HitResult.Location.X, HitResult.Location.Y, HitResult.Location.Z);
		return HitResult.GetActor();
	}
	return nullptr;
}

bool ADroneOpsPlayerController::GetWorldLocationUnderCursor(FVector& OutLocation) const
{
	FHitResult HitResult;
	if (GetHitResultUnderCursor(ECC_Visibility, false, HitResult))
	{
		OutLocation = HitResult.Location;
		return true;
	}
	return false;
}

AActor* ADroneOpsPlayerController::GetSelectableDroneUnderCursor(FVector* OutFallbackWorldLocation) const
{
	FHitResult MapHit;
	if (GetHitResultUnderCursor(ECC_Visibility, false, MapHit))
	{
		if (OutFallbackWorldLocation)
		{
			*OutFallbackWorldLocation = MapHit.Location;
		}
	}

	FHitResult DroneHit;
	if (!GetHitResultUnderCursor(ECC_Visibility, true, DroneHit))
	{
		return nullptr;
	}

	if (OutFallbackWorldLocation && MapHit.GetActor() == nullptr)
	{
		*OutFallbackWorldLocation = DroneHit.Location;
	}

	AActor* HitActor = DroneHit.GetActor();
	UPrimitiveComponent* HitComponent = DroneHit.GetComponent();
	if (!HitActor || !HitComponent)
	{
		return nullptr;
	}

	AActor* SelectableActor = HitActor;
	if (!IsFr2ControllableDroneActor(SelectableActor))
	{
		const int32 HitDroneId = ResolveDroneIdFromActor(HitActor);
		if (HitDroneId <= 0)
		{
			return nullptr;
		}

		SelectableActor = DroneRegistry ? DroneRegistry->GetSenderPawn(HitDroneId) : nullptr;
		if (!IsFr2ControllableDroneActor(SelectableActor))
		{
			SelectableActor = FindShadowDroneById(GetWorld(), HitDroneId);
		}
	}

	if (!IsFr2ControllableDroneActor(SelectableActor))
	{
		return nullptr;
	}
	UE_LOG(LogTemp, Verbose, TEXT("Cursor DroneHit: %s.%s (%s) -> Select %s"),
		*HitActor->GetName(),
		*HitComponent->GetName(),
		*HitComponent->GetClass()->GetName(),
		*SelectableActor->GetName());
	return SelectableActor;
}

AActor* ADroneOpsPlayerController::FindNearestSelectableDroneOnScreen(float MaxScreenDistance) const
{
	return nullptr;
}

AActor* ADroneOpsPlayerController::ResolveDroneActorById(int32 DroneId) const
{
	if (!DroneRegistry || DroneId <= 0)
	{
		return nullptr;
	}

	if (APawn* SenderPawn = DroneRegistry->GetSenderPawn(DroneId))
	{
		if (IsFr2ControllableDroneActor(SenderPawn))
		{
			return SenderPawn;
		}
	}

	AActor* ReceiverActor = DroneRegistry->GetReceiverActor(DroneId);
	return IsFr2ControllableDroneActor(ReceiverActor) ? ReceiverActor : nullptr;
}

AActor* ADroneOpsPlayerController::ResolveFollowViewTargetByDroneId(int32 DroneId) const
{
	if (AActor* DroneActor = ResolveDroneActorById(DroneId))
	{
		return DroneActor;
	}

	if (IsValid(SelectedDroneActor))
	{
		return SelectedDroneActor.Get();
	}

	if (IsValid(LastFollowViewTarget))
	{
		return LastFollowViewTarget.Get();
	}

	return GetPawn();
}

void ADroneOpsPlayerController::ApplyFollowViewTarget(int32 DroneId)
{
	AActor* FollowTarget = ResolveFollowViewTargetByDroneId(DroneId);
	if (!IsValid(FollowTarget))
	{
		UE_LOG(LogTemp, Warning, TEXT("ApplyFollowViewTarget failed: no valid target for DroneId=%d"), DroneId);
		return;
	}

	const int32 ResolvedDroneId = ResolveDroneIdFromActor(FollowTarget);
	if (ResolvedDroneId > 0)
	{
		CameraModeState.FollowDroneId = ResolvedDroneId;
	}
	else
	{
		CameraModeState.FollowDroneId = DroneId;
	}

	LastFollowViewTarget = FollowTarget;
	CameraModeState.LastFollowLocation = FollowTarget->GetActorLocation();
	CameraModeState.LastFollowRotation = FollowTarget->GetActorRotation();

	// 切换跟随目标时重置俯仰角到斜视(-60°)，ArmLength 归位到默认值
	bFollowTopDownPitch = false;
	FollowTargetPitch = -60.f;
	if (AUE5DroneControlCharacter* DroneChar = Cast<AUE5DroneControlCharacter>(FollowTarget))
	{
		if (USpringArmComponent* Boom = DroneChar->GetCameraBoom())
		{
			Boom->SetRelativeRotation(FRotator(-60.f, 0.f, 0.f));
			Boom->TargetArmLength = 2000.f;
		}
	}

	SetViewTargetWithBlend(FollowTarget, 0.35f);

	UE_LOG(LogTemp, Log, TEXT("Switched to Follow Camera (FollowDroneId=%d, Target=%s)"),
		CameraModeState.FollowDroneId,
		*FollowTarget->GetName());
}

void ADroneOpsPlayerController::OnTopDownToggle()
{
	// Toggle off: restore the exact ViewTarget that was active before Space was pressed
	if (CameraModeState.CameraMode == EDroneCameraMode::TopDown)
	{
		AActor* RestoreTarget = IsValid(PreTopDownViewTarget) ? PreTopDownViewTarget.Get() : nullptr;
		if (!RestoreTarget)
		{
			RestoreTarget = ResolveFollowViewTargetByDroneId(CameraModeState.FollowDroneId);
		}

		CameraModeState.CameraMode = PreTopDownMode;
		if (IsValid(RestoreTarget))
		{
			SetViewTargetWithBlend(RestoreTarget, 0.25f);
			LastFollowViewTarget = RestoreTarget;
		}
		bShowMouseCursor = true;
		SetInputMode(FInputModeGameAndUI());

		UE_LOG(LogTemp, Log, TEXT("TopDown: exit, restored target=%s"),
			RestoreTarget ? *RestoreTarget->GetName() : TEXT("<none>"));
		return;
	}

	// Toggle on — only reachable from Follow (Free is filtered out in Tick)
	PreTopDownMode = CameraModeState.CameraMode;
	PreTopDownViewTarget = GetViewTarget();

	if (!TopDownCamActor || !IsValid(TopDownCamActor))
	{
		TopDownCamActor = GetWorld()->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), FVector::ZeroVector, FRotator(-90.0f, 0.0f, 0.0f));
	}
	if (!IsValid(TopDownCamActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("OnTopDownToggle: failed to spawn TopDownCamActor"));
		return;
	}

	// Anchor on whatever we're currently looking at, fall back to selection / registry
	AActor* FocusActor = IsValid(PreTopDownViewTarget) ? PreTopDownViewTarget.Get() : nullptr;
	if (!FocusActor)
	{
		int32 FocusId = SelectedDroneId > 0 ? SelectedDroneId : CameraModeState.FollowDroneId;
		if (FocusId <= 0 && DroneRegistry)
		{
			FocusId = DroneRegistry->GetPrimarySelectedDrone();
		}
		FocusActor = ResolveFollowViewTargetByDroneId(FocusId);
	}

	const FVector FocusLoc = IsValid(FocusActor) ? FocusActor->GetActorLocation() : FVector::ZeroVector;
	TopDownCamActor->SetActorLocationAndRotation(
		FVector(FocusLoc.X, FocusLoc.Y, FocusLoc.Z + TopDownHeightCm),
		FRotator(-90.0f, 0.0f, 0.0f));

	CameraModeState.CameraMode = EDroneCameraMode::TopDown;
	SetViewTargetWithBlend(TopDownCamActor, 0.25f);
	bShowMouseCursor = true;
	SetInputMode(FInputModeGameAndUI());

	UE_LOG(LogTemp, Log, TEXT("TopDown: enter, cached target=%s Height=%.0f"),
		IsValid(PreTopDownViewTarget) ? *PreTopDownViewTarget->GetName() : TEXT("<none>"), TopDownHeightCm);
}

void ADroneOpsPlayerController::OnPauseToggle()
{
	if (SelectedDroneId <= 0)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange, TEXT("未选中无人机，无法暂停/恢复"));
		}
		return;
	}

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return;
	}

	UDroneNetworkManager* NetworkManager = GI->GetSubsystem<UDroneNetworkManager>();
	if (!NetworkManager)
	{
		return;
	}

	if (!NetworkManager->CanSendToBackend())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, *UDroneNetworkManager::GetIsolationBlockedMessage());
		}
		return;
	}

	if (!NetworkManager->GetWebSocketClient() || !NetworkManager->GetWebSocketClient()->IsConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("OnPauseToggle: WebSocket not connected"));
		return;
	}

	const bool bCurrentlyPaused = PausedDroneIds.Contains(SelectedDroneId);
	const bool bNewPaused = !bCurrentlyPaused;

	// Collect all multi-selected drones; fall back to primary if none
	TArray<int32> TargetIds = DroneRegistry ? DroneRegistry->GetMultiSelectedDrones() : TArray<int32>();
	if (TargetIds.IsEmpty())
	{
		TargetIds.Add(SelectedDroneId);
	}

	NetworkManager->SendPauseCommand(TargetIds, bNewPaused);

	for (int32 Id : TargetIds)
	{
		if (bNewPaused) PausedDroneIds.Add(Id);
		else            PausedDroneIds.Remove(Id);

		// Freeze / unfreeze the mirror drone (RealTimeDroneReceiver)
		if (AActor* ReceiverActor = DroneRegistry ? DroneRegistry->GetReceiverActor(Id) : nullptr)
		{
			if (ARealTimeDroneReceiver* Receiver = Cast<ARealTimeDroneReceiver>(ReceiverActor))
			{
				Receiver->SetPaused(bNewPaused);
			}
		}

		// Freeze / unfreeze the shadow drone (MultiDroneCharacter)
		if (APawn* SenderPawn = DroneRegistry ? DroneRegistry->GetSenderPawn(Id) : nullptr)
		{
			if (AMultiDroneCharacter* Shadow = Cast<AMultiDroneCharacter>(SenderPawn))
			{
				Shadow->SetPaused(bNewPaused);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("OnPauseToggle: SenderPawn for drone %d is not AMultiDroneCharacter (class=%s)"),
					Id, *SenderPawn->GetClass()->GetName());
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("OnPauseToggle: no SenderPawn registered for drone %d"), Id);
		}
	}

	const FString StatusText = bNewPaused ? TEXT("已暂停") : TEXT("已恢复");
	UE_LOG(LogTemp, Log, TEXT("Drone %d %s"), SelectedDroneId, *StatusText);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow,
			FString::Printf(TEXT("无人机 %d %s"), SelectedDroneId, *StatusText));
	}
}

void ADroneOpsPlayerController::ResetShadowDronesToMirrors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!DroneRegistry)
	{
		if (UGameInstance* GameInstance = GetGameInstance())
		{
			DroneRegistry = GameInstance->GetSubsystem<UDroneRegistrySubsystem>();
		}
	}

	int32 ResetCount = 0;
	int32 MissingShadowCount = 0;
	int32 StoppedPathCount = 0;

	for (TActorIterator<ARealTimeDroneReceiver> It(World); It; ++It)
	{
		ARealTimeDroneReceiver* Mirror = *It;
		if (!IsValid(Mirror) || Mirror->DroneId <= 0)
		{
			continue;
		}

		const int32 DroneId = Mirror->DroneId;
		if (DroneRegistry)
		{
			AActor* RegisteredReceiver = DroneRegistry->GetReceiverActor(DroneId);
			if (RegisteredReceiver != Mirror)
			{
				continue;
			}
		}

		AActor* ShadowActor = nullptr;
		if (DroneRegistry)
		{
			ShadowActor = DroneRegistry->GetSenderPawn(DroneId);
		}

		if (!IsValid(ShadowActor))
		{
			for (TActorIterator<AMultiDroneCharacter> ShadowIt(World); ShadowIt; ++ShadowIt)
			{
				AMultiDroneCharacter* Candidate = *ShadowIt;
				if (IsValid(Candidate) && Candidate->DroneId == DroneId)
				{
					ShadowActor = Candidate;
					break;
				}
			}
		}

		ADronePathActor* PathActorFallback = nullptr;
		for (TActorIterator<ADronePathActor> PathIt(World); PathIt; ++PathIt)
		{
			ADronePathActor* PathActor = *PathIt;
			if (!IsValid(PathActor))
			{
				continue;
			}

			AActor* ControlledDrone = PathActor->ControlledDrone.Get();
			const bool bControlsShadowActor = IsValid(ShadowActor) && ControlledDrone == ShadowActor;
			const bool bControlsDroneId = IsValid(ControlledDrone) && ResolveDroneIdFromActor(ControlledDrone) == DroneId;
			const bool bPathActorMatchesDroneId = !IsValid(ShadowActor) && PathActor->GetPathNumericId() == DroneId;

			if (!bControlsShadowActor && !bControlsDroneId && !bPathActorMatchesDroneId)
			{
				continue;
			}

			if (PathActor->IsMovementActive() || PathActor->IsExecutionActive())
			{
				PathActor->UpdatePathStatus(EPathStatus::Standby);
				++StoppedPathCount;
			}

			if (!IsValid(ShadowActor) && bPathActorMatchesDroneId)
			{
				PathActorFallback = PathActor;
			}
		}

		if (!IsValid(ShadowActor))
		{
			ShadowActor = PathActorFallback;
		}

		if (!IsValid(ShadowActor))
		{
			++MissingShadowCount;
			UE_LOG(LogTemp, Warning, TEXT("ResetShadowDronesToMirrors: no shadow actor found for DroneId=%d"), DroneId);
			continue;
		}

		if (AUE5DroneControlCharacter* ShadowDrone = Cast<AUE5DroneControlCharacter>(ShadowActor))
		{
			ShadowDrone->StopClickTargetSending();
		}

		const FVector MirrorLocation = Mirror->GetActorLocation();
		ShadowActor->SetActorLocation(MirrorLocation, false, nullptr, ETeleportType::TeleportPhysics);
		++ResetCount;

		UE_LOG(LogTemp, Log, TEXT("ResetShadowDronesToMirrors: DroneId=%d shadow=%s mirror=%s location=(%.1f, %.1f, %.1f)"),
			DroneId,
			*ShadowActor->GetName(),
			*Mirror->GetName(),
			MirrorLocation.X,
			MirrorLocation.Y,
			MirrorLocation.Z);
	}

	UE_LOG(LogTemp, Log, TEXT("ResetShadowDronesToMirrors complete: reset=%d stoppedPaths=%d missingShadow=%d"),
		ResetCount,
		StoppedPathCount,
		MissingShadowCount);

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, MissingShadowCount > 0 ? FColor::Orange : FColor::Green,
			FString::Printf(TEXT("Reset shadow drones: %d, stopped preview paths: %d, missing shadows: %d"),
				ResetCount,
				StoppedPathCount,
				MissingShadowCount));
	}
}

void ADroneOpsPlayerController::OnSwitchToTopDown()
{
	// 编辑模式下禁用 0：切换跟随视角会打断编辑相机。
	if (bPathEditMode)
	{
		return;
	}
	SwitchToNextMultiDroneFollowView(0.35f);
}

void ADroneOpsPlayerController::SwitchToNextMultiDroneFollowView(float BlendTime)
{
	TArray<AMultiDroneCharacter*> Actors;
	for (TActorIterator<AMultiDroneCharacter> It(GetWorld()); It; ++It)
	{
		Actors.Add(*It);
	}

	if (Actors.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("OnSwitchToTopDown: No AMultiDroneCharacter found in level"));
		return;
	}

	MultiDroneCharacterIndex = MultiDroneCharacterIndex % Actors.Num();
	AMultiDroneCharacter* Target = Actors[MultiDroneCharacterIndex];
	MultiDroneCharacterIndex = (MultiDroneCharacterIndex + 1) % Actors.Num();

	const int32 ResolvedId = ResolveDroneIdFromActor(Target);
	if (ResolvedId > 0)
	{
		LastFollowedDroneId = ResolvedId;
		CameraModeState.FollowDroneId = ResolvedId;
	}
	LastFollowViewTarget = Target;

	CameraModeState.CameraMode = EDroneCameraMode::Follow;
	bShowMouseCursor = true;
	SetInputMode(FInputModeGameAndUI());
	SetViewTargetWithBlend(Target, BlendTime);
	UE_LOG(LogTemp, Log, TEXT("Camera switched to MultiDroneCharacter[%d]: %s (key 0, LastFollowedDroneId=%d)"),
		MultiDroneCharacterIndex - 1, *Target->GetName(), LastFollowedDroneId);
}

void ADroneOpsPlayerController::OnSwitchToRealTimeDrone()
{
	// 编辑模式下禁用 1：切换到真机视角会打断编辑相机。
	if (bPathEditMode)
	{
		return;
	}

	TArray<ARealTimeDroneReceiver*> Actors;
	for (TActorIterator<ARealTimeDroneReceiver> It(GetWorld()); It; ++It)
	{
		Actors.Add(*It);
	}

	if (Actors.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("OnSwitchToRealTimeDrone: No ARealTimeDroneReceiver found in level"));
		return;
	}

	RealTimeDroneIndex = RealTimeDroneIndex % Actors.Num();
	ARealTimeDroneReceiver* Target = Actors[RealTimeDroneIndex];
	RealTimeDroneIndex = (RealTimeDroneIndex + 1) % Actors.Num();

	const int32 ResolvedId = ResolveDroneIdFromActor(Target);
	if (ResolvedId > 0)
	{
		LastFollowedDroneId = ResolvedId;
		CameraModeState.FollowDroneId = ResolvedId;
	}
	LastFollowViewTarget = Target;

	CameraModeState.CameraMode = EDroneCameraMode::Follow;
	bShowMouseCursor = true;
	SetInputMode(FInputModeGameAndUI());
	SetViewTargetWithBlend(Target, 0.35f);
	UE_LOG(LogTemp, Log, TEXT("Camera switched to RealTimeDroneReceiver[%d]: %s (key 1, LastFollowedDroneId=%d)"),
		RealTimeDroneIndex - 1, *Target->GetName(), LastFollowedDroneId);
}

void ADroneOpsPlayerController::TestSendArrayTask()
{
	UGameInstance* GI = GetGameInstance();
	if (!GI) return;

	UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>();
	if (!NetMgr) return;

	FOnHttpResponse Cb;
	Cb.BindDynamic(this, &ADroneOpsPlayerController::OnTestArrayTaskComplete);
	NetMgr->SendArrayTask(TMap<int32, ADronePathActor*>(), EDroneCommandMode::Scout, Cb);
	UE_LOG(LogTemp, Log, TEXT("[TestSendArrayTask] Called SendArrayTask"));
}

void ADroneOpsPlayerController::OnTestArrayTaskComplete(bool bSuccess, const FString& ResponseBody)
{
	UE_LOG(LogTemp, Log, TEXT("[TestSendArrayTask] bSuccess=%d Body=%s"), bSuccess, *ResponseBody);
}


void ADroneOpsPlayerController::OnTestToast()
{
	UUIManagerBlueprintLibrary::ShowToast(this, TEXT("测试：无人机注册成功"), 2.0f);
}

void ADroneOpsPlayerController::OnReturnToMainMenu()
{
	UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: B key pressed, returning to MainMenu"));
	UGameplayStatics::OpenLevel(this, MainMenuLevelName);
}

// 敌对目标点管理

void ADroneOpsPlayerController::SpawnHostileTarget()
{
	TSubclassOf<AHostileTargetActor> SpawnClass = HostileTargetClass;
	if (!SpawnClass)
	{
		SpawnClass = AHostileTargetActor::StaticClass();
	}

	if (CameraModeState.CameraMode == EDroneCameraMode::Free && !bShiftHeld)
	{
		return;
	}

	FVector WorldLocation;
	if (!GetWorldLocationUnderCursor(WorldLocation))
	{
		UE_LOG(LogTemp, Verbose, TEXT("[HostileTarget] No hit under cursor"));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 检查是否与现有目标点重叠（避免叠放）
	const float MinDistance = 200.0f;
	for (TActorIterator<AHostileTargetActor> It(World); It; ++It)
	{
		if (FVector::Dist(It->GetActorLocation(), WorldLocation) < MinDistance)
		{
			UE_LOG(LogTemp, Warning, TEXT("[HostileTarget] Too close to existing target"));
			return;
		}
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	// Lift the marker half a metre above a map hit so it is not hidden inside terrain/tiles.
	WorldLocation.Z += 50.0f;
	AHostileTargetActor* NewTarget = World->SpawnActor<AHostileTargetActor>(
		SpawnClass, WorldLocation, FRotator::ZeroRotator, SpawnParams);

	if (NewTarget)
	{
		UE_LOG(LogTemp, Log, TEXT("[HostileTarget] Spawned target T-%d at (%.1f, %.1f, %.1f)"),
			NewTarget->TargetId, WorldLocation.X, WorldLocation.Y, WorldLocation.Z);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green,
				FString::Printf(TEXT("敌对目标 T-%d 已放置"), NewTarget->TargetId));
		}
	}
}

void ADroneOpsPlayerController::RemoveHostileTarget(AHostileTargetActor* Target)
{
	if (!IsValid(Target))
	{
		return;
	}

	if (Target == SelectedHostileTarget)
	{
		SelectedHostileTarget = nullptr;
	}

	Target->Destroy();
}

UHostileTargetManager* ADroneOpsPlayerController::GetHostileTargetManager() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	return World->GetSubsystem<UHostileTargetManager>();
}

AHostileTargetActor* ADroneOpsPlayerController::GetHostileTargetUnderCursor() const
{
	FHitResult HitResult;
	if (!GetHitResultUnderCursor(ECC_Visibility, false, HitResult))
	{
		return nullptr;
	}
	return Cast<AHostileTargetActor>(HitResult.GetActor());
}

// 敌对目标点发现检测

void ADroneOpsPlayerController::CheckHostileTargetDetection()
{
	UHostileTargetManager* Manager = GetHostileTargetManager();
	if (!Manager)
	{
		return;
	}

	// 获取所有发生发现事件 (DroneId, TargetId)
	TArray<TPair<int32, int32>> Detections = Manager->CheckAllPatrollingDrones();

	for (const auto& Detection : Detections)
	{
		const int32 DroneId = Detection.Key;
		const int32 TargetId = Detection.Value;

		HandleTargetDiscovery(DroneId, TargetId);
	}
}

void ADroneOpsPlayerController::HandleTargetDiscovery(int32 DroneId, int32 TargetId)
{
    UHostileTargetManager* Manager = GetHostileTargetManager();
    if (!Manager) return;

    if (!Manager->TryAssignTarget(TargetId, DroneId))
    {
        UE_LOG(LogTemp, Log, TEXT("[HostileTarget] Target T-%d already assigned, skipping"), TargetId);
        return;
    }

    if (DroneRegistry)
    {
        DroneRegistry->UpdateLocalState(DroneId, EUELocalDroneState::TargetDetectedPending);
    }

    AHostileTargetActor* Target = Manager->GetTarget(TargetId);
    if (!IsValid(Target)) return;

    if (!PreviewConfirmPopupClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[HostileTarget] PreviewConfirmPopupClass not set"));
        return;
    }

    UPreviewConfirmPopupWidget* Popup = CreateWidget<UPreviewConfirmPopupWidget>(this, PreviewConfirmPopupClass);
    if (!Popup)
    {
        UE_LOG(LogTemp, Warning, TEXT("[HostileTarget] Failed to create popup"));
        return;
    }

    Popup->SetAttackConfirmData(DroneId, TargetId, Target->GetHostileTargetLocation());
    Popup->OnAttackConfirmMade.AddDynamic(this, &ADroneOpsPlayerController::OnAttackConfirmMade);
    Popup->AddToViewport(100);

    UE_LOG(LogTemp, Log, TEXT("[HostileTarget] Drone %d discovered target T-%d, showing confirm popup"),
        DroneId, TargetId);
}

// 攻击确认回调

void ADroneOpsPlayerController::OnAttackConfirmMade(int32 DroneId, int32 TargetId, bool bAttack)
{
	UE_LOG(LogTemp, Log, TEXT("[HostileTarget] Attack confirm: Drone %d Target T-%d -> %s"),
		DroneId, TargetId, bAttack ? TEXT("ATTACK") : TEXT("DECLINE"));

	if (bAttack)
	{
		// 用户点击“攻击”
		UHostileTargetManager* Manager = GetHostileTargetManager();
		if (!Manager)
		{
			return;
		}

		// 获取影子机
		AMultiDroneCharacter* Shadow = Manager->GetShadowDrone(DroneId);
		if (!IsValid(Shadow))
		{
			UE_LOG(LogTemp, Warning, TEXT("[HostileTarget] Shadow drone %d not found"), DroneId);
			return;
		}

		// 获取目标位置
		AHostileTargetActor* Target = Manager->GetTarget(TargetId);
		if (!IsValid(Target))
		{
			return;
		}

		// 停止巡逻并飞向目标
		Shadow->StopPatrolAndAttack(Target->GetHostileTargetLocation());
	}
	else
	{
		// 用户点击“不攻击”：路径继续；目标离开范围后 Manager 会重置其可发现状态。
		if (DroneRegistry)
		{
			DroneRegistry->UpdateLocalState(DroneId, EUELocalDroneState::TargetDeclined);
		}
		UE_LOG(LogTemp, Log, TEXT("[HostileTarget] Attack declined, drone %d continues patrol"), DroneId);
	}
}


// ================= 路径编辑模式 =================

bool ADroneOpsPlayerController::BeginPathEditMode(const TArray<int32>& DroneIds)
{
	if (!DroneRegistry)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			DroneRegistry = GI->GetSubsystem<UDroneRegistrySubsystem>();
		}
	}

	UWorld* World = GetWorld();
	if (!World || !DroneRegistry)
	{
		return false;
	}

	// 从头开始：清掉可能残留的上一次编辑
	ClearEditingPaths();

	for (int32 DroneId : DroneIds)
	{
		if (DroneId <= 0)
		{
			continue;
		}

		APawn* ShadowPawn = DroneRegistry->GetSenderPawn(DroneId);
		if (!IsValid(ShadowPawn))
		{
			continue;
		}

		const FVector StartLocation = ShadowPawn->GetActorLocation();

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADronePathActor* PathActor = World->SpawnActor<ADronePathActor>(
			ADronePathActor::StaticClass(), FTransform::Identity, SpawnParams);
		if (!IsValid(PathActor))
		{
			continue;
		}

		PathActor->SetPathNumericId(DroneId);
		// 第一航点 = 影子机当前位置
		PathActor->AddWaypoint(StartLocation, 0.0f);

		EditingPaths.Add(PathActor);
		EditingDroneIds.Add(DroneId);
		EditingPathOrigins.Add(StartLocation);
		EditingPathActive.Add(true);
	}

	// 允许不选中任何无人机就进入编辑模式：进入后可在场景中点击无人机动态增删。
	// 编队参考原点取第一条已有路径起点；无路径时留零，加入首架时 RecomputeEditFormationRefOrigin 会补上。
	EditFormationRefOrigin = EditingPathOrigins.Num() > 0 ? EditingPathOrigins[0] : FVector::ZeroVector;
	bPathEditMode = true;
	SetEditSelectedWaypoint(nullptr);
	EditActiveAxis = EGizmoAxis::None;
	bEditDraggingWaypoint = false;
	ClearEditUndoHistory(); // 每次进入编辑为全新的撤销会话

	// 切换到自由相机，让用户能环视/靠近所编辑的路径
	EnterEditCamera();

	UE_LOG(LogTemp, Log, TEXT("[PathEdit] Begin edit mode with %d path(s)"), EditingPaths.Num());
	return true;
}

void ADroneOpsPlayerController::EndPathEditMode()
{
	// 结束交互（但保留临时路径供保存/派发/取消阶段处理）
	if (bEditDraggingWaypoint)
	{
		HandleEditModeReleased();
	}
	SetEditSelectedWaypoint(nullptr);
	EditActiveAxis = EGizmoAxis::None;
	bEditDraggingWaypoint = false;
	bPathEditMode = false;

	// 恢复进入编辑前的相机/输入/Pawn 控制
	ExitEditCamera();
}

TMap<int32, FDronePathSaveData> ADroneOpsPlayerController::BuildEditingPathsData() const
{
	TMap<int32, FDronePathSaveData> Result;

	for (int32 i = 0; i < EditingPaths.Num(); ++i)
	{
		ADronePathActor* PathActor = EditingPaths[i];
		if (!IsValid(PathActor) || !EditingDroneIds.IsValidIndex(i))
		{
			continue;
		}

		const int32 DroneId = EditingDroneIds[i];

		FDronePathSaveData PathData;
		PathData.PathId = DroneId;
		PathData.bClosedLoop = PathActor->bClosedLoop;

		for (int32 WpIndex = 0; WpIndex < PathActor->GetWaypointCount(); ++WpIndex)
		{
			FDroneWaypointSaveData WpData;
			WpData.Location = PathActor->GetWaypointWorldLocation(WpIndex);
			WpData.SegmentSpeed = PathActor->GetWaypointSegmentSpeed(WpIndex);
			if (PathActor->Waypoints.IsValidIndex(WpIndex))
			{
				WpData.WaitTime = PathActor->Waypoints[WpIndex].WaitTime;
			}
			PathData.Waypoints.Add(WpData);
		}

		Result.Add(DroneId, PathData);
	}

	return Result;
}

bool ADroneOpsPlayerController::TryBuildBackendRelativePathData(
	const TMap<int32, FDronePathSaveData>& WorldPathData,
	TMap<int32, FDronePathSaveData>& OutBackendPathData,
	FString& OutError) const
{
	OutBackendPathData.Reset();
	OutError.Reset();

	if (WorldPathData.IsEmpty())
	{
		OutError = TEXT("没有可派发的路径");
		return false;
	}

	const UGameInstance* GI = GetGameInstance();
	const UDroneNetworkManager* NetworkManager = GI
		? GI->GetSubsystem<UDroneNetworkManager>()
		: nullptr;
	if (!DroneRegistry || !NetworkManager)
	{
		OutError = TEXT("路径坐标转换服务未就绪");
		return false;
	}

	const TScriptInterface<ICoordinateService> CoordService = DroneRegistry->GetCoordinateService();
	UObject* CoordObject = CoordService.GetObject();
	if (!CoordObject
		|| !ICoordinateService::Execute_IsCoordinateSystemReady(CoordObject)
		|| !ICoordinateService::Execute_IsGeographicSupported(CoordObject))
	{
		OutError = TEXT("坐标服务未就绪，无法转换编辑路径");
		return false;
	}

	for (const TPair<int32, FDronePathSaveData>& Pair : WorldPathData)
	{
		const int32 DroneId = Pair.Key;
		if (!ValidateDispatchDrone(DroneId, OutError))
		{
			OutBackendPathData.Reset();
			return false;
		}

		const FDronePathSaveData& WorldPath = Pair.Value;
		if (WorldPath.Waypoints.Num() < 2)
		{
			OutError = FString::Printf(
				TEXT("无人机 %d 的路径只有起点，请至少添加一个目标航点"), DroneId);
			OutBackendPathData.Reset();
			return false;
		}

		FDronePathSaveData BackendPath = WorldPath;
		for (int32 WaypointIndex = 0; WaypointIndex < BackendPath.Waypoints.Num(); ++WaypointIndex)
		{
			FDroneWaypointSaveData& Waypoint = BackendPath.Waypoints[WaypointIndex];
			const FVector WorldLocation = Waypoint.Location;
			if (!FMath::IsFinite(WorldLocation.X)
				|| !FMath::IsFinite(WorldLocation.Y)
				|| !FMath::IsFinite(WorldLocation.Z))
			{
				OutError = FString::Printf(
					TEXT("无人机 %d 的第 %d 个航点包含无效世界坐标"),
					DroneId, WaypointIndex + 1);
				OutBackendPathData.Reset();
				return false;
			}

			// ACesiumGeoreference returns (longitude, latitude, ellipsoid height).
			const FVector LongitudeLatitudeHeight =
				ICoordinateService::Execute_WorldToGeographic(CoordObject, WorldLocation);
			FGeographicCoordinate3D TargetCoordinate;
			TargetCoordinate.Longitude = LongitudeLatitudeHeight.X;
			TargetCoordinate.Latitude = LongitudeLatitudeHeight.Y;
			TargetCoordinate.AltitudeMslMeters =
				LongitudeLatitudeHeight.Z - NetworkManager->GeoidSeparationMeters;
			if (!FMath::IsFinite(TargetCoordinate.Longitude)
				|| TargetCoordinate.Longitude < -180.0 || TargetCoordinate.Longitude > 180.0
				|| !FMath::IsFinite(TargetCoordinate.Latitude)
				|| TargetCoordinate.Latitude < -90.0 || TargetCoordinate.Latitude > 90.0
				|| !FMath::IsFinite(TargetCoordinate.AltitudeMslMeters))
			{
				OutError = FString::Printf(
					TEXT("无人机 %d 的第 %d 个航点无法还原为有效 WGS84 坐标"),
					DroneId, WaypointIndex + 1);
				OutBackendPathData.Reset();
				return false;
			}

			FVector BackendRelativeTarget = FVector::ZeroVector;
			FString ConvertError;
			if (!TryBuildGeographicBackendTarget(
				DroneId,
				TargetCoordinate,
				FVector::ZeroVector,
				NetworkManager,
				BackendRelativeTarget,
				ConvertError))
			{
				OutError = FString::Printf(
					TEXT("无人机 %d 的第 %d 个航点转换失败：%s"),
					DroneId, WaypointIndex + 1, *ConvertError);
				OutBackendPathData.Reset();
				return false;
			}

			Waypoint.Location = BackendRelativeTarget;
		}

		OutBackendPathData.Add(DroneId, MoveTemp(BackendPath));
	}

	return OutBackendPathData.Num() == WorldPathData.Num();
}

void ADroneOpsPlayerController::ClearEditingPaths()
{
	SetEditSelectedWaypoint(nullptr);
	EditActiveAxis = EGizmoAxis::None;
	bEditDraggingWaypoint = false;

	for (ADronePathActor* PathActor : EditingPaths)
	{
		if (IsValid(PathActor))
		{
			PathActor->StopMovement();
			PathActor->Destroy();
		}
	}

	EditingPaths.Reset();
	EditingDroneIds.Reset();
	EditingPathOrigins.Reset();
	EditingPathActive.Reset();
	bPathEditMode = false;
	ClearEditUndoHistory(); // 路径已销毁，撤销记录随之失效

	// 防御性恢复相机（弹窗取消等路径可能不经过 EndPathEditMode）
	ExitEditCamera();
}

void ADroneOpsPlayerController::HandleEditModePressed()
{
	if (bEditDraggingWaypoint)
	{
		return;
	}

	FHitResult HitResult;
	if (!GetHitResultUnderCursor(ECC_Visibility, false, HitResult))
	{
		SetEditSelectedWaypoint(nullptr);
		return;
	}

	// 命中航点 gizmo（或航点本体）→ 选中并可能进入拖拽
	ADroneWaypointActor* HitWaypoint = Cast<ADroneWaypointActor>(HitResult.GetActor());
	if (CanInteractWithEditWaypoint(HitWaypoint))
	{
		const bool bAdditiveModifier =
			IsInputKeyDown(EKeys::LeftControl) || IsInputKeyDown(EKeys::RightControl) ||
			IsInputKeyDown(EKeys::LeftShift) || IsInputKeyDown(EKeys::RightShift);

		// Shift/Ctrl+点击：切换该航点在多选集合中的存在，不进入拖拽。
		if (bAdditiveModifier)
		{
			ToggleEditWaypointSelection(HitWaypoint);
			return;
		}

		// 普通点击：若命中航点已在多选集合内则保留整个集合（可整体拖拽），
		// 否则收敛为仅选中该航点（标准编辑器手感：点未选中项＝单选它）。
		if (IsEditWaypointSelected(HitWaypoint))
		{
			// 命中已选航点：把它切为主操作航点，Gizmo 只显示在它上面（其余选中仅保留高亮）。
			EditSelectedWaypoint = HitWaypoint;
			RefreshSelectionGizmoVisual();
		}
		else
		{
			SetEditSelectedWaypoint(HitWaypoint);
		}

		const EGizmoAxis HitAxis = HitWaypoint->GetGizmoAxisFromComponent(HitResult.GetComponent());
		SetEditActiveAxis(HitAxis);

		if (EditActiveAxis == EGizmoAxis::None)
		{
			// 只是选中航点，未命中拖拽轴
			return;
		}

		bEditDraggingWaypoint = true;
		SetIgnoreLookInput(true);
		SetIgnoreMoveInput(true);
		// 记录拖拽前各选中航点的旧位置（用于 Ctrl+Z 撤销移动），再进入延迟提交。
		CaptureMoveUndoStart();
		// 整个多选集合一起进入延迟提交，拖拽结束时统一刷新路径。
		BeginDeferredUpdateForSelection();

		double MouseX = 0.0;
		double MouseY = 0.0;
		if (GetMousePosition(MouseX, MouseY))
		{
			EditLastMouseScreenPos = FVector2D(MouseX, MouseY);
		}
		return;
	}

	// 命中的是在编路径上的航点（此处即被锁定的起点：CanInteract 已拒绝）——
	// 起点固定不可选，视为"点空白处"：清选中并结束，不落到下面的无人机增删/加点分支，
	// 避免点起点标记误触发冻结无人机或追加航点。
	if (IsValid(HitWaypoint) && IsValid(HitWaypoint->PathActor) && EditingPaths.Contains(HitWaypoint->PathActor))
	{
		SetEditSelectedWaypoint(nullptr);
		return;
	}

	// 命中无人机 → 动态增删编辑集合（点未选中的加入，点已选中的移出），不加航点。
	if (AActor* DroneActor = GetSelectableDroneUnderCursor())
	{
		const int32 ClickedDroneId = ResolveDroneIdFromActor(DroneActor);
		if (ClickedDroneId > 0)
		{
			SetEditSelectedWaypoint(nullptr);
			ToggleDroneInEditMode(ClickedDroneId);
			return;
		}
	}

	// 命中地面/地形 → 给所有临时路径追加航点（编队平移）
	SetEditSelectedWaypoint(nullptr);
	AddWaypointToAllEditingPaths(HitResult.Location);
}

void ADroneOpsPlayerController::HandleEditModeReleased()
{
	const bool bWasDragging = bEditDraggingWaypoint;
	if (bEditDraggingWaypoint)
	{
		EndDeferredUpdateForSelection(true);
	}

	bEditDraggingWaypoint = false;
	SetIgnoreLookInput(false);
	SetIgnoreMoveInput(false);
	SetEditActiveAxis(EGizmoAxis::None);

	// 拖拽结束：若确有位移，压入一条整体移动的撤销记录。
	if (bWasDragging)
	{
		CommitMoveUndoIfMoved();
	}
	else
	{
		PendingMoveUndoItems.Reset();
	}
}

void ADroneOpsPlayerController::UpdateDraggedEditWaypoint()
{
	if (!bPathEditMode || EditActiveAxis == EGizmoAxis::None)
	{
		HandleEditModeReleased();
		return;
	}

	if (!CanInteractWithEditWaypoint(EditSelectedWaypoint))
	{
		HandleEditModeReleased();
		SetEditSelectedWaypoint(nullptr);
		return;
	}

	const float DragDelta = ResolveEditAxisDragDelta();
	if (FMath::IsNearlyZero(DragDelta))
	{
		return;
	}

	// 主选中确定拖拽轴/位移量，整个多选集合按相同世界轴位移同步移动。
	MoveSelectionAlongAxis(EditActiveAxis, DragDelta * EditGizmoDragSensitivity);
}

float ADroneOpsPlayerController::ResolveEditAxisDragDelta()
{
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return 0.0f;
	}

	const FVector2D CurrentMousePosition(MouseX, MouseY);
	const FVector2D MouseDelta = CurrentMousePosition - EditLastMouseScreenPos;
	EditLastMouseScreenPos = CurrentMousePosition;

	ADroneWaypointActor* WaypointActor = EditSelectedWaypoint;
	if (IsValid(WaypointActor) && EditActiveAxis != EGizmoAxis::None)
	{
		FVector AxisWorldDirection = FVector::ZeroVector;
		switch (EditActiveAxis)
		{
		case EGizmoAxis::X: AxisWorldDirection = WaypointActor->GetActorForwardVector(); break;
		case EGizmoAxis::Y: AxisWorldDirection = WaypointActor->GetActorRightVector(); break;
		case EGizmoAxis::Z: AxisWorldDirection = WaypointActor->GetActorUpVector(); break;
		default: break;
		}

		FVector2D AxisOriginScreenPosition = FVector2D::ZeroVector;
		FVector2D AxisTipScreenPosition = FVector2D::ZeroVector;
		const float AxisProjectionDistance = FMath::Max(WaypointActor->GizmoHandleLength, 25.0f);
		if (!AxisWorldDirection.IsNearlyZero()
			&& ProjectWorldLocationToScreen(WaypointActor->GetActorLocation(), AxisOriginScreenPosition)
			&& ProjectWorldLocationToScreen(WaypointActor->GetActorLocation() + (AxisWorldDirection * AxisProjectionDistance), AxisTipScreenPosition))
		{
			const FVector2D AxisScreenDirection = AxisTipScreenPosition - AxisOriginScreenPosition;
			const float AxisScreenLength = AxisScreenDirection.Size();
			if (AxisScreenLength > KINDA_SMALL_NUMBER)
			{
				return FVector2D::DotProduct(MouseDelta, AxisScreenDirection / AxisScreenLength);
			}
		}
	}

	if (EditActiveAxis == EGizmoAxis::Z)
	{
		return static_cast<float>(-MouseDelta.Y);
	}

	if (FMath::Abs(MouseDelta.X) >= FMath::Abs(MouseDelta.Y))
	{
		return static_cast<float>(MouseDelta.X);
	}

	return static_cast<float>(MouseDelta.Y);
}

void ADroneOpsPlayerController::SetEditSelectedWaypoint(ADroneWaypointActor* NewWaypoint)
{
	// 单选语义：清空整个多选集合，只保留 NewWaypoint（可为空＝全清）。
	// 既有调用点（点空白、删除后、退出编辑等）无需改动即获得"清空所有选中"的正确行为。
	const bool bAlreadySingle = (EditSelectedWaypoints.Num() <= 1) && (EditSelectedWaypoint == NewWaypoint);
	if (bAlreadySingle)
	{
		return;
	}

	ClearEditWaypointSelection();

	EditSelectedWaypoint = NewWaypoint;
	EditActiveAxis = EGizmoAxis::None;

	if (IsValid(EditSelectedWaypoint))
	{
		EditSelectedWaypoints.Add(EditSelectedWaypoint);
		EditSelectedWaypoint->SetSelected(true);
		EditSelectedWaypoint->SetActiveGizmoAxis(EGizmoAxis::None);
	}

	RefreshSelectionGizmoVisual();
}

bool ADroneOpsPlayerController::IsEditWaypointSelected(const ADroneWaypointActor* WaypointActor) const
{
	return WaypointActor != nullptr && EditSelectedWaypoints.Contains(WaypointActor);
}

void ADroneOpsPlayerController::ClearEditWaypointSelection()
{
	for (ADroneWaypointActor* Waypoint : EditSelectedWaypoints)
	{
		if (IsValid(Waypoint))
		{
			Waypoint->SetSelected(false);
			Waypoint->SetActiveGizmoAxis(EGizmoAxis::None);
		}
	}
	EditSelectedWaypoints.Reset();
	EditSelectedWaypoint = nullptr;
	EditActiveAxis = EGizmoAxis::None;
}

void ADroneOpsPlayerController::ToggleEditWaypointSelection(ADroneWaypointActor* WaypointActor)
{
	if (!CanInteractWithEditWaypoint(WaypointActor))
	{
		return;
	}

	EditActiveAxis = EGizmoAxis::None;

	if (EditSelectedWaypoints.Contains(WaypointActor))
	{
		WaypointActor->SetSelected(false);
		WaypointActor->SetActiveGizmoAxis(EGizmoAxis::None);
		EditSelectedWaypoints.Remove(WaypointActor);
		// 主选中被移除时，回退到集合末项（若空则置空）。
		if (EditSelectedWaypoint == WaypointActor)
		{
			EditSelectedWaypoint = EditSelectedWaypoints.Num() > 0 ? EditSelectedWaypoints.Last().Get() : nullptr;
		}
	}
	else
	{
		WaypointActor->SetSelected(true);
		EditSelectedWaypoints.Add(WaypointActor);
		EditSelectedWaypoint = WaypointActor; // 新加入的成为主选中
	}

	RefreshSelectionGizmoVisual();
}

void ADroneOpsPlayerController::SelectAllEditWaypoints()
{
	ClearEditWaypointSelection();

	for (ADronePathActor* PathActor : EditingPaths)
	{
		if (!IsValid(PathActor))
		{
			continue;
		}

		for (const TObjectPtr<ADroneWaypointActor>& Waypoint : PathActor->GetWaypointHandleActors())
		{
			ADroneWaypointActor* WaypointActor = Waypoint.Get();
			// 只纳入可交互航点（在编路径、非运动中）。首航点可随编队整体拖拽，
			// 但删除时由 RemoveSelectedEditWaypoint 单独保护，口径一致。
			if (!CanInteractWithEditWaypoint(WaypointActor) || EditSelectedWaypoints.Contains(WaypointActor))
			{
				continue;
			}
			WaypointActor->SetSelected(true);
			EditSelectedWaypoints.Add(WaypointActor);
			EditSelectedWaypoint = WaypointActor;
		}
	}

	EditActiveAxis = EGizmoAxis::None;
	RefreshSelectionGizmoVisual();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
			FString::Printf(TEXT("已全选 %d 个航点"), EditSelectedWaypoints.Num()));
	}
}

void ADroneOpsPlayerController::HandleSelectAllWaypointsKey()
{
	if (bPathEditMode)
	{
		SelectAllEditWaypoints();
	}
}

void ADroneOpsPlayerController::BeginDeferredUpdateForSelection()
{
	for (ADroneWaypointActor* Waypoint : EditSelectedWaypoints)
	{
		if (IsValid(Waypoint))
		{
			Waypoint->BeginDeferredPathUpdate();
		}
	}
}

void ADroneOpsPlayerController::EndDeferredUpdateForSelection(bool bCommit)
{
	for (ADroneWaypointActor* Waypoint : EditSelectedWaypoints)
	{
		if (IsValid(Waypoint))
		{
			Waypoint->EndDeferredPathUpdate(bCommit);
		}
	}
}

void ADroneOpsPlayerController::MoveSelectionAlongAxis(EGizmoAxis Axis, float DeltaDistance)
{
	// MoveAlongGizmoAxis 按世界 X/Y/Z 平移，集合内每个航点位移一致＝整体平移。
	for (ADroneWaypointActor* Waypoint : EditSelectedWaypoints)
	{
		if (CanInteractWithEditWaypoint(Waypoint))
		{
			Waypoint->MoveAlongGizmoAxis(Axis, DeltaDistance);
		}
	}
}

void ADroneOpsPlayerController::PruneInvalidSelection()
{
	EditSelectedWaypoints.RemoveAll([](const TObjectPtr<ADroneWaypointActor>& Waypoint)
	{
		return !IsValid(Waypoint.Get());
	});

	if (EditSelectedWaypoint != nullptr && !IsValid(EditSelectedWaypoint))
	{
		// 主操作航点失效（被删除/路径重建）：换成集合末项作为新主，并把 Gizmo 迁移过去。
		EditSelectedWaypoint = EditSelectedWaypoints.Num() > 0 ? EditSelectedWaypoints.Last().Get() : nullptr;
		RefreshSelectionGizmoVisual();
	}
}

void ADroneOpsPlayerController::RefreshSelectionGizmoVisual()
{
	// 仅主操作航点显示可交互 Gizmo 轴，其余选中航点只保留选中高亮，避免多组 Gizmo 重叠误操作。
	for (ADroneWaypointActor* Waypoint : EditSelectedWaypoints)
	{
		if (!IsValid(Waypoint))
		{
			continue;
		}

		const bool bIsPrimary = (Waypoint == EditSelectedWaypoint);
		Waypoint->SetGizmoInteractable(bIsPrimary);
		if (!bIsPrimary)
		{
			Waypoint->SetActiveGizmoAxis(EGizmoAxis::None);
		}
	}
}

void ADroneOpsPlayerController::SetEditActiveAxis(EGizmoAxis NewAxis)
{
	EditActiveAxis = NewAxis;
	if (IsValid(EditSelectedWaypoint))
	{
		EditSelectedWaypoint->SetActiveGizmoAxis(EditActiveAxis);
	}
}

bool ADroneOpsPlayerController::CanInteractWithEditWaypoint(const ADroneWaypointActor* WaypointActor) const
{
	if (!bPathEditMode || !IsValid(WaypointActor) || !IsValid(WaypointActor->PathActor))
	{
		return false;
	}

	// 只允许操作本次编辑创建的路径上的航点
	const int32 PathIndex = EditingPaths.IndexOfByKey(WaypointActor->PathActor);
	if (PathIndex == INDEX_NONE)
	{
		return false;
	}

	// 冻结路径（已取消选中的无人机）的航点：保留可见但不可选中/拖拽，不加入可拖动集合。
	if (EditingPathActive.IsValidIndex(PathIndex) && !EditingPathActive[PathIndex])
	{
		return false;
	}

	// 起点（每条路径首航点＝影子机当前位置/编队锚点）固定，不可选中/拖拽/删除。
	// 从统一的交互口径拦截，保证点选、Ctrl+点击、Ctrl+A、拖拽、删除行为一致。
	if (WaypointActor->WaypointIndex == 0)
	{
		return false;
	}

	// 播放中路径的航点不可拖动。
	return !WaypointActor->PathActor->IsMovementActive();
}

void ADroneOpsPlayerController::AddWaypointToAllEditingPaths(const FVector& WorldLocation)
{
	const FVector Offset = WorldLocation - EditFormationRefOrigin;

	TArray<ADronePathActor*> AffectedPaths;
	for (int32 i = 0; i < EditingPaths.Num(); ++i)
	{
		ADronePathActor* PathActor = EditingPaths[i];
		if (!IsValid(PathActor) || !EditingPathOrigins.IsValidIndex(i))
		{
			continue;
		}

		// 冻结的路径（已取消选中）保留可见但不再加点。
		if (!EditingPathActive.IsValidIndex(i) || !EditingPathActive[i])
		{
			continue;
		}

		// 编队平移：每条路径以自身首航点为基准，叠加相同偏移
		const FVector NewWaypointLocation = EditingPathOrigins[i] + Offset;
		PathActor->AddWaypoint(NewWaypointLocation, EditDefaultSegmentSpeed);
		AffectedPaths.Add(PathActor);
	}

	// 一次地图点击加点作为一步操作记录，供 Ctrl+Z 撤销。
	PushAddWaypointsUndo(AffectedPaths);
}

void ADroneOpsPlayerController::RecomputeEditFormationRefOrigin()
{
	// 编队参考原点 = 当前第一条【活跃】路径的起点（与 BeginPathEditMode 约定一致）。
	// 增删无人机后必须重算，否则 AddWaypointToAllEditingPaths 的偏移基准陈旧，
	// 会导致新航点落在"光标 + 旧锚点到新锚点距离"处。
	for (int32 i = 0; i < EditingPathOrigins.Num(); ++i)
	{
		if (EditingPathActive.IsValidIndex(i) && EditingPathActive[i])
		{
			EditFormationRefOrigin = EditingPathOrigins[i];
			return;
		}
	}
}

bool ADroneOpsPlayerController::ToggleDroneInEditMode(int32 DroneId)
{
	if (DroneId <= 0)
	{
		return false;
	}

	// 已在编辑集合且当前活跃 → 冻结（保留路径，不再加点）；否则 → 激活（复活或新建）。
	const int32 Index = EditingDroneIds.IndexOfByKey(DroneId);
	const bool bCurrentlyActive = (Index != INDEX_NONE) && EditingPathActive.IsValidIndex(Index) && EditingPathActive[Index];

	if (bCurrentlyActive)
	{
		FreezeDroneInEditMode(DroneId);
	}
	else
	{
		AddDroneToEditMode(DroneId);
	}
	return true;
}

void ADroneOpsPlayerController::AddDroneToEditMode(int32 DroneId)
{
	if (DroneId <= 0 || !DroneRegistry)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 离线无人机不可加入编辑
	EDroneControlLockReason LockReason = EDroneControlLockReason::None;
	if (DroneRegistry->IsControlLocked(DroneId, LockReason) && LockReason == EDroneControlLockReason::Offline)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PathEdit] drone %d offline, cannot add to edit"), DroneId);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange,
				FString::Printf(TEXT("Drone %d 离线，无法加入编辑"), DroneId));
		}
		return;
	}

	APawn* ShadowPawn = DroneRegistry->GetSenderPawn(DroneId);
	if (!IsValid(ShadowPawn))
	{
		return;
	}

	// 影子机在编辑期间不再跟随镜像机，否则每帧被拉回，路径首航点会漂移。
	if (AMultiDroneCharacter* Shadow = Cast<AMultiDroneCharacter>(ShadowPawn))
	{
		Shadow->bFollowingMirror = false;
	}

	// 已有(可能已冻结)的路径 → 直接复活，接续在其末尾加点，不新建。
	const int32 ExistingIndex = EditingDroneIds.IndexOfByKey(DroneId);
	if (ExistingIndex != INDEX_NONE)
	{
		if (EditingPathActive.IsValidIndex(ExistingIndex))
		{
			EditingPathActive[ExistingIndex] = true;
		}
	}
	else
	{
		const FVector StartLocation = ShadowPawn->GetActorLocation();

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADronePathActor* PathActor = World->SpawnActor<ADronePathActor>(
			ADronePathActor::StaticClass(), FTransform::Identity, SpawnParams);
		if (!IsValid(PathActor))
		{
			return;
		}

		PathActor->SetPathNumericId(DroneId);
		// 第一航点 = 影子机当前位置（与 BeginPathEditMode 一致）
		PathActor->AddWaypoint(StartLocation, 0.0f);

		EditingPaths.Add(PathActor);
		EditingDroneIds.Add(DroneId);
		EditingPathOrigins.Add(StartLocation);
		EditingPathActive.Add(true);
	}

	// 同步选中集合与高亮
	TArray<int32> Multi = DroneRegistry->GetMultiSelectedDrones();
	Multi.AddUnique(DroneId);
	DroneRegistry->SetPrimarySelectedDrone(DroneId);
	DroneRegistry->SetMultiSelectedDrones(Multi);
	SetDronePrimarySelectedState(ShadowPawn, true);

	// 编队锚点跟随第一条活跃路径，保证点击落点始终在光标处。
	RecomputeEditFormationRefOrigin();

	UE_LOG(LogTemp, Log, TEXT("[PathEdit] activated drone %d (%d path(s), active recomputed)"), DroneId, EditingPaths.Num());
}

void ADroneOpsPlayerController::FreezeDroneInEditMode(int32 DroneId)
{
	const int32 Index = EditingDroneIds.IndexOfByKey(DroneId);
	if (Index == INDEX_NONE)
	{
		return;
	}

	// 冻结而非销毁：路径保留在场景中可见，仅停止接收新航点（退出编辑模式才清）。
	if (EditingPathActive.IsValidIndex(Index))
	{
		EditingPathActive[Index] = false;
	}

	// 被冻结路径上的航点必须移出多选集合：结束相关拖拽、清除其高亮/Gizmo，
	// 保证"冻结路径航点不在可拖动集合、也无残留高亮"（验收 4/5）。
	if (EditingPaths.IsValidIndex(Index) && IsValid(EditingPaths[Index]))
	{
		ADronePathActor* FrozenPath = EditingPaths[Index];

		const bool bPrimaryFrozen = IsValid(EditSelectedWaypoint) && EditSelectedWaypoint->PathActor == FrozenPath;
		if (bPrimaryFrozen)
		{
			// 正拖拽的主航点被冻结：先结束拖拽交互，避免继续操作已冻结路径。
			HandleEditModeReleased();
		}

		for (int32 i = EditSelectedWaypoints.Num() - 1; i >= 0; --i)
		{
			ADroneWaypointActor* Waypoint = EditSelectedWaypoints[i].Get();
			if (!IsValid(Waypoint) || Waypoint->PathActor == FrozenPath)
			{
				if (IsValid(Waypoint))
				{
					Waypoint->SetSelected(false);
					Waypoint->SetActiveGizmoAxis(EGizmoAxis::None);
				}
				EditSelectedWaypoints.RemoveAt(i);
			}
		}

		// 主航点若被移出，迁移到剩余集合末项（或置空），并刷新 Gizmo 归属。
		if (!EditSelectedWaypoints.Contains(EditSelectedWaypoint))
		{
			EditSelectedWaypoint = EditSelectedWaypoints.Num() > 0 ? EditSelectedWaypoints.Last().Get() : nullptr;
			EditActiveAxis = EGizmoAxis::None;
		}
		RefreshSelectionGizmoVisual();
	}

	// 同步选中集合与高亮（移出选中，但不动路径数组）
	if (DroneRegistry)
	{
		TArray<int32> Multi = DroneRegistry->GetMultiSelectedDrones();
		Multi.Remove(DroneId);
		if (Multi.IsEmpty())
		{
			DroneRegistry->ClearSelection();
		}
		else
		{
			const int32 CurrentPrimary = DroneRegistry->GetPrimarySelectedDrone();
			const int32 NewPrimary = (CurrentPrimary == DroneId) ? Multi[0] : CurrentPrimary;
			DroneRegistry->SetPrimarySelectedDrone(NewPrimary);
			DroneRegistry->SetMultiSelectedDrones(Multi);
		}

		if (APawn* ShadowPawn = DroneRegistry->GetSenderPawn(DroneId))
		{
			SetDronePrimarySelectedState(ShadowPawn, false);
		}
	}

	// 冻结后第一条活跃路径可能变化，重算编队锚点避免落点偏移。
	RecomputeEditFormationRefOrigin();

	UE_LOG(LogTemp, Log, TEXT("[PathEdit] froze drone %d (path kept, %d path(s) total)"), DroneId, EditingPaths.Num());
}

bool ADroneOpsPlayerController::SetEditingPathClosedLoop(int32 DroneId, bool bClosedLoop)
{
	for (int32 i = 0; i < EditingPaths.Num(); ++i)
	{
		if (EditingDroneIds.IsValidIndex(i) && EditingDroneIds[i] == DroneId && IsValid(EditingPaths[i]))
		{
			EditingPaths[i]->SetClosedLoop(bClosedLoop);
			return true;
		}
	}
	return false;
}

void ADroneOpsPlayerController::SetAllEditingPathsClosedLoop(bool bClosedLoop)
{
	for (ADronePathActor* PathActor : EditingPaths)
	{
		if (IsValid(PathActor))
		{
			PathActor->SetClosedLoop(bClosedLoop);
		}
	}
}

// ================= 编队旋转预览（JSON 路径：运行锚点平移 + 水平旋转）=================

FVector ADroneOpsPlayerController::ApplyFormationTransform(const FVector& NodeEditLocation, const FVector& RefEditOrigin, const FVector& TargetBase, float YawDegrees)
{
	// 相对位置 = 节点(编辑系) - 源锚点
	const FVector Relative = NodeEditLocation - RefEditOrigin;

	// 只绕世界 Z 旋转水平分量；高度(Z)保持不变。
	const float YawRad = FMath::DegreesToRadians(YawDegrees);
	const float CosYaw = FMath::Cos(YawRad);
	const float SinYaw = FMath::Sin(YawRad);

	const FVector RotatedRelative(
		Relative.X * CosYaw - Relative.Y * SinYaw,
		Relative.X * SinYaw + Relative.Y * CosYaw,
		Relative.Z);

	// 旋转后位置 = 目标基准 + RotateZ(相对位置)
	return TargetBase + RotatedRelative;
}

bool ADroneOpsPlayerController::ResolveRunAnchorDrone(int32& OutDroneId, FVector& OutAnchorWorld)
{
	if (!DroneRegistry)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			DroneRegistry = GI->GetSubsystem<UDroneRegistrySubsystem>();
		}
	}
	if (!DroneRegistry)
	{
		return false;
	}

	auto TryResolve = [this](int32 DroneId, FVector& OutLoc) -> bool
	{
		if (DroneId <= 0)
		{
			return false;
		}
		APawn* ShadowPawn = DroneRegistry->GetSenderPawn(DroneId);
		if (!IsValid(ShadowPawn))
		{
			return false;
		}
		OutLoc = ShadowPawn->GetActorLocation();
		return true;
	};

	// 1. 主选中无人机
	const int32 Primary = DroneRegistry->GetPrimarySelectedDrone();
	if (Primary > 0)
	{
		FVector Loc;
		if (TryResolve(Primary, Loc))
		{
			OutDroneId = Primary;
			OutAnchorWorld = Loc;
			return true;
		}
	}

	// 2. 最小可用 DroneId
	int32 BestId = MAX_int32;
	FVector BestLoc = FVector::ZeroVector;
	bool bFound = false;
	for (const FDroneDescriptor& Desc : DroneRegistry->GetAllDroneDescriptors())
	{
		if (Desc.DroneId <= 0 || Desc.DroneId >= BestId)
		{
			continue;
		}
		FVector Loc;
		if (TryResolve(Desc.DroneId, Loc))
		{
			BestId = Desc.DroneId;
			BestLoc = Loc;
			bFound = true;
		}
	}

	if (bFound)
	{
		OutDroneId = BestId;
		OutAnchorWorld = BestLoc;
		return true;
	}

	return false;
}

bool ADroneOpsPlayerController::BeginFormationRotatePreview(const FDronePathsSaveData& PathsData)
{
	EndFormationRotatePreview();

	if (PathsData.Paths.IsEmpty())
	{
		return false;
	}

	int32 AnchorDroneId = INDEX_NONE;
	FVector AnchorWorld = FVector::ZeroVector;
	if (!ResolveRunAnchorDrone(AnchorDroneId, AnchorWorld))
	{
		return false;
	}

	// 源锚点：JSON 中 AnchorDroneId 对应路径的首航点；缺失则退回第一条路径首航点。
	const FDronePathSaveData* AnchorPath = PathsData.Paths.FindByPredicate(
		[&PathsData](const FDronePathSaveData& P)
		{
			return P.PathId == PathsData.AnchorDroneId && P.Waypoints.Num() > 0;
		});
	if (!AnchorPath)
	{
		AnchorPath = PathsData.Paths.FindByPredicate(
			[](const FDronePathSaveData& P) { return P.Waypoints.Num() > 0; });
	}
	if (!AnchorPath)
	{
		return false;
	}

	FormationSourcePaths = PathsData;
	FormationRefEditOrigin = AnchorPath->Waypoints[0].Location;
	FormationRunAnchorWorld = AnchorWorld;
	FormationRunAnchorDroneId = AnchorDroneId;
	FormationYawDegrees = 0.0f;
	bFormationRotateActive = true;

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 生成预览路径可视化 Actor（只显示不移动）。
	for (const FDronePathSaveData& PathData : FormationSourcePaths.Paths)
	{
		if (PathData.Waypoints.IsEmpty())
		{
			continue;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADronePathActor* PathActor = World->SpawnActor<ADronePathActor>(
			ADronePathActor::StaticClass(), FTransform::Identity, SpawnParams);
		if (!IsValid(PathActor))
		{
			continue;
		}

		PathActor->SetPathNumericId(PathData.PathId);
		PathActor->bClosedLoop = PathData.bClosedLoop;
		FormationPreviewActors.Add(PathActor);
	}

	// 生成锚点旋转环 Gizmo。
	FActorSpawnParameters GizmoParams;
	GizmoParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FormationGizmoActor = World->SpawnActor<AFormationRotationGizmoActor>(
		AFormationRotationGizmoActor::StaticClass(), FTransform(AnchorWorld), GizmoParams);

	// 缓存环半径供平面命中判定使用。
	if (IsValid(FormationGizmoActor))
	{
		FormationRingRadiusCm = FormationGizmoActor->RingRadiusCm;
	}

	RefreshFormationPreview();
	return true;
}

void ADroneOpsPlayerController::RefreshFormationPreview()
{
	if (!bFormationRotateActive)
	{
		return;
	}

	// 按当前 Yaw 与锚点重新摆放每条预览路径的节点。
	for (int32 i = 0; i < FormationPreviewActors.Num() && i < FormationSourcePaths.Paths.Num(); ++i)
	{
		ADronePathActor* PathActor = FormationPreviewActors[i];
		const FDronePathSaveData& PathData = FormationSourcePaths.Paths[i];
		if (!IsValid(PathActor))
		{
			continue;
		}

		PathActor->ClearWaypoints();
		for (int32 w = 0; w < PathData.Waypoints.Num(); ++w)
		{
			const FVector WorldLoc = ApplyFormationTransform(
				PathData.Waypoints[w].Location, FormationRefEditOrigin, FormationRunAnchorWorld, FormationYawDegrees);
			const float Speed = (w == 0) ? 0.0f : PathData.Waypoints[w].SegmentSpeed;
			PathActor->AddWaypoint(WorldLoc, Speed);
		}
		PathActor->RefreshPath();
	}

	if (IsValid(FormationGizmoActor))
	{
		FormationGizmoActor->SetGizmoWorldLocation(FormationRunAnchorWorld);
	}
}

void ADroneOpsPlayerController::EndFormationRotatePreview()
{
	EndFormationRingDrag();

	for (ADronePathActor* PathActor : FormationPreviewActors)
	{
		if (IsValid(PathActor))
		{
			PathActor->Destroy();
		}
	}
	FormationPreviewActors.Reset();

	if (IsValid(FormationGizmoActor))
	{
		FormationGizmoActor->Destroy();
	}
	FormationGizmoActor = nullptr;

	bFormationRotateActive = false;
	bDraggingFormationRing = false;
	FormationYawDegrees = 0.0f;
	FormationSourcePaths = FDronePathsSaveData();
}

bool ADroneOpsPlayerController::ComputeCursorRadiusOnAnchorPlane(float& OutRadiusCm) const
{
	// 把鼠标反投影成世界射线，与"过锚点、水平"的平面（Z=锚点.Z）求交，
	// 得到光标在世界水平面上的落点，返回它离锚点的距离。用于按下时判定是否点中旋转环。
	FVector WorldOrigin = FVector::ZeroVector;
	FVector WorldDirection = FVector::ZeroVector;
	if (!DeprojectMousePositionToWorld(WorldOrigin, WorldDirection))
	{
		return false;
	}

	if (FMath::Abs(WorldDirection.Z) < KINDA_SMALL_NUMBER)
	{
		return false;
	}

	const float T = (FormationRunAnchorWorld.Z - WorldOrigin.Z) / WorldDirection.Z;
	if (T <= 0.0f)
	{
		return false;
	}

	const FVector HitPoint = WorldOrigin + WorldDirection * T;
	OutRadiusCm = FVector2D(HitPoint.X - FormationRunAnchorWorld.X, HitPoint.Y - FormationRunAnchorWorld.Y).Size();
	return true;
}

bool ADroneOpsPlayerController::IsCursorOnFormationRing() const
{
	float RadiusCm = 0.0f;
	if (!ComputeCursorRadiusOnAnchorPlane(RadiusCm))
	{
		return false;
	}

	// 命中容差：环半径 ± 较大的带宽（细环很难点，放宽到 ~40% 半径或至少 200cm）。
	const float Tolerance = FMath::Max(FormationRingRadiusCm * 0.4f, 200.0f);
	return FMath::Abs(RadiusCm - FormationRingRadiusCm) <= Tolerance;
}

void ADroneOpsPlayerController::HandleFormationRingPressed()
{
	if (!bFormationRotateActive || !IsValid(FormationGizmoActor))
	{
		return;
	}

	bDraggingFormationRing = true;
	FormationGizmoActor->SetRingHighlighted(true);
	SetIgnoreLookInput(true);
	SetIgnoreMoveInput(true);
}

void ADroneOpsPlayerController::UpdateFormationRingDrag()
{
	if (!bDraggingFormationRing || !bFormationRotateActive)
	{
		return;
	}

	// GameAndUI 捕获期间真实光标被冻结，无法用光标绝对位置；改用原始鼠标增量：
	// 水平拖动映射为绕世界 Z 的偏航角（与自由相机转视角一致）。
	float MouseDX = 0.0f;
	float MouseDY = 0.0f;
	GetInputMouseDelta(MouseDX, MouseDY);

	if (FMath::IsNearlyZero(MouseDX))
	{
		return;
	}

	FormationYawDegrees = FMath::UnwindDegrees(FormationYawDegrees + MouseDX * FormationRotateDegPerMouseUnit);
	RefreshFormationPreview();
}

void ADroneOpsPlayerController::EndFormationRingDrag()
{
	if (bDraggingFormationRing)
	{
		SetIgnoreLookInput(false);
		SetIgnoreMoveInput(false);
	}
	bDraggingFormationRing = false;
	if (IsValid(FormationGizmoActor))
	{
		FormationGizmoActor->SetRingHighlighted(false);
	}
}

// ================= 编辑模式相机 =================

void ADroneOpsPlayerController::EnterEditCamera()
{
	if (bEditCameraActive)
	{
		return;
	}

	// 记录进入前状态，退出时还原
	EditPrevCameraMode = CameraModeState.CameraMode;
	EditPrevViewTarget = GetViewTarget();

	// 把自由相机放到当前视角位置，无缝切换
	if (IsValid(FreeCamActor) && PlayerCameraManager)
	{
		const FVector CamLoc = PlayerCameraManager->GetCameraLocation();
		const FRotator CamRot = PlayerCameraManager->GetCameraRotation();
		FreeCamActor->SetActorLocationAndRotation(CamLoc, CamRot);
		FreeCamRotation = CamRot;
		SetViewTargetWithBlend(FreeCamActor, 0.0f);
	}

	CameraModeState.CameraMode = EDroneCameraMode::Free;

	// 禁用 Pawn 输入，防止 WASD 继续控制无人机本体
	if (APawn* ControlledPawn = GetPawn())
	{
		ControlledPawn->DisableInput(this);
	}

	// 编辑时鼠标可见（用于点选航点 / 点地图加点），右键按下才捕获转视角
	bEditCameraLooking = false;
	bShowMouseCursor = true;
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);

	bEditCameraActive = true;
	UE_LOG(LogTemp, Log, TEXT("[PathEdit] Enter edit camera (free-fly)"));
}

void ADroneOpsPlayerController::ExitEditCamera()
{
	if (!bEditCameraActive)
	{
		return;
	}
	bEditCameraActive = false;
	bEditCameraLooking = false;

	// 恢复 Pawn 输入
	if (APawn* ControlledPawn = GetPawn())
	{
		ControlledPawn->EnableInput(this);
	}

	bShowMouseCursor = true;
	SetInputMode(FInputModeGameAndUI());

	// 还原相机模式与视角目标
	CameraModeState.CameraMode = EditPrevCameraMode;
	AActor* RestoreTarget = IsValid(EditPrevViewTarget) ? EditPrevViewTarget.Get() : nullptr;
	if (!RestoreTarget)
	{
		RestoreTarget = ResolveFollowViewTargetByDroneId(CameraModeState.FollowDroneId);
	}
	if (IsValid(RestoreTarget))
	{
		SetViewTargetWithBlend(RestoreTarget, 0.25f);
		LastFollowViewTarget = RestoreTarget;
	}
	EditPrevViewTarget = nullptr;

	UE_LOG(LogTemp, Log, TEXT("[PathEdit] Exit edit camera, restored view"));
}

void ADroneOpsPlayerController::TickEditCamera(float DeltaTime)
{
	if (!bEditCameraActive || !IsValid(FreeCamActor))
	{
		return;
	}

	// 按住右键：用鼠标移动量旋转视角
	if (bEditCameraLooking)
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		GetInputMouseDelta(MouseX, MouseY);
		if (MouseX != 0.0f || MouseY != 0.0f)
		{
			FreeCamRotation.Yaw += MouseX * FreeCamMouseSensitivity * 100.0f;
			FreeCamRotation.Pitch = FMath::Clamp(
				FreeCamRotation.Pitch - MouseY * FreeCamMouseSensitivity * 100.0f, -89.0f, 89.0f);
			FreeCamActor->SetActorRotation(FreeCamRotation);
		}
	}

	// WASD/QE 移动（相对当前视角方向）
	FVector MoveDir = FVector::ZeroVector;
	const FRotationMatrix RotMat(FreeCamRotation);
	if (IsInputKeyDown(EKeys::W)) MoveDir += RotMat.GetScaledAxis(EAxis::X);
	if (IsInputKeyDown(EKeys::S)) MoveDir -= RotMat.GetScaledAxis(EAxis::X);
	if (IsInputKeyDown(EKeys::A)) MoveDir -= RotMat.GetScaledAxis(EAxis::Y);
	if (IsInputKeyDown(EKeys::D)) MoveDir += RotMat.GetScaledAxis(EAxis::Y);
	if (IsInputKeyDown(EKeys::Q)) MoveDir -= FVector::UpVector;
	if (IsInputKeyDown(EKeys::E)) MoveDir += FVector::UpVector;

	if (!MoveDir.IsNearlyZero())
	{
		MoveDir.Normalize();
		FreeCamActor->AddActorWorldOffset(MoveDir * FreeCamMoveSpeed * DeltaTime);
	}
}

void ADroneOpsPlayerController::OnEditLookPressed()
{
	if (bPathEditMode)
	{
		bEditCameraLooking = true;
	}
}

void ADroneOpsPlayerController::OnEditLookReleased()
{
	bEditCameraLooking = false;
}

// ================= 编辑模式航点增删 =================

void ADroneOpsPlayerController::HandleDeleteWaypointKey()
{
	if (bPathEditMode)
	{
		RemoveSelectedEditWaypoint();
	}
}

void ADroneOpsPlayerController::HandleUndoWaypointKey()
{
	if (bPathEditMode)
	{
		UndoLastEditWaypoint();
	}
}

void ADroneOpsPlayerController::RemoveSelectedEditWaypoint()
{
	// 本期不做批量删除：仅删除主操作航点，保持既有单点删除行为。
	// 删除后 SetEditSelectedWaypoint(nullptr) 会清空整个多选集合，避免残留高亮/悬空引用。
	if (!IsValid(EditSelectedWaypoint) || !CanInteractWithEditWaypoint(EditSelectedWaypoint))
	{
		return;
	}

	ADronePathActor* OwningPath = EditSelectedWaypoint->PathActor;
	const int32 WpIndex = EditSelectedWaypoint->WaypointIndex;

	// 首航点（影子机起点）禁止删除（CanInteract 已拦截 index==0，此处再防御一次）
	if (WpIndex <= 0)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange, TEXT("首航点（起点）不可删除"));
		}
		return;
	}

	// 删除前捕获完整航点数据（本地空间，含速度/等待/扩展），供 Ctrl+Z 还原。
	if (IsValid(OwningPath) && OwningPath->Waypoints.IsValidIndex(WpIndex))
	{
		PushDeleteWaypointUndo(OwningPath, WpIndex, OwningPath->Waypoints[WpIndex]);
	}

	// 先清选中，避免删除后悬空引用；再从路径移除该航点
	SetEditSelectedWaypoint(nullptr);
	if (IsValid(OwningPath))
	{
		OwningPath->RemoveWaypoint(WpIndex);
	}
}

void ADroneOpsPlayerController::PushAddWaypointsUndo(const TArray<ADronePathActor*>& AffectedPaths)
{
	if (AffectedPaths.Num() == 0)
	{
		return;
	}

	FEditUndoEntry Entry;
	Entry.Kind = EEditUndoKind::AddWaypoints;
	for (ADronePathActor* Path : AffectedPaths)
	{
		if (!IsValid(Path))
		{
			continue;
		}
		FEditWaypointUndoItem Item;
		Item.Path = Path;
		Item.Index = Path->GetWaypointCount() - 1; // 刚追加的航点在末尾
		Entry.Items.Add(Item);
	}

	if (Entry.Items.Num() > 0)
	{
		EditUndoStack.Add(MoveTemp(Entry));
	}
}

void ADroneOpsPlayerController::PushDeleteWaypointUndo(ADronePathActor* Path, int32 Index, const FDroneWaypoint& WaypointData)
{
	if (!IsValid(Path))
	{
		return;
	}

	FEditUndoEntry Entry;
	Entry.Kind = EEditUndoKind::DeleteWaypoint;
	FEditWaypointUndoItem Item;
	Item.Path = Path;
	Item.Index = Index;
	Item.WaypointData = WaypointData; // 完整本地空间数据，还原时原样插回
	Entry.Items.Add(Item);
	EditUndoStack.Add(MoveTemp(Entry));
}

void ADroneOpsPlayerController::CaptureMoveUndoStart()
{
	// 记录本次拖拽将要移动的每个航点（可交互者）的旧世界位置。
	PendingMoveUndoItems.Reset();
	for (ADroneWaypointActor* Waypoint : EditSelectedWaypoints)
	{
		if (!CanInteractWithEditWaypoint(Waypoint))
		{
			continue;
		}
		FEditWaypointUndoItem Item;
		Item.Path = Waypoint->PathActor;
		Item.Index = Waypoint->WaypointIndex;
		Item.OldWorldLocation = Waypoint->GetActorLocation();
		PendingMoveUndoItems.Add(Item);
	}
}

void ADroneOpsPlayerController::CommitMoveUndoIfMoved()
{
	if (PendingMoveUndoItems.Num() == 0)
	{
		return;
	}

	// 仅当至少一个航点确实发生位移时才记录一步撤销。
	bool bAnyMoved = false;
	for (const FEditWaypointUndoItem& Item : PendingMoveUndoItems)
	{
		ADronePathActor* Path = Item.Path.Get();
		if (!IsValid(Path))
		{
			continue;
		}
		const FVector NewLoc = Path->GetWaypointWorldLocation(Item.Index);
		if (!NewLoc.Equals(Item.OldWorldLocation, 0.5f))
		{
			bAnyMoved = true;
			break;
		}
	}

	if (bAnyMoved)
	{
		FEditUndoEntry Entry;
		Entry.Kind = EEditUndoKind::MoveWaypoints;
		Entry.Items = PendingMoveUndoItems; // 保存旧位置，撤销时还原
		EditUndoStack.Add(MoveTemp(Entry));
	}

	PendingMoveUndoItems.Reset();
}

void ADroneOpsPlayerController::ClearEditUndoHistory()
{
	EditUndoStack.Reset();
	PendingMoveUndoItems.Reset();
}

void ADroneOpsPlayerController::UndoLastEditWaypoint()
{
	if (EditUndoStack.Num() == 0)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange, TEXT("没有可撤销的操作"));
		}
		return;
	}

	// 撤销会重建航点句柄，先清选中避免悬空引用。
	SetEditSelectedWaypoint(nullptr);

	FEditUndoEntry Entry = EditUndoStack.Pop(EAllowShrinking::No);
	const TCHAR* OpDesc = TEXT("操作");

	switch (Entry.Kind)
	{
	case EEditUndoKind::AddWaypoints:
	{
		// 反向：移除刚追加的航点（每条受影响路径末尾一个）。
		OpDesc = TEXT("增添");
		for (const FEditWaypointUndoItem& Item : Entry.Items)
		{
			ADronePathActor* Path = Item.Path.Get();
			if (!IsValid(Path))
			{
				continue;
			}
			// LIFO 保证这些航点仍在末尾；按记录索引移除，退化时移除最后一个。
			const int32 LastIndex = Path->GetWaypointCount() - 1;
			const int32 RemoveIndex = (Item.Index == LastIndex) ? Item.Index : LastIndex;
			if (RemoveIndex >= 1)
			{
				Path->RemoveWaypoint(RemoveIndex);
			}
		}
		break;
	}

	case EEditUndoKind::DeleteWaypoint:
	{
		// 反向：把被删航点原样插回原索引。
		OpDesc = TEXT("删除");
		for (const FEditWaypointUndoItem& Item : Entry.Items)
		{
			ADronePathActor* Path = Item.Path.Get();
			if (IsValid(Path))
			{
				Path->InsertWaypoint(Item.Index, Item.WaypointData);
			}
		}
		break;
	}

	case EEditUndoKind::MoveWaypoints:
	{
		// 反向：把每个航点移回旧世界位置。
		OpDesc = TEXT("移动");
		for (const FEditWaypointUndoItem& Item : Entry.Items)
		{
			ADronePathActor* Path = Item.Path.Get();
			if (IsValid(Path))
			{
				Path->UpdateWaypoint(Item.Index, Item.OldWorldLocation);
			}
		}
		break;
	}
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Cyan,
			FString::Printf(TEXT("已撤销：%s（剩余 %d 步）"), OpDesc, EditUndoStack.Num()));
	}
}

// ---- 框选矩形 Widget 通知 ----
// 通过 UFunction 反射调用蓝图函数，避免 C++ 依赖具体蓝图类

void ADroneOpsPlayerController::NotifyBoxSelectShow()
{
	if (!BoxSelectWidgetInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("NotifyBoxSelectShow: BoxSelectWidgetInstance is null"));
		return;
	}
	UFunction* Func = BoxSelectWidgetInstance->FindFunction(FName("ShowSelectionRect"));
	if (Func)
	{
		BoxSelectWidgetInstance->ProcessEvent(Func, nullptr);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("NotifyBoxSelectShow: Function 'ShowSelectionRect' not found on widget"));
	}
}

void ADroneOpsPlayerController::NotifyBoxSelectUpdate(FVector2D Start, FVector2D End)
{
	if (!BoxSelectWidgetInstance) return;
	UFunction* Func = BoxSelectWidgetInstance->FindFunction(FName("UpdateSelectionRect"));
	if (!Func) return;

	// 参数结构体按蓝图函数签名顺序排列：startpos (Vector2D), endpos (Vector2D)
	struct { FVector2D startpos; FVector2D endpos; } Params{ Start, End };
	BoxSelectWidgetInstance->ProcessEvent(Func, &Params);
}

void ADroneOpsPlayerController::NotifyBoxSelectHide()
{
	if (!BoxSelectWidgetInstance) return;
	UFunction* Func = BoxSelectWidgetInstance->FindFunction(FName("HideSelectionRect"));
	if (Func) BoxSelectWidgetInstance->ProcessEvent(Func, nullptr);
}

TArray<int32> ADroneOpsPlayerController::GetSelectedDroneIdsForDispatch() const
{
	TArray<int32> SelectedIds;
	if (!DroneRegistry)
	{
		return SelectedIds;
	}

	TSet<int32> SeenIds;
	for (const int32 DroneId : DroneRegistry->GetMultiSelectedDrones())
	{
		if (DroneId > 0 && !SeenIds.Contains(DroneId))
		{
			SeenIds.Add(DroneId);
			SelectedIds.Add(DroneId);
		}
	}

	const int32 PrimaryId = DroneRegistry->GetPrimarySelectedDrone();
	if (SelectedIds.IsEmpty() && PrimaryId > 0)
	{
		SelectedIds.Add(PrimaryId);
	}

	SelectedIds.Sort();
	return SelectedIds;
}

bool ADroneOpsPlayerController::ValidateDispatchDrone(int32 DroneId, FString& OutError) const
{
	if (!DroneRegistry)
	{
		OutError = TEXT("无人机注册表不可用");
		return false;
	}

	EDroneControlLockReason LockReason = EDroneControlLockReason::None;
	if (DroneRegistry->IsControlLocked(DroneId, LockReason))
	{
		OutError = FString::Printf(TEXT("无人机 %d 当前不可控，无法派发"), DroneId);
		return false;
	}
	if (!IsValid(ResolveDroneActorById(DroneId)))
	{
		OutError = FString::Printf(TEXT("无人机 %d 的影子机不可用"), DroneId);
		return false;
	}

	return true;
}

FGeographicDispatchResult ADroneOpsPlayerController::AddGeographicWaypointsInEditMode(
	EGeographicCoordinateSystem CoordinateSystem,
	const TArray<FGeographicCoordinate3D>& Coordinates)
{
	FGeographicDispatchResult Result;
	if (!bPathEditMode)
	{
		Result.Message = TEXT("批量坐标仅用于路径编辑");
		return Result;
	}
	if (Coordinates.IsEmpty())
	{
		Result.Message = TEXT("请输入至少一组批量坐标");
		return Result;
	}

	// Transaction phase 1: convert every tuple before touching any path.
	TArray<FVector> ConvertedTargets;
	ConvertedTargets.Reserve(Coordinates.Num());
	for (int32 CoordinateIndex = 0; CoordinateIndex < Coordinates.Num(); ++CoordinateIndex)
	{
		const FGeographicCoordinate3D& Coordinate = Coordinates[CoordinateIndex];
		FVector WorldTarget = FVector::ZeroVector;
		FString ConvertError;
		if (!TryConvertGeographicToWorld(
			CoordinateSystem,
			Coordinate.Longitude,
			Coordinate.Latitude,
			Coordinate.AltitudeMslMeters,
			WorldTarget,
			ConvertError))
		{
			Result.Message = FString::Printf(
				TEXT("第 %d 个坐标转换失败：%s"), CoordinateIndex + 1, *ConvertError);
			return Result;
		}
		ConvertedTargets.Add(WorldTarget);
	}

	struct FPathWaypointWritePlan
	{
		ADronePathActor* PathActor = nullptr;
		int32 InitialWaypointCount = 0;
		TArray<FVector> WorldLocations;
	};

	// Transaction phase 2: validate every active destination and build all relative-formation writes.
	TArray<FPathWaypointWritePlan> WritePlans;
	for (int32 PathIndex = 0; PathIndex < EditingPaths.Num(); ++PathIndex)
	{
		if (!EditingPathActive.IsValidIndex(PathIndex) || !EditingPathActive[PathIndex])
		{
			continue;
		}
		if (!EditingPathOrigins.IsValidIndex(PathIndex) || !IsValid(EditingPaths[PathIndex]))
		{
			Result.Message = FString::Printf(TEXT("第 %d 条编辑路径不可用，未添加任何航点"), PathIndex + 1);
			return Result;
		}

		FPathWaypointWritePlan& Plan = WritePlans.AddDefaulted_GetRef();
		Plan.PathActor = EditingPaths[PathIndex];
		Plan.InitialWaypointCount = Plan.PathActor->GetWaypointCount();
		Plan.WorldLocations.Reserve(ConvertedTargets.Num());
		for (const FVector& WorldTarget : ConvertedTargets)
		{
			// Exactly the same formation rule as AddWaypointToAllEditingPaths:
			// each path origin receives the target offset from the shared edit reference origin.
			const FVector Offset = WorldTarget - EditFormationRefOrigin;
			Plan.WorldLocations.Add(EditingPathOrigins[PathIndex] + Offset);
		}
	}

	if (WritePlans.IsEmpty())
	{
		Result.Message = TEXT("当前没有可添加航点的编辑路径");
		return Result;
	}

	// Transaction phase 3: append in tuple order. Roll back every path on an unexpected write failure.
	bool bWriteSucceeded = true;
	for (FPathWaypointWritePlan& Plan : WritePlans)
	{
		for (const FVector& WorldLocation : Plan.WorldLocations)
		{
			if (Plan.PathActor->AddWaypoint(WorldLocation, EditDefaultSegmentSpeed) == INDEX_NONE)
			{
				bWriteSucceeded = false;
				break;
			}
		}
		if (!bWriteSucceeded)
		{
			break;
		}
	}

	if (!bWriteSucceeded)
	{
		for (FPathWaypointWritePlan& Plan : WritePlans)
		{
			while (IsValid(Plan.PathActor) && Plan.PathActor->GetWaypointCount() > Plan.InitialWaypointCount)
			{
				Plan.PathActor->RemoveWaypoint(Plan.PathActor->GetWaypointCount() - 1);
			}
		}
		Result.Message = TEXT("批量航点写入失败，已回滚全部路径");
		return Result;
	}

	Result.bSuccess = true;
	Result.DispatchedCount = WritePlans.Num() * ConvertedTargets.Num();
	Result.Message = FString::Printf(
		TEXT("已为 %d 条路径各添加 %d 个航点"), WritePlans.Num(), ConvertedTargets.Num());
	UE_LOG(LogTemp, Log, TEXT("[PathEdit] Batch added %d geographic waypoint(s) to %d active path(s)"),
		ConvertedTargets.Num(), WritePlans.Num());
	return Result;
}

FGeographicDispatchResult ADroneOpsPlayerController::BuildPerDroneGeographicDispatchPlan(
	EGeographicCoordinateSystem CoordinateSystem,
	const TArray<FDroneGeographicTarget>& Targets,
	bool bForDispatch,
	TArray<FGeographicDispatchSlot>& OutSlots) const
{
	FGeographicDispatchResult Result;
	OutSlots.Reset();

	if (!DroneRegistry)
	{
		Result.Message = TEXT("无人机注册表不可用");
		return Result;
	}

	UDroneNetworkManager* NetworkManager = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDroneNetworkManager>()
		: nullptr;
	if (bForDispatch
		&& (!NetworkManager || !NetworkManager->CanSendToBackend()
			|| !NetworkManager->GetWebSocketClient()
			|| !NetworkManager->GetWebSocketClient()->IsConnected()))
	{
		Result.Message = TEXT("后端连接未就绪");
		return Result;
	}

	const TArray<int32> SelectedIds = GetSelectedDroneIdsForDispatch();
	if (SelectedIds.IsEmpty())
	{
		Result.Message = TEXT("请先选择无人机");
		return Result;
	}

	TMap<int32, FGeographicCoordinate3D> TargetByDroneId;
	for (const FDroneGeographicTarget& Target : Targets)
	{
		if (!SelectedIds.Contains(Target.DroneId))
		{
			Result.Message = FString::Printf(TEXT("无人机 %d 未被选中，不能加入本次逐机派发"), Target.DroneId);
			return Result;
		}
		if (TargetByDroneId.Contains(Target.DroneId))
		{
			Result.Message = FString::Printf(TEXT("无人机 %d 存在重复目标"), Target.DroneId);
			return Result;
		}
		TargetByDroneId.Add(Target.DroneId, Target.Coordinate);
	}

	for (const int32 DroneId : SelectedIds)
	{
		if (!TargetByDroneId.Contains(DroneId))
		{
			Result.Message = FString::Printf(TEXT("请填写无人机 %d 的目标坐标"), DroneId);
			return Result;
		}
	}
	if (TargetByDroneId.Num() != SelectedIds.Num())
	{
		Result.Message = TEXT("逐机目标必须与当前选中的无人机一一对应");
		return Result;
	}

	int32 PrimaryId = DroneRegistry->GetPrimarySelectedDrone();
	if (!SelectedIds.Contains(PrimaryId))
	{
		PrimaryId = SelectedIds[0];
	}

	for (const int32 DroneId : SelectedIds)
	{
		if (!ValidateDispatchDrone(DroneId, Result.Message))
		{
			return Result;
		}

		const FGeographicCoordinate3D* Coordinate = TargetByDroneId.Find(DroneId);
		check(Coordinate);

		FVector WorldTarget = FVector::ZeroVector;
		FString ConvertError;
		if (!TryConvertGeographicToWorld(
			CoordinateSystem,
			Coordinate->Longitude,
			Coordinate->Latitude,
			Coordinate->AltitudeMslMeters,
			WorldTarget,
			ConvertError))
		{
			Result.Message = FString::Printf(TEXT("无人机 %d：%s"), DroneId, *ConvertError);
			return Result;
		}

		FGeographicDispatchSlot Slot;
		Slot.DroneId = DroneId;
		Slot.WorldTarget = WorldTarget;
		Slot.bIsPrimary = DroneId == PrimaryId;
		if (bForDispatch)
		{
			if (!TryBuildGeographicBackendTarget(
				DroneId,
				*Coordinate,
				FVector::ZeroVector,
				NetworkManager,
				Slot.BackendRelativeTarget,
				Result.Message))
			{
				OutSlots.Reset();
				return Result;
			}
			Slot.bHasBackendRelativeTarget = true;
		}
		OutSlots.Add(Slot);
	}

	Result.bSuccess = true;
	Result.DispatchedCount = OutSlots.Num();
	Result.Message = bForDispatch ? TEXT("可以在UE内逐机派发") : TEXT("可以预览逐机目标");
	return Result;
}

FGeographicDispatchResult ADroneOpsPlayerController::ValidatePerDroneGeographicTargets(
	EGeographicCoordinateSystem CoordinateSystem,
	const TArray<FDroneGeographicTarget>& Targets,
	bool bForDispatch) const
{
	TArray<FGeographicDispatchSlot> Slots;
	return BuildPerDroneGeographicDispatchPlan(CoordinateSystem, Targets, bForDispatch, Slots);
}

FGeographicDispatchResult ADroneOpsPlayerController::DispatchPerDroneGeographicTargets(
	EGeographicCoordinateSystem CoordinateSystem,
	const TArray<FDroneGeographicTarget>& Targets,
	bool bPreviewOnly)
{
	TArray<FGeographicDispatchSlot> Slots;
	FGeographicDispatchResult Result = BuildPerDroneGeographicDispatchPlan(
		CoordinateSystem, Targets, !bPreviewOnly, Slots);
	if (!Result.bSuccess)
	{
		return Result;
	}

	if (bPreviewOnly)
	{
		ActiveGeographicPreviewSlots = Slots;
		Result.Message = FString::Printf(TEXT("已预览 %d 个逐机目标点"), Slots.Num());
		return Result;
	}

	UDroneNetworkManager* NetworkManager = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDroneNetworkManager>()
		: nullptr;
	ActiveGeographicPreviewSlots.Reset();
	for (const FGeographicDispatchSlot& Slot : Slots)
	{
		check(Slot.bHasBackendRelativeTarget);
		NetworkManager->SendMoveCommand(
			Slot.DroneId,
			Slot.BackendRelativeTarget,
			DroneRegistry->GetDroneCommandMode(Slot.DroneId));
		if (AMultiDroneCharacter* Shadow = Cast<AMultiDroneCharacter>(ResolveDroneActorById(Slot.DroneId)))
		{
			Shadow->MoveToTarget3D(Slot.WorldTarget);
		}
	}
	Result.Message = FString::Printf(TEXT("已派发 %d 个逐机目标"), Slots.Num());
	UE_LOG(LogTemp, Log, TEXT("DispatchPerDroneGeographicTargets: dispatched %d exact targets"), Slots.Num());
	return Result;
}
