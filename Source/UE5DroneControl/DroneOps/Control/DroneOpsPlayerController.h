// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "Components/WidgetComponent.h"
#include "Camera/CameraActor.h"
#include "DroneOpsPlayerController.generated.h"

class UDroneRegistrySubsystem;

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

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void Tick(float DeltaTime) override;

	// Input handlers
	void OnPrimaryClick();
	void OnShowInfo();
	void OnFreeCamToggle();
	void OnPauseToggle();
	void OnShiftPressed()  { bShiftHeld = true; }
	void OnShiftReleased() { bShiftHeld = false; }

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

private:
	AActor* GetSelectableDroneUnderCursor(FVector* OutFallbackWorldLocation = nullptr) const;
	AActor* FindNearestSelectableDroneOnScreen(float MaxScreenDistance) const;
	AActor* ResolveDroneActorById(int32 DroneId) const;
	AActor* ResolveFollowViewTargetByDroneId(int32 DroneId) const;
	void ApplyFollowViewTarget(int32 DroneId);

	// FR-04: Free camera actor (spawned at runtime)
	UPROPERTY()
	TObjectPtr<ACameraActor> FreeCamActor;

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

	/** Last non-free camera target so F can restore it */
	UPROPERTY()
	TObjectPtr<AActor> LastFollowViewTarget = nullptr;

	/** Tracks which drones are currently paused (toggled by P key) */
	TSet<int32> PausedDroneIds;

	/** Whether Shift is held — used for multi-select on click */
	bool bShiftHeld = false;

	/** Open drone info panel for given DroneId (C++ implementation) */
	void OpenDroneInfoPanel(int32 DroneId);
};
