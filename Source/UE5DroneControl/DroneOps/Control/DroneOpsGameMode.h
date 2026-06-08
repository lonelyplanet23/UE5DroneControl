 // Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TimerManager.h"
#include "DroneOpsGameMode.generated.h"

class APawn;
class AController;
class ARealTimeDroneReceiver;
class AMultiDroneCharacter;

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
	virtual void PreInitializeComponents() override;

	// Initialize coordinate service (can be overridden in blueprints)
	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	virtual void InitializeCoordinateService();

	// Initialize drone registry (can be overridden in blueprints)
	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	virtual void InitializeDroneRegistry();

	// Use Cesium coordinate service (false = SimpleCoordinateService)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DroneOps")
	bool bUseCesiumCoordinates = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DroneOps|Drone Spawn")
	bool bSpawnReceiversFromRegistry = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DroneOps|Drone Spawn")
	TSubclassOf<AMultiDroneCharacter> ShadowDroneClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DroneOps|Drone Spawn")
	bool bSpawnShadowDrones = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DroneOps|Drone Spawn")
	TSubclassOf<ARealTimeDroneReceiver> ReceiverDroneClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DroneOps|Drone Spawn")
	FVector ReceiverSpawnOrigin = FVector(0.0f, 0.0f, 300.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DroneOps|Drone Spawn")
	float ReceiverSpawnSpacingCm = 400.0f;

private:
	APawn* FindUnpossessedPlacedPawn(bool bLogDiscoveredPawns) const;
	void PossessPlacedPawn(APlayerController* PlayerController, bool bSilentIfNotFound, bool bLogDiscoveredPawns);
	void RetryPossessPlacedPawns();
	void SpawnReceiversFromRegistry();
	ARealTimeDroneReceiver* FindReceiverForDroneId(int32 DroneId) const;
	AMultiDroneCharacter* FindShadowForDroneId(int32 DroneId) const;

	/** Read PendingOrigin from DroneNetworkManager and apply to CesiumGeoreference if set. */
	void ApplyPendingGeoreferenceOrigin();

	/** Read [CesiumTileServer] config and switch Cesium URL-based sources to the local tile server when enabled. */
	void ApplyCesiumTileServerConfig();

	/** Called by CesiumGeoreference::OnGeoreferenceUpdated after origin change. */
	UFUNCTION()
	void OnGeoreferenceUpdated();

	FTimerHandle RetryPossessTimerHandle;
	int32 RemainingPossessRetries = 0;

	/** True when we deferred SpawnReceiversFromRegistry to wait for Georeference update. */
	bool bPendingSpawnAfterGeoreferenceUpdate = false;
};
