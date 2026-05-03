#include "DroneRuntimeInteractionPlayerController.h"

#include "InputCoreTypes.h"
#include "../PathEditor/DronePathActor.h"
#include "../PathEditor/DroneWaypointActor.h"

ADroneRuntimeInteractionPlayerController::ADroneRuntimeInteractionPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

void ADroneRuntimeInteractionPlayerController::SetEditMode(bool bEnableEditMode)
{
	if (bIsEditMode == bEnableEditMode)
	{
		if (!bIsEditMode)
		{
			ClearSelectedWaypoint();
		}
		return;
	}

	bIsEditMode = bEnableEditMode;

	if (!bIsEditMode)
	{
		HandleLeftMouseReleased();
		ClearSelectedWaypoint();
	}
}

void ADroneRuntimeInteractionPlayerController::ToggleEditMode()
{
	SetEditMode(!bIsEditMode);
}

ADroneWaypointActor* ADroneRuntimeInteractionPlayerController::GetSelectedWaypoint() const
{
	return SelectedWaypoint;
}

void ADroneRuntimeInteractionPlayerController::SetSelectedWaypoint(ADroneWaypointActor* NewWaypoint)
{
	if (SelectedWaypoint == NewWaypoint)
	{
		return;
	}

	if (IsValid(SelectedWaypoint))
	{
		SelectedWaypoint->SetSelected(false);
		SelectedWaypoint->SetActiveGizmoAxis(EGizmoAxis::None);
	}

	SelectedWaypoint = NewWaypoint;
	ActiveAxis = EGizmoAxis::None;

	if (IsValid(SelectedWaypoint))
	{
		SelectedWaypoint->SetSelected(true);
		SelectedWaypoint->SetActiveGizmoAxis(EGizmoAxis::None);
	}

	OnWaypointSelected.Broadcast(SelectedWaypoint);
}

void ADroneRuntimeInteractionPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ConfigureRuntimeMouseInteraction();
}

void ADroneRuntimeInteractionPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent == nullptr)
	{
		return;
	}

	FInputChord ToggleEditModeChord(EKeys::One);
	ToggleEditModeChord.bCtrl = true;
	InputComponent->BindKey(ToggleEditModeChord, IE_Pressed, this, &ADroneRuntimeInteractionPlayerController::HandleToggleEditMode);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ADroneRuntimeInteractionPlayerController::HandleLeftMousePressed);
	InputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &ADroneRuntimeInteractionPlayerController::HandleLeftMouseReleased);
}

void ADroneRuntimeInteractionPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	if (SelectedWaypoint != nullptr && !IsValid(SelectedWaypoint))
	{
		SetSelectedWaypoint(nullptr);
	}

	if (!bIsEditMode)
	{
		if (bIsDraggingWaypoint)
		{
			HandleLeftMouseReleased();
		}

		if (SelectedWaypoint != nullptr)
		{
			ClearSelectedWaypoint();
		}

		return;
	}

	if (bIsDraggingWaypoint)
	{
		UpdateDraggedWaypointLocation();
		return;
	}
}

void ADroneRuntimeInteractionPlayerController::ConfigureRuntimeMouseInteraction()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	SetInputMode(InputMode);
}

void ADroneRuntimeInteractionPlayerController::HandleToggleEditMode()
{
	ToggleEditMode();
}

void ADroneRuntimeInteractionPlayerController::HandleLeftMousePressed()
{
	if (!bIsEditMode || bIsDraggingWaypoint)
	{
		return;
	}

	FHitResult HitResult;
	if (!GetHitResultUnderCursor(ECC_Visibility, false, HitResult))
	{
		ClearSelectedWaypoint();
		return;
	}

	ADroneWaypointActor* HitWaypointActor = Cast<ADroneWaypointActor>(HitResult.GetActor());
	if (!CanInteractWithWaypoint(HitWaypointActor))
	{
		ClearSelectedWaypoint();
		return;
	}

	SetSelectedWaypoint(HitWaypointActor);

	const EGizmoAxis HitAxis = HitWaypointActor->GetGizmoAxisFromComponent(HitResult.GetComponent());
	SetActiveAxis(HitAxis);

	if (ActiveAxis == EGizmoAxis::None)
	{
		return;
	}

	SetWaypointDragging(true);
	SelectedWaypoint->BeginDeferredPathUpdate();
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (GetMousePosition(MouseX, MouseY))
	{
		LastMouseScreenPosition = FVector2D(MouseX, MouseY);
	}
}

