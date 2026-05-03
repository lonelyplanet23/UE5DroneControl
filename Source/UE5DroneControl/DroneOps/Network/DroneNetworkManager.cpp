#include "DroneOps/Network/DroneNetworkManager.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UDroneNetworkManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	HttpClient = NewObject<UDroneHttpClient>(this);
	HttpClient->BaseUrl = BackendBaseUrl;

	WsClient = NewObject<UDroneWebSocketClient>(this);
	// Force correct port — property may carry a stale serialized value from the editor
	WsClient->ServerUrl = TEXT("ws://127.0.0.1:8081/ws");
	WebSocketUrl = WsClient->ServerUrl;
	WsClient->OnMessage.AddDynamic(this, &UDroneNetworkManager::OnWsMessage);

	StartPolling();
	ConnectWebSocket();

	UE_LOG(LogTemp, Log, TEXT("[DroneNetworkManager] Initialized. Backend=%s  WS=%s"), *BackendBaseUrl, *WsClient->ServerUrl);
}

void UDroneNetworkManager::Deinitialize()
{
	StopPolling();

	if (WsClient)
	{
		WsClient->Disconnect();
	}

	Super::Deinitialize();
}

// ---- Polling ----

void UDroneNetworkManager::StartPolling()
{
	UWorld* World = GetGameInstance()->GetWorld();
	if (!World)
	{
		return;
	}

	World->GetTimerManager().SetTimer(PollTimer, this,
		&UDroneNetworkManager::PollDroneList,
		PollIntervalSec, true, 0.5f); // first tick after 0.5s
}

void UDroneNetworkManager::StopPolling()
{
	if (UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr)
	{
		World->GetTimerManager().ClearTimer(PollTimer);
	}
}

void UDroneNetworkManager::PollDroneList()
{
	if (!HttpClient)
	{
		return;
	}

	FOnHttpResponse Callback;
	Callback.BindDynamic(this, &UDroneNetworkManager::OnDroneListResponse);
	HttpClient->Get(TEXT("/api/drones"), Callback);
}

void UDroneNetworkManager::OnDroneListResponse(bool bSuccess, const FString& Body)
{
	if (!bSuccess)
	{
		return;
	}

	// Parse JSON array
	TSharedPtr<FJsonValue> RootValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, RootValue) || !RootValue.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] Failed to parse drone list JSON"));
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* DroneArray;
	if (!RootValue->TryGetArray(DroneArray))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] Drone list JSON is not an array"));
		return;
	}

	TArray<TSharedPtr<FJsonObject>> DroneObjects;
	for (const TSharedPtr<FJsonValue>& Val : *DroneArray)
	{
		if (Val->Type == EJson::Object)
		{
			DroneObjects.Add(Val->AsObject());
		}
	}

	SyncDroneListToRegistry(DroneObjects);
}

void UDroneNetworkManager::SyncDroneListToRegistry(const TArray<TSharedPtr<FJsonObject>>& DroneObjects)
{
	UDroneRegistrySubsystem* Registry = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UDroneRegistrySubsystem>()
		: nullptr;

	if (!Registry)
	{
		return;
	}

	for (const TSharedPtr<FJsonObject>& Obj : DroneObjects)
	{
		FDroneDescriptor Desc;

		// Required: id
		int32 Id = 0;
		if (!Obj->TryGetNumberField(TEXT("id"), Id) || Id <= 0)
		{
			continue;
		}
		Desc.DroneId = Id;

		// Optional fields — use defaults if absent
		FString Name;
		if (Obj->TryGetStringField(TEXT("name"), Name))
		{
			Desc.Name = Name;
		}
		else
		{
			Desc.Name = FString::Printf(TEXT("UAV-%d"), Id);
		}

		int32 MavId = 0;
		if (Obj->TryGetNumberField(TEXT("mavlink_system_id"), MavId))
		{
			Desc.MavlinkSystemId = MavId;
		}

		int32 BitIdx = 0;
		if (Obj->TryGetNumberField(TEXT("bit_index"), BitIdx))
		{
			Desc.BitIndex = BitIdx;
		}

		int32 Port = 0;
		if (Obj->TryGetNumberField(TEXT("ue_receive_port"), Port))
		{
			Desc.UEReceivePort = Port;
		}

		FString Prefix;
		if (Obj->TryGetStringField(TEXT("topic_prefix"), Prefix))
		{
			Desc.TopicPrefix = Prefix;
		}

		// Register (or update) in the local registry
		if (!Registry->IsDroneRegistered(Id))
		{
			Registry->RegisterDrone(Desc);
			UE_LOG(LogTemp, Log, TEXT("[DroneNetworkManager] Registered drone %d (%s)"), Id, *Desc.Name);
		}
	}
}

// ---- WebSocket ----

void UDroneNetworkManager::ConnectWebSocket()
{
	if (WsClient)
	{
		WsClient->Connect();
	}
}

