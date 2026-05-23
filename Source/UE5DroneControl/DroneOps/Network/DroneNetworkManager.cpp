#include "DroneOps/Network/DroneNetworkManager.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "PathEditor/DronePathActor.h"
#include "PathEditor/DroneWaypointTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

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

	UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] Config check: BackendBaseUrl=%s  WebSocketUrl=%s  PollIntervalSec=%.1f"), *BackendBaseUrl, *WebSocketUrl, PollIntervalSec);
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

void UDroneNetworkManager::OnRegisterDroneResponse(bool bSuccess, const FString& Body)
{
	if (bSuccess)
	{
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
		{
			int32 DroneId = 0;
			Root->TryGetNumberField(TEXT("id"), DroneId);

			if (DroneId > 0)
			{
				FDroneDescriptor Desc;
				Desc.DroneId = DroneId;
				Desc.Slot = PendingRegisterSlot;
				Desc.IpAddress = PendingRegisterIpAddress;
				Desc.ControlPort = 8889 + (PendingRegisterSlot - 1) * 2;
				Desc.UEReceivePort = 8888 + (PendingRegisterSlot - 1) * 2;
				Desc.MavlinkSystemId = PendingRegisterSlot;
				Desc.BitIndex = PendingRegisterSlot > 0 ? PendingRegisterSlot - 1 : 0;
				Desc.TopicPrefix = FString::Printf(TEXT("/px4_%d"), PendingRegisterSlot);

				FString IdStr;
				if (Root->TryGetStringField(TEXT("id_str"), IdStr))
				{
					Desc.BackendIdString = IdStr;
				}

				FString Name;
				if (Root->TryGetStringField(TEXT("name"), Name))
				{
					Desc.Name = Name;
				}
				else
				{
					Desc.Name = FString::Printf(TEXT("UAV-%d"), PendingRegisterSlot);
				}

				if (UDroneRegistrySubsystem* Registry = GetGameInstance()
					? GetGameInstance()->GetSubsystem<UDroneRegistrySubsystem>()
					: nullptr)
				{
					Registry->RegisterDrone(Desc);
					Registry->MarkDroneAvailability(DroneId, EDroneAvailability::Offline);
				}
			}
		}

		PollDroneList();
	}

	PendingRegisterCallback.ExecuteIfBound(bSuccess, Body);
	PendingRegisterCallback.Unbind();
	PendingRegisterSlot = 0;
	PendingRegisterIpAddress.Reset();
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

	TSet<int32> SeenBackendIds;
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
		SeenBackendIds.Add(Id);

		FString IdStr;
		if (Obj->TryGetStringField(TEXT("id_str"), IdStr))
		{
			Desc.BackendIdString = IdStr;
		}

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

		int32 Slot = 0;
		if (Obj->TryGetNumberField(TEXT("slot"), Slot))
		{
			Desc.Slot = Slot;
		}

		FString Ip;
		if (Obj->TryGetStringField(TEXT("ip"), Ip))
		{
			Desc.IpAddress = Ip;
		}

		int32 ControlPort = 0;
		if (Obj->TryGetNumberField(TEXT("port"), ControlPort))
		{
			Desc.ControlPort = ControlPort;
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

		Registry->RegisterDrone(Desc);

		FString Status;
		if (Obj->TryGetStringField(TEXT("status"), Status))
		{
			const FString Normalized = Status.ToLower();
			if (Normalized == TEXT("online"))
			{
				Registry->MarkDroneAvailability(Id, EDroneAvailability::Online);
			}
			else if (Normalized == TEXT("lost"))
			{
				Registry->MarkDroneAvailability(Id, EDroneAvailability::Lost);
			}
			else
			{
				Registry->MarkDroneAvailability(Id, EDroneAvailability::Offline);
			}
		}
	}

	for (const FDroneDescriptor& LocalDesc : Registry->GetAllDroneDescriptors())
	{
		if (LocalDesc.DroneId > 0 && !SeenBackendIds.Contains(LocalDesc.DroneId))
		{
			Registry->MarkDroneAvailability(LocalDesc.DroneId, EDroneAvailability::Lost);
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

	if (MsgType != TEXT("telemetry"))
	{
		UE_LOG(LogTemp, Log, TEXT("[DroneNetworkManager] WS non-telemetry: type=%s"), *MsgType);
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

		// Cache the GPS anchor so late-spawning actors can catch up.
		if (Event == TEXT("power_on") || Event == TEXT("reconnect"))
		{
			FGpsAnchorCache& Cache = CachedGpsAnchors.FindOrAdd(DroneId);
			Cache.Lat = GpsLat;
			Cache.Lon = GpsLon;
			Cache.Alt = GpsAlt;
		}

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

	// Assembly progress: { "type": "assembling", "array_id": "a1", "ready_count": N, "total_count": N }
	if (MsgType == TEXT("assembling"))
	{
		FString ArrayId;
		Root->TryGetStringField(TEXT("array_id"), ArrayId);

		int32 ReadyCount = 0, TotalCount = 0;
		Root->TryGetNumberField(TEXT("ready_count"), ReadyCount);
		Root->TryGetNumberField(TEXT("total_count"), TotalCount);

		UE_LOG(LogTemp, Log, TEXT("[DroneNetworkManager] Assembling array=%s %d/%d"),
			*ArrayId, ReadyCount, TotalCount);

		OnAssemblingProgress.Broadcast(ArrayId, ReadyCount, TotalCount);
		return;
	}

	// Assembly complete: { "type": "assembly_complete", "array_id": "a1" }
	if (MsgType == TEXT("assembly_complete"))
	{
		FString ArrayId;
		Root->TryGetStringField(TEXT("array_id"), ArrayId);

		UE_LOG(LogTemp, Log, TEXT("[DroneNetworkManager] Assembly complete array=%s"), *ArrayId);

		OnAssemblyComplete.Broadcast(ArrayId);
		return;
	}

	// Assembly timeout: { "type": "assembly_timeout", "array_id": "a1", "ready_count": N, "total_count": N }
	if (MsgType == TEXT("assembly_timeout"))
	{
		FString ArrayId;
		Root->TryGetStringField(TEXT("array_id"), ArrayId);

		int32 ReadyCount = 0, TotalCount = 0;
		Root->TryGetNumberField(TEXT("ready_count"), ReadyCount);
		Root->TryGetNumberField(TEXT("total_count"), TotalCount);

		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] Assembly timeout array=%s %d/%d"),
			*ArrayId, ReadyCount, TotalCount);

		OnAssemblyTimeout.Broadcast(ArrayId, ReadyCount, TotalCount);
		return;
	}
}

// ---- Control commands ----

void UDroneNetworkManager::SendMoveCommand(int32 DroneId, const FVector& TargetWorldLocation, EDroneCommandMode Mode)
{
	if (!WsClient || !WsClient->IsConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] WS not connected, move command dropped"));
		return;
	}

	// { "mode": "move|scout|patrol|attack", "drone_id": "N", "x": ..., "y": ..., "z": ... }
	// Coordinates are relative to the drone's GPS anchor (AnchorWorldLocation), subtracted by SendTargetCommand.
	const FString ProtocolMode = DroneCommandModeToProtocolString(Mode);
	const FString Json = FString::Printf(
		TEXT("{\"mode\":\"%s\",\"drone_id\":\"%d\",\"x\":%.2f,\"y\":%.2f,\"z\":%.2f}"),
		*ProtocolMode,
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

void UDroneNetworkManager::RegisterDroneToBackend(int32 Slot, const FString& IpAddress, FOnHttpResponse OnComplete)
{
	if (!HttpClient)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] RegisterDroneToBackend: HttpClient not ready"));
		OnComplete.ExecuteIfBound(false, TEXT(""));
		return;
	}

	if (Slot <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] RegisterDroneToBackend: invalid slot %d"), Slot);
		OnComplete.ExecuteIfBound(false, TEXT("{\"detail\":\"invalid slot\"}"));
		return;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("name"), FString::Printf(TEXT("UAV-%d"), Slot));
	Root->SetStringField(TEXT("model"), TEXT("PX4"));
	Root->SetNumberField(TEXT("slot"), Slot);
	Root->SetStringField(TEXT("ip"), IpAddress);
	Root->SetNumberField(TEXT("port"), 8889 + (Slot - 1) * 2);
	Root->SetStringField(TEXT("video_url"), TEXT(""));

	FString Body;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	UE_LOG(LogTemp, Log, TEXT("[DroneNetworkManager] RegisterDroneToBackend: POST /api/drones slot=%d ip=%s"),
		Slot, *IpAddress);

	PendingRegisterCallback = OnComplete;
	PendingRegisterSlot = Slot;
	PendingRegisterIpAddress = IpAddress;

	FOnHttpResponse InternalCallback;
	InternalCallback.BindDynamic(this, &UDroneNetworkManager::OnRegisterDroneResponse);
	HttpClient->Post(TEXT("/api/drones"), Body, InternalCallback);
}

