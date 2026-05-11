// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOpsPlayerController.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/ICoordinateService.h"
#include "DroneOps/Interfaces/DroneSelectableInterface.h"
#include "DroneOps/Interfaces/DroneInfoProviderInterface.h"
#include "UI/UIManagerBlueprintLibrary.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Blueprint/UserWidget.h"
#include "DrawDebugHelpers.h"

ADroneOpsPlayerController::ADroneOpsPlayerController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = true;
	bEnableMouseOverEvents = true;

	PrimaryActorTick.bCanEverTick = true;

	// Set default paths to the widgets
	// These are the expected default paths in your content browser
	static ConstructorHelpers::FClassFinder<UUserWidget> InfoPanelFinder(TEXT("/Game/DroneOps/UI/WBP_DroneInfoPanel"));
	if (InfoPanelFinder.Succeeded())
	{
		DroneInfoPanelWidgetClass = InfoPanelFinder.Class;
	}

	static ConstructorHelpers::FClassFinder<UUserWidget> HUDFinder(TEXT("/Game/DroneOps/UI/WBP_DroneOpsHUD"));
	if (HUDFinder.Succeeded())
	{
		DroneOpsHUDWidgetClass = HUDFinder.Class;
	}
}

void ADroneOpsPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Get drone registry subsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance)
	{
		DroneRegistry = GameInstance->GetSubsystem<UDroneRegistrySubsystem>();
		if (DroneRegistry)
		{
			UE_LOG(LogTemp, Log, TEXT("DroneOpsPlayerController: Connected to DroneRegistry"));
		}
	}

	// Initialize camera mode
	CameraModeState.CameraMode = EDroneCameraMode::Follow;
	CameraModeState.FollowDroneId = 0;

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
}

void ADroneOpsPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		// Bind legacy input actions (will be replaced with Enhanced Input later)
		InputComponent->BindAction("PrimaryClick", IE_Pressed, this, &ADroneOpsPlayerController::OnPrimaryClick);
		InputComponent->BindAction("ShowInfo", IE_Pressed, this, &ADroneOpsPlayerController::OnShowInfo);
		InputComponent->BindAction("FreeCamToggle", IE_Pressed, this, &ADroneOpsPlayerController::OnFreeCamToggle);
		InputComponent->BindKey(EKeys::T, IE_Pressed, this, &ADroneOpsPlayerController::OnTestToast);
	}
}

void ADroneOpsPlayerController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Update hovered drone
	AActor* HoveredActor = GetActorUnderCursor();
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
}

void ADroneOpsPlayerController::OnPrimaryClick()
{
	// Get click location
	FVector WorldLocation;
	if (GetWorldLocationUnderCursor(WorldLocation))
	{
		// Check if we clicked on a drone
		AActor* ClickedActor = GetActorUnderCursor();
		if (ClickedActor)
		{
			HandleDroneClick(ClickedActor);
		}
		else
		{
			HandleMapClick(WorldLocation);
		}
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
		CameraModeState.CameraMode = EDroneCameraMode::Free;
		UE_LOG(LogTemp, Log, TEXT("Switched to Free Camera"));
		// TODO: Spawn and possess free camera pawn
	}
	else
	{
		CameraModeState.CameraMode = EDroneCameraMode::Follow;
		UE_LOG(LogTemp, Log, TEXT("Switched to Follow Camera"));
		// TODO: Return to follow mode
	}
}

void ADroneOpsPlayerController::HandleMapClick(const FVector& WorldLocation)
{
	if (!DroneRegistry)
	{
		return;
	}

	int32 SelectedDroneId = DroneRegistry->GetPrimarySelectedDrone();
	if (SelectedDroneId <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("No drone selected"));
		return;
	}

	// Check if drone is locked
	EDroneControlLockReason LockReason;
	if (DroneRegistry->IsControlLocked(SelectedDroneId, LockReason))
	{
		UE_LOG(LogTemp, Warning, TEXT("Drone %d is locked: %d"), SelectedDroneId, (int32)LockReason);
		return;
	}

	// Send target command
	SendTargetCommand(SelectedDroneId, WorldLocation);

	// Draw debug marker
	DrawDebugSphere(GetWorld(), WorldLocation, 50.0f, 12, FColor::Green, false, 2.0f);
}

void ADroneOpsPlayerController::HandleDroneClick(AActor* ClickedActor)
{
	if (!DroneRegistry)
	{
		return;
	}

	// TODO: Get DroneId from clicked actor (via interface or component)
	// For now, just log
	UE_LOG(LogTemp, Log, TEXT("Clicked on actor: %s"), *ClickedActor->GetName());

	// Example: DroneRegistry->SetPrimarySelectedDrone(DroneId);
}

void ADroneOpsPlayerController::SendTargetCommand(int32 DroneId, const FVector& TargetWorldLocation)
{
	if (!DroneRegistry)
	{
		return;
	}

	// Get coordinate service
	TScriptInterface<ICoordinateService> CoordService = DroneRegistry->GetCoordinateService();
	if (!CoordService.GetObject())
	{
		UE_LOG(LogTemp, Error, TEXT("No coordinate service available"));
		return;
	}

	// Convert to NED using the interface
	FVector NedLocation = ICoordinateService::Execute_WorldToNed(CoordService.GetObject(), TargetWorldLocation);

	// Create command
	FDroneTargetCommand Command;
	Command.DroneId = DroneId;
	Command.TargetWorldLocation = TargetWorldLocation;
	Command.TargetNedLocation = NedLocation;
	Command.Mode = 0; // Default mode
	Command.IssuedAt = GetWorld()->GetTimeSeconds();

	UE_LOG(LogTemp, Log, TEXT("Target command for Drone %d: World=(%.1f, %.1f, %.1f) NED=(%.2f, %.2f, %.2f)"),
		DroneId,
		TargetWorldLocation.X, TargetWorldLocation.Y, TargetWorldLocation.Z,
		NedLocation.X, NedLocation.Y, NedLocation.Z);

	// TODO: Send via DroneCommandSenderComponent
	// For now, this is just a placeholder
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

void ADroneOpsPlayerController::OnTestToast()
{
	UUIManagerBlueprintLibrary::ShowToast(this, TEXT("测试：无人机注册成功"), 2.0f);
}
