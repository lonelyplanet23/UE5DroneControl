// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "Components/WidgetComponent.h"
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
	/** Current open info panel, if any */
	UPROPERTY()
	UUserWidget* CurrentDroneInfoPanel = nullptr;

	/** Open drone info panel for given DroneId (C++ implementation) */
	void OpenDroneInfoPanel(int32 DroneId);
};