void UDroneNetworkManager::SendArrayTask(const TMap<int32, ADronePathActor*>& PathMap, EDroneCommandMode Mode, FOnHttpResponse OnComplete)
{
	if (!HttpClient)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] SendArrayTask: HttpClient not ready"));
		return;
	}

	// Build JSON body:
	// { "mode": "scout|patrol|attack", "paths": [ { "pathId": N, "drone_id": "N", "bClosedLoop": bool,
	//                "waypoints": [ { "location": {"x":..,"y":..,"z":..},
	//                                 "segmentSpeed": .., "waitTime": .. } ] } ] }
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("mode"), DroneCommandModeToProtocolString(Mode));
	TArray<TSharedPtr<FJsonValue>> PathsArray;

	for (const TPair<int32, ADronePathActor*>& Pair : PathMap)
	{
		const int32 DroneId = Pair.Key;
		const ADronePathActor* Path = Pair.Value;
		if (!IsValid(Path))
		{
			continue;
		}

		TSharedPtr<FJsonObject> PathObj = MakeShared<FJsonObject>();
		PathObj->SetNumberField(TEXT("pathId"), Path->GetPathNumericId());
		PathObj->SetStringField(TEXT("drone_id"), FString::FromInt(DroneId));
		PathObj->SetBoolField(TEXT("bClosedLoop"), Path->bClosedLoop);

		TArray<TSharedPtr<FJsonValue>> WaypointsArray;
		for (const FDroneWaypoint& Wp : Path->Waypoints)
		{
			TSharedPtr<FJsonObject> WpObj = MakeShared<FJsonObject>();

			// location is stored in path-local space; convert to world for the backend
			const FVector WorldLoc = Path->GetActorTransform().TransformPosition(Wp.Location);
			TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
			LocObj->SetNumberField(TEXT("x"), WorldLoc.X);
			LocObj->SetNumberField(TEXT("y"), WorldLoc.Y);
			LocObj->SetNumberField(TEXT("z"), WorldLoc.Z);
			WpObj->SetObjectField(TEXT("location"), LocObj);

			WpObj->SetNumberField(TEXT("segmentSpeed"), Wp.SegmentSpeed);
			WpObj->SetNumberField(TEXT("waitTime"), Wp.WaitTime);

			WaypointsArray.Add(MakeShared<FJsonValueObject>(WpObj));
		}

		PathObj->SetArrayField(TEXT("waypoints"), WaypointsArray);
		PathsArray.Add(MakeShared<FJsonValueObject>(PathObj));
	}

	Root->SetArrayField(TEXT("paths"), PathsArray);

	FString Body;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	UE_LOG(LogTemp, Log, TEXT("[DroneNetworkManager] SendArrayTask: POST /api/arrays, %d paths, mode=%s"),
		PathMap.Num(), *DroneCommandModeToProtocolString(Mode));
	HttpClient->Post(TEXT("/api/arrays"), Body, OnComplete);
}

