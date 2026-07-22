// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneRegistrySubsystem.h"
#include "ICoordinateService.h"
#include "GameFramework/Pawn.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
FString GetDroneRegistrySavePath()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DroneRegistry.json"));
}

FString AvailabilityToString(EDroneAvailability Availability)
{
	switch (Availability)
	{
	case EDroneAvailability::Online:
		return TEXT("online");
	case EDroneAvailability::Lost:
		return TEXT("lost");
	case EDroneAvailability::Offline:
	default:
		return TEXT("offline");
	}
}

EDroneAvailability AvailabilityFromString(const FString& Status)
{
	const FString Normalized = Status.ToLower();
	if (Normalized == TEXT("online"))
	{
		return EDroneAvailability::Online;
	}
	if (Normalized == TEXT("lost") || Normalized == TEXT("lost_connection"))
	{
		return EDroneAvailability::Lost;
	}
	return EDroneAvailability::Offline;
}
}

UDroneRegistrySubsystem::UDroneRegistrySubsystem()
{
}

void UDroneRegistrySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogTemp, Log, TEXT("DroneRegistrySubsystem initialized"));
	LoadRegisteredDrones();
}

void UDroneRegistrySubsystem::Deinitialize()
{
	DroneDescriptors.Empty();
	SenderPawns.Empty();
	ReceiverActors.Empty();
	TelemetryCache.Empty();
	TaskStateCache.Empty();
	ControlLocks.Empty();
	CommandModes.Empty();
	LabelSettingsMap.Empty();

	Super::Deinitialize();
}

void UDroneRegistrySubsystem::SetCoordinateService(TScriptInterface<ICoordinateService> Service)
{
	CoordinateService = Service;
	UE_LOG(LogTemp, Log, TEXT("Coordinate service set"));
}

TScriptInterface<ICoordinateService> UDroneRegistrySubsystem::GetCoordinateService() const
{
	return CoordinateService;
}

void UDroneRegistrySubsystem::RegisterDrone(const FDroneDescriptor& Descriptor)
{
	if (Descriptor.DroneId <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("Invalid DroneId: %d"), Descriptor.DroneId);
		return;
	}

	const FDroneDescriptor* Existing = DroneDescriptors.Find(Descriptor.DroneId);
	const bool bWasRegistered = Existing != nullptr;
	const bool bChanged = !Existing ||
		Existing->Name != Descriptor.Name ||
		Existing->Slot != Descriptor.Slot ||
		Existing->BackendIdString != Descriptor.BackendIdString ||
		Existing->IpAddress != Descriptor.IpAddress ||
		Existing->ControlPort != Descriptor.ControlPort ||
		Existing->MavlinkSystemId != Descriptor.MavlinkSystemId ||
		Existing->BitIndex != Descriptor.BitIndex ||
		Existing->UEReceivePort != Descriptor.UEReceivePort ||
		Existing->TopicPrefix != Descriptor.TopicPrefix;

	DroneDescriptors.Add(Descriptor.DroneId, Descriptor);
	FDroneTelemetrySnapshot& Snapshot = TelemetryCache.FindOrAdd(Descriptor.DroneId);
	Snapshot.DroneId = Descriptor.DroneId;
	if (!CommandModes.Contains(Descriptor.DroneId))
	{
		CommandModes.Add(Descriptor.DroneId, EDroneCommandMode::Move);
	}
	// Seed label settings with the registered name if not yet configured.
	if (!LabelSettingsMap.Contains(Descriptor.DroneId))
	{
		LabelSettingsMap.Add(Descriptor.DroneId, FDroneLabelSettings(Descriptor.Name));
	}

	UE_LOG(LogTemp, Log, TEXT("Drone registered: %s (ID=%d, BitIndex=%d)"),
		*Descriptor.Name, Descriptor.DroneId, Descriptor.BitIndex);

	if (!bWasRegistered)
	{
		OnDroneRegistered.Broadcast(Descriptor.DroneId);
	}
	if (bChanged)
	{
		SaveRegisteredDrones();
	}
}