void UDroneNetworkManager::OnWsMessage(const FString& Message)
{
	// Parse top-level JSON to route by "type" field
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}

	FString MsgType;
	if (!Root->TryGetStringField(TEXT("type"), MsgType))
	{
		return;
	}

	// Telemetry push (flat): { "type": "telemetry", "drone_id": N, "x":..., "y":..., "z":...,
	//                          "yaw":..., "pitch":..., "roll":..., "speed":..., "battery":... }
	if (MsgType == TEXT("telemetry"))
	{
		UDroneRegistrySubsystem* Registry = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UDroneRegistrySubsystem>()
			: nullptr;

		if (!Registry)
		{
			return;
		}

		int32 DroneId = 0;
		Root->TryGetNumberField(TEXT("drone_id"), DroneId);

		FDroneTelemetrySnapshot Snap;
		Snap.DroneId = DroneId;

		double X = 0, Y = 0, Z = 0;
		Root->TryGetNumberField(TEXT("x"), X);
		Root->TryGetNumberField(TEXT("y"), Y);
		Root->TryGetNumberField(TEXT("z"), Z);
		Snap.WorldLocation = FVector(X, Y, Z);

		double Pitch = 0, Yaw = 0, Roll = 0;
		Root->TryGetNumberField(TEXT("pitch"), Pitch);
		Root->TryGetNumberField(TEXT("yaw"), Yaw);
		Root->TryGetNumberField(TEXT("roll"), Roll);
		Snap.Attitude = FRotator(Pitch, Yaw, Roll);

		double Speed = 0;
		Root->TryGetNumberField(TEXT("speed"), Speed);
		Snap.Velocity = FVector(Speed, 0, 0); // scalar speed stored in X component

		int32 Battery = -1;
		Root->TryGetNumberField(TEXT("battery"), Battery);
		// Battery stored in GeographicLocation.Z as a convenient spare float field
		Snap.GeographicLocation.Z = static_cast<double>(Battery);

		Snap.LastUpdateTime = FPlatformTime::Seconds();
		Snap.Availability = EDroneAvailability::Online;

		Registry->UpdateTelemetry(DroneId, Snap);
		return;
	}

	// State event: { "type": "event", "drone_id": N, "event": "...", "gps_lat":..., ... }
	if (MsgType == TEXT("event"))
	{
		int32 DroneId = 0;
		Root->TryGetNumberField(TEXT("drone_id"), DroneId);

		FString Event;
		Root->TryGetStringField(TEXT("event"), Event);

		double GpsLat = 0, GpsLon = 0, GpsAlt = 0;
		Root->TryGetNumberField(TEXT("gps_lat"), GpsLat);
		Root->TryGetNumberField(TEXT("gps_lon"), GpsLon);
		Root->TryGetNumberField(TEXT("gps_alt"), GpsAlt);

		UE_LOG(LogTemp, Log, TEXT("[DroneNetworkManager] Event drone=%d type=%s lat=%.6f lon=%.6f alt=%.1f"),
			DroneId, *Event, GpsLat, GpsLon, GpsAlt);

		OnDroneWsEvent.Broadcast(DroneId, Event, GpsLat, GpsLon, GpsAlt);
		return;
	}

	// Alert: { "type": "alert", "drone_id": N, "alert": "...", "value": N }
	if (MsgType == TEXT("alert"))
	{
		int32 DroneId = 0;
		Root->TryGetNumberField(TEXT("drone_id"), DroneId);

		FString Alert;
		Root->TryGetStringField(TEXT("alert"), Alert);

		int32 Value = 0;
		Root->TryGetNumberField(TEXT("value"), Value);

		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] Alert drone=%d type=%s value=%d"),
			DroneId, *Alert, Value);

		OnDroneWsAlert.Broadcast(DroneId, Alert, Value);
		return;
	}
}

// ---- Control commands ----

void UDroneNetworkManager::SendMoveCommand(int32 DroneId, const FVector& TargetWorldLocation)
{
	if (!WsClient || !WsClient->IsConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] WS not connected, move command dropped"));
		return;
	}

	// { "type": "move", "drone_id": "N", "x": ..., "y": ..., "z": ... }
	// Coordinates should be relative to the drone's anchor (AnchorWorldLocation).
	// TODO: subtract AnchorWorldLocation once the Cesium anchor flow (柯垣丞) is ready.
	const FString Json = FString::Printf(
		TEXT("{\"type\":\"move\",\"drone_id\":\"%d\",\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}"),
		DroneId,
		TargetWorldLocation.X, TargetWorldLocation.Y, TargetWorldLocation.Z);

	WsClient->SendMessage(Json);
}

void UDroneNetworkManager::SendPauseCommand(const TArray<int32>& DroneIds, bool bPause)
{
	if (!WsClient || !WsClient->IsConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] WS not connected, pause command dropped"));
		return;
	}

	// Build drone_ids JSON array: ["1","2",...]
	FString IdsArray;
	for (int32 i = 0; i < DroneIds.Num(); ++i)
	{
		if (i > 0) IdsArray += TEXT(",");
		IdsArray += FString::Printf(TEXT("\"%d\""), DroneIds[i]);
	}

	const FString Type = bPause ? TEXT("pause") : TEXT("resume");
	const FString Json = FString::Printf(
		TEXT("{\"type\":\"%s\",\"drone_ids\":[%s]}"), *Type, *IdsArray);

	WsClient->SendMessage(Json);
}
