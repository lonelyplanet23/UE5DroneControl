// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOpsPlayerController.h"
#include "InputCoreTypes.h"
#include "Camera/CameraActor.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/ICoordinateService.h"
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
#include "UI/GeographicTargetPanelWidget.h"
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
	struct FDroneInfoPanelUpdateParams
	{
		FDroneTelemetrySnapshot Snapshot;
		FString DroneName;
	};

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

	// Keep the Blueprint property overridable, but provide the project panel as
	// the C++ fallback so middle-click works when a controller BP has no value.
	static ConstructorHelpers::FClassFinder<UUserWidget> DroneInfoPanelClass(
		TEXT("/Game/DroneOps/UI/WBP_DroneInfoPanel"));
	if (DroneInfoPanelClass.Succeeded())
	{
		DroneInfoPanelWidgetClass = DroneInfoPanelClass.Class;
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

	// Create Geographic Target Panel (persistent; coordinate icon button toggles the body)
	UUIManagerBlueprintLibrary::ShowGeographicTargetPanel(this);

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
	const TArray<int32> SelectedDroneIds = DroneRegistry->GetMultiSelectedDrones();
	for (const int32 DroneId : SelectedDroneIds)
	{
		EDroneControlLockReason LockReason = EDroneControlLockReason::None;
		if (DroneRegistry->IsControlLocked(DroneId, LockReason))
		{
			continue;
		}

		AMultiDroneCharacter* ShadowDrone = Cast<AMultiDroneCharacter>(DroneRegistry->GetSenderPawn(DroneId));
		if (!IsValid(ShadowDrone))
		{
			continue;
		}

		ShadowDrone->SetVerticalMoveInput(Direction);
		CurrentControlledDrones.AddUnique(TWeakObjectPtr<AMultiDroneCharacter>(ShadowDrone));
	}

	// Selection or lock state may change while a key is held. Stop actors that
	// are no longer part of the canonical registry selection.
	for (const TWeakObjectPtr<AMultiDroneCharacter>& PreviousDrone : VerticallyControlledDrones)
	{
		if (AMultiDroneCharacter* ShadowDrone = PreviousDrone.Get())
		{
			if (!CurrentControlledDrones.Contains(PreviousDrone))
			{
				ShadowDrone->StopVerticalMove(false);
			}
		}
	}

	VerticallyControlledDrones = MoveTemp(CurrentControlledDrones);
}

void ADroneOpsPlayerController::StopSelectedDroneVerticalControl(bool bSendFinalCommand)
{
	for (const TWeakObjectPtr<AMultiDroneCharacter>& ControlledDrone : VerticallyControlledDrones)
	{
		if (AMultiDroneCharacter* ShadowDrone = ControlledDrone.Get())
		{
			ShadowDrone->StopVerticalMove(bSendFinalCommand);
		}
	}
	VerticallyControlledDrones.Empty();
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
	if (USequenceDispatchPanelWidget::IsPanelInteractive() || UGeographicTargetPanelWidget::IsPanelInteractive())
	{
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
	if (!DroneRegistry || !DroneInfoPanelWidgetClass || DroneId <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[FR-03] OpenDroneInfoPanel: DroneRegistry=%p, WidgetClass=%p"),
			DroneRegistry.Get(), DroneInfoPanelWidgetClass.Get());
		return;
	}

	CloseCurrentDroneInfoPanel();

	UUserWidget* NewPanel = CreateWidget(this, DroneInfoPanelWidgetClass);
	if (!NewPanel)
	{
		UE_LOG(LogTemp, Error, TEXT("[FR-03] Failed to create info panel widget"));
		return;
	}

	if (!NewPanel->FindFunction(FName("UpdateFromSnapshot")))
	{
		UE_LOG(LogTemp, Error, TEXT("[FR-03] UpdateFromSnapshot function not found on widget"));
		return;
	}

	NewPanel->AddToViewport();
	NewPanel->SetPositionInViewport(FVector2D(10, 10));
	CurrentDroneInfoPanel = NewPanel;
	CurrentDroneInfoDroneId = DroneId;

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
	DroneRegistry->GetTelemetry(CurrentDroneInfoDroneId, Snapshot);
	Snapshot.DroneId = CurrentDroneInfoDroneId;

	FString DroneName = FString::Printf(TEXT("Drone-%d"), CurrentDroneInfoDroneId);
	FDroneDescriptor Descriptor;
	if (DroneRegistry->GetDroneDescriptor(CurrentDroneInfoDroneId, Descriptor) && !Descriptor.Name.IsEmpty())
	{
		DroneName = Descriptor.Name;
	}

	UFunction* UpdateFunction = CurrentDroneInfoPanel->FindFunction(FName("UpdateFromSnapshot"));
	if (!UpdateFunction || UpdateFunction->ParmsSize != sizeof(FDroneInfoPanelUpdateParams))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[FR-03] UpdateFromSnapshot signature mismatch: function=%p, expected=%d, actual=%d"),
			UpdateFunction,
			static_cast<int32>(sizeof(FDroneInfoPanelUpdateParams)),
			UpdateFunction ? UpdateFunction->ParmsSize : 0);
		CloseCurrentDroneInfoPanel();
		return;
	}

	FDroneInfoPanelUpdateParams Params;
	Params.Snapshot = Snapshot;
	Params.DroneName = MoveTemp(DroneName);
	CurrentDroneInfoPanel->ProcessEvent(UpdateFunction, &Params);
}