void UDroneNetworkManager::SendArrayTaskFromData(const TMap<int32, FDronePathSaveData>& PathDataMap, EDroneCommandMode Mode, FOnHttpResponse OnComplete)
{
	if (!HttpClient)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DroneNetworkManager] SendArrayTaskFromData: HttpClient not ready"));
		return;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("mode"), DroneCommandModeToProtocolString(Mode));
	TArray<TSharedPtr<FJsonValue>> PathsArray;

	for (const TPair<int32, FDronePathSaveData>& Pair : PathDataMap)
	{
		const int32 DroneId = Pair.Key;
		const FDronePathSaveData& PathData = Pair.Value;

		TSharedPtr<FJsonObject> PathObj = MakeShared<FJsonObject>();
		PathObj->SetNumberField(TEXT("pathId"), PathData.PathId);
		PathObj->SetStringField(TEXT("drone_id"), FString::FromInt(DroneId));
		PathObj->SetBoolField(TEXT("bClosedLoop"), PathData.bClosedLoop);

		TArray<TSharedPtr<FJsonValue>> WaypointsArray;
		for (const FDroneWaypointSaveData& Wp : PathData.Waypoints)
		{
			TSharedPtr<FJsonObject> WpObj = MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
			LocObj->SetNumberField(TEXT("x"), Wp.Location.X);
			LocObj->SetNumberField(TEXT("y"), Wp.Location.Y);
			LocObj->SetNumberField(TEXT("z"), Wp.Location.Z);
			WpObj->SetObjectField(TEXT("location"), LocObj);
			WpObj->SetNumberField(TEXT("segmentSpeed"), Wp.SegmentSpeed);
			WpObj->SetNumberField(TEXT("waitTime"), Wp.WaitTime);
			WaypointsArray.Add(MakeShared<FJsonValueObject>(WpObj));
		}

		PathObj->SetArrayField(TEXT("waypoints"), WaypointsArray);
		PathsArray.Add(MakeShared<FJsonValueObject>(PathObj));
	}

	Root->SetArrayField(TEXT("paths"), PathsArray);

	FString Body;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Body);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	UE_LOG(LogTemp, Log, TEXT("[DroneNetworkManager] SendArrayTaskFromData: POST /api/arrays, %d paths, mode=%s"),
		PathDataMap.Num(), *DroneCommandModeToProtocolString(Mode));
	HttpClient->Post(TEXT("/api/arrays"), Body, OnComplete);
}

bool UDroneNetworkManager::GetCachedGpsAnchor(int32 DroneId, double& OutLat, double& OutLon, double& OutAlt) const
{
	const FGpsAnchorCache* Found = CachedGpsAnchors.Find(DroneId);
	if (!Found)
	{
		return false;
	}
	OutLat = Found->Lat;
	OutLon = Found->Lon;
	OutAlt = Found->Alt;
	return true;
}
