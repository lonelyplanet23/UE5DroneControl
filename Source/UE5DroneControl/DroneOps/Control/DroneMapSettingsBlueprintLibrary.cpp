// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneMapSettingsBlueprintLibrary.h"
#include "DroneOps/Control/DroneOpsGameMode.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"

namespace
{
constexpr const TCHAR* Section = TEXT("CesiumTileServer");

void GetBoolConfig(const TCHAR* Key, bool& Value)
{
	if (GConfig)
	{
		GConfig->GetBool(Section, Key, Value, GEngineIni);
	}
}

void GetIntConfig(const TCHAR* Key, int32& Value)
{
	if (GConfig)
	{
		GConfig->GetInt(Section, Key, Value, GEngineIni);
	}
}

void GetFloatConfig(const TCHAR* Key, float& Value)
{
	if (GConfig)
	{
		GConfig->GetFloat(Section, Key, Value, GEngineIni);
	}
}

void GetStringConfig(const TCHAR* Key, FString& Value)
{
	if (GConfig)
	{
		GConfig->GetString(Section, Key, Value, GEngineIni);
	}
}

ADroneOpsGameMode* GetDroneOpsGameMode(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	return Cast<ADroneOpsGameMode>(UGameplayStatics::GetGameMode(WorldContextObject));
}
}

void UDroneMapSettingsBlueprintLibrary::LoadDroneMapSettings(FDroneMapSettings& OutSettings)
{
	OutSettings = FDroneMapSettings();

	GetBoolConfig(TEXT("UseLocalTileServer"), OutSettings.bUseOfflineMap);
	GetStringConfig(TEXT("LocalTileServerUrl"), OutSettings.LocalTileServerUrl);
	GetStringConfig(TEXT("RasterTemplateUrl"), OutSettings.RasterTemplateUrl);
	GetStringConfig(TEXT("RasterCoordinateSystem"), OutSettings.RasterCoordinateSystem);
	GetBoolConfig(TEXT("UseOfflineRasterPlaneLod"), OutSettings.bUseOfflineRasterPlaneLod);
	GetIntConfig(TEXT("OfflineRasterPlaneFarZoom"), OutSettings.FarZoom);
	GetIntConfig(TEXT("OfflineRasterPlaneFarRadius"), OutSettings.FarRadius);
	GetIntConfig(TEXT("OfflineRasterPlaneMidZoom"), OutSettings.MidZoom);
	GetIntConfig(TEXT("OfflineRasterPlaneMidRadius"), OutSettings.MidRadius);
	GetIntConfig(TEXT("OfflineRasterPlaneNearZoom"), OutSettings.NearZoom);
	GetIntConfig(TEXT("OfflineRasterPlaneNearRadius"), OutSettings.NearRadius);
	GetIntConfig(TEXT("OfflineRasterPlaneZoom"), OutSettings.SingleZoom);
	GetIntConfig(TEXT("OfflineRasterPlaneRadius"), OutSettings.SingleRadius);
	GetIntConfig(TEXT("OfflineRasterPlaneMaxTiles"), OutSettings.MaxTiles);
	GetBoolConfig(TEXT("OfflineRasterPlaneDynamicCoverage"), OutSettings.bDynamicCoverage);
	GetFloatConfig(TEXT("OfflineRasterPlaneFootprintScale"), OutSettings.FootprintScale);
	GetFloatConfig(TEXT("OfflineRasterPlaneTileBrightness"), OutSettings.TileBrightness);
	GetBoolConfig(TEXT("UseOfflineCesiumSunSky"), OutSettings.bUseOfflineCesiumSunSky);
	GetFloatConfig(TEXT("OfflineSunSkySolarTime"), OutSettings.SunSkySolarTime);
}

