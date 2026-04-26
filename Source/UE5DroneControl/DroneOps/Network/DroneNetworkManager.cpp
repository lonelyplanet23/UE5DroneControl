#include "DroneOps/Network/DroneNetworkManager.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
bool ParseDroneIdString(const FString& RawId, int32& OutId)
{
	FString IdText = RawId;
	if (IdText.StartsWith(TEXT("d")) || IdText.StartsWith(TEXT("D")))
	{
		IdText.RightChopInline(1, EAllowShrinking::No);
	}
	return !IdText.IsEmpty() && IdText.IsNumeric() && LexTryParseString(OutId, *IdText) && OutId > 0;
}

bool ParseDroneIdField(const TSharedPtr<FJsonObject>& Object, const FString& FieldName, int32& OutId)
{
	if (!Object.IsValid())
	{
		return false;
	}

	if (Object->TryGetNumberField(FieldName, OutId) && OutId > 0)
	{
		return true;
	}

	FString RawId;
	return Object->TryGetStringField(FieldName, RawId) && ParseDroneIdString(RawId, OutId);
}
}

void UDroneNetworkManager::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	HttpClient = NewObject<UDroneHttpClient>(this);
	HttpClient->BaseUrl = BackendBaseUrl;

	WsClient = NewObject<UDroneWebSocketClient>(this);
	WsClient->ServerUrl = WebSocketUrl.IsEmpty() ? TEXT("ws://127.0.0.1:8081/") : WebSocketUrl;
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

	// Parse JSON object: { "drones": [...] }.
	// Keep backward compatibility with the old top-level array shape.
	TSharedPtr<FJsonValue> RootValue;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, RootValue) || !RootValue.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] Failed to parse drone list JSON"));
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* DroneArray;
	TSharedPtr<FJsonObject> RootObject = RootValue->Type == EJson::Object
		? RootValue->AsObject()
		: nullptr;
	if (RootObject.IsValid())
	{
		if (!RootObject->TryGetArrayField(TEXT("drones"), DroneArray))
		{
			UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] Drone list JSON has no 'drones' array"));
			return;
		}
	}
	else if (!RootValue->TryGetArray(DroneArray))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] Drone list JSON is neither object nor array"));
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

	TSet<int32> SeenDroneIds;

	for (const TSharedPtr<FJsonObject>& Obj : DroneObjects)
	{
		FDroneDescriptor Desc;

		// Required: id
		int32 Id = 0;
		if (!ParseDroneIdField(Obj, TEXT("id"), Id))
		{
			continue;
		}
		Desc.DroneId = Id;
		SeenDroneIds.Add(Id);

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
		else
		{
			Desc.MavlinkSystemId = Id;
		}

		int32 BitIdx = 0;
		if (Obj->TryGetNumberField(TEXT("bit_index"), BitIdx))
		{
			Desc.BitIndex = BitIdx;
		}
		else
		{
			int32 Slot = 0;
			if (Obj->TryGetNumberField(TEXT("slot"), Slot) && Slot > 0)
			{
				Desc.BitIndex = Slot - 1;
			}
			else
			{
				Desc.BitIndex = Id - 1;
			}
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

		Registry->RegisterDrone(Desc);
		UE_LOG(LogTemp, Verbose, TEXT("[DroneNetworkManager] Synced drone %d (%s)"), Id, *Desc.Name);
	}

	for (const FDroneDescriptor& ExistingDesc : Registry->GetAllDroneDescriptors())
	{
		if (!SeenDroneIds.Contains(ExistingDesc.DroneId))
		{
			Registry->UnregisterDrone(ExistingDesc.DroneId);
			UE_LOG(LogTemp, Log, TEXT("[DroneNetworkManager] Unregistered stale drone %d"), ExistingDesc.DroneId);
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

	// Telemetry push: doc format is flat, legacy format nests fields in "data".
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
		if (!ParseDroneIdField(Root, TEXT("drone_id"), DroneId))
		{
			return;
		}

		const TSharedPtr<FJsonObject>* NestedData = nullptr;
		const TSharedPtr<FJsonObject> DataObject = Root->TryGetObjectField(TEXT("data"), NestedData)
			? *NestedData
			: Root;

		FDroneTelemetrySnapshot Snap;
		Snap.DroneId = DroneId;

		// Position (UE world coords, cm)
		double X = 0, Y = 0, Z = 0;
		DataObject->TryGetNumberField(TEXT("x"), X);
		DataObject->TryGetNumberField(TEXT("y"), Y);
		DataObject->TryGetNumberField(TEXT("z"), Z);
		Snap.WorldLocation = FVector(X, Y, Z);

		// Attitude (degrees)
		double Pitch = 0, Yaw = 0, Roll = 0;
		DataObject->TryGetNumberField(TEXT("pitch"), Pitch);
		DataObject->TryGetNumberField(TEXT("yaw"), Yaw);
		DataObject->TryGetNumberField(TEXT("roll"), Roll);
		Snap.Attitude = FRotator(Pitch, Yaw, Roll);

		Snap.LastUpdateTime = FPlatformTime::Seconds();
		Snap.Availability = EDroneAvailability::Online;

		Registry->UpdateTelemetry(DroneId, Snap);
		return;
	}

	if (MsgType == TEXT("event"))
	{
		UDroneRegistrySubsystem* Registry = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UDroneRegistrySubsystem>()
			: nullptr;
		if (!Registry)
		{
			return;
		}

		int32 DroneId = 0;
		if (!ParseDroneIdField(Root, TEXT("drone_id"), DroneId))
		{
			return;
		}

		FString EventName;
		if (!Root->TryGetStringField(TEXT("event"), EventName))
		{
			return;
		}

		FDroneTelemetrySnapshot Snapshot;
		if (!Registry->GetTelemetry(DroneId, Snapshot))
		{
			Snapshot.DroneId = DroneId;
		}

		Snapshot.LastUpdateTime = FPlatformTime::Seconds();
		Snapshot.Availability = (EventName == TEXT("lost_connection"))
			? EDroneAvailability::Lost
			: EDroneAvailability::Online;
		Registry->UpdateTelemetry(DroneId, Snapshot);
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

	// { "type": "move", "drone_id": "d1", "x": ..., "y": ..., "z": ... }
	const FString Json = FString::Printf(
		TEXT("{\"type\":\"move\",\"drone_id\":\"d%d\",\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}"),
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
		TEXT("{\"type\":\"%s\",\"drone_id\":\"d%d\"}"), *Type, DroneId);

	WsClient->SendMessage(Json);
}
