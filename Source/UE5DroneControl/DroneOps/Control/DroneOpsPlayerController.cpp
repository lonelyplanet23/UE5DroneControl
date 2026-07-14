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
		InputComponent->BindKey(EKeys::MiddleMouseButton, IE_Pressed, this, &ADroneOpsPlayerController::OnShowInfo);
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
	if (!DroneRegistry)
	{
		return 0;
	}

	TSet<int32> UniqueSelectedIds;
	for (const int32 DroneId : DroneRegistry->GetMultiSelectedDrones())
	{
		if (DroneId > 0)
		{
			UniqueSelectedIds.Add(DroneId);
		}
	}

	if (!UniqueSelectedIds.IsEmpty())
	{
		return UniqueSelectedIds.Num();
	}

	return DroneRegistry->GetPrimarySelectedDrone() > 0 ? 1 : 0;
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
