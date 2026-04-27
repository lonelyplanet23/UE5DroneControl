#include "DroneTaskManager.h"

#include "../PathEditor/DronePathActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogDroneTaskManager, Log, All);

int32 UDroneTaskManager::AddTask(const FDroneTask& NewTask)
{
	return AddTaskForPath(NewTask, INDEX_NONE);
}

int32 UDroneTaskManager::AddTaskForPath(const FDroneTask& NewTask, int32 PathId)
{
	const int32 TaskIndex = Tasks.Add(NewTask);
	PathIds.Add(PathId);
	return TaskIndex;
}

bool UDroneTaskManager::UpdateTask(int32 TaskIndex, ETaskStatus NewStatus)
{
	if (!IsValidTaskIndex(TaskIndex))
	{
		UE_LOG(LogDroneTaskManager, Warning, TEXT("UpdateTask failed. Invalid task index %d on %s."), TaskIndex, *GetNameSafe(this));
		return false;
	}

	Tasks[TaskIndex].TaskStatus = NewStatus;
	return true;
}

ETaskStatus UDroneTaskManager::GetTaskStatus(int32 TaskIndex) const
{
	if (!IsValidTaskIndex(TaskIndex))
	{
		UE_LOG(LogDroneTaskManager, Warning, TEXT("GetTaskStatus failed. Invalid task index %d on %s."), TaskIndex, *GetNameSafe(this));
		return ETaskStatus::Standby;
	}

	return Tasks[TaskIndex].TaskStatus;
}

bool UDroneTaskManager::RemoveTask(int32 TaskIndex)
{
	if (!IsValidTaskIndex(TaskIndex))
	{
		UE_LOG(LogDroneTaskManager, Warning, TEXT("RemoveTask failed. Invalid task index %d on %s."), TaskIndex, *GetNameSafe(this));
		return false;
	}

	Tasks.RemoveAt(TaskIndex);
	PathIds.RemoveAt(TaskIndex);
	return true;
}

int32 UDroneTaskManager::GetTaskCount() const
{
	return Tasks.Num();
}

bool UDroneTaskManager::GetTask(int32 TaskIndex, FDroneTask& OutTask) const
{
	if (!IsValidTaskIndex(TaskIndex))
	{
		UE_LOG(LogDroneTaskManager, Warning, TEXT("GetTask failed. Invalid task index %d on %s."), TaskIndex, *GetNameSafe(this));
		return false;
	}

	OutTask = Tasks[TaskIndex];
	return true;
}

bool UDroneTaskManager::SetTaskPathId(int32 TaskIndex, int32 PathId)
{
	if (!IsValidTaskIndex(TaskIndex))
	{
		UE_LOG(LogDroneTaskManager, Warning, TEXT("SetTaskPathId failed. Invalid task index %d on %s."), TaskIndex, *GetNameSafe(this));
		return false;
	}

	PathIds[TaskIndex] = PathId;
	return true;
}

int32 UDroneTaskManager::GetTaskPathId(int32 TaskIndex) const
{
	if (!IsValidTaskIndex(TaskIndex))
	{
		UE_LOG(LogDroneTaskManager, Warning, TEXT("GetTaskPathId failed. Invalid task index %d on %s."), TaskIndex, *GetNameSafe(this));
		return INDEX_NONE;
	}

	return PathIds[TaskIndex];
}

void UDroneTaskManager::ClearTasks()
{
	Tasks.Reset();
	PathIds.Reset();
}

void UDroneTaskManager::SynchronizePaths(const TArray<ADronePathActor*>& PathActors, float StartTime)
{
	TArray<TObjectPtr<ADronePathActor>> UniquePaths;
	UniquePaths.Reserve(PathActors.Num());

	for (ADronePathActor* PathActor : PathActors)
	{
		if (IsValid(PathActor) && !UniquePaths.Contains(PathActor))
		{
			UniquePaths.Add(PathActor);
		}
	}

	if (UniquePaths.IsEmpty())
	{
		UE_LOG(LogDroneTaskManager, Warning, TEXT("SynchronizePaths called with no valid path actors on %s."), *GetNameSafe(this));
		return;
	}

	float CurrentTime = 0.0f;
	if (UWorld* World = UniquePaths[0]->GetWorld())
	{
		CurrentTime = World->GetTimeSeconds();
	}

	const ETaskStatus TargetStatus = StartTime <= CurrentTime ? ETaskStatus::InFlight : ETaskStatus::Standby;

	for (ADronePathActor* PathActor : UniquePaths)
	{
		PathActor->OnExecutionStateChanged.RemoveAll(this);
		PathActor->OnExecutionStateChanged.AddUObject(this, &UDroneTaskManager::HandlePathExecutionStateChanged);
		PathActor->ScheduleExecution(StartTime);
		UpdateTasksForPathState(PathActor, TargetStatus);
	}
}

