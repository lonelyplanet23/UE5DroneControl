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
#include "UE5DroneControlCharacter.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/EngineTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/OverlapResult.h"
#include "UI/UIManagerBlueprintLibrary.h"
#include "UI/SequenceDispatchPanelWidget.h"
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
			HUDWidget->AddToViewport();
			UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: HUD widget created and added to viewport"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("DroneOpsPlayerController: Failed to create HUD widget"));
		}
	}

	// Create Sequence Dispatch Panel (persistent, bottom-right)
	UUIManagerBlueprintLibrary::ShowSequenceDispatchPanel(this);

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

	// 路径编辑模式下：驱动自由相机 + gizmo 拖拽，跳过常规相机/悬停逻辑
	if (bPathEditMode)
	{
		if (EditSelectedWaypoint != nullptr && !IsValid(EditSelectedWaypoint))
		{
			SetEditSelectedWaypoint(nullptr);
		}

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
	if (WasInputKeyJustPressed(EKeys::SpaceBar))
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

	// 敌对目标发现检测

	if (!bPathEditMode)
	{
		CheckHostileTargetDetection();

		// 检测目标是否离开发现范围
		if (UHostileTargetManager* Manager = GetHostileTargetManager())
		{
			Manager->ResetUndetectedTargets();
		}
	}
}

void ADroneOpsPlayerController::OnPrimaryClick()
{
	// 路径编辑模式：地图点击加点 / gizmo 拖拽，优先于常规选择与派发
	if (bPathEditMode)
	{
		HandleEditModePressed();
		return;
	}

	// 面板处于交互状态时不处理游戏点击
	if (USequenceDispatchPanelWidget::IsPanelInteractive())
	{
		return;
	}

	// Shift+左键：放置敌对目标点

	if (bShiftHeld)
	{
		SpawnHostileTarget();
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
	if (bPathEditMode)
	{
		HandleEditModeReleased();
	}
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
	if (!DroneRegistry || !DroneInfoPanelWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[FR-03] OpenDroneInfoPanel: DroneRegistry=%p, WidgetClass=%p"),
			DroneRegistry.Get(), DroneInfoPanelWidgetClass.Get());
		return;
	}

	// Close existing panel if any
	if (CurrentDroneInfoPanel && CurrentDroneInfoPanel->IsValidLowLevel())
	{
		CurrentDroneInfoPanel->RemoveFromParent();
		CurrentDroneInfoPanel = nullptr;
	}

	// Get telemetry snapshot
	FDroneTelemetrySnapshot Snapshot;
	bool bGotTelemetry = DroneRegistry->GetTelemetry(DroneId, Snapshot);
	Snapshot.DroneId = DroneId;
	UE_LOG(LogTemp, Log, TEXT("[FR-03] GetTelemetry for DroneId=%d, result=%d"), DroneId, bGotTelemetry);

	// Get drone descriptor
	FDroneDescriptor Descriptor;
	bool bGotDescriptor = DroneRegistry->GetDroneDescriptor(DroneId, Descriptor);
	UE_LOG(LogTemp, Log, TEXT("[FR-03] GetDroneDescriptor for DroneId=%d, name=%s, result=%d"),
		DroneId, *Descriptor.Name, bGotDescriptor);

	// Create widget instance
	UUserWidget* NewPanel = CreateWidget(this, DroneInfoPanelWidgetClass);
	if (!NewPanel)
	{
		UE_LOG(LogTemp, Error, TEXT("[FR-03] Failed to create info panel widget"));
		return;
	}

	// Call UpdateFromSnapshot on the widget
	// The function is BlueprintCallable, so we can call it via reflection
	UFunction* Func = NewPanel->FindFunction(FName("UpdateFromSnapshot"));
	if (Func)
	{
		// Prepare parameters: struct Snapshot + string DroneName
		TArray<uint8> ParamsBuffer;
		ParamsBuffer.SetNum(Func->ParmsSize);

		// First parameter is Snapshot
		FDroneTelemetrySnapshot* pSnapshot = reinterpret_cast<FDroneTelemetrySnapshot*>(ParamsBuffer.GetData());
		*pSnapshot = Snapshot;

		// Second parameter is DroneName (FString)
		FString* pName = reinterpret_cast<FString*>(ParamsBuffer.GetData() + sizeof(FDroneTelemetrySnapshot));
		*pName = Descriptor.Name;

		// Call the function
		NewPanel->ProcessEvent(Func, ParamsBuffer.GetData());
		UE_LOG(LogTemp, Log, TEXT("[FR-03] Called UpdateFromSnapshot with DroneId=%d, name=%s"), DroneId, *Descriptor.Name);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[FR-03] UpdateFromSnapshot function not found on widget"));
	}

	// Add to viewport at top-left
	NewPanel->AddToViewport();
	NewPanel->SetPositionInViewport(FVector2D(10, 10));

	// Save reference
	CurrentDroneInfoPanel = NewPanel;
	UE_LOG(LogTemp, Log, TEXT("[FR-03] Info panel opened successfully for DroneId=%d"), DroneId);
}

