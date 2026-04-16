#include "DroneOps/Network/DroneWebSocketClient.h"
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

	Socket = FWebSocketsModule::Get().CreateWebSocket(ServerUrl, TEXT("ws"));
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