void UDroneTaskManager::PausePaths(const TArray<ADronePathActor*>& PathActors)
{
	TArray<TObjectPtr<ADronePathActor>> UniquePaths;
	UniquePaths.Reserve(PathActors.Num());

	for (ADronePathActor* PathActor : PathActors)
	{
		if (IsValid(PathActor) && !UniquePaths.Contains(PathActor))
		{
			UniquePaths.Add(PathActor);
		}
	}

	for (ADronePathActor* PathActor : UniquePaths)
	{
		PathActor->OnExecutionStateChanged.RemoveAll(this);
		PathActor->OnExecutionStateChanged.AddUObject(this, &UDroneTaskManager::HandlePathExecutionStateChanged);
		PathActor->PauseExecution();
		UpdateTasksForPathState(PathActor, ETaskStatus::Standby);
	}
}

void UDroneTaskManager::UpdatePathStatus(ADronePathActor* Path, EPathStatus NewStatus)
{
	if (!IsValid(Path))
	{
		UE_LOG(LogDroneTaskManager, Warning, TEXT("UpdatePathStatus failed. Invalid path on %s."), *GetNameSafe(this));
		return;
	}

	Path->OnExecutionStateChanged.RemoveAll(this);
	Path->OnExecutionStateChanged.AddUObject(this, &UDroneTaskManager::HandlePathExecutionStateChanged);
	Path->UpdatePathStatus(NewStatus);
	UpdateTasksForPathState(Path, ConvertPathStatusToTaskStatus(NewStatus));
}

EPathStatus UDroneTaskManager::GetCurrentPathStatus(const ADronePathActor* Path) const
{
	if (!IsValid(Path))
	{
		UE_LOG(LogDroneTaskManager, Warning, TEXT("GetCurrentPathStatus failed. Invalid path on %s."), *GetNameSafe(this));
		return EPathStatus::Standby;
	}

	return Path->GetCurrentPathStatus();
}

void UDroneTaskManager::PausePathExecution(ADronePathActor* Path)
{
	if (!IsValid(Path))
	{
		UE_LOG(LogDroneTaskManager, Warning, TEXT("PausePathExecution failed. Invalid path on %s."), *GetNameSafe(this));
		return;
	}

	Path->OnExecutionStateChanged.RemoveAll(this);
	Path->OnExecutionStateChanged.AddUObject(this, &UDroneTaskManager::HandlePathExecutionStateChanged);
	Path->PausePathExecution();
	UpdateTasksForPathState(Path, ETaskStatus::Standby);
}

bool UDroneTaskManager::IsValidTaskIndex(int32 TaskIndex) const
{
	return Tasks.IsValidIndex(TaskIndex) && PathIds.IsValidIndex(TaskIndex);
}

void UDroneTaskManager::UpdateTasksForPathState(const ADronePathActor* PathActor, ETaskStatus NewStatus)
{
	if (!IsValid(PathActor) || PathActor->PathNumericId == INDEX_NONE)
	{
		return;
	}

	for (int32 TaskIndex = 0; TaskIndex < PathIds.Num(); ++TaskIndex)
	{
		if (PathIds[TaskIndex] == PathActor->PathNumericId && Tasks.IsValidIndex(TaskIndex))
		{
			Tasks[TaskIndex].TaskStatus = NewStatus;
		}
	}
}

void UDroneTaskManager::HandlePathExecutionStateChanged(ADronePathActor* PathActor, EDronePathExecutionState NewExecutionState)
{
	if (!IsValid(PathActor))
	{
		return;
	}

	switch (NewExecutionState)
	{
	case EDronePathExecutionState::Running:
		UpdateTasksForPathState(PathActor, ETaskStatus::InFlight);
		break;

	case EDronePathExecutionState::Completed:
		UpdateTasksForPathState(PathActor, ETaskStatus::Completed);
		break;

	case EDronePathExecutionState::Paused:
	case EDronePathExecutionState::Idle:
	case EDronePathExecutionState::Scheduled:
	default:
		UpdateTasksForPathState(PathActor, ETaskStatus::Standby);
		break;
	}
}

ETaskStatus UDroneTaskManager::ConvertPathStatusToTaskStatus(EPathStatus PathStatus) const
{
	switch (PathStatus)
	{
	case EPathStatus::InFlight:
		return ETaskStatus::InFlight;

	case EPathStatus::Completed:
		return ETaskStatus::Completed;

	case EPathStatus::Standby:
	default:
		return ETaskStatus::Standby;
	}
}