bool UDroneRegistrySubsystem::SaveRegisteredDrones() const
{
	TArray<TSharedPtr<FJsonValue>> DroneArray;
	for (const TPair<int32, FDroneDescriptor>& Pair : DroneDescriptors)
	{
		const FDroneDescriptor& Desc = Pair.Value;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("drone_id"), Desc.DroneId);
		Obj->SetStringField(TEXT("backend_id_str"), Desc.BackendIdString);
		Obj->SetStringField(TEXT("name"), Desc.Name);
		Obj->SetNumberField(TEXT("slot"), Desc.Slot);
		Obj->SetStringField(TEXT("ip"), Desc.IpAddress);
		Obj->SetNumberField(TEXT("control_port"), Desc.ControlPort);
		Obj->SetNumberField(TEXT("mavlink_system_id"), Desc.MavlinkSystemId);
		Obj->SetNumberField(TEXT("bit_index"), Desc.BitIndex);
		Obj->SetNumberField(TEXT("ue_receive_port"), Desc.UEReceivePort);
		Obj->SetStringField(TEXT("topic_prefix"), Desc.TopicPrefix);

		const FDroneTelemetrySnapshot* Snapshot = TelemetryCache.Find(Desc.DroneId);
		Obj->SetStringField(TEXT("availability"),
			AvailabilityToString(Snapshot ? Snapshot->Availability : EDroneAvailability::Offline));

		DroneArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("drones"), DroneArray);

	FString Output;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Output);
	if (!FJsonSerializer::Serialize(Root.ToSharedRef(), Writer))
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to serialize drone registry"));
		return false;
	}

	const FString SavePath = GetDroneRegistrySavePath();
	if (!FFileHelper::SaveStringToFile(Output, *SavePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to save drone registry: %s"), *SavePath);
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("Drone registry saved: %s"), *SavePath);
	return true;
}

bool UDroneRegistrySubsystem::LoadRegisteredDrones()
{
	const FString SavePath = GetDroneRegistrySavePath();
	FString Input;
	if (!FFileHelper::LoadFileToString(Input, *SavePath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Input);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to parse drone registry: %s"), *SavePath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* DroneArray = nullptr;
	if (!Root->TryGetArrayField(TEXT("drones"), DroneArray))
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& Value : *DroneArray)
	{
		const TSharedPtr<FJsonObject> Obj = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Obj.IsValid())
		{
			continue;
		}

		FDroneDescriptor Desc;
		Obj->TryGetNumberField(TEXT("drone_id"), Desc.DroneId);
		if (Desc.DroneId <= 0)
		{
			continue;
		}

		Obj->TryGetStringField(TEXT("backend_id_str"), Desc.BackendIdString);
		Obj->TryGetStringField(TEXT("name"), Desc.Name);
		Obj->TryGetNumberField(TEXT("slot"), Desc.Slot);
		Obj->TryGetStringField(TEXT("ip"), Desc.IpAddress);
		Obj->TryGetNumberField(TEXT("control_port"), Desc.ControlPort);
		Obj->TryGetNumberField(TEXT("mavlink_system_id"), Desc.MavlinkSystemId);
		Obj->TryGetNumberField(TEXT("bit_index"), Desc.BitIndex);
		Obj->TryGetNumberField(TEXT("ue_receive_port"), Desc.UEReceivePort);
		Obj->TryGetStringField(TEXT("topic_prefix"), Desc.TopicPrefix);

		DroneDescriptors.Add(Desc.DroneId, Desc);
		if (!CommandModes.Contains(Desc.DroneId))
		{
			CommandModes.Add(Desc.DroneId, EDroneCommandMode::Move);
		}

		FString AvailabilityText;
		Obj->TryGetStringField(TEXT("availability"), AvailabilityText);
		FDroneTelemetrySnapshot Snapshot;
		Snapshot.DroneId = Desc.DroneId;
		Snapshot.Availability = AvailabilityFromString(AvailabilityText);
		if (Snapshot.Availability == EDroneAvailability::Online)
		{
			// A saved "online" state is only historical. The current run must
			// prove connectivity again through backend polling or telemetry.
			Snapshot.Availability = EDroneAvailability::Lost;
		}
		TelemetryCache.Add(Desc.DroneId, Snapshot);
	}

	UE_LOG(LogTemp, Log, TEXT("Loaded %d drones from local registry"), DroneDescriptors.Num());
	return true;
}

