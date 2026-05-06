// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DroneOps/Core/SimpleCoordinateService.h"
#include "CesiumCoordinateService.generated.h"

class ACesiumGeoreference;

/**
 * Cesium-backed coordinate service.
 * GPS (lat/lon/alt) → UE5 world coordinates via CesiumGeoreference.
 * NED ↔ UE offset conversion reuses SimpleCoordinateService linear formulas.
 */
UCLASS()
class UE5DRONECONTROL_API UCesiumCoordinateService : public USimpleCoordinateService
{
	GENERATED_BODY()

public:
	/** Call once after construction to cache the CesiumGeoreference from the world. */
	void Initialize(UWorld* World);

	// ICoordinateService overrides
	virtual FVector GeographicToWorld_Implementation(double Latitude, double Longitude, double Altitude) const override;
	virtual FVector WorldToGeographic_Implementation(const FVector& WorldLocation) const override;
	virtual bool IsGeographicSupported_Implementation() const override;
	virtual bool IsCoordinateSystemReady_Implementation() const override;

	/**
	 * Log axis alignment info: checks which UE5 axis corresponds to North and East
	 * at the Georeference origin. Results are printed to UE_LOG.
	 */
	void ValidateAxisAlignment() const;

private:
	UPROPERTY()
	TObjectPtr<ACesiumGeoreference> Georeference;
};
