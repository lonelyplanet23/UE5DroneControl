#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneOps/Network/DroneHttpClient.h"
#include "DroneOps/Network/DroneWebSocketClient.h"
#include "PathEditor/DronePathSaveLibrary.h"
#include "DroneNetworkManager.generated.h"

class ADronePathActor;

/**
 * GameInstance-level subsystem that owns the HTTP and WebSocket clients.
 *
 * Responsibilities:
 *   - Poll GET /api/drones every PollIntervalSec and sync results into
 *     DroneRegistrySubsystem.
 *   - Own the WebSocket connection; route incoming messages to the registry.
 *   - Expose SendMoveCommand / SendPauseCommand for the player controller.
 */
UCLASS(config=Game)
class UE5DRONECONTROL_API UDroneNetworkManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- Configuration ----

	/** Backend base URL, e.g. "http://127.0.0.1:8080" */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Network")
	FString BackendBaseUrl = TEXT("http://10.196.184.78:8080");

	/** WebSocket URL, e.g. "ws://127.0.0.1:8081/ws" */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Network")
	FString WebSocketUrl = TEXT("ws://10.196.184.78:8081/ws");

	/** How often (seconds) to poll GET /api/drones */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Network")
	float PollIntervalSec = 3.0f;

	// ---- Pending Georeference Origin (set before transitioning to preview level) ----

	/** Latitude to apply to CesiumGeoreference on preview level load. 0 = not set. */
	UPROPERTY(BlueprintReadWrite, Category = "Network")
	double PendingOriginLatitude = 0.0;

	/** Longitude to apply to CesiumGeoreference on preview level load. 0 = not set. */
	UPROPERTY(BlueprintReadWrite, Category = "Network")
	double PendingOriginLongitude = 0.0;

	/** Altitude (WGS84 ellipsoid height, metres) to apply to CesiumGeoreference on preview level load. -1 = not set. */
	UPROPERTY(BlueprintReadWrite, Category = "Network")
	double PendingOriginAltitude = -1.0;

	/** Returns true if a pending origin has been written and not yet consumed. */
	UFUNCTION(BlueprintPure, Category = "Network")
	bool HasPendingGeoreferenceOrigin() const { return PendingOriginAltitude >= 0.0; }

	/** Clear the pending origin after it has been consumed. */
	UFUNCTION(BlueprintCallable, Category = "Network")
	void ClearPendingGeoreferenceOrigin() { PendingOriginLatitude = 0.0; PendingOriginLongitude = 0.0; PendingOriginAltitude = -1.0; }

	// ---- Control API ----

	/**
	 * Send a move command for the given drone via WebSocket.
	 * @param DroneId  Target drone id (from registry)
	 * @param TargetWorldLocation  UE world coordinates (cm)
	 * @param Mode  Protocol mode: move / scout / patrol / attack
	 */
	UFUNCTION(BlueprintCallable, Category = "Network")
	void SendMoveCommand(int32 DroneId, const FVector& TargetWorldLocation, EDroneCommandMode Mode = EDroneCommandMode::Move);

	/**
	 * Send pause or resume command for the selected drones.
	 * @param DroneIds  Target drone ids
	 * @param bPause    true = pause, false = resume
	 */
	UFUNCTION(BlueprintCallable, Category = "Network")
	void SendPauseCommand(const TArray<int32>& DroneIds, bool bPause);

	/**
	 * Register a drone on the backend via HTTP POST /api/drones.
	 * Slot is the backend port-map slot (1-6). The backend assigns the numeric id.
	 */
	UFUNCTION(BlueprintCallable, Category = "Network")
	void RegisterDroneToBackend(int32 Slot, const FString& IpAddress, FOnHttpResponse OnComplete);

	/**
	 * Submit an array task to the backend via HTTP POST /api/arrays.
	 * PathMap maps drone id -> ADronePathActor.
	 * OnComplete is called with (bSuccess, ResponseBody).
	 */
	UFUNCTION(BlueprintCallable, Category = "Network")
	void SendArrayTask(const TMap<int32, ADronePathActor*>& PathMap, EDroneCommandMode Mode, FOnHttpResponse OnComplete);

	/**
	 * Submit an array task from pre-loaded path save data (no actors needed).
	 * PathDataMap maps drone id -> FDronePathSaveData (waypoints already in world space).
	 */
	UFUNCTION(BlueprintCallable, Category = "Network")
	void SendArrayTaskFromData(const TMap<int32, FDronePathSaveData>& PathDataMap, EDroneCommandMode Mode, FOnHttpResponse OnComplete);

	// ---- Events ----

	// Fired when a WebSocket "event" message arrives (power_on / reconnect / lost_connection).
	// GpsLat/GpsLon/GpsAlt are only valid for power_on and reconnect.
	DECLARE_MULTICAST_DELEGATE_FiveParams(FOnDroneWsEvent,
		int32 /*DroneId*/, const FString& /*Event*/,
		double /*GpsLat*/, double /*GpsLon*/, double /*GpsAlt*/);
	FOnDroneWsEvent OnDroneWsEvent;

	/**
	 * Returns the last GPS anchor received via power_on or reconnect for the given drone.
	 * Returns false if no anchor has been received yet for that drone.
	 * Actors can call this in BeginPlay to catch up on events that arrived before they spawned.
	 */
	bool GetCachedGpsAnchor(int32 DroneId, double& OutLat, double& OutLon, double& OutAlt) const;

	// Fired when a WebSocket "alert" message arrives (low_battery / lost_connection).
	// Value is battery % for low_battery, 0 otherwise.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnDroneWsAlert,
		int32 /*DroneId*/, const FString& /*Alert*/, int32 /*Value*/);
	FOnDroneWsAlert OnDroneWsAlert;

	// Fired when assembling progress is pushed: { "type": "assembling", ... }
	// ReadyCount / TotalCount reflect current assembly progress.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAssemblingProgress,
		const FString& /*ArrayId*/, int32 /*ReadyCount*/, int32 /*TotalCount*/);
	FOnAssemblingProgress OnAssemblingProgress;

	// Fired when assembly completes: { "type": "assembly_complete", ... }
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnAssemblyComplete, const FString& /*ArrayId*/);
	FOnAssemblyComplete OnAssemblyComplete;

	// Fired when assembly times out: { "type": "assembly_timeout", ... }
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnAssemblyTimeout,
		const FString& /*ArrayId*/, int32 /*ReadyCount*/, int32 /*TotalCount*/);
	FOnAssemblyTimeout OnAssemblyTimeout;

	// ---- Accessors ----

	UFUNCTION(BlueprintPure, Category = "Network")
	UDroneHttpClient* GetHttpClient() const { return HttpClient; }

	UFUNCTION(BlueprintPure, Category = "Network")
	UDroneWebSocketClient* GetWebSocketClient() const { return WsClient; }

private:
	UPROPERTY()
	TObjectPtr<UDroneHttpClient> HttpClient;

	UPROPERTY()
	TObjectPtr<UDroneWebSocketClient> WsClient;

	FTimerHandle PollTimer;

	FOnHttpResponse PendingRegisterCallback;
	int32 PendingRegisterSlot = 0;
	FString PendingRegisterIpAddress;

	void StartPolling();
	void StopPolling();
	void PollDroneList();

	UFUNCTION()
	void OnDroneListResponse(bool bSuccess, const FString& Body);

	UFUNCTION()
	void OnRegisterDroneResponse(bool bSuccess, const FString& Body);

	void ConnectWebSocket();

	UFUNCTION()
	void OnWsConnected();

	UFUNCTION()
	void OnWsMessage(const FString& Message);

	void SyncDroneListToRegistry(const TArray<TSharedPtr<FJsonObject>>& DroneObjects);

	// Cached GPS anchors keyed by DroneId — updated on power_on / reconnect.
	struct FGpsAnchorCache
	{
		double Lat = 0.0;
		double Lon = 0.0;
		double Alt = 0.0;
	};
	TMap<int32, FGpsAnchorCache> CachedGpsAnchors;
};
