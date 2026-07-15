// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneRegistrySubsystem.generated.h"

class ICoordinateService;
class APawn;
class AActor;

/**
 * Delegate for drone registration events
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDroneRegistered, int32, DroneId);

/**
 * Delegate for primary selection change
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnPrimarySelectionChanged, int32, OldDroneId, int32, NewDroneId);

/**
 * Delegate for telemetry update
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTelemetryUpdated, int32, DroneId, const FDroneTelemetrySnapshot&, Snapshot);

/**
 * Delegate for control lock change
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnControlLockChanged, int32, DroneId, EDroneControlLockReason, LockReason, bool, bLocked);

/**
 * Delegate for command mode changes.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDroneCommandModeChanged, int32, DroneId, EDroneCommandMode, Mode);

/**
 * Delegate for task state updates
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnTaskStateUpdated,
    int32, DroneId, EDroneTaskState, NewState, int32, CurrentWp, int32, TotalWp);

/**
 * Global drone registry and state management subsystem
 * Central hub for all drone-related state and events
 */
UCLASS()
class UE5DRONECONTROL_API UDroneRegistrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UDroneRegistrySubsystem();

	// Subsystem lifecycle
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Coordinate service management
	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void SetCoordinateService(TScriptInterface<ICoordinateService> Service);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	TScriptInterface<ICoordinateService> GetCoordinateService() const;

	// Drone registration
	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void RegisterDrone(const FDroneDescriptor& Descriptor);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	bool SaveRegisteredDrones() const;

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	bool LoadRegisteredDrones();

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void MarkDroneAvailability(int32 DroneId, EDroneAvailability Availability);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void RegisterSenderPawn(int32 DroneId, APawn* PawnRef);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void RegisterReceiverActor(int32 DroneId, AActor* ReceiverRef);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void UnregisterDrone(int32 DroneId);

	// Telemetry management
	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void UpdateTelemetry(int32 DroneId, const FDroneTelemetrySnapshot& Snapshot);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	bool GetTelemetry(int32 DroneId, FDroneTelemetrySnapshot& OutSnapshot) const;

	// Selection management
	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void SetPrimarySelectedDrone(int32 DroneId);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	int32 GetPrimarySelectedDrone() const { return PrimarySelectedDroneId; }

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void SetMultiSelectedDrones(const TArray<int32>& DroneIds);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	TArray<int32> GetMultiSelectedDrones() const { return MultiSelectedDroneIds; }

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void ClearSelection();

	// Control lock management
	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void ApplyControlLock(int32 DroneId, EDroneControlLockReason LockReason);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void ReleaseControlLock(int32 DroneId);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	bool IsControlLocked(int32 DroneId, EDroneControlLockReason& OutReason) const;

	// Command mode management
	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void SetDroneCommandMode(int32 DroneId, EDroneCommandMode Mode);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void SetDroneCommandModeFromString(int32 DroneId, const FString& Mode);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	void CycleDroneCommandMode(int32 DroneId);

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	EDroneCommandMode GetDroneCommandMode(int32 DroneId) const;

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	FString GetDroneCommandModeString(int32 DroneId) const;

	// Mask generation for multi-drone control
	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	int32 GetSelectedDroneMask() const;

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	int32 GetDroneBitIndex(int32 DroneId) const;

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	int32 GetDroneByBitIndex(int32 BitIndex) const;

	// Query functions
	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	TArray<FDroneDescriptor> GetAllDroneDescriptors() const;

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	bool GetDroneDescriptor(int32 DroneId, FDroneDescriptor& OutDescriptor) const;

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	APawn* GetSenderPawn(int32 DroneId) const;

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	AActor* GetReceiverActor(int32 DroneId) const;

	UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
	bool IsDroneRegistered(int32 DroneId) const;

	// Events
	UPROPERTY(BlueprintAssignable, Category = "DroneRegistry")
	FOnDroneRegistered OnDroneRegistered;

	UPROPERTY(BlueprintAssignable, Category = "DroneRegistry")
	FOnPrimarySelectionChanged OnPrimarySelectionChanged;

	UPROPERTY(BlueprintAssignable, Category = "DroneRegistry")
	FOnTelemetryUpdated OnTelemetryUpdated;

	UPROPERTY(BlueprintAssignable, Category = "DroneRegistry")
	FOnControlLockChanged OnControlLockChanged;

	UPROPERTY(BlueprintAssignable, Category = "DroneRegistry")
	FOnDroneCommandModeChanged OnDroneCommandModeChanged;

	UPROPERTY(BlueprintAssignable, Category = "DroneRegistry")
    FOnTaskStateUpdated OnTaskStateUpdated;

	// ===== 任务状态管理 =====
    UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
    void UpdateTaskState(int32 DroneId, EDroneTaskState State,
                         const FString& ErrorDetail = "",
                         int32 CurrentWaypoint = 0, int32 TotalWaypoints = 0);

    UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
    void UpdateLocalState(int32 DroneId, EUELocalDroneState LocalState);

    UFUNCTION(BlueprintCallable, Category = "DroneRegistry")
    bool GetTaskState(int32 DroneId, EDroneTaskState& OutState, FString& OutError) const;

private:
	// Coordinate service
	UPROPERTY()
	TScriptInterface<ICoordinateService> CoordinateService;

	// Drone registry
	UPROPERTY()
	TMap<int32, FDroneDescriptor> DroneDescriptors;

	UPROPERTY()
	TMap<int32, TObjectPtr<APawn>> SenderPawns;

	UPROPERTY()
	TMap<int32, TObjectPtr<AActor>> ReceiverActors;

	// Telemetry cache
	UPROPERTY()
	TMap<int32, FDroneTelemetrySnapshot> TelemetryCache;

	// Selection state
	UPROPERTY()
	int32 PrimarySelectedDroneId = 0;

	UPROPERTY()
	TArray<int32> MultiSelectedDroneIds;

	// Control lock state
	UPROPERTY()
	TMap<int32, EDroneControlLockReason> ControlLocks;

	// Current per-drone command mode used by point dispatch.
	UPROPERTY()
	TMap<int32, EDroneCommandMode> CommandModes;
};