void UDroneMapSettingsBlueprintLibrary::SaveDroneMapSettings(const FDroneMapSettings& Settings)
{
	if (!GConfig)
	{
		return;
	}

	const bool bOffline = Settings.bUseOfflineMap;
	GConfig->SetBool(Section, TEXT("UseLocalTileServer"), bOffline, GEngineIni);
	GConfig->SetBool(Section, TEXT("UseOfflineRasterPlane"), bOffline, GEngineIni);
	GConfig->SetBool(Section, TEXT("SwitchTilesetsToLocal"), false, GEngineIni);
	GConfig->SetBool(Section, TEXT("CreateUrlTemplateRasterOverlay"), false, GEngineIni);
	GConfig->SetBool(Section, TEXT("DisableNonUrlRasterOverlays"), bOffline, GEngineIni);

	GConfig->SetString(Section, TEXT("LocalTileServerUrl"), *Settings.LocalTileServerUrl, GEngineIni);
	GConfig->SetString(Section, TEXT("RasterTemplateUrl"), *Settings.RasterTemplateUrl, GEngineIni);
	GConfig->SetString(Section, TEXT("RasterCoordinateSystem"), *Settings.RasterCoordinateSystem, GEngineIni);
	GConfig->SetBool(Section, TEXT("UseOfflineRasterPlaneLod"), Settings.bUseOfflineRasterPlaneLod, GEngineIni);
	GConfig->SetInt(Section, TEXT("OfflineRasterPlaneFarZoom"), Settings.FarZoom, GEngineIni);
	GConfig->SetInt(Section, TEXT("OfflineRasterPlaneFarRadius"), Settings.FarRadius, GEngineIni);
	GConfig->SetInt(Section, TEXT("OfflineRasterPlaneMidZoom"), Settings.MidZoom, GEngineIni);
	GConfig->SetInt(Section, TEXT("OfflineRasterPlaneMidRadius"), Settings.MidRadius, GEngineIni);
	GConfig->SetInt(Section, TEXT("OfflineRasterPlaneNearZoom"), Settings.NearZoom, GEngineIni);
	GConfig->SetInt(Section, TEXT("OfflineRasterPlaneNearRadius"), Settings.NearRadius, GEngineIni);
	GConfig->SetInt(Section, TEXT("OfflineRasterPlaneZoom"), Settings.SingleZoom, GEngineIni);
	GConfig->SetInt(Section, TEXT("OfflineRasterPlaneRadius"), Settings.SingleRadius, GEngineIni);
	GConfig->SetInt(Section, TEXT("OfflineRasterPlaneMaxTiles"), Settings.MaxTiles, GEngineIni);
	GConfig->SetBool(Section, TEXT("OfflineRasterPlaneDynamicCoverage"), Settings.bDynamicCoverage, GEngineIni);
	GConfig->SetFloat(Section, TEXT("OfflineRasterPlaneFootprintScale"), Settings.FootprintScale, GEngineIni);
	GConfig->SetFloat(Section, TEXT("OfflineRasterPlaneTileBrightness"), Settings.TileBrightness, GEngineIni);
	GConfig->SetBool(Section, TEXT("UseOfflineCesiumSunSky"), Settings.bUseOfflineCesiumSunSky, GEngineIni);
	GConfig->SetFloat(Section, TEXT("OfflineSunSkySolarTime"), Settings.SunSkySolarTime, GEngineIni);
	GConfig->Flush(false, GEngineIni);
}

bool UDroneMapSettingsBlueprintLibrary::ApplyDroneMapSettingsToCurrentWorld(
	UObject* WorldContextObject,
	const FDroneMapSettings& Settings,
	bool bRebuildOfflinePlane)
{
	SaveDroneMapSettings(Settings);

	if (ADroneOpsGameMode* GameMode = GetDroneOpsGameMode(WorldContextObject))
	{
		GameMode->ApplyMapModeFromConfig(bRebuildOfflinePlane);
		return true;
	}

	return false;
}

bool UDroneMapSettingsBlueprintLibrary::SetOfflineMapEnabled(
	UObject* WorldContextObject,
	bool bUseOfflineMap,
	bool bApplyToCurrentWorld)
{
	FDroneMapSettings Settings;
	LoadDroneMapSettings(Settings);
	Settings.bUseOfflineMap = bUseOfflineMap;
	SaveDroneMapSettings(Settings);

	if (bApplyToCurrentWorld)
	{
		if (ADroneOpsGameMode* GameMode = GetDroneOpsGameMode(WorldContextObject))
		{
			GameMode->SetOfflineMapModeRuntime(bUseOfflineMap, true);
			return true;
		}
	}

	return !bApplyToCurrentWorld;
}

bool UDroneMapSettingsBlueprintLibrary::IsOfflineMapEnabled()
{
	FDroneMapSettings Settings;
	LoadDroneMapSettings(Settings);
	return Settings.bUseOfflineMap;
}

bool UDroneMapSettingsBlueprintLibrary::ValidateCurrentMapCoordinates(
	UObject* WorldContextObject,
	float ToleranceMeters,
	FString& OutReport)
{
	if (ADroneOpsGameMode* GameMode = GetDroneOpsGameMode(WorldContextObject))
	{
		return GameMode->ValidateMapCoordinateAlignment(ToleranceMeters, OutReport);
	}

	OutReport = TEXT("Map validation failed: current GameMode is not DroneOpsGameMode. Open the preview/DroneOps level first.");
	return false;
}
