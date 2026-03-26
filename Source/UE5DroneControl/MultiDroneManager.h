// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MultiDroneManager.generated.h"

class ARealTimeDroneReceiver;
class AUE5DroneControlCharacter;

UCLASS()
class UE5DRONECONTROL_API AMultiDroneManager : public AActor
{
	GENERATED_BODY()

public:
	AMultiDroneManager();

	virtual void BeginPlay() override;

	// Index of the currently active drone (0-based). Set to -1 for top-down view.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multi Drone")
	int32 ActiveDroneIndex;

	// Switch camera to the specified drone receiver.
	UFUNCTION(BlueprintCallable, Category = "Multi Drone")
	void SwitchToDrone(int32 Index);

	// Get the active sender character (top-down character) for command sending.
	UFUNCTION(BlueprintCallable, Category = "Multi Drone")
	AUE5DroneControlCharacter* GetActiveSender() const;

	// Get the active receiver (real-time drone).
	UFUNCTION(BlueprintCallable, Category = "Multi Drone")
	ARealTimeDroneReceiver* GetActiveReceiver() const;

	// Refresh cached lists (call if actors are spawned dynamically).
	UFUNCTION(BlueprintCallable, Category = "Multi Drone")
	void RefreshDroneLists();

	// Cached receiver list (sorted by ListenPort).
	UPROPERTY(BlueprintReadOnly, Category = "Multi Drone")
	TArray<ARealTimeDroneReceiver*> DroneReceivers;

	// Cached sender list (order follows discovery).
	UPROPERTY(BlueprintReadOnly, Category = "Multi Drone")
	TArray<AUE5DroneControlCharacter*> DroneSenders;
};
