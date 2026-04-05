// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOps/Drone/DroneTelemetryComponent.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

UDroneTelemetryComponent::UDroneTelemetryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 1.0f; // Check offline status at 1 Hz
}

void UDroneTelemetryComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UDroneTelemetryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bHasValidTelemetry)
	{
		return;
	}

	// Mark as Lost if no update received within timeout
	const double Now = FPlatformTime::Seconds();
	if ((Now - LastUpdateRealTime) > OfflineTimeoutSec)
	{
		if (CurrentSnapshot.Availability != EDroneAvailability::Lost)
		{
			CurrentSnapshot.Availability = EDroneAvailability::Lost;
			PropagateToRegistry();
		}
	}
}

void UDroneTelemetryComponent::PushSnapshot(const FDroneTelemetrySnapshot& Snapshot)
{
	CurrentSnapshot = Snapshot;
	CurrentSnapshot.Availability = EDroneAvailability::Online;

	// LastUpdateRealTime uses FPlatformTime for interval calculations in C++
	LastUpdateRealTime = FPlatformTime::Seconds();
	// CurrentSnapshot.LastUpdateTime uses world time for UI display (blueprint does Now - LastUpdateTime)
	if (GetWorld())
	{
		CurrentSnapshot.LastUpdateTime = GetWorld()->GetTimeSeconds();
	}

	bHasValidTelemetry = true;

	OnSnapshotUpdated.Broadcast(CurrentSnapshot);
	PropagateToRegistry();
}

float UDroneTelemetryComponent::GetSecondsSinceLastUpdate() const
{
	if (!bHasValidTelemetry)
	{
		return TNumericLimits<float>::Max();
	}
	return static_cast<float>(FPlatformTime::Seconds() - LastUpdateRealTime);
}

void UDroneTelemetryComponent::PropagateToRegistry()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UDroneRegistrySubsystem* Registry = GameInstance->GetSubsystem<UDroneRegistrySubsystem>();
	if (Registry && CurrentSnapshot.DroneId > 0)
	{
		Registry->UpdateTelemetry(CurrentSnapshot.DroneId, CurrentSnapshot);
	}
}
