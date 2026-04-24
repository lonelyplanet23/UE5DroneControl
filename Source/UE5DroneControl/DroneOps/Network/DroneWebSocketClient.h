#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "IWebSocket.h"
#include "DroneWebSocketClient.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWsConnected);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWsDisconnected, const FString&, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWsMessage, const FString&, Message);

/**
 * WebSocket client for real-time drone telemetry and event push.
 * Handles connect / auto-reconnect / send / receive.
 *
 * Usage:
 *   1. Set ServerUrl (e.g. "ws://127.0.0.1:8080/ws")
 *   2. Call Connect()
 *   3. Bind OnMessage to parse incoming JSON
 *   4. Call SendMessage() to send control JSON
 */
UCLASS(BlueprintType)
class UE5DRONECONTROL_API UDroneWebSocketClient : public UObject
{
	GENERATED_BODY()

public:
	/** WebSocket server URL, e.g. "ws://127.0.0.1:8081/ws" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	FString ServerUrl = TEXT("ws://127.0.0.1:8081/ws");

	/** Seconds between reconnect attempts when disconnected */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	float ReconnectIntervalSec = 3.0f;

	/** Whether to attempt auto-reconnect on disconnect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Network")
	bool bAutoReconnect = true;

	// ---- Events ----

	UPROPERTY(BlueprintAssignable, Category = "Network")
	FOnWsConnected OnConnected;

	UPROPERTY(BlueprintAssignable, Category = "Network")
	FOnWsDisconnected OnDisconnected;

	/** Fired for every incoming text message */
	UPROPERTY(BlueprintAssignable, Category = "Network")
	FOnWsMessage OnMessage;

	// ---- API ----

	UFUNCTION(BlueprintCallable, Category = "Network")
	void Connect();

	UFUNCTION(BlueprintCallable, Category = "Network")
	void Disconnect();

	UFUNCTION(BlueprintCallable, Category = "Network")
	void SendMessage(const FString& JsonMessage);

	UFUNCTION(BlueprintPure, Category = "Network")
	bool IsConnected() const;

private:
	TSharedPtr<IWebSocket> Socket;
	FTimerHandle ReconnectTimer;

	void BindSocketEvents();
	void ScheduleReconnect();
};
