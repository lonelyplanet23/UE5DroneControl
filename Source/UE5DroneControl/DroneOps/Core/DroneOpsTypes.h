// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DroneOpsTypes.generated.h"

/**
 * Camera mode for drone operations
 */
UENUM(BlueprintType)
enum class EDroneCameraMode : uint8
{
	Follow UMETA(DisplayName = "Follow Drone"),
	Free UMETA(DisplayName = "Free Camera"),
	TopDown UMETA(DisplayName = "Top Down")
};

/**
 * Drone availability status
 */
UENUM(BlueprintType)
enum class EDroneAvailability : uint8
{
	Online UMETA(DisplayName = "Online"),
	Offline UMETA(DisplayName = "Offline"),
	Lost UMETA(DisplayName = "Lost Connection")
};

// ========== task ==========
UENUM(BlueprintType)
enum class EDroneTaskState : uint8
{
	Standby     UMETA(DisplayName = "待命"),
	Assembling  UMETA(DisplayName = "集结中"),
	Moving      UMETA(DisplayName = "移动中"),
	Scouting    UMETA(DisplayName = "侦察中"),
	Patrolling  UMETA(DisplayName = "巡逻中"),
	Attacking   UMETA(DisplayName = "攻击中"),
	Paused      UMETA(DisplayName = "暂停"),
	Avoiding    UMETA(DisplayName = "避障中"),
	Completed   UMETA(DisplayName = "已完成"),
	Error       UMETA(DisplayName = "异常")
};

// ========== UE-only ==========
UENUM(BlueprintType)
enum class EUELocalDroneState : uint8
{
	None                    UMETA(DisplayName = "无"),
	TargetDetectedPending   UMETA(DisplayName = "目标确认待命"),
	LocalAttacking          UMETA(DisplayName = "攻击中（UE预演）"),
	LocalAttackCompleted    UMETA(DisplayName = "UE攻击完成"),
	TargetDeclined          UMETA(DisplayName = "用户拒绝攻击"),
	/** 仅供本地预演测试使用：允许离线影子机参与巡逻目标识别。 */
	LocalPatrolling         UMETA(DisplayName = "本地巡逻预演")
};

/**
 * Reason why drone control is locked
 */
UENUM(BlueprintType)
enum class EDroneControlLockReason : uint8
{
	None UMETA(DisplayName = "Not Locked"),
	FormationPlayback UMETA(DisplayName = "Formation Playback"),
	Offline UMETA(DisplayName = "Drone Offline"),
	InvalidTarget UMETA(DisplayName = "Invalid Target")
};

/**
 * UE -> Backend command mode for target-point commands.
 * Protocol strings are: move / scout / patrol / attack.
 */
UENUM(BlueprintType)
enum class EDroneCommandMode : uint8
{
	Move UMETA(DisplayName = "Move"),
	Scout UMETA(DisplayName = "Scout"),
	Patrol UMETA(DisplayName = "Patrol"),
	Attack UMETA(DisplayName = "Attack")
};

static inline EDroneTaskState DroneTaskStateFromProtocolString(const FString& StateText)
{
	const FString State = StateText.ToLower();
	if (State == TEXT("moving")) return EDroneTaskState::Moving;
	if (State == TEXT("assembling")) return EDroneTaskState::Assembling;
	if (State == TEXT("scouting")) return EDroneTaskState::Scouting;
	if (State == TEXT("patrolling")) return EDroneTaskState::Patrolling;
	if (State == TEXT("attacking")) return EDroneTaskState::Attacking;
	if (State == TEXT("paused")) return EDroneTaskState::Paused;
	if (State == TEXT("avoiding")) return EDroneTaskState::Avoiding;
	if (State == TEXT("completed")) return EDroneTaskState::Completed;
	if (State == TEXT("error")) return EDroneTaskState::Error;
	return EDroneTaskState::Standby;
}

static inline FString DroneCommandModeToProtocolString(EDroneCommandMode Mode)
{
	switch (Mode)
	{
	case EDroneCommandMode::Scout:
		return TEXT("scout");
	case EDroneCommandMode::Patrol:
		return TEXT("patrol");
	case EDroneCommandMode::Attack:
		return TEXT("attack");
	case EDroneCommandMode::Move:
	default:
		return TEXT("move");
	}
}

