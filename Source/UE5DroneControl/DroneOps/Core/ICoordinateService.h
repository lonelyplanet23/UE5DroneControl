// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ICoordinateService.generated.h"

/**
 * Interface for coordinate transformation services
 * Provides conversion between UE5 world coordinates, NED coordinates, and geographic coordinates
 */
UINTERFACE(MinimalAPI, Blueprintable)
class UCoordinateService : public UInterface
{
	GENERATED_BODY()
};

class ICoordinateService
{
	GENERATED_BODY()

public:
	/**
	 * Convert UE5 world coordinates to NED coordinates (meters)
	 * @param WorldLocation UE5 world location in centimeters
	 * @return NED location in meters (North, East, Down)
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Coordinate")
	FVector WorldToNed(const FVector& WorldLocation) const;
	virtual FVector WorldToNed_Implementation(const FVector& WorldLocation) const { return FVector::ZeroVector; }

	/**
	 * Convert NED coordinates to UE5 world coordinates
	 * @param NedLocation NED location in meters (North, East, Down)
	 * @return UE5 world location in centimeters
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Coordinate")
	FVector NedToWorld(const FVector& NedLocation) const;
	virtual FVector NedToWorld_Implementation(const FVector& NedLocation) const { return FVector::ZeroVector; }

	/**
	 * Convert UE5 world coordinates to geographic coordinates (optional, for Cesium)
	 * @param WorldLocation UE5 world location in centimeters
	 * @return Geographic location (Longitude, Latitude, HeightMeters), matching ACesiumGeoreference.
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Coordinate")
	FVector WorldToGeographic(const FVector& WorldLocation) const;
	virtual FVector WorldToGeographic_Implementation(const FVector& WorldLocation) const { return FVector::ZeroVector; }

	/**
	 * Convert geographic coordinates to UE5 world coordinates (optional, for Cesium)
	 * @param Latitude Latitude in degrees
	 * @param Longitude Longitude in degrees
	 * @param Altitude Altitude in meters
	 * @return UE5 world location in centimeters
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Coordinate")
	FVector GeographicToWorld(double Latitude, double Longitude, double Altitude) const;
	virtual FVector GeographicToWorld_Implementation(double Latitude, double Longitude, double Altitude) const { return FVector::ZeroVector; }

	/**
	 * Check if geographic coordinate conversion is supported
	 * @return True if geographic conversion is available (Cesium), false otherwise
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Coordinate")
	bool IsGeographicSupported() const;
	virtual bool IsGeographicSupported_Implementation() const { return false; }

	/**
	 * Check if coordinate system is ready for use
	 * @return True if coordinate system is initialized
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Coordinate")
	bool IsCoordinateSystemReady() const;
	virtual bool IsCoordinateSystemReady_Implementation() const { return false; }
};
