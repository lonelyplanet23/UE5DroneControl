#include "DroneLinkMonitorSubsystem.h"
#include "Engine/World.h"

UDroneLinkMonitorSubsystem::UDroneLinkMonitorSubsystem()
{
}

void UDroneLinkMonitorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("DroneLinkMonitorSubsystem initialized"));
}

void UDroneLinkMonitorSubsystem::Deinitialize()
{
	LatencyMap.Empty();
	DelayHistory.Empty();
	LastSentTimestamps.Empty();
	LastSentSequences.Empty();
	ExpectedSequences.Empty();
	PreviousLinkQuality.Empty();
	Super::Deinitialize();
}

void UDroneLinkMonitorSubsystem::Tick(float DeltaTime)
{
	CheckStaleLinks();
}

void UDroneLinkMonitorSubsystem::RecordCommandSent(int32 DroneId, int32 Sequence, double SendTimestamp)
{
	LastSentTimestamps.Add(DroneId, SendTimestamp);
	LastSentSequences.Add(DroneId, Sequence);

	FDroneLatencyStats& Stats = LatencyMap.FindOrAdd(DroneId);
	Stats.DroneId = DroneId;
	Stats.PacketsSent++;
	TotalPacketsSent++;

	if (!ExpectedSequences.Contains(DroneId))
	{
		ExpectedSequences.Add(DroneId, Sequence);
	}
}

void UDroneLinkMonitorSubsystem::RecordTelemetryReceived(int32 DroneId, double TelemetryTimestamp)
{
	const double Now = FPlatformTime::Seconds();

	FDroneLatencyStats& Stats = LatencyMap.FindOrAdd(DroneId);
	Stats.DroneId = DroneId;
	Stats.PacketsReceived++;
	TotalPacketsReceived++;

	const double* pSentTime = LastSentTimestamps.Find(DroneId);
	if (pSentTime && *pSentTime > 0.0)
	{
		float Rtt = static_cast<float>((Now - *pSentTime) * 1000.0);
		Stats.RoundTripDelayMs = Rtt;
		Stats.UplinkDelayMs = Rtt * 0.5f;
		Stats.DownlinkDelayMs = Rtt * 0.5f;

		UpdateDelayHistory(DroneId, Rtt);
	}

	Stats.LastUpdateTime = Now;
	Stats.UpdateQuality();

	const ELinkQuality OldQuality = PreviousLinkQuality.FindRef(DroneId);
	if (OldQuality != Stats.LinkQuality)
	{
		PreviousLinkQuality.Add(DroneId, Stats.LinkQuality);
		OnLinkQualityChanged.Broadcast(DroneId, Stats.LinkQuality);
	}

	OnLatencyStatsUpdated.Broadcast(DroneId, Stats);
}

void UDroneLinkMonitorSubsystem::RecordPacketLost(int32 DroneId)
{
	FDroneLatencyStats& Stats = LatencyMap.FindOrAdd(DroneId);
	Stats.DroneId = DroneId;
	Stats.PacketsLost++;

	int32 TotalExpected = Stats.PacketsReceived + Stats.PacketsLost;
	if (TotalExpected > 0)
	{
		Stats.PacketLossRate = static_cast<float>(Stats.PacketsLost) / static_cast<float>(TotalExpected);
	}
}

void UDroneLinkMonitorSubsystem::UpdateDelayHistory(int32 DroneId, float DelayMs)
{
	TArray<float>& History = DelayHistory.FindOrAdd(DroneId);
	History.Add(DelayMs);

	if (History.Num() > HistoryWindowSize)
	{
		History.RemoveAt(0);
	}

	FDroneLatencyStats& Stats = LatencyMap.FindOrAdd(DroneId);
	Stats.DroneId = DroneId;

	if (History.Num() > 0)
	{
		float Sum = 0.0f;
		Stats.MaxDelayMs = TNumericLimits<float>::Lowest();
		Stats.MinDelayMs = TNumericLimits<float>::Max();

		for (float Val : History)
		{
			Sum += Val;
			if (Val > Stats.MaxDelayMs) Stats.MaxDelayMs = Val;
			if (Val < Stats.MinDelayMs) Stats.MinDelayMs = Val;
		}

		Stats.AvgDelayMs = Sum / History.Num();

		if (History.Num() >= 2)
		{
			float JitterSum = 0.0f;
			for (int32 i = 1; i < History.Num(); i++)
			{
				JitterSum += FMath::Abs(History[i] - History[i - 1]);
			}
			Stats.JitterMs = JitterSum / (History.Num() - 1);
		}
	}
}

void UDroneLinkMonitorSubsystem::CheckStaleLinks()
{
	const double Now = FPlatformTime::Seconds();

	for (auto& Pair : LatencyMap)
	{
		int32 DroneId = Pair.Key;
		FDroneLatencyStats& Stats = Pair.Value;

		if (Stats.PacketsReceived > 0 && (Now - Stats.LastUpdateTime) > StaleTimeoutSec)
		{
			ELinkQuality OldQuality = Stats.LinkQuality;
			Stats.LinkQuality = ELinkQuality::Disconnected;

			if (OldQuality != Stats.LinkQuality)
			{
				PreviousLinkQuality.Add(DroneId, Stats.LinkQuality);
				OnLinkQualityChanged.Broadcast(DroneId, Stats.LinkQuality);
			}
		}
	}
}

bool UDroneLinkMonitorSubsystem::GetLatencyStats(int32 DroneId, FDroneLatencyStats& OutStats) const
{
	const FDroneLatencyStats* Found = LatencyMap.Find(DroneId);
	if (Found)
	{
		OutStats = *Found;
		return true;
	}
	return false;
}

ELinkQuality UDroneLinkMonitorSubsystem::GetLinkQuality(int32 DroneId) const
{
	const FDroneLatencyStats* Found = LatencyMap.Find(DroneId);
	return Found ? Found->LinkQuality : ELinkQuality::Disconnected;
}

float UDroneLinkMonitorSubsystem::GetRoundTripDelayMs(int32 DroneId) const
{
	const FDroneLatencyStats* Found = LatencyMap.Find(DroneId);
	return Found ? Found->RoundTripDelayMs : -1.0f;
}

float UDroneLinkMonitorSubsystem::GetAverageDelayMs(int32 DroneId) const
{
	const FDroneLatencyStats* Found = LatencyMap.Find(DroneId);
	return Found ? Found->AvgDelayMs : -1.0f;
}

float UDroneLinkMonitorSubsystem::GetPacketLossRate(int32 DroneId) const
{
	const FDroneLatencyStats* Found = LatencyMap.Find(DroneId);
	return Found ? Found->PacketLossRate : -1.0f;
}

TArray<FDroneLatencyStats> UDroneLinkMonitorSubsystem::GetAllLatencyStats() const
{
	TArray<FDroneLatencyStats> Result;
	LatencyMap.GenerateValueArray(Result);
	return Result;
}
