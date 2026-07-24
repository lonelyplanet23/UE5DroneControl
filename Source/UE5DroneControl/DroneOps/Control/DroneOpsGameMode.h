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
class UMaterialInstanceDynamic;
class UTexture2D;

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

	/** Apply the current [CesiumTileServer] config to the running map. Safe to call from UI. */
	UFUNCTION(BlueprintCallable, Category = "DroneOps|Map")
	void ApplyMapModeFromConfig(bool bRebuildOfflinePlane = true);

	/** Switch between online Cesium imagery and the offline Ovit raster plane at runtime. */
	UFUNCTION(BlueprintCallable, Category = "DroneOps|Map")
	void SetOfflineMapModeRuntime(bool bUseOfflineMap, bool bRebuildNow = true);

	/**
	 * Validate that Cesium WGS84 transforms and offline raster tile placement agree at the current origin.
	 * Returns true when the measured round-trip errors are within ToleranceMeters.
	 */
	UFUNCTION(BlueprintCallable, Category = "DroneOps|Map")
	bool ValidateMapCoordinateAlignment(float ToleranceMeters, FString& OutReport) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;
	virtual void PreInitializeComponents() override;

	// Initialize coordinate service (can be overridden in blueprints)
	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	virtual void InitializeCoordinateService();

	// Initialize drone registry (can be overridden in blueprints)
	UFUNCTION(BlueprintCallable, Category = "DroneOps")
	virtual void InitializeDroneRegistry();

	/** Spawn a receiver/ shadow pair when the backend registers a drone after level startup. */
	UFUNCTION()
	void HandleDroneRegistered(int32 DroneId);

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
#if WITH_DEV_AUTOMATION_TESTS
	friend class FLocalPreviewIsolationPolicyTest;
#endif
	static bool ShouldSpawnMirrorDrones(bool bStrictIsolation)
	{
		return !bStrictIsolation;
	}

	APawn* FindUnpossessedPlacedPawn(bool bLogDiscoveredPawns) const;
	void PossessPlacedPawn(APlayerController* PlayerController, bool bSilentIfNotFound, bool bLogDiscoveredPawns);
	void RetryPossessPlacedPawns();
	void SpawnReceiversFromRegistry();
	ARealTimeDroneReceiver* FindReceiverForDroneId(int32 DroneId) const;
	AMultiDroneCharacter* FindShadowForDroneId(int32 DroneId) const;

	/** Read PendingOrigin from DroneNetworkManager and apply to CesiumGeoreference if set. */
	void ApplyPendingGeoreferenceOrigin();
	void ApplyStagedStrictLocalPreviewIsolation();

	/** Read [CesiumTileServer] config and switch Cesium URL-based sources to the local tile server when enabled. */
	void ApplyCesiumTileServerConfig();

	/** Warm local raster tiles around a geospatial point so nearby map imagery is ready when the view moves. */
	void PrefetchLocalRasterTilesAround(double Latitude, double Longitude);

	/** Create a flat runtime mesh around the georeference origin and project local raster tiles onto it. */
	void CreateOfflineRasterPlaneAround(double Latitude, double Longitude, double HeightMeters);

	/** Destroy the runtime offline raster plane and clear its transient tile state. */
	void DestroyOfflineRasterPlane();

	/** Ensure the offline map still has Cesium sky, sunlight, and skylight for normal material rendering. */
	void EnsureOfflineCesiumSunSky(double Latitude, double Longitude);

	/** Expand/rebuild the offline raster plane when the camera rises enough that the current coverage is visibly too small. */
	void UpdateOfflineRasterPlaneCoverageFromCamera();

	/** Estimate how many tiles are needed to cover the camera's current ground footprint for a given zoom. */
	int32 ComputeOfflineRasterPlaneRadiusForCamera(int32 Zoom, int32 BaseRadius, double Latitude, double HeightMeters) const;

	/** Request a tile texture with in-memory caching, retries, and a fallback texture while loading. */
	void RequestOfflineRasterTile(const FString& TileUrl, UMaterialInstanceDynamic* DynamicMaterial);

	/** Return a small neutral texture used before/when an offline tile request fails. */
	UTexture2D* GetOfflineRasterFallbackTexture();

	/** Keep the recent tile texture cache bounded. */
	void PruneOfflineRasterTileCache();

	/** Called by CesiumGeoreference::OnGeoreferenceUpdated after origin change. */
	UFUNCTION()
	void OnGeoreferenceUpdated();

	FTimerHandle RetryPossessTimerHandle;
	int32 RemainingPossessRetries = 0;

	/** True when we deferred SpawnReceiversFromRegistry to wait for Georeference update. */
	bool bPendingSpawnAfterGeoreferenceUpdate = false;

	bool bOfflineRasterPlaneActive = false;
	double OfflineRasterPlaneLatitude = 0.0;
	double OfflineRasterPlaneLongitude = 0.0;
	double OfflineRasterPlaneHeightMeters = 0.0;
	double LastOfflineRasterPlaneCoverageUpdateSeconds = 0.0;
	FString LastOfflineRasterPlaneCoverageKey;
	int32 OfflineRasterTileCacheFrame = 0;
	int32 OfflineRasterTileMaxCacheItems = 1024;
	int32 OfflineRasterTileMaxRetries = 2;
	int32 OfflineRasterTileMinimumBytes = 1024;
	float OfflineRasterTileRetryDelaySeconds = 1.0f;

	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UTexture2D>> OfflineRasterTileTextureCache;

	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> OfflineRasterFallbackTexture;

	TMap<FString, int32> OfflineRasterTileLastUsedFrame;
	TMap<FString, int32> OfflineRasterTileRetryCounts;
	TSet<FString> OfflineRasterTileRequestsInFlight;
	TMap<FString, TArray<TWeakObjectPtr<UMaterialInstanceDynamic>>> OfflineRasterTileWaitingMaterials;
};