void UDroneRegistrySubsystem::MarkDroneAvailability(int32 DroneId, EDroneAvailability Availability)
{
	if (!DroneDescriptors.Contains(DroneId))
	{
		return;
	}

	FDroneTelemetrySnapshot Snapshot;
	if (const FDroneTelemetrySnapshot* Existing = TelemetryCache.Find(DroneId))
	{
		Snapshot = *Existing;
	}
	if (Snapshot.DroneId == DroneId && Snapshot.Availability == Availability)
	{
		return;
	}
	Snapshot.DroneId = DroneId;
	Snapshot.Availability = Availability;
	// Availability is a connection-state transition, not fresh telemetry. Keep
	// LastUpdateTime intact so a disconnected information panel can truthfully
	// show when it last received flight data.
	TelemetryCache.Add(DroneId, Snapshot);
	OnTelemetryUpdated.Broadcast(DroneId, Snapshot);
	SaveRegisteredDrones();
}

void UDroneRegistrySubsystem::RegisterSenderPawn(int32 DroneId, APawn* PawnRef)
{
	if (!PawnRef)
	{
		UE_LOG(LogTemp, Warning, TEXT("Null pawn for DroneId: %d"), DroneId);
		return;
	}

	SenderPawns.Add(DroneId, PawnRef);
	UE_LOG(LogTemp, Log, TEXT("Sender pawn registered for DroneId: %d"), DroneId);
}

void UDroneRegistrySubsystem::RegisterReceiverActor(int32 DroneId, AActor* ReceiverRef)
{
	if (!ReceiverRef)
	{
		UE_LOG(LogTemp, Warning, TEXT("Null receiver for DroneId: %d"), DroneId);
		return;
	}

	ReceiverActors.Add(DroneId, ReceiverRef);
	UE_LOG(LogTemp, Log, TEXT("Receiver actor registered for DroneId: %d"), DroneId);
}

void UDroneRegistrySubsystem::UnregisterDrone(int32 DroneId)
{
	DroneDescriptors.Remove(DroneId);
	SenderPawns.Remove(DroneId);
	ReceiverActors.Remove(DroneId);
	TelemetryCache.Remove(DroneId);
	TaskStateCache.Remove(DroneId);
	ControlLocks.Remove(DroneId);
	CommandModes.Remove(DroneId);

	UE_LOG(LogTemp, Log, TEXT("Drone unregistered: %d"), DroneId);
	SaveRegisteredDrones();
}

void UDroneRegistrySubsystem::UpdateTelemetry(int32 DroneId, const FDroneTelemetrySnapshot& Snapshot)
{
	const FDroneTelemetrySnapshot* Existing = TelemetryCache.Find(DroneId);
	const bool bAvailabilityChanged = !Existing || Existing->Availability != Snapshot.Availability;
	FDroneTelemetrySnapshot Merged = Snapshot;
	if (Existing)
	{
		// telemetry 只负责位置/连接等高频数据，不能将由 drone_task_state 和
		// UE 本地预演逻辑维护的状态重置为结构体默认值。
		Merged.TaskMode = Existing->TaskMode;
		Merged.TaskState = Existing->TaskState;
		Merged.TaskErrorDetail = Existing->TaskErrorDetail;
		Merged.CurrentWaypointIndex = Existing->CurrentWaypointIndex;
		Merged.TotalWaypoints = Existing->TotalWaypoints;
		Merged.LocalState = Existing->LocalState;
	}
	TelemetryCache.Add(DroneId, Merged);
	OnTelemetryUpdated.Broadcast(DroneId, Merged);
	if (bAvailabilityChanged)
	{
		SaveRegisteredDrones();
	}
}

