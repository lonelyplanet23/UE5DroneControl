#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DronePathConflictLibrary.generated.h"

class ADronePathActor;

UCLASS()
class UE5DRONECONTROL_API UDronePathConflictLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Drone Path|Conflict Detection")
	static TArray<FString> CheckPathConflictsWithVolume(const TArray<ADronePathActor*>& PathActors);
};