void ADroneOpsPlayerController::OnFreeCamToggle()
{
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

		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		SetInputMode(InputMode);
	}
}

void ADroneOpsPlayerController::OnShiftReleased()
{
	bShiftHeld = false;

	if (CameraModeState.CameraMode == EDroneCameraMode::Free)
	{
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());
	}
}

void ADroneOpsPlayerController::HandleMapClick(const FVector& WorldLocation)
{
	if (!SelectedDroneActor || SelectedDroneId <= 0)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange, TEXT("未选中无人机，无法派发目标点"));
		}
		return;
	}

	if (DroneRegistry)
	{
		EDroneControlLockReason LockReason = EDroneControlLockReason::None;
		if (DroneRegistry->IsControlLocked(SelectedDroneId, LockReason))
		{
			UE_LOG(LogTemp, Warning, TEXT("Drone %d is locked, map target ignored"), SelectedDroneId);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("当前无人机不可控，目标点派发失败"));
			}
			return;
		}
	}

	AActor* TargetActor = SelectedDroneActor.Get();
	if (!IsValid(TargetActor))
	{
		TargetActor = ResolveDroneActorById(SelectedDroneId);
		SelectedDroneActor = TargetActor;
	}

	int32 EffectiveDroneId = SelectedDroneId;
	if (TargetActor)
	{
		const int32 ActorDroneId = ResolveDroneIdFromActor(TargetActor);
		if (ActorDroneId > 0)
		{
			EffectiveDroneId = ActorDroneId;
			SelectedDroneId = ActorDroneId;
		}
	}

	// SetClickTargetLocation 内部（AMultiDroneCharacter 重写版本）已驱动 WebSocket 发送
	if (AUE5DroneControlCharacter* SelectedChar = Cast<AUE5DroneControlCharacter>(TargetActor))
	{
		SelectedChar->SetClickTargetLocation(WorldLocation, 1);
	}

	const FString ActorName = TargetActor ? TargetActor->GetName() : FString::Printf(TEXT("Drone-%d"), EffectiveDroneId);
	UE_LOG(LogTemp, Log, TEXT("FR2 Dispatch: Actor=%s SelectedDroneId=%d EffectiveDroneId=%d"),
		*ActorName, SelectedDroneId, EffectiveDroneId);

	// Dispatch to all other multi-selected drones
	if (DroneRegistry)
	{
		TArray<int32> MultiIds = DroneRegistry->GetMultiSelectedDrones();
		constexpr float SpacingCm = 100.0f;
		int32 NextIndex = 1;
		for (int32 Id : MultiIds)
		{
			if (Id == EffectiveDroneId) continue;

			EDroneControlLockReason LockReason = EDroneControlLockReason::None;
			if (DroneRegistry->IsControlLocked(Id, LockReason)) continue;

			const FVector SlotLocation = WorldLocation + ComputeMultiDispatchOffset(NextIndex, SpacingCm);
			++NextIndex;

			if (AActor* OtherActor = ResolveDroneActorById(Id))
			{
				if (AUE5DroneControlCharacter* OtherChar = Cast<AUE5DroneControlCharacter>(OtherActor))
				{
					OtherChar->SetClickTargetLocation(SlotLocation, 1);
				}
			}
		}
		if (MultiIds.Num() > 1)
		{
			const int32 Count = MultiIds.Num();
			UE_LOG(LogTemp, Log, TEXT("Multi-dispatch to %d drones -> (%.0f, %.0f, %.0f)"),
				Count, WorldLocation.X, WorldLocation.Y, WorldLocation.Z);
		}
	}

	const FString TargetName = ActorName;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
			FString::Printf(TEXT("目标点已派发 -> %s (%.0f, %.0f, %.0f)"),
				*TargetName, WorldLocation.X, WorldLocation.Y, WorldLocation.Z));
	}

	DrawDebugSphere(GetWorld(), WorldLocation, 50.0f, 12, FColor::Green, false, 3.0f);
}

