// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneInfoProviderInterface.generated.h"

/**
 * Interface for providing drone information to the info panel UI.
 * Any actor that can display drone info should implement this interface.
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UDroneInfoProvider : public UInterface
{
	GENERATED_BODY()
};

class UE5DRONECONTROL_API IDroneInfoProvider
{
	GENERATED_BODY()

public:
	/**
	 * Get the current telemetry snapshot of this drone.
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "DroneInfo")
	FDroneTelemetrySnapshot GetDroneInfoSnapshot() const;
	virtual FDroneTelemetrySnapshot GetDroneInfoSnapshot_Implementation() const = 0;

	/**
	 * Get the display name of this drone.
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "DroneInfo")
	FString GetDroneDisplayName() const;
	virtual FString GetDroneDisplayName_Implementation() const = 0;

	/**
	 * Get the theme color for UI display.
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "DroneInfo")
	FLinearColor GetThemeColor() const;
	virtual FLinearColor GetThemeColor_Implementation() const = 0;

	// Note: GetDroneId() is already provided by IDroneSelectableInterface
	// We don't need to redefine it here
};