static inline EDroneCommandMode DroneCommandModeFromProtocolString(const FString& ModeText)
{
	const FString Normalized = ModeText.ToLower();
	if (Normalized == TEXT("scout") || Normalized == TEXT("recon"))
	{
		return EDroneCommandMode::Scout;
	}
	if (Normalized == TEXT("patrol"))
	{
		return EDroneCommandMode::Patrol;
	}
	if (Normalized == TEXT("attack"))
	{
		return EDroneCommandMode::Attack;
	}
	return EDroneCommandMode::Move;
}

static inline FText DroneCommandModeToDisplayText(EDroneCommandMode Mode)
{
	switch (Mode)
	{
	case EDroneCommandMode::Scout:
		return FText::FromString(TEXT("侦察"));
	case EDroneCommandMode::Patrol:
		return FText::FromString(TEXT("巡逻"));
	case EDroneCommandMode::Attack:
		return FText::FromString(TEXT("攻击"));
	case EDroneCommandMode::Move:
	default:
		return FText::FromString(TEXT("移动"));
	}
}

/**
 * Drone descriptor - static identity information
 */
USTRUCT(BlueprintType)
struct FDroneDescriptor
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone")
	FString Name = TEXT("UAV");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone")
	int32 DroneId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone")
	int32 Slot = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone")
	FString BackendIdString;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone")
	FString IpAddress;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone")
	int32 ControlPort = 8889;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone")
	int32 MavlinkSystemId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone")
	int32 BitIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone")
	FLinearColor ThemeColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone")
	int32 UEReceivePort = 8888;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone")
	FString TopicPrefix = TEXT("/px4_1");

	/** Browser-playable MediaMTX WebRTC page for this drone (for example http://mediamtx:8889/drone-1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video")
	FString VideoUrl;

	FDroneDescriptor() = default;
};

/**
 * Drone telemetry snapshot - runtime state
 */
USTRUCT(BlueprintType)
struct FDroneTelemetrySnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	int32 DroneId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	EDroneAvailability Availability = EDroneAvailability::Offline;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	FVector NedLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	FVector GeographicLocation = FVector::ZeroVector; // Lat, Lon, Alt
	
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	FRotator Attitude = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	float Altitude = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	int32 Battery = -1;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	bool bArmed = false;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	bool bOffboard = false;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	bool bGpsFix = false;

	/** True only for PX4 VehicleLocalPosition in the TrajectorySetpoint NED frame. */
	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	bool bLocalPositionValid = false;

	UPROPERTY(BlueprintReadOnly, Category = "Telemetry")
	double LastUpdateTime = 0.0;

	// ===== adding =====
	// 1. GPS
	UPROPERTY(BlueprintReadOnly) double GpsLatitude = 0.0;
	UPROPERTY(BlueprintReadOnly) double GpsLongitude = 0.0;
	UPROPERTY(BlueprintReadOnly) double GpsAltitude = 0.0;  

	// 2.BatteryPercent 
	UPROPERTY(BlueprintReadOnly) int32 BatteryPercent = -1; 

	// 3. taskstate
	UPROPERTY(BlueprintReadOnly) EDroneCommandMode TaskMode = EDroneCommandMode::Move;
	UPROPERTY(BlueprintReadOnly) EDroneTaskState TaskState = EDroneTaskState::Standby;
	UPROPERTY(BlueprintReadOnly) FString TaskErrorDetail;

	// 4. waypoint info
	UPROPERTY(BlueprintReadOnly) int32 CurrentWaypointIndex = 0;
	UPROPERTY(BlueprintReadOnly) int32 TotalWaypoints = 0;

	// 5. UE-only
	UPROPERTY(BlueprintReadOnly) EUELocalDroneState LocalState = EUELocalDroneState::None;

	FDroneTelemetrySnapshot() = default;
};

