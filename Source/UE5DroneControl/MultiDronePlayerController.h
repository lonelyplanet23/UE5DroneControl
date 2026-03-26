// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5DroneControlPlayerController.h"
#include "MultiDronePlayerController.generated.h"

class AMultiDroneManager;

UCLASS()
class UE5DRONECONTROL_API AMultiDronePlayerController : public AUE5DroneControlPlayerController
{
	GENERATED_BODY()

public:
	AMultiDronePlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	// Input handlers (rebound to support multi-drone routing)
	void OnInputStarted();
	void OnSetDestinationTriggered();
	void OnSetDestinationReleased();
	void OnTouchTriggered();
	void OnTouchReleased();
	void UpdateCachedDestination();

	// Drone switching
	void SwitchToTopDownCharacter();
	void SwitchToDrone1();
	void SwitchToDrone2();
	void SwitchToDrone3();
	void SwitchToDroneIndex(int32 Index);

	UPROPERTY()
	AMultiDroneManager* CachedManager;
};
