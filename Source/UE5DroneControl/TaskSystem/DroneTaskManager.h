#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DroneTaskManager.generated.h"

class ADronePathActor;
enum class EDronePathExecutionState : uint8;
enum class EPathStatus : uint8;

UENUM(BlueprintType)
enum class ETaskStatus : uint8
{
	Standby UMETA(DisplayName = "Standby"),
	InFlight UMETA(DisplayName = "In Flight"),
	Completed UMETA(DisplayName = "Completed")
};

USTRUCT(BlueprintType)
struct FDroneTask
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Task")
	FVector StartPoint = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Task")
	FVector EndPoint = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Task", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float TaskDuration = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Drone Task")
	ETaskStatus TaskStatus = ETaskStatus::Standby;
};

UCLASS(BlueprintType, Blueprintable)
class UE5DRONECONTROL_API UDroneTaskManager : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone Task")
	TArray<FDroneTask> Tasks;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Drone Task")
	TArray<int32> PathIds;

	UFUNCTION(BlueprintCallable, Category = "Drone Task")
	int32 AddTask(const FDroneTask& NewTask);

	UFUNCTION(BlueprintCallable, Category = "Drone Task")
	int32 AddTaskForPath(const FDroneTask& NewTask, int32 PathId);

	UFUNCTION(BlueprintCallable, Category = "Drone Task")
	bool UpdateTask(int32 TaskIndex, ETaskStatus NewStatus);

	UFUNCTION(BlueprintPure, Category = "Drone Task")
	ETaskStatus GetTaskStatus(int32 TaskIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Drone Task")
	bool RemoveTask(int32 TaskIndex);

	UFUNCTION(BlueprintPure, Category = "Drone Task")
	int32 GetTaskCount() const;

	UFUNCTION(BlueprintPure, Category = "Drone Task")
	bool GetTask(int32 TaskIndex, FDroneTask& OutTask) const;

	UFUNCTION(BlueprintCallable, Category = "Drone Task")
	bool SetTaskPathId(int32 TaskIndex, int32 PathId);

	UFUNCTION(BlueprintPure, Category = "Drone Task")
	int32 GetTaskPathId(int32 TaskIndex) const;

	UFUNCTION(BlueprintCallable, Category = "Drone Task")
	void ClearTasks();

	UFUNCTION(BlueprintCallable, Category = "Drone Task|Scheduling")
	void SynchronizePaths(const TArray<ADronePathActor*>& PathActors, float StartTime);

	UFUNCTION(BlueprintCallable, Category = "Drone Task|Scheduling")
	void PausePaths(const TArray<ADronePathActor*>& PathActors);

	UFUNCTION(BlueprintCallable, Category = "Drone Task|Scheduling")
	void UpdatePathStatus(ADronePathActor* Path, EPathStatus NewStatus);

	UFUNCTION(BlueprintPure, Category = "Drone Task|Scheduling")
	EPathStatus GetCurrentPathStatus(const ADronePathActor* Path) const;

	UFUNCTION(BlueprintCallable, Category = "Drone Task|Scheduling")
	void PausePathExecution(ADronePathActor* Path);

private:
	bool IsValidTaskIndex(int32 TaskIndex) const;
	void UpdateTasksForPathState(const ADronePathActor* PathActor, ETaskStatus NewStatus);
	void HandlePathExecutionStateChanged(ADronePathActor* PathActor, EDronePathExecutionState NewExecutionState);
	ETaskStatus ConvertPathStatusToTaskStatus(EPathStatus PathStatus) const;
};