USTRUCT(BlueprintType)
struct FDroneTaskStateSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Task")
	int32 DroneId = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Task")
	FString ArrayId;
	UPROPERTY(BlueprintReadOnly, Category = "Task")
	EDroneCommandMode Mode = EDroneCommandMode::Move;
	UPROPERTY(BlueprintReadOnly, Category = "Task")
	EDroneTaskState State = EDroneTaskState::Standby;
	UPROPERTY(BlueprintReadOnly, Category = "Task")
	int32 CurrentWaypoint = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Task")
	int32 WaypointCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "Task")
	FString Detail;
	UPROPERTY(BlueprintReadOnly, Category = "Task")
	double UpdatedAt = 0.0;
};

/**
 * Drone target command
 */
USTRUCT(BlueprintType)
struct FDroneTargetCommand
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Command")
	int32 DroneId = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Command")
	FVector TargetWorldLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Command")
	FVector TargetNedLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Command")
	FVector TargetGeographicLocation = FVector::ZeroVector; // Optional

	UPROPERTY(BlueprintReadWrite, Category = "Command")
	int32 Mode = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Command")
	double IssuedAt = 0.0;

	FDroneTargetCommand() = default;
};

/**
 * Multi-drone control packet (32 bytes UDP structure)
 */
USTRUCT(BlueprintType)
struct FMultiDroneControlPacket
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Control")
	double Timestamp = 0.0;

	UPROPERTY(BlueprintReadWrite, Category = "Control")
	float X = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Control")
	float Y = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Control")
	float Z = 0.0f;

	UPROPERTY(BlueprintReadWrite, Category = "Control")
	int32 Mode = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Control")
	int32 DroneMask = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Control")
	int32 Sequence = 0;

	FMultiDroneControlPacket() = default;
};

/**
 * Per-drone top-label display settings (name tag above the drone in 3D).
 * Stored in DroneRegistrySubsystem and propagated to both shadow and mirror actors.
 */
USTRUCT(BlueprintType)
struct FDroneLabelSettings
{
	GENERATED_BODY()

	/** Label text shown above the drone.  Starts equal to the drone's registered name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DroneLabel")
	FString DisplayName;

	/** Colour of the label text.  Default matches the panel body text (not a connection-status colour). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DroneLabel")
	FLinearColor LabelColor = FLinearColor(0.86f, 0.91f, 0.97f, 1.0f);

	/** Font size for the 3D label widget only.  Does not affect the sidebar panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DroneLabel")
	int32 FontSize = 16;

	FDroneLabelSettings() = default;
	explicit FDroneLabelSettings(const FString& InName)
		: DisplayName(InName)
	{}
};

/**
 * Map center configuration
 */
USTRUCT(BlueprintType)
struct FMapCenterConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	double Latitude = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	double Longitude = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	double Altitude = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Map")
	bool bIsInitialized = false;

	FMapCenterConfig() = default;
};

/**
 * Camera mode state
 */
USTRUCT(BlueprintType)
struct FCameraModeState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Camera")
	EDroneCameraMode CameraMode = EDroneCameraMode::Follow;

	UPROPERTY(BlueprintReadWrite, Category = "Camera")
	int32 FollowDroneId = 0;

	UPROPERTY(BlueprintReadWrite, Category = "Camera")
	FVector LastFollowLocation = FVector::ZeroVector;

	UPROPERTY(BlueprintReadWrite, Category = "Camera")
	FRotator LastFollowRotation = FRotator::ZeroRotator;

	FCameraModeState() = default;
};

/**
 * Formation member slot
 */
USTRUCT(BlueprintType)
struct FFormationMemberSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	int32 DroneId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	FString RoleName = TEXT("Member");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	FVector RelativeOffset = FVector::ZeroVector;

	FFormationMemberSlot() = default;
};

/**
 * Formation step
 */
USTRUCT(BlueprintType)
struct FFormationStep
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	int32 StepId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	float DurationSec = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Formation")
	TMap<int32, FVector> MemberTargets;

	FFormationStep() = default;
};

/**
 * Formation execution state
 */
USTRUCT(BlueprintType)
struct FFormationExecutionState
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Formation")
	int32 PresetId = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Formation")
	int32 StepIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Formation")
	float ElapsedSec = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Formation")
	bool bPaused = false;

	UPROPERTY(BlueprintReadOnly, Category = "Formation")
	TArray<int32> ParticipantIds;

	FFormationExecutionState() = default;
};
