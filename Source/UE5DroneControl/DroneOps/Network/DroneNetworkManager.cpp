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
	WsClient->ServerUrl = WebSocketUrl;
	WsClient->OnMessage.AddDynamic(this, &UDroneNetworkManager::OnWsMessage);

	StartPolling();
	ConnectWebSocket();

	UE_LOG(LogTemp, Log, TEXT("[DroneNetworkManager] Initialized. Backend=%s"), *BackendBaseUrl);
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

	// Telemetry push: { "type": "telemetry", "drone_id": N, "data": {...} }
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

		const TSharedPtr<FJsonObject>* DataObj;
		if (!Root->TryGetObjectField(TEXT("data"), DataObj))
		{
			return;
		}

		FDroneTelemetrySnapshot Snap;
		Snap.DroneId = DroneId;

		// Position (UE world coords, cm)
		double X = 0, Y = 0, Z = 0;
		(*DataObj)->TryGetNumberField(TEXT("x"), X);
		(*DataObj)->TryGetNumberField(TEXT("y"), Y);
		(*DataObj)->TryGetNumberField(TEXT("z"), Z);
		Snap.WorldLocation = FVector(X, Y, Z);

		// Attitude (degrees)
		double Pitch = 0, Yaw = 0, Roll = 0;
		(*DataObj)->TryGetNumberField(TEXT("pitch"), Pitch);
		(*DataObj)->TryGetNumberField(TEXT("yaw"), Yaw);
		(*DataObj)->TryGetNumberField(TEXT("roll"), Roll);
		Snap.Attitude = FRotator(Pitch, Yaw, Roll);

		Snap.LastUpdateTime = FPlatformTime::Seconds();
		Snap.Availability = EDroneAvailability::Online;

		Registry->UpdateTelemetry(DroneId, Snap);
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

	// { "type": "move", "drone_id": N, "x": ..., "y": ..., "z": ... }
	const FString Json = FString::Printf(
		TEXT("{\"type\":\"move\",\"drone_id\":%d,\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}"),
		DroneId,
		TargetWorldLocation.X, TargetWorldLocation.Y, TargetWorldLocation.Z);

	WsClient->SendMessage(Json);
}

void UDroneNetworkManager::SendPauseCommand(int32 DroneId, bool bPause)
{
	if (!WsClient || !WsClient->IsConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] WS not connected, pause command dropped"));
		return;
	}

	const FString Type = bPause ? TEXT("pause") : TEXT("resume");
	const FString Json = FString::Printf(
		TEXT("{\"type\":\"%s\",\"drone_id\":%d}"), *Type, DroneId);

	WsClient->SendMessage(Json);
}
