#include "DroneOps/Network/DroneWebSocketClient.h"
#include "HttpModule.h"
#include "WebSocketsModule.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UDroneWebSocketClient::Connect()
{
	if (Socket && Socket->IsConnected())
	{
		return;
	}

	// Cancel any pending reconnect
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReconnectTimer);
	}

	// UE 5.7 copies FHttpModule's proxy address while the WebSockets module creates
	// its libwebsockets context, but it ignores HttpNoProxy. Keep the project-wide
	// proxy enabled for Cesium/Google HTTP requests and initialize this local drone
	// WebSocket context without it, otherwise ws://127.0.0.1 and private backend
	// addresses are incorrectly sent through Clash.
	FHttpModule& HttpModule = FHttpModule::Get();
	const FString SavedHttpProxyAddress = HttpModule.GetProxyAddress();
	const bool bTemporarilyClearHttpProxy = !SavedHttpProxyAddress.IsEmpty();
	if (bTemporarilyClearHttpProxy)
	{
		HttpModule.SetProxyAddress(TEXT(""));
	}

	FWebSocketsModule& WebSocketsModule = FWebSocketsModule::Get();

	if (bTemporarilyClearHttpProxy)
	{
		HttpModule.SetProxyAddress(SavedHttpProxyAddress);
		UE_LOG(LogTemp, Log, TEXT("[DroneWS] Initialized WebSockets without HTTP proxy; keeping %s for Cesium HTTP requests."), *SavedHttpProxyAddress);
	}

	Socket = WebSocketsModule.CreateWebSocket(ServerUrl, TEXT("ws"));
	BindSocketEvents();
	Socket->Connect();

	UE_LOG(LogTemp, Log, TEXT("[DroneWS] Connecting to %s"), *ServerUrl);
}

void UDroneWebSocketClient::Disconnect()
{
	bAutoReconnect = false;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReconnectTimer);
	}

	if (Socket)
	{
		Socket->Close();
		Socket.Reset();
	}
}

void UDroneWebSocketClient::SendMessage(const FString& JsonMessage)
{
	if (!IsConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneWS] SendMessage called but not connected"));
		return;
	}
	Socket->Send(JsonMessage);
}

bool UDroneWebSocketClient::IsConnected() const
{
	return Socket.IsValid() && Socket->IsConnected();
}

void UDroneWebSocketClient::BindSocketEvents()
{
	if (!Socket)
	{
		return;
	}

	Socket->OnConnected().AddLambda([this]()
	{
		UE_LOG(LogTemp, Log, TEXT("[DroneWS] Connected to %s"), *ServerUrl);
		OnConnected.Broadcast();
	});

	Socket->OnConnectionError().AddLambda([this](const FString& Error)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneWS] Connection error: %s"), *Error);
		OnDisconnected.Broadcast(Error);
		ScheduleReconnect();
	});

	Socket->OnClosed().AddLambda([this](int32 StatusCode, const FString& Reason, bool bWasClean)
	{
		UE_LOG(LogTemp, Log, TEXT("[DroneWS] Closed (code=%d, reason=%s, clean=%d)"),
			StatusCode, *Reason, bWasClean);
		OnDisconnected.Broadcast(Reason);
		ScheduleReconnect();
	});

	Socket->OnMessage().AddLambda([this](const FString& Message)
	{
		OnMessage.Broadcast(Message);
	});
}

void UDroneWebSocketClient::ScheduleReconnect()
{
	if (!bAutoReconnect)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().SetTimer(ReconnectTimer, [this]()
	{
		UE_LOG(LogTemp, Log, TEXT("[DroneWS] Attempting reconnect to %s"), *ServerUrl);
		Connect();
	}, ReconnectIntervalSec, false);
}
