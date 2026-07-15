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
	// 源锚点无人机 ID：编队旋转/平移的基准路径。默认 1（编辑关卡固定 DroneId=1）。
	// 读取旧 JSON 缺该字段时保持默认 1。
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Path|Save")
	int32 AnchorDroneId = 1;

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

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Save", meta = (WorldContext = "WorldContextObject"))
	static bool SaveAllPathsInWorldToJson(UObject* WorldContextObject, const FString& FileName);

	UFUNCTION(BlueprintCallable, Category = "Drone|IO")
	static bool LoadPathsFromJson(const FString& FilePath, FDronePathsSaveData& OutData);

	/**
	 * 把路径数据写回指定的绝对文件路径（覆盖）。用于在回放面板里就地修改某个 JSON 的循环状态等。
	 * 与 SavePathsToJson 不同：这里直接指定完整文件路径，不做 DronePaths 目录拼接。
	 */
	UFUNCTION(BlueprintCallable, Category = "Drone|IO")
	static bool SavePathsDataToFile(const FString& FilePath, const FDronePathsSaveData& Data);
};
