// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneOpsPlayerController.generated.h"

class UDroneRegistrySubsystem;

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

	AActor* GetSelectableDroneUnderCursor(FVector* OutFallbackWorldLocation = nullptr) const;
	AActor* FindNearestSelectableDroneOnScreen(float MaxScreenDistance) const;
	AActor* ResolveDroneActorById(int32 DroneId) const;
};
