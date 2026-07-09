// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DroneMapSettingsBlueprintLibrary.generated.h"

USTRUCT(BlueprintType)
struct UE5DRONECONTROL_API FDroneMapSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	bool bUseOfflineMap = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FString LocalTileServerUrl = TEXT("127.0.0.1:8870");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FString RasterTemplateUrl = TEXT("127.0.0.1:8870/getomap_205_$z_$x_$y_0_0.jpg");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FString RasterCoordinateSystem = TEXT("GCJ02");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	bool bUseOfflineRasterPlaneLod = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	int32 FarZoom = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	int32 FarRadius = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	int32 MidZoom = 17;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	int32 MidRadius = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	int32 NearZoom = 18;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	int32 NearRadius = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	int32 SingleZoom = 18;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	int32 SingleRadius = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	int32 MaxTiles = 1200;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	bool bDynamicCoverage = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	float FootprintScale = 1.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	float TileBrightness = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	bool bUseOfflineCesiumSunSky = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	float SunSkySolarTime = 13.0f;
};

UCLASS()
class UE5DRONECONTROL_API UDroneMapSettingsBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "DroneOps|MapSettings")
	static void LoadDroneMapSettings(FDroneMapSettings& OutSettings);

	UFUNCTION(BlueprintCallable, Category = "DroneOps|MapSettings")
	static void SaveDroneMapSettings(const FDroneMapSettings& Settings);

	UFUNCTION(BlueprintCallable, Category = "DroneOps|MapSettings", meta = (WorldContext = "WorldContextObject"))
	static bool ApplyDroneMapSettingsToCurrentWorld(UObject* WorldContextObject, const FDroneMapSettings& Settings, bool bRebuildOfflinePlane = true);

	UFUNCTION(BlueprintCallable, Category = "DroneOps|MapSettings", meta = (WorldContext = "WorldContextObject"))
	static bool SetOfflineMapEnabled(UObject* WorldContextObject, bool bUseOfflineMap, bool bApplyToCurrentWorld = true);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DroneOps|MapSettings")
	static bool IsOfflineMapEnabled();

	UFUNCTION(BlueprintCallable, Category = "DroneOps|MapSettings", meta = (WorldContext = "WorldContextObject"))
	static bool ValidateCurrentMapCoordinates(UObject* WorldContextObject, float ToleranceMeters, FString& OutReport);
};