bool UDroneRegistrySubsystem::GetTelemetry(int32 DroneId, FDroneTelemetrySnapshot& OutSnapshot) const
{
	const FDroneTelemetrySnapshot* Found = TelemetryCache.Find(DroneId);
	if (Found)
	{
		OutSnapshot = *Found;
		return true;
	}
	return false;
}

void UDroneRegistrySubsystem::UpdateTaskState(int32 DroneId, const FDroneTaskStateSnapshot& State)
{
	if (DroneId <= 0)
	{
		return;
	}

	FDroneTaskStateSnapshot Stored = State;
	Stored.DroneId = DroneId;
	TaskStateCache.Add(DroneId, Stored);

	FDroneTelemetrySnapshot& Telemetry = TelemetryCache.FindOrAdd(DroneId);
	Telemetry.DroneId = DroneId;
	Telemetry.TaskMode = Stored.Mode;
	Telemetry.TaskState = Stored.State;
	Telemetry.TaskErrorDetail = Stored.Detail;
	Telemetry.CurrentWaypointIndex = Stored.CurrentWaypoint;
	Telemetry.TotalWaypoints = Stored.WaypointCount;
	Telemetry.LastUpdateTime = FPlatformTime::Seconds();
	OnDroneTaskStateUpdated.Broadcast(DroneId, Stored);
	OnTaskStateUpdated.Broadcast(DroneId, Stored.State, Stored.CurrentWaypoint, Stored.WaypointCount);
	OnTelemetryUpdated.Broadcast(DroneId, Telemetry);
}

bool UDroneRegistrySubsystem::GetTaskState(int32 DroneId, FDroneTaskStateSnapshot& OutState) const
{
	const FDroneTaskStateSnapshot* Found = TaskStateCache.Find(DroneId);
	if (!Found)
	{
		return false;
	}

	OutState = *Found;
	return true;
}

void UDroneRegistrySubsystem::SetPrimarySelectedDrone(int32 DroneId)
{
	int32 OldDroneId = PrimarySelectedDroneId;
	PrimarySelectedDroneId = DroneId;

	// Update multi-selection to include primary
	if (DroneId > 0 && !MultiSelectedDroneIds.Contains(DroneId))
	{
		MultiSelectedDroneIds.Empty();
		MultiSelectedDroneIds.Add(DroneId);
	}

	UE_LOG(LogTemp, Log, TEXT("Primary selection changed: %d -> %d"), OldDroneId, DroneId);
	OnPrimarySelectionChanged.Broadcast(OldDroneId, DroneId);
}

void UDroneRegistrySubsystem::SetMultiSelectedDrones(const TArray<int32>& DroneIds)
{
	MultiSelectedDroneIds = DroneIds;
	if (DroneIds.IsEmpty())
	{
		// Keep the selection invariant explicit: an empty multi-selection has no primary.
		// This is important when Shift-click toggles the final selected drone off.
		if (PrimarySelectedDroneId > 0)
		{
			SetPrimarySelectedDrone(0);
		}
		OnMultiSelectionChanged.Broadcast();
		return;
	}

	// Update primary to first in list if not already selected
	if (!DroneIds.Contains(PrimarySelectedDroneId))
	{
		SetPrimarySelectedDrone(DroneIds[0]);
	}
	OnMultiSelectionChanged.Broadcast();
}

void UDroneRegistrySubsystem::ClearSelection()
{
	int32 OldDroneId = PrimarySelectedDroneId;
	PrimarySelectedDroneId = 0;
	MultiSelectedDroneIds.Empty();

	OnPrimarySelectionChanged.Broadcast(OldDroneId, 0);
	OnMultiSelectionChanged.Broadcast();
}

