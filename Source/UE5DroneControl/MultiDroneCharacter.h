// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5DroneControlCharacter.h"
#include "DroneOps/Interfaces/DroneSelectableInterface.h"
#include "PathEditor/DronePathActor.h"
#include "MultiDroneCharacter.generated.h"

class UDroneSelectionComponent;
class UDroneCommandSenderComponent;
class UDroneGroundProjectionComponent;
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
	TObjectPtr<UDroneGroundProjectionComponent> GroundProjectionComponent;

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

	/**
	 * Isolate this shadow drone from backend assembly events while it is driven by a
	 * local-only path preview. Enabling it exits assembly immediately; disabling it
	 * resumes normal mirror following.
	 */
	UFUNCTION(BlueprintCallable, Category = "Path Preview")
	void SetLocalPathPreviewActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "Path Preview")
	bool IsLocalPathPreviewActive() const { return bLocalPathPreviewActive; }

	/** Pause or resume all local movement (click-target and assembly following). */
	UFUNCTION(BlueprintCallable, Category = "Assembly")
	void SetPaused(bool bPause);

	/**
	 * True while the shadow drone has not yet received a player command after power_on.
	 * In this state the shadow drone follows the mirror drone every tick.
	 * Automatically set to true on power_on/reconnect, false on first player command.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Assembly")
	bool bFollowingMirror = true;

	// ---- Delay metric ----

	/**
	 * Distance (cm) between this shadow drone and its mirror drone.
	 * Updated every Tick when a mirror drone is registered.
	 * Exposed to Blueprint for UI display.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Telemetry")
	float MirrorDelayDistance = 0.0f;

	// ---- IDroneSelectableInterface ----
	virtual int32 GetDroneId_Implementation() const override { return DroneId; }
	virtual void OnPrimarySelected_Implementation() override;
	virtual void OnSecondarySelected_Implementation(bool bSelected) override;
	virtual void OnHoveredChanged_Implementation(bool bHovered) override;
	virtual void OnDeselected_Implementation() override;

	virtual void SetClickTargetLocation(FVector TargetLocation, int32 Mode = 1) override;
	virtual void StopClickTargetSending() override;

	/**
	 * Move the shadow drone toward a full 3D world target (XYZ) as a one-shot local visual move.
	 * Unlike SetClickTargetLocation this honours the target Z and does NOT drive periodic WebSocket
	 * resends — the backend command is expected to be sent once separately by the dispatcher, and the
	 * backend owns reliable resend. Used by the geographic (lon/lat/alt) target dispatch.
	 */
	UFUNCTION(BlueprintCallable, Category = "Drone Network")
	void MoveToTarget3D(FVector WorldTarget);

	/** Set continuous vertical input (-1 down, +1 up). Local Z is handled separately from click movement. */
	void SetVerticalMoveInput(float Direction);

	/** Stop vertical movement. Optionally send the final height target once before stopping. */
	void StopVerticalMove(bool bSendFinalCommand = true);

	// ===== 敌对目标攻击 =====

	/** 停止巡逻并飞向目标点（由攻击确认弹窗触发） */
	UFUNCTION(BlueprintCallable, Category = "Drone|Attack")
	void StopPatrolAndAttack(const FVector& TargetLocation);

	/** 是否正在本地攻击中（UE预演） */
	UFUNCTION(BlueprintPure, Category = "Drone|Attack")
	bool IsLocalAttacking() const { return bIsLocalAttacking; }

	/** 是否已完成本地攻击 */
	UFUNCTION(BlueprintPure, Category = "Drone|Attack")
	bool IsLocalAttackCompleted() const { return bIsLocalAttackCompleted; }

	/** 获取当前攻击目标位置 */
	UFUNCTION(BlueprintPure, Category = "Drone|Attack")
	FVector GetAttackTargetLocation() const { return AttackTargetLocation; }

	/** 重置本地攻击状态（恢复巡逻） */
	UFUNCTION(BlueprintCallable, Category = "Drone|Attack")
	void ResetLocalAttackState();

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FWgs84GeographicDispatchLatentCommand;
#endif

	bool bInAssemblyMode = false;
	// Backend "assembling" events must not overwrite a local-only path preview.
	bool bLocalPathPreviewActive = false;
	bool bIsPaused = false;
	bool bWasMovingBeforePause = false;
	bool bVerticalMoveActive = false;
	float VerticalMoveDirection = 0.0f;
	FVector VerticalCommandTargetLocation = FVector::ZeroVector;

	// One-shot 3D local visual move (geographic target dispatch). Honours target Z; no WS resend.
	bool bOneShot3DMoveActive = false;
	FVector OneShot3DTarget = FVector::ZeroVector;

	/** Local move speed (cm/s) for the one-shot 3D move toward a geographic target. */
	UPROPERTY(EditAnywhere, Category = "Vertical Control", meta = (ClampMin = "1.0"))
	float OneShot3DMoveSpeedCmPerSec = 600.0f;

	/** Arrival threshold (cm) for the one-shot 3D move. */
	UPROPERTY(EditAnywhere, Category = "Vertical Control", meta = (ClampMin = "1.0"))
	float OneShot3DArriveThresholdCm = 50.0f;

	/** Local shadow-drone vertical speed in centimeters per second. */
	UPROPERTY(EditAnywhere, Category = "Vertical Control", meta = (ClampMin = "1.0"))
	float VerticalMoveSpeedCmPerSec = 300.0f;

	/** Minimum target height in centimeters relative to the drone GPS anchor. */
	UPROPERTY(EditAnywhere, Category = "Vertical Control")
	float MinVerticalHeightCm = 50.0f;

	/** Maximum target height in centimeters relative to the drone GPS anchor. */
	UPROPERTY(EditAnywhere, Category = "Vertical Control")
	float MaxVerticalHeightCm = 10000.0f;

	/**
	 * Shared WebSocket target interval for click and vertical movement.
	 * Backend currently consumes one command at 2 Hz, so the default is 0.5 seconds.
	 */
	UPROPERTY(EditAnywhere, Category = "Vertical Control", meta = (ClampMin = "0.05"))
	float WebSocketTargetSendIntervalSec = 0.5f;

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

	// Called on power_on / reconnect to sync shadow drone position to mirror drone
	void OnDroneWsEvent(int32 InDroneId, const FString& Event, double GpsLat, double GpsLon, double GpsAlt);

	// Send a WebSocket move command to the backend with anchor subtraction applied.
	void SendWebSocketMoveCommand();
	void RefreshVerticalCommandTarget();
	float GetVerticalAnchorWorldZ() const;

	float WsSendTimer = 0.0f;

	// ===== 本地攻击状态 =====

	/** 是否正在本地攻击中（UE-only） */
	bool bIsLocalAttacking = false;

	/** 本地攻击是否已完成（到达目标悬停） */
	bool bIsLocalAttackCompleted = false;

	/** 当前攻击目标位置 */
	FVector AttackTargetLocation = FVector::ZeroVector;

	/** 攻击到达阈值（厘米） */
	UPROPERTY(EditAnywhere, Category = "Drone|Attack", meta = (ClampMin = "10.0"))
	float AttackArrivalThresholdCm = 100.0f;

	/** 本地攻击移动速度（cm/s） */
	UPROPERTY(EditAnywhere, Category = "Drone|Attack", meta = (ClampMin = "100.0"))
	float LocalAttackSpeed = 500.0f;

	/** 攻击时是否朝向目标 */
	UPROPERTY(EditAnywhere, Category = "Drone|Attack")
	bool bOrientToAttackTarget = true;

	/** 缓存的 PathActor（用于暂停巡逻） */
	TWeakObjectPtr<ADronePathActor> CachedPathActor;
};
