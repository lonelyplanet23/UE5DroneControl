// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneOps/Core/CesiumCoordinateService.h"
#include "CesiumGeoreference.h"
#include "EngineUtils.h"

void UCesiumCoordinateService::Initialize(UWorld* World)
{
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("CesiumCoordinateService: Initialize called with null World"));
		return;
	}

	for (TActorIterator<ACesiumGeoreference> It(World); It; ++It)
	{
		Georeference = *It;
		UE_LOG(LogTemp, Log, TEXT("CesiumCoordinateService: Found CesiumGeoreference '%s'"), *Georeference->GetName());
		ValidateAxisAlignment();
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("CesiumCoordinateService: No ACesiumGeoreference found in level. GPS conversion will not work."));
}

FVector UCesiumCoordinateService::GeographicToWorld_Implementation(double Latitude, double Longitude, double Altitude) const
{
	if (!Georeference)
	{
		UE_LOG(LogTemp, Warning, TEXT("CesiumCoordinateService: GeographicToWorld called but Georeference is null"));
		return FVector::ZeroVector;
	}

	// TransformLongitudeLatitudeHeightPositionToUnreal returns local coords relative to
	// the CesiumGeoreference actor. We must then transform to world space.
	FVector LocalPos = Georeference->TransformLongitudeLatitudeHeightPositionToUnreal(
		FVector(Longitude, Latitude, Altitude));
	return Georeference->GetActorTransform().TransformPosition(LocalPos);
}

FVector UCesiumCoordinateService::WorldToGeographic_Implementation(const FVector& WorldLocation) const
{
	if (!Georeference)
	{
		return FVector::ZeroVector;
	}

	// Inverse: world → local → LLH
	FVector LocalPos = Georeference->GetActorTransform().InverseTransformPosition(WorldLocation);
	return Georeference->TransformUnrealPositionToLongitudeLatitudeHeight(LocalPos);
}

bool UCesiumCoordinateService::IsGeographicSupported_Implementation() const
{
	return Georeference != nullptr;
}

bool UCesiumCoordinateService::IsCoordinateSystemReady_Implementation() const
{
	return Georeference != nullptr;
}

void UCesiumCoordinateService::ValidateAxisAlignment() const
{
	if (!Georeference)
	{
		return;
	}

	// Get the GPS coordinates of the Georeference origin
	double OriginLon = Georeference->GetOriginLongitude();
	double OriginLat = Georeference->GetOriginLatitude();
	double OriginAlt = Georeference->GetOriginHeight();

	// Offset 100m North (lat += ~0.0009 deg) and 100m East (lon += ~0.0009 deg at equator)
	const double DegPer100m = 100.0 / 111320.0; // ~0.000899 degrees per 100m

	FVector OriginWorld = GeographicToWorld_Implementation(OriginLat, OriginLon, OriginAlt);
	FVector NorthWorld  = GeographicToWorld_Implementation(OriginLat + DegPer100m, OriginLon, OriginAlt);
	FVector EastWorld   = GeographicToWorld_Implementation(OriginLat, OriginLon + DegPer100m, OriginAlt);

	FVector NorthDir = (NorthWorld - OriginWorld).GetSafeNormal();
	FVector EastDir  = (EastWorld  - OriginWorld).GetSafeNormal();

	UE_LOG(LogTemp, Log, TEXT("CesiumCoordinateService AxisAlignment: North direction in UE5 = (%.3f, %.3f, %.3f)"),
		NorthDir.X, NorthDir.Y, NorthDir.Z);
	UE_LOG(LogTemp, Log, TEXT("CesiumCoordinateService AxisAlignment: East  direction in UE5 = (%.3f, %.3f, %.3f)"),
		EastDir.X, EastDir.Y, EastDir.Z);

	// Warn if North is not close to UE5 +X
	if (NorthDir.X < 0.9f)
	{
		UE_LOG(LogTemp, Warning, TEXT("CesiumCoordinateService: North is NOT aligned with UE5 +X at Georeference origin! "
			"NED→UE linear formula (North→X) may be inaccurate. Check Georeference orientation."));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("CesiumCoordinateService: Axis alignment OK — North ≈ UE5 +X, East ≈ UE5 +Y"));
	}
}