void UDroneRegistrySubsystem::AddToMultiSelection(int32 DroneId)
{
	if (DroneId <= 0 || MultiSelectedDroneIds.Contains(DroneId))
	{
		return;
	}
	MultiSelectedDroneIds.Add(DroneId);
	if (PrimarySelectedDroneId <= 0)
	{
		// 直接赋值而非调用 SetPrimarySelectedDrone，避免后者清空 MultiSelectedDroneIds
		int32 OldId = PrimarySelectedDroneId;
		PrimarySelectedDroneId = DroneId;
		OnPrimarySelectionChanged.Broadcast(OldId, DroneId);
	}
	OnMultiSelectionChanged.Broadcast();
}

void UDroneRegistrySubsystem::RemoveFromMultiSelection(int32 DroneId)
{
	if (DroneId <= 0 || !MultiSelectedDroneIds.Contains(DroneId))
	{
		return;
	}
	MultiSelectedDroneIds.Remove(DroneId);
	if (PrimarySelectedDroneId == DroneId)
	{
		int32 OldId = PrimarySelectedDroneId;
		PrimarySelectedDroneId = MultiSelectedDroneIds.IsEmpty() ? 0 : MultiSelectedDroneIds[0];
		OnPrimarySelectionChanged.Broadcast(OldId, PrimarySelectedDroneId);
	}
	OnMultiSelectionChanged.Broadcast();
}

bool UDroneRegistrySubsystem::IsDroneSelected(int32 DroneId) const
{
	return MultiSelectedDroneIds.Contains(DroneId);
}

void UDroneRegistrySubsystem::ApplyControlLock(int32 DroneId, EDroneControlLockReason LockReason)
{
	ControlLocks.Add(DroneId, LockReason);
	UE_LOG(LogTemp, Log, TEXT("Control lock applied to DroneId %d: %d"), DroneId, (int32)LockReason);
	OnControlLockChanged.Broadcast(DroneId, LockReason, true);
}

void UDroneRegistrySubsystem::ReleaseControlLock(int32 DroneId)
{
	if (ControlLocks.Contains(DroneId))
	{
		ControlLocks.Remove(DroneId);
		UE_LOG(LogTemp, Log, TEXT("Control lock released for DroneId: %d"), DroneId);
		OnControlLockChanged.Broadcast(DroneId, EDroneControlLockReason::None, false);
	}
}

bool UDroneRegistrySubsystem::IsControlLocked(int32 DroneId, EDroneControlLockReason& OutReason) const
{
	const EDroneControlLockReason* Found = ControlLocks.Find(DroneId);
	if (Found)
	{
		OutReason = *Found;
		return true;
	}
	OutReason = EDroneControlLockReason::None;
	return false;
}

void UDroneRegistrySubsystem::SetDroneCommandMode(int32 DroneId, EDroneCommandMode Mode)
{
	if (DroneId <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("SetDroneCommandMode: invalid DroneId %d"), DroneId);
		return;
	}

	CommandModes.Add(DroneId, Mode);
	UE_LOG(LogTemp, Log, TEXT("Drone %d command mode -> %s"),
		DroneId, *DroneCommandModeToProtocolString(Mode));
	OnDroneCommandModeChanged.Broadcast(DroneId, Mode);
}

void UDroneRegistrySubsystem::SetDroneCommandModeFromString(int32 DroneId, const FString& Mode)
{
	SetDroneCommandMode(DroneId, DroneCommandModeFromProtocolString(Mode));
}

void UDroneRegistrySubsystem::CycleDroneCommandMode(int32 DroneId)
{
	switch (GetDroneCommandMode(DroneId))
	{
	case EDroneCommandMode::Scout:
		SetDroneCommandMode(DroneId, EDroneCommandMode::Patrol);
		break;
	case EDroneCommandMode::Patrol:
		SetDroneCommandMode(DroneId, EDroneCommandMode::Attack);
		break;
	case EDroneCommandMode::Attack:
		SetDroneCommandMode(DroneId, EDroneCommandMode::Scout);
		break;
	case EDroneCommandMode::Move:
	default:
		SetDroneCommandMode(DroneId, EDroneCommandMode::Scout);
		break;
	}
}