void ADroneRuntimeInteractionPlayerController::HandleLeftMouseReleased()
{
	if (bIsDraggingWaypoint)
	{
		if (ADroneWaypointActor* WaypointActor = GetSelectedWaypoint())
		{
			WaypointActor->EndDeferredPathUpdate(true);
		}
	}

	SetWaypointDragging(false);
	SetActiveAxis(EGizmoAxis::None);
}

void ADroneRuntimeInteractionPlayerController::SetWaypointDragging(bool bEnableDragging)
{
	if (bIsDraggingWaypoint == bEnableDragging)
	{
		return;
	}

	bIsDraggingWaypoint = bEnableDragging;
	SetIgnoreLookInput(bEnableDragging);
	SetIgnoreMoveInput(bEnableDragging);
}

void ADroneRuntimeInteractionPlayerController::UpdateDraggedWaypointLocation()
{
	if (!bIsEditMode || ActiveAxis == EGizmoAxis::None)
	{
		HandleLeftMouseReleased();
		return;
	}

	ADroneWaypointActor* WaypointActor = GetSelectedWaypoint();
	if (!CanInteractWithWaypoint(WaypointActor))
	{
		HandleLeftMouseReleased();
		ClearSelectedWaypoint();
		return;
	}

	const float DragDelta = ResolveAxisDragDelta();
	if (FMath::IsNearlyZero(DragDelta))
	{
		return;
	}

	WaypointActor->MoveAlongGizmoAxis(ActiveAxis, DragDelta * GizmoDragSensitivity);
}

float ADroneRuntimeInteractionPlayerController::ResolveAxisDragDelta()
{
	double MouseX = 0.0;
	double MouseY = 0.0;
	if (!GetMousePosition(MouseX, MouseY))
	{
		return 0.0f;
	}

	const FVector2D CurrentMousePosition(MouseX, MouseY);
	const FVector2D MouseDelta = CurrentMousePosition - LastMouseScreenPosition;
	LastMouseScreenPosition = CurrentMousePosition;

	ADroneWaypointActor* WaypointActor = GetSelectedWaypoint();
	if (IsValid(WaypointActor) && ActiveAxis != EGizmoAxis::None)
	{
		FVector AxisWorldDirection = FVector::ZeroVector;
		switch (ActiveAxis)
		{
		case EGizmoAxis::X:
			AxisWorldDirection = WaypointActor->GetActorForwardVector();
			break;

		case EGizmoAxis::Y:
			AxisWorldDirection = WaypointActor->GetActorRightVector();
			break;

		case EGizmoAxis::Z:
			AxisWorldDirection = WaypointActor->GetActorUpVector();
			break;

		case EGizmoAxis::None:
		default:
			break;
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

	if (ActiveAxis == EGizmoAxis::Z)
	{
		return static_cast<float>(-MouseDelta.Y);
	}

	if (FMath::Abs(MouseDelta.X) >= FMath::Abs(MouseDelta.Y))
	{
		return static_cast<float>(MouseDelta.X);
	}

	return static_cast<float>(MouseDelta.Y);
}

bool ADroneRuntimeInteractionPlayerController::CanInteractWithWaypoint(const ADroneWaypointActor* WaypointActor) const
{
	if (!bIsEditMode || !IsValid(WaypointActor) || !IsValid(WaypointActor->PathActor))
	{
		return false;
	}

	return !WaypointActor->PathActor->IsMovementActive();
}

void ADroneRuntimeInteractionPlayerController::ClearSelectedWaypoint()
{
	SetSelectedWaypoint(nullptr);
}

void ADroneRuntimeInteractionPlayerController::SetActiveAxis(EGizmoAxis NewActiveAxis)
{
	ActiveAxis = NewActiveAxis;

	if (ADroneWaypointActor* CurrentWaypoint = GetSelectedWaypoint())
	{
		CurrentWaypoint->SetActiveGizmoAxis(ActiveAxis);
	}
}
