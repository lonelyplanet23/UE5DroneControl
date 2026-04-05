 // Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DroneOpsGameMode.generated.h"

/**
 * Game mode for drone operations
 * Initializes the drone registry and coordinate service
 */
UCLASS()
class UE5DRONECONTROL_API ADroneOpsGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADroneOpsGameMode();

protected:
	virtual void BeginPlay() override;

	// Initialize coordinate service (can be overridden in blueprints)
	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	virtual void InitializeCoordinateService();

	// Initialize drone registry (can be overridden in blueprints)
	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	virtual void InitializeDroneRegistry();

	// Use Cesium coordinate service (false = SimpleCoordinateService)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DroneOps")
	bool bUseCesiumCoordinates = false;
};