EDroneCommandMode UDroneRegistrySubsystem::GetDroneCommandMode(int32 DroneId) const
{
	const EDroneCommandMode* Found = CommandModes.Find(DroneId);
	return Found ? *Found : EDroneCommandMode::Move;
}

FString UDroneRegistrySubsystem::GetDroneCommandModeString(int32 DroneId) const
{
	return DroneCommandModeToProtocolString(GetDroneCommandMode(DroneId));
}

int32 UDroneRegistrySubsystem::GetSelectedDroneMask() const
{
	uint32 Mask = 0;

	for (int32 DroneId : MultiSelectedDroneIds)
	{
		const FDroneDescriptor* Descriptor = DroneDescriptors.Find(DroneId);
		if (Descriptor && Descriptor->BitIndex >= 0 && Descriptor->BitIndex < 32)
		{
			Mask |= (1u << Descriptor->BitIndex);
		}
	}

	return static_cast<int32>(Mask);
}

int32 UDroneRegistrySubsystem::GetDroneBitIndex(int32 DroneId) const
{
	const FDroneDescriptor* Descriptor = DroneDescriptors.Find(DroneId);
	return Descriptor ? Descriptor->BitIndex : -1;
}

int32 UDroneRegistrySubsystem::GetDroneByBitIndex(int32 BitIndex) const
{
	for (const auto& Pair : DroneDescriptors)
	{
		if (Pair.Value.BitIndex == BitIndex)
		{
			return Pair.Key;
		}
	}
	return 0;
}

TArray<FDroneDescriptor> UDroneRegistrySubsystem::GetAllDroneDescriptors() const
{
	TArray<FDroneDescriptor> Result;
	DroneDescriptors.GenerateValueArray(Result);
	return Result;
}

bool UDroneRegistrySubsystem::GetDroneDescriptor(int32 DroneId, FDroneDescriptor& OutDescriptor) const
{
	const FDroneDescriptor* Found = DroneDescriptors.Find(DroneId);
	if (Found)
	{
		OutDescriptor = *Found;
		return true;
	}
	return false;
}

APawn* UDroneRegistrySubsystem::GetSenderPawn(int32 DroneId) const
{
	const TObjectPtr<APawn>* Found = SenderPawns.Find(DroneId);
	return Found ? Found->Get() : nullptr;
}

AActor* UDroneRegistrySubsystem::GetReceiverActor(int32 DroneId) const
{
	const TObjectPtr<AActor>* Found = ReceiverActors.Find(DroneId);
	return Found ? Found->Get() : nullptr;
}

bool UDroneRegistrySubsystem::IsDroneRegistered(int32 DroneId) const
{
	return DroneDescriptors.Contains(DroneId);
}

void UDroneRegistrySubsystem::UpdateLocalState(int32 DroneId, EUELocalDroneState LocalState)
{
    FDroneTelemetrySnapshot& Snap = TelemetryCache.FindOrAdd(DroneId);
    Snap.DroneId = DroneId;
    Snap.LocalState = LocalState;
    Snap.LastUpdateTime = FPlatformTime::Seconds();
    OnTelemetryUpdated.Broadcast(DroneId, Snap);
}

void UDroneRegistrySubsystem::SetDroneLabelSettings(int32 DroneId, const FDroneLabelSettings& Settings)
{
	if (DroneId <= 0)
	{
		return;
	}
	LabelSettingsMap.Add(DroneId, Settings);
	OnDroneLabelSettingsChanged.Broadcast(DroneId, Settings);
}

bool UDroneRegistrySubsystem::GetDroneLabelSettings(int32 DroneId, FDroneLabelSettings& OutSettings) const
{
	const FDroneLabelSettings* Found = LabelSettingsMap.Find(DroneId);
	if (Found)
	{
		OutSettings = *Found;
		return true;
	}
	// Fall back to the registered name so a label always shows something sensible.
	const FDroneDescriptor* Desc = DroneDescriptors.Find(DroneId);
	if (Desc)
	{
		OutSettings = FDroneLabelSettings(Desc->Name);
		return true;
	}
	return false;
}

