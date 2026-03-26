// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UE5DroneControlCharacter.h"
#include "MultiDroneCharacter.generated.h"

UCLASS()
class UE5DRONECONTROL_API AMultiDroneCharacter : public AUE5DroneControlCharacter
{
	GENERATED_BODY()

public:
	// 0-based drone index (UAV1 -> 0, UAV2 -> 1, etc.)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Multi Drone")
	int32 DroneIndex = 0;
};
