#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DronePathSaveLibrary.generated.h"

class ADronePathActor;

USTRUCT(BlueprintType)
struct FDroneWaypointSaveData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Path|Save")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Path|Save")
	float SegmentSpeed = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Path|Save")
	float WaitTime = 0.0f;
};

USTRUCT(BlueprintType)
struct FDronePathSaveData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Path|Save")
	int32 PathId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Path|Save")
	bool bClosedLoop = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Path|Save")
	TArray<FDroneWaypointSaveData> Waypoints;
};

USTRUCT(BlueprintType)
struct FDronePathsSaveData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Path|Save")
	TArray<FDronePathSaveData> Paths;
};

UCLASS()
class UE5DRONECONTROL_API UDronePathSaveLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Drone Path|Save")
	static bool SavePathsToJson(const TMap<int32, ADronePathActor*>& PathMap, const FString& FileName);

	UFUNCTION(BlueprintCallable, Category = "Drone|IO")
	static bool LoadPathsFromJson(const FString& FilePath, FDronePathsSaveData& OutData);
};
