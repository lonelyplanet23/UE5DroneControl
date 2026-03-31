#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneLinkMonitorSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLatencyStatsUpdated, int32, DroneId, const FDroneLatencyStats&, Stats);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnLinkQualityChanged, int32, DroneId, ELinkQuality, NewQuality);

UCLASS()
class UE5DRONECONTROL_API UDroneLinkMonitorSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UDroneLinkMonitorSubsystem();

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "LinkMonitor")
	void RecordCommandSent(int32 DroneId, int32 Sequence, double SendTimestamp);

	UFUNCTION(BlueprintCallable, Category = "LinkMonitor")
	void RecordTelemetryReceived(int32 DroneId, double TelemetryTimestamp);

	UFUNCTION(BlueprintCallable, Category = "LinkMonitor")
	void RecordPacketLost(int32 DroneId);

	UFUNCTION(BlueprintPure, Category = "LinkMonitor")
	bool GetLatencyStats(int32 DroneId, FDroneLatencyStats& OutStats) const;

	UFUNCTION(BlueprintPure, Category = "LinkMonitor")
	ELinkQuality GetLinkQuality(int32 DroneId) const;

	UFUNCTION(BlueprintPure, Category = "LinkMonitor")
	float GetRoundTripDelayMs(int32 DroneId) const;

	UFUNCTION(BlueprintPure, Category = "LinkMonitor")
	float GetAverageDelayMs(int32 DroneId) const;

	UFUNCTION(BlueprintPure, Category = "LinkMonitor")
	float GetPacketLossRate(int32 DroneId) const;

	UFUNCTION(BlueprintPure, Category = "LinkMonitor")
	TArray<FDroneLatencyStats> GetAllLatencyStats() const;

	UFUNCTION(BlueprintPure, Category = "LinkMonitor")
	int32 GetTotalPacketsSent() const { return TotalPacketsSent; }

	UFUNCTION(BlueprintPure, Category = "LinkMonitor")
	int32 GetTotalPacketsReceived() const { return TotalPacketsReceived; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LinkMonitor")
	float StaleTimeoutSec = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "LinkMonitor")
	int32 HistoryWindowSize = 50;

	UPROPERTY(BlueprintAssignable, Category = "LinkMonitor")
	FOnLatencyStatsUpdated OnLatencyStatsUpdated;

	UPROPERTY(BlueprintAssignable, Category = "LinkMonitor")
	FOnLinkQualityChanged OnLinkQualityChanged;

private:
	UPROPERTY()
	TMap<int32, FDroneLatencyStats> LatencyMap;

	UPROPERTY()
	TMap<int32, TArray<float>> DelayHistory;

	UPROPERTY()
	TMap<int32, double> LastSentTimestamps;

	UPROPERTY()
	TMap<int32, int32> LastSentSequences;

	UPROPERTY()
	TMap<int32, int32> ExpectedSequences;

	UPROPERTY()
	TMap<int32, ELinkQuality> PreviousLinkQuality;

	int32 TotalPacketsSent = 0;
	int32 TotalPacketsReceived = 0;

	void UpdateDelayHistory(int32 DroneId, float DelayMs);
	void CheckStaleLinks();
};
