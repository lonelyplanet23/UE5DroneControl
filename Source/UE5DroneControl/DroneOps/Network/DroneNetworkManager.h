#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DroneOps/Network/DroneHttpClient.h"
#include "DroneOps/Network/DroneWebSocketClient.h"
#include "DroneNetworkManager.generated.h"

/**
 * GameInstance-level subsystem that owns the HTTP and WebSocket clients.
 *
 * Responsibilities:
 *   - Poll GET /api/drones every PollIntervalSec and sync results into
 *     DroneRegistrySubsystem.
 *   - Own the WebSocket connection; route incoming messages to the registry.
 *   - Expose SendMoveCommand / SendPauseCommand for the player controller.
 */
UCLASS()
class UE5DRONECONTROL_API UDroneNetworkManager : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- Configuration ----

	/** Backend base URL, e.g. "http://127.0.0.1:8080" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	FString BackendBaseUrl = TEXT("http://127.0.0.1:8080");

	/** WebSocket URL, e.g. "ws://127.0.0.1:8081/ws" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	FString WebSocketUrl = TEXT("ws://127.0.0.1:8081/ws");

	/** How often (seconds) to poll GET /api/drones */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	float PollIntervalSec = 3.0f;

	// ---- Control API ----

	/**
	 * Send a move command for the given drone via WebSocket.
	 * @param DroneId  Target drone id (from registry)
	 * @param TargetWorldLocation  UE world coordinates (cm)
	 */
	UFUNCTION(BlueprintCallable, Category = "Network")
	void SendMoveCommand(int32 DroneId, const FVector& TargetWorldLocation);

	/**
	 * Send pause or resume command for the selected drones.
	 * @param DroneIds  Target drone ids
	 * @param bPause    true = pause, false = resume
	 */
	UFUNCTION(BlueprintCallable, Category = "Network")
	void SendPauseCommand(const TArray<int32>& DroneIds, bool bPause);

	// ---- Events ----

	// Fired when a WebSocket "event" message arrives (power_on / reconnect / lost_connection).
	// GpsLat/GpsLon/GpsAlt are only valid for power_on and reconnect.
	DECLARE_MULTICAST_DELEGATE_FiveParams(FOnDroneWsEvent,
		int32 /*DroneId*/, const FString& /*Event*/,
		double /*GpsLat*/, double /*GpsLon*/, double /*GpsAlt*/);
	FOnDroneWsEvent OnDroneWsEvent;

	// Fired when a WebSocket "alert" message arrives (low_battery / lost_connection).
	// Value is battery % for low_battery, 0 otherwise.
	DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnDroneWsAlert,
		int32 /*DroneId*/, const FString& /*Alert*/, int32 /*Value*/);
	FOnDroneWsAlert OnDroneWsAlert;

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

	void StartPolling();
	void StopPolling();
	void PollDroneList();

	UFUNCTION()
	void OnDroneListResponse(bool bSuccess, const FString& Body);

	void ConnectWebSocket();

	UFUNCTION()
	void OnWsMessage(const FString& Message);

	void SyncDroneListToRegistry(const TArray<TSharedPtr<FJsonObject>>& DroneObjects);
};
