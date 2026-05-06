// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5DroneControlCharacter.h"
#include "DroneOps/Interfaces/DroneSelectableInterface.h"
#include "MultiDroneCharacter.generated.h"

class UDroneSelectionComponent;
class UDroneCommandSenderComponent;
class UWidgetComponent;

/**
 * Multi-drone sender pawn.
 * Implements IDroneSelectableInterface so DroneOpsPlayerController can
 * identify and select it via a hit-test.
 */
UCLASS()
class UE5DRONECONTROL_API AMultiDroneCharacter : public AUE5DroneControlCharacter, public IDroneSelectableInterface
{
	GENERATED_BODY()

public:
	AMultiDroneCharacter();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

	// ---- Identity ----

	// Display name shown in UI (e.g. "UAV1")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	FString DroneName = TEXT("UAV");

	// Integer DroneId (1 / 2 / 3 …) – primary key in DroneRegistrySubsystem
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	int32 DroneId = 1;

	// MAVLink system id (2 / 3 / 4 …)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	int32 MavlinkSystemId = 2;

	// Bit position in the 32-bit selection mask (0 / 1 / 2 …)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	int32 BitIndex = 0;

	// UI / outline theme colour
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	FLinearColor ThemeColor = FLinearColor::White;

	// ROS2 topic prefix (e.g. "/px4_1")
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	FString TopicPrefix = TEXT("/px4_1");

	// UE5 telemetry receive port (8888 / 8890 / 8892)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	int32 UEReceivePort = 8888;

	// Legacy 0-based index kept for backward compat (optional)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Identity")
	int32 DroneIndex = 0;

	// ---- Components ----

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDroneSelectionComponent> SelectionComponent;

	// CommandSenderComponent is optional here; the canonical sender lives on
	// MultiDroneManager.  We keep a reference so blueprints can call it directly.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UDroneCommandSenderComponent> CommandSenderComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWidgetComponent> SelectionWidgetComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Selection UI")
	FText SelectedWidgetText = FText::FromString(TEXT("已选中"));

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Selection UI")
	FVector SelectionWidgetRelativeLocation = FVector(0.0f, 0.0f, 180.0f);

	// ---- Assembly (集结) behavior ----

	/**
	 * Enter assembly mode: shadow drone stops independent movement and
	 * follows the mirror drone (ARealTimeDroneReceiver with same DroneId).
	 * Called when "assembling" WebSocket event is received.
	 */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	void EnterAssemblyMode();

	/**
	 * Exit assembly mode: shadow drone resumes independent movement.
	 * Called when "assembly_complete" or "assembly_timeout" WebSocket event is received.
	 */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	void ExitAssemblyMode();

	UFUNCTION(BlueprintPure, Category = "Assembly")
	bool IsInAssemblyMode() const { return bInAssemblyMode; }

	// ---- Delay metric ----

	/**
	 * Distance (cm) between this shadow drone and its mirror drone.
	 * Updated every Tick when a mirror drone is registered.
	 * Exposed to Blueprint for UI display.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	float MirrorDelayDistance = 0.0f;

	// ---- IDroneSelectableInterface ----
	virtual int32 GetDroneId_Implementation() const override { return DroneId; }
	virtual void OnPrimarySelected_Implementation() override;
	virtual void OnSecondarySelected_Implementation(bool bSelected) override;
	virtual void OnHoveredChanged_Implementation(bool bHovered) override;
	virtual void OnDeselected_Implementation() override;

	virtual void SetClickTargetLocation(FVector TargetLocation, int32 Mode = 1) override;
	virtual void StopClickTargetSending() override;

private:
	// Called on power_on / reconnect to sync shadow drone position to mirror drone
	void OnDroneWsEvent(int32 InDroneId, const FString& Event, double GpsLat, double GpsLon, double GpsAlt);
	bool bInAssemblyMode = false;

	// Smooth speed for following mirror drone position (cm/s interp speed)
	UPROPERTY(EditAnywhere, Category = "Assembly", meta = (ClampMin = "1.0"))
	float AssemblyFollowInterpSpeed = 8.0f;

	// Cached registry reference (set in BeginPlay)
	UPROPERTY()
	TObjectPtr<class UDroneRegistrySubsystem> Registry;

	void SubscribeToAssemblyEvents();
	void OnAssemblingProgress(const FString& ArrayId, int32 ReadyCount, int32 TotalCount);
	void OnAssemblyComplete(const FString& ArrayId);
	void OnAssemblyTimeout(const FString& ArrayId, int32 ReadyCount, int32 TotalCount);
};
