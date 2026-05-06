 // Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TimerManager.h"
#include "DroneOpsGameMode.generated.h"

class APawn;
class AController;

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
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	// Initialize coordinate service (can be overridden in blueprints)
	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	virtual void InitializeCoordinateService();

	// Initialize drone registry (can be overridden in blueprints)
	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	virtual void InitializeDroneRegistry();

	// Use Cesium coordinate service (false = SimpleCoordinateService)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DroneOps")
	bool bUseCesiumCoordinates = true;

private:
	APawn* FindUnpossessedPlacedPawn(bool bLogDiscoveredPawns) const;
	void PossessPlacedPawn(APlayerController* PlayerController, bool bSilentIfNotFound, bool bLogDiscoveredPawns);
	void RetryPossessPlacedPawns();

	FTimerHandle RetryPossessTimerHandle;
	int32 RemainingPossessRetries = 0;
};
