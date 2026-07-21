// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GeographicTypes.generated.h"

/**
 * Geographic coordinate system used when dispatching a target by
 * longitude / latitude / altitude.
 *
 * Currently only WGS84 is supported; the enum exists so future coordinate
 * systems (e.g. GCJ-02, CGCS2000) can be added without changing the widget
 * or dispatch API signature.
 */
UENUM(BlueprintType)
enum class EGeographicCoordinateSystem : uint8
{
	WGS84 UMETA(DisplayName = "WGS84")
};

/** A WGS84 three-dimensional coordinate entered as (longitude, latitude, altitude MSL). */
USTRUCT(BlueprintType)
struct FGeographicCoordinate3D
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geographic Coordinate")
	double Longitude = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geographic Coordinate")
	double Latitude = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geographic Coordinate")
	double AltitudeMslMeters = 0.0;
};

/** Exact geographic target assigned to one selected drone. */
USTRUCT(BlueprintType)
struct FDroneGeographicTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geographic Dispatch")
	int32 DroneId = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Geographic Dispatch")
	FGeographicCoordinate3D Coordinate;
};

/** Shared parser/formatter for copy-pasted (longitude, latitude, altitude) vectors. */
class UE5DRONECONTROL_API FGeographicCoordinateTextParser
{
public:
	static bool Parse(const FString& Text, FGeographicCoordinate3D& OutCoordinate, FString& OutError);

	/**
	 * Strictly parses a complete sequence of tuples:
	 * (longitude,latitude,altitude)(longitude,latitude,altitude)
	 * Only whitespace may appear between tuples. One invalid tuple rejects the entire batch.
	 */
	static bool ParseBatch(
		const FString& Text,
		TArray<FGeographicCoordinate3D>& OutCoordinates,
		FString& OutError);

	static FString Format(const FGeographicCoordinate3D& Coordinate);
};

/**
 * Builds an absolute PX4-local target using one fresh, time-coherent GPS/local-NED
 * sample. Vectors use the existing wire convention: X=North cm, Y=East cm,
 * Z=Up cm.
 */
class UE5DRONECONTROL_API FGeographicDispatchOffsetCalculator
{
public:
	static bool CalculateBackendRelativeOffset(
		const FGeographicCoordinate3D& TargetCoordinate,
		const FGeographicCoordinate3D& CurrentCoordinate,
		const FVector& CurrentLocalUeOffset,
		const FVector& AdditionalLocalUeOffset,
		FVector& OutRelativeOffset);
};

/**
 * Result of a geographic target dispatch (or preview) request.
 */
USTRUCT(BlueprintType)
struct FGeographicDispatchResult
{
	GENERATED_BODY()

	/** True if the request succeeded (targets computed; commands sent unless preview-only). */
	UPROPERTY(BlueprintReadOnly, Category = "Geographic Dispatch")
	bool bSuccess = false;

	/** Number of drones that received a target (0 for a failed request). */
	UPROPERTY(BlueprintReadOnly, Category = "Geographic Dispatch")
	int32 DispatchedCount = 0;

	/** Human-readable status / error message for the UI. */
	UPROPERTY(BlueprintReadOnly, Category = "Geographic Dispatch")
	FString Message;

	FGeographicDispatchResult() = default;
};
