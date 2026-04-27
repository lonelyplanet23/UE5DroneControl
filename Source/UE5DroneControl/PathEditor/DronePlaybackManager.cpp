#include "DronePlaybackManager.h"

#include "Components/StaticMeshComponent.h"
#include "DronePathActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"

ADronePlaybackManager::ADronePlaybackManager()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ADronePlaybackManager::BeginPlay()
{
	Super::BeginPlay();
}

void ADronePlaybackManager::PlayFromData(const FDronePathsSaveData& Data)
{
	StopPlayback();
	CachedPlaybackData = Data;

	if (Data.Paths.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	for (const FDronePathSaveData& PathData : Data.Paths)
	{
		if (PathData.Waypoints.IsEmpty())
		{
			continue;
		}

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ADronePathActor* PathActor = World->SpawnActor<ADronePathActor>(ADronePathActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (!IsValid(PathActor))
		{
			continue;
		}

		PathActor->SetPathNumericId(PathData.PathId);
		PathActor->bClosedLoop = PathData.bClosedLoop;

		for (const FDroneWaypointSaveData& WaypointData : PathData.Waypoints)
		{
			const int32 NewWaypointIndex = PathActor->AddWaypoint(WaypointData.Location, WaypointData.SegmentSpeed);
			if (PathActor->Waypoints.IsValidIndex(NewWaypointIndex))
			{
				PathActor->Waypoints[NewWaypointIndex].WaitTime = WaypointData.WaitTime;
				PathActor->Waypoints[NewWaypointIndex].SegmentSpeed = (NewWaypointIndex == 0) ? 0.0f : FMath::Max(0.0f, WaypointData.SegmentSpeed);
			}
		}

		PathActor->RefreshPath();

		const FVector SpawnLocation = PathActor->GetWaypointWorldLocation(0);
		AActor* DroneActor = SpawnPlaybackDrone(SpawnLocation);
		if (!IsValid(DroneActor))
		{
			PathActor->Destroy();
			continue;
		}

		PathActor->StartMovement(DroneActor);
		ActivePaths.Add(PathActor);
		ActiveDrones.Add(DroneActor);
	}

	if (ActivePaths.IsEmpty())
	{
		StopPlayback();
		return;
	}

	bIsPlaying = true;
	bIsPaused = false;
	PlaybackTime = 0.0f;
	StartTime = World->GetTimeSeconds();
}

void ADronePlaybackManager::PausePlayback()
{
	if (!bIsPlaying || bIsPaused)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (IsValid(World))
	{
		PlaybackTime = World->GetTimeSeconds() - StartTime;
	}

	PausedPathStates.Reset();

	for (ADronePathActor* PathActor : ActivePaths)
	{
		if (!IsValid(PathActor) || !PathActor->IsMovementActive() || !PathActor->ControlledDrone.IsValid())
		{
			continue;
		}

		const float ElapsedSegmentTime = IsValid(World) ? (World->GetTimeSeconds() - PathActor->SegmentStartTime) : 0.0f;

		FDronePlaybackPauseState& PauseState = PausedPathStates.AddDefaulted_GetRef();
		PauseState.PathActor = PathActor;
		PauseState.DroneActor = PathActor->ControlledDrone.Get();
		PauseState.SegmentIndex = PathActor->CurrentSegmentIndex;
		PauseState.RemainingSegmentTime = FMath::Max(PathActor->SegmentDuration - ElapsedSegmentTime, 0.0f);

		PathActor->StopMovement();
	}

	bIsPaused = true;
}

void ADronePlaybackManager::ResumePlayback()
{
	if (!bIsPlaying || !bIsPaused)
	{
		return;
	}

	for (const FDronePlaybackPauseState& PauseState : PausedPathStates)
	{
		if (!IsValid(PauseState.PathActor) || !IsValid(PauseState.DroneActor))
		{
			continue;
		}

		PauseState.PathActor->ResumeMovement(PauseState.DroneActor, PauseState.SegmentIndex, PauseState.RemainingSegmentTime);
	}

	if (UWorld* World = GetWorld())
	{
		StartTime = World->GetTimeSeconds() - PlaybackTime;
	}

	bIsPaused = false;
}

void ADronePlaybackManager::StopPlayback()
{
	ClearActivePlaybackActors();
	PausedPathStates.Reset();
	bIsPlaying = false;
	bIsPaused = false;
	PlaybackTime = 0.0f;
	StartTime = 0.0f;
}

void ADronePlaybackManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsPlaying)
	{
		return;
	}

	if (!bIsPaused)
	{
		if (UWorld* World = GetWorld())
		{
			PlaybackTime = World->GetTimeSeconds() - StartTime;
		}
	}

	bool bHasValidPath = false;
	bool bAllPathsCompleted = true;

	for (ADronePathActor* PathActor : ActivePaths)
	{
		if (!IsValid(PathActor))
		{
			continue;
		}

		bHasValidPath = true;
		if (PathActor->GetCurrentPathStatus() != EPathStatus::Completed)
		{
			bAllPathsCompleted = false;
			break;
		}
	}

	if (!bHasValidPath || !bAllPathsCompleted)
	{
		return;
	}

	if (bLoop && !CachedPlaybackData.Paths.IsEmpty())
	{
		PlayFromData(CachedPlaybackData);
		return;
	}

	StopPlayback();
}

void ADronePlaybackManager::ClearActivePlaybackActors()
{
	for (ADronePathActor* PathActor : ActivePaths)
	{
		if (IsValid(PathActor))
		{
			PathActor->StopMovement();
			PathActor->Destroy();
		}
	}

	for (AActor* DroneActor : ActiveDrones)
	{
		if (IsValid(DroneActor))
		{
			DroneActor->Destroy();
		}
	}

	ActivePaths.Reset();
	ActiveDrones.Reset();
}

AActor* ADronePlaybackManager::SpawnPlaybackDrone(const FVector& SpawnLocation)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	if (DroneActorClass != nullptr)
	{
		return World->SpawnActor<AActor>(DroneActorClass, SpawnLocation, FRotator::ZeroRotator, SpawnParameters);
	}

	AStaticMeshActor* FallbackActor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), SpawnLocation, FRotator::ZeroRotator, SpawnParameters);
	if (!IsValid(FallbackActor))
	{
		return nullptr;
	}

	UStaticMeshComponent* StaticMeshComponent = FallbackActor->GetStaticMeshComponent();
	if (IsValid(StaticMeshComponent))
	{
		if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
		{
			StaticMeshComponent->SetStaticMesh(CubeMesh);
		}

		StaticMeshComponent->SetMobility(EComponentMobility::Movable);
		StaticMeshComponent->SetWorldScale3D(FVector(0.25f));
	}

	return FallbackActor;
}