void ADroneOpsPlayerController::CloseCurrentDroneInfoPanel()
{
	GetWorldTimerManager().ClearTimer(DroneInfoRefreshTimerHandle);
	if (IsValid(CurrentDroneInfoPanel) && CurrentDroneInfoPanel->IsInViewport())
	{
		CurrentDroneInfoPanel->RemoveFromParent();
	}
	CurrentDroneInfoPanel = nullptr;
	CurrentDroneInfoDroneId = 0;
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

int32 ADroneOpsPlayerController::GetSelectedDroneCountForDispatch() const
{
	return GetSelectedDroneIdsForDispatch().Num();
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

<<<<<<< HEAD
=======
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
	const bool bBackendConnected = NetworkManager && NetworkManager->GetWebSocketClient()
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

>>>>>>> 77cb3cc (7.22需求)
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

	// Dispatch is atomic from the UI's perspective: validate every selected drone before sending
	// the first command. Never silently skip a locked or unanchored secondary drone because doing so
	// would change the stable DroneId-to-spiral-slot mapping.
	if (bForDispatch)
	{
		if (!NetworkManager || !NetworkManager->GetWebSocketClient()
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
		FGeographicDispatchSlot Slot;
		Slot.DroneId = Id;
		Slot.bIsPrimary = (i == 0);
		Slot.WorldTarget = BaseTarget + ComputeMultiDispatchOffset(SlotIndex, SpacingCm);
		OutSlots.Add(Slot);
		++SlotIndex;
	}

	Result.bSuccess = true;
	Result.DispatchedCount = OutSlots.Num();
	Result.Message = bForDispatch ? TEXT("可以派发") : TEXT("可以预览");
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
		OutSlots.Add(Slot);
	}

	Result.bSuccess = true;
	Result.DispatchedCount = OutSlots.Num();
	Result.Message = bForDispatch ? TEXT("可以在UE内逐机派发") : TEXT("可以预览逐机目标");
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

FGeographicDispatchResult ADroneOpsPlayerController::ValidatePerDroneGeographicTargets(
	EGeographicCoordinateSystem CoordinateSystem,
	const TArray<FDroneGeographicTarget>& Targets,
	bool bForDispatch) const
{
	TArray<FGeographicDispatchSlot> Slots;
	return BuildPerDroneGeographicDispatchPlan(CoordinateSystem, Targets, bForDispatch, Slots);
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
		const ARealTimeDroneReceiver* Receiver = CastChecked<ARealTimeDroneReceiver>(
			DroneRegistry->GetReceiverActor(Slot.DroneId));
		const FVector SendLocation = Slot.WorldTarget - Receiver->AnchorWorldLocation;
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

	Result = ExecuteWorldDispatchPlan(Slots);
	if (Result.bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("DispatchPerDroneGeographicTargets: dispatched %d exact targets"), Slots.Num());
	}
	return Result;
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
			const int32 NewPrimaryId = CurrentMulti.Last();
			DroneRegistry->SetPrimarySelectedDrone(NewPrimaryId);
			SelectedDroneId = NewPrimaryId;
			SelectedDroneActor = ResolveDroneActorById(NewPrimaryId);
		}
		else
		{
			SelectedDroneId = 0;
			SelectedDroneActor = nullptr;
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
