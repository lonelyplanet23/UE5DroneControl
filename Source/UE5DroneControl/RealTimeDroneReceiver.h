// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5DroneControlCharacter.h"
#include "DroneOps/Interfaces/DroneSelectableInterface.h"
#include "Networking.h"
#include "Sockets.h"
#include "RealTimeDroneReceiver.generated.h"

class UDroneTelemetryComponent;
class UDroneSelectionComponent;

/**
 * YAML format telemetry data structure
 */
USTRUCT(BlueprintType)
struct FDroneYAMLData
{
	GENERATED_BODY()

	UPROPERTY()
	int64 Timestamp = 0;

	// Position (NED, meters)
	UPROPERTY()
	FVector Position = FVector::ZeroVector;

	// Quaternion (X, Y, Z, W)
	UPROPERTY()
	FQuat Quaternion = FQuat::Identity;

	// Velocity (optional)
	UPROPERTY()
	FVector Velocity = FVector::ZeroVector;

	// Angular velocity (optional)
	UPROPERTY()
	FVector AngularVelocity = FVector::ZeroVector;
};

/**
 * Real-time drone receiver (ARealTimeDroneReceiver)
 * Polling mode, supports YAML telemetry.
 * Converts NED -> UE5 coordinates and drives the drone mesh.
 * Also implements IDroneSelectableInterface for click selection.
 */
UCLASS()
class UE5DRONECONTROL_API ARealTimeDroneReceiver : public AUE5DroneControlCharacter, public IDroneSelectableInterface
{
	GENERATED_BODY()

public:
	ARealTimeDroneReceiver();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void Tick(float DeltaTime) override;

	// ---- Drone Identity ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	FString DroneName = TEXT("UAV");

	// Integer DroneId (1/2/3...) - primary key in DroneRegistrySubsystem
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	int32 DroneId = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	int32 MavlinkSystemId = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	int32 BitIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	FLinearColor ThemeColor = FLinearColor::White;

	// ---- Selection Component ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDroneSelectionComponent> SelectionComponent;

	// ---- Telemetry Component ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDroneTelemetryComponent> TelemetryComponent;

	// ---- IDroneSelectableInterface ----
	virtual int32 GetDroneId_Implementation() const override { return DroneId; }
	virtual void OnPrimarySelected_Implementation() override;
	virtual void OnSecondarySelected_Implementation(bool bSelected) override;
	virtual void OnHoveredChanged_Implementation(bool bHovered) override;
	virtual void OnDeselected_Implementation() override;

	// ---- Receive Config ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	int32 ListenPort = 8888;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	bool bAutoDetectPort = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config", meta = (EditCondition = "bAutoDetectPort"))
	int32 PortScanStart = 7000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config", meta = (EditCondition = "bAutoDetectPort"))
	int32 PortScanEnd = 9000;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config", meta = (EditCondition = "bAutoDetectPort"))
	float AutoDetectTimeout = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float SmoothSpeed = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float ScaleFactor = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	bool bAutoFaceTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	bool bUseReceivedRotation = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float MaxUpdateFrequency = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RealTime Config")
	float RotationDeadZone = 0.5f;

private:
	FSocket* ListenSocket;

	FVector InitialLocation = FVector::ZeroVector;
	FVector TargetLocation;
	FRotator TargetRotation;
	FRotator LastRotation;
	FVector ReferencePosition = FVector::ZeroVector;
	bool bHasReceivedFirstData = false;

	int32 CurrentDetectedPort = -1;
	float AutoDetectStartTime = 0.0f;
	bool bReceivedDataInAutoDetect = false;
	float LastUpdateTime = 0.0f;

	TArray<uint8> PendingData;
	bool bHasPendingData = false;

	void ProcessPacket(const TArray<uint8>& Data);
	void UpdateRotationOnly(const TArray<uint8>& Data);
	bool ParseYAMLData(const FString& YAMLString, FDroneYAMLData& OutData);
	FRotator QuatToEuler(const FQuat& Q);
	FVector NEDToUE5(const FVector& NEDPos);
	void AutoDetectPort();
	bool CreateAndBindSocket(int32 Port);

	// Push current position/attitude into TelemetryComponent
	void PushTelemetry(const FDroneYAMLData& DroneData, const FVector& WorldPos, const FRotator& WorldRot);
};