void ADroneOpsPlayerController::HandleDroneClick(AActor* ClickedActor)
{
	if (!ClickedActor)
	{
		return;
	}

	if (!IsFr2ControllableDroneActor(ClickedActor))
	{
		return;
	}

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow,
			FString::Printf(TEXT("Selected: %s - click map to assign target"), *ClickedActor->GetName()));
	}

	if (!DroneRegistry)
	{
		SelectedDroneId = 0;
		SelectedDroneActor = ClickedActor;
		return;
	}

	const int32 ClickedDroneId = ResolveDroneIdFromActor(ClickedActor);
	if (ClickedDroneId <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Clicked drone has invalid DroneId: %s"), *ClickedActor->GetName());
		return;
	}

	EDroneControlLockReason LockReason = EDroneControlLockReason::None;
	if (DroneRegistry->IsControlLocked(ClickedDroneId, LockReason) &&
		LockReason == EDroneControlLockReason::Offline)
	{
		UE_LOG(LogTemp, Warning, TEXT("Drone %d is offline, cannot select"), ClickedDroneId);
		return;
	}

	if (SelectedDroneActor && SelectedDroneActor != ClickedActor && !bShiftHeld)
	{
		SetDronePrimarySelectedState(SelectedDroneActor.Get(), false);
	}

	SelectedDroneId = ClickedDroneId;
	SelectedDroneActor = ClickedActor;

	if (bShiftHeld && DroneRegistry)
	{
		// Shift+click: toggle this drone in the multi-selection
		TArray<int32> CurrentMulti = DroneRegistry->GetMultiSelectedDrones();
		if (CurrentMulti.Contains(ClickedDroneId))
		{
			CurrentMulti.Remove(ClickedDroneId);
			SetDronePrimarySelectedState(ClickedActor, false);
		}
		else
		{
			CurrentMulti.Add(ClickedDroneId);
			SetDronePrimarySelectedState(ClickedActor, true);
		}
		DroneRegistry->SetMultiSelectedDrones(CurrentMulti);

		// Keep primary on the last clicked drone (or first remaining)
		if (!CurrentMulti.IsEmpty())
		{
			DroneRegistry->SetPrimarySelectedDrone(CurrentMulti.Last());
		}
	}
	else
	{
		// Normal click: clear multi-selection, select only this drone
		DroneRegistry->SetPrimarySelectedDrone(ClickedDroneId);
		SetDronePrimarySelectedState(ClickedActor, true);
	}

	FDroneDescriptor Desc;
	FString DisplayName = FString::Printf(TEXT("Drone-%d"), ClickedDroneId);
	if (DroneRegistry->GetDroneDescriptor(ClickedDroneId, Desc))
	{
		DisplayName = Desc.Name;
	}

	UE_LOG(LogTemp, Log, TEXT("Primary selected: %s (ID=%d)"), *DisplayName, ClickedDroneId);

	// 跟随模式下，直接切换到点击的无人机Actor，不经过Registry查找（避免DroneId冲突导致视角错位）
	if (CameraModeState.CameraMode == EDroneCameraMode::Follow)
	{
		LastFollowViewTarget = ClickedActor;
		CameraModeState.FollowDroneId = ClickedDroneId;
		LastFollowedDroneId = ClickedDroneId;
		CameraModeState.LastFollowLocation = ClickedActor->GetActorLocation();
		CameraModeState.LastFollowRotation = ClickedActor->GetActorRotation();
		SetViewTargetWithBlend(ClickedActor, 0.35f);
		UE_LOG(LogTemp, Log, TEXT("Switched to Follow Camera (FollowDroneId=%d, Target=%s)"),
			ClickedDroneId, *ClickedActor->GetName());
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
	if (!NetworkManager || !NetworkManager->GetWebSocketClient() || !NetworkManager->GetWebSocketClient()->IsConnected())
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
	if (!HostileTargetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[HostileTarget] HostileTargetClass not set in PlayerController"));
		return;
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
	AHostileTargetActor* NewTarget = World->SpawnActor<AHostileTargetActor>(
		HostileTargetClass, WorldLocation, FRotator::ZeroRotator, SpawnParams);

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

		// 防重：同一目标只弹窗一次
		if (PendingAttackConfirmTargets.Contains(TargetId))
		{
			continue;
		}

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

    PendingAttackConfirmTargets.Add(TargetId);

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
		// 用户点击“不攻击”：什么都不做，目标继续存在，路径继续
		// PendingAttackConfirmTargets 保留，目标保持 bIsDiscovered = true
		// 直到目标离开检测范围后由 ResetUndetectedTargets 重置
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
	}

	if (EditingPaths.IsEmpty())
	{
		return false;
	}

	EditFormationRefOrigin = EditingPathOrigins[0];
	bPathEditMode = true;
	SetEditSelectedWaypoint(nullptr);
	EditActiveAxis = EGizmoAxis::None;
	bEditDraggingWaypoint = false;

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
	bPathEditMode = false;

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
		SetEditSelectedWaypoint(HitWaypoint);

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
		HitWaypoint->BeginDeferredPathUpdate();

		double MouseX = 0.0;
		double MouseY = 0.0;
		if (GetMousePosition(MouseX, MouseY))
		{
			EditLastMouseScreenPos = FVector2D(MouseX, MouseY);
		}
		return;
	}

	// 命中地面/地形 → 给所有临时路径追加航点（编队平移）
	SetEditSelectedWaypoint(nullptr);
	AddWaypointToAllEditingPaths(HitResult.Location);
}

