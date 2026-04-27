#pragma once

#include "CoreMinimal.h"
#include "DroneWaypointTypes.generated.h"

USTRUCT(BlueprintType)
struct FDroneWaypointExtensionData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Extension")
	TMap<FName, FString> Metadata;
};

USTRUCT(BlueprintType)
struct FDroneWaypoint
{
	GENERATED_BODY()

public:
	// Stored in local space relative to the owning path actor for stable serialization.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float WaitTime = 0.0f;

	// Speed in m/s from the previous waypoint to this waypoint. Index 0 is always forced to 0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint", meta = (ClampMin = "0.0", ClampMax = "15.0", UIMin = "0.0", UIMax = "15.0"))
	float SegmentSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waypoint")
	int32 Index = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Extension")
	FDroneWaypointExtensionData ExtensionData;
};
