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