void ADroneOpsPlayerController::HandleEditModeReleased()
{
	if (bEditDraggingWaypoint)
	{
		if (IsValid(EditSelectedWaypoint))
		{
			EditSelectedWaypoint->EndDeferredPathUpdate(true);
		}
	}

	bEditDraggingWaypoint = false;
	SetIgnoreLookInput(false);
	SetIgnoreMoveInput(false);
	SetEditActiveAxis(EGizmoAxis::None);
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

	EditSelectedWaypoint->MoveAlongGizmoAxis(EditActiveAxis, DragDelta * EditGizmoDragSensitivity);
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
	if (EditSelectedWaypoint == NewWaypoint)
	{
		return;
	}

	if (IsValid(EditSelectedWaypoint))
	{
		EditSelectedWaypoint->SetSelected(false);
		EditSelectedWaypoint->SetActiveGizmoAxis(EGizmoAxis::None);
	}

	EditSelectedWaypoint = NewWaypoint;
	EditActiveAxis = EGizmoAxis::None;

	if (IsValid(EditSelectedWaypoint))
	{
		EditSelectedWaypoint->SetSelected(true);
		EditSelectedWaypoint->SetActiveGizmoAxis(EGizmoAxis::None);
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
	if (!EditingPaths.Contains(WaypointActor->PathActor))
	{
		return false;
	}

	return !WaypointActor->PathActor->IsMovementActive();
}

void ADroneOpsPlayerController::AddWaypointToAllEditingPaths(const FVector& WorldLocation)
{
	const FVector Offset = WorldLocation - EditFormationRefOrigin;

	for (int32 i = 0; i < EditingPaths.Num(); ++i)
	{
		ADronePathActor* PathActor = EditingPaths[i];
		if (!IsValid(PathActor) || !EditingPathOrigins.IsValidIndex(i))
		{
			continue;
		}

		// 编队平移：每条路径以自身首航点为基准，叠加相同偏移
		const FVector NewWaypointLocation = EditingPathOrigins[i] + Offset;
		PathActor->AddWaypoint(NewWaypointLocation, EditDefaultSegmentSpeed);
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
	if (!IsValid(EditSelectedWaypoint) || !CanInteractWithEditWaypoint(EditSelectedWaypoint))
	{
		return;
	}

	ADronePathActor* OwningPath = EditSelectedWaypoint->PathActor;
	const int32 WpIndex = EditSelectedWaypoint->WaypointIndex;

	// 首航点（影子机起点）禁止删除
	if (WpIndex <= 0)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange, TEXT("首航点（起点）不可删除"));
		}
		return;
	}

	// 先清选中，避免删除后悬空引用；再从路径移除该航点
	SetEditSelectedWaypoint(nullptr);
	if (IsValid(OwningPath))
	{
		OwningPath->RemoveWaypoint(WpIndex);
	}
}

void ADroneOpsPlayerController::UndoLastEditWaypoint()
{
	// 逐条路径移除各自最后一个航点，但保留首航点；无可撤销时提示
	bool bRemovedAny = false;
	for (ADronePathActor* PathActor : EditingPaths)
	{
		if (!IsValid(PathActor))
		{
			continue;
		}

		const int32 Count = PathActor->GetWaypointCount();
		if (Count > 1)
		{
			// 若选中的航点正好被移除，先清选中
			if (IsValid(EditSelectedWaypoint)
				&& EditSelectedWaypoint->PathActor == PathActor
				&& EditSelectedWaypoint->WaypointIndex == Count - 1)
			{
				SetEditSelectedWaypoint(nullptr);
			}
			PathActor->RemoveLastWaypoint();
			bRemovedAny = true;
		}
	}

	if (!bRemovedAny && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Orange, TEXT("没有可撤销的航点"));
	}
}