#include "DronePlaybackManager.h"

#include "Components/SkeletalMeshComponent.h"
#include "DronePathActor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MultiDroneCharacter.h"
#include "UE5DroneControlCharacter.h"

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

	if (Data.Paths.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	FDronePathsSaveData PlaybackData = Data;
	PlaybackData.Paths.Sort([](const FDronePathSaveData& A, const FDronePathSaveData& B)
	{
		const int32 AId = A.PathId == INDEX_NONE ? MAX_int32 : A.PathId;
		const int32 BId = B.PathId == INDEX_NONE ? MAX_int32 : B.PathId;
		return AId < BId;
	});

	TArray<AActor*> ShadowDrones;
	if (bUseExistingShadowDrones)
	{
		EnsurePlaybackDrones(PlaybackData.Paths.Num());

		if (SpawnedMultiDrones.Num() > 0)
		{
			for (AActor* Multi : SpawnedMultiDrones)
			{
				ShadowDrones.Add(Multi);
			}
		}
		else
		{
			for (AMultiDroneCharacter* Multi : FindShadowDrones())
			{
				ShadowDrones.Add(Multi);
			}
		}

		if (ShadowDrones.IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("DronePlaybackManager: no shadow drones found for local playback."));
			return;
		}
	}

	CachedPlaybackData = PlaybackData;

	for (int32 PathIndex = 0; PathIndex < PlaybackData.Paths.Num(); ++PathIndex)
	{
		const FDronePathSaveData& PathData = PlaybackData.Paths[PathIndex];
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

		const float DefaultSpeedMps = ADronePathActor::GetDefaultSegmentSpeedMps();

		for (int32 WaypointIndex = 0; WaypointIndex < PathData.Waypoints.Num(); ++WaypointIndex)
		{
			const FDroneWaypointSaveData& WaypointData = PathData.Waypoints[WaypointIndex];
			const float EffectiveSpeed = (WaypointIndex == 0)
				? 0.0f
				: (WaypointData.SegmentSpeed > KINDA_SMALL_NUMBER ? WaypointData.SegmentSpeed : DefaultSpeedMps);

			const int32 NewWaypointIndex = PathActor->AddWaypoint(WaypointData.Location, EffectiveSpeed);
			if (PathActor->Waypoints.IsValidIndex(NewWaypointIndex))
			{
				PathActor->Waypoints[NewWaypointIndex].WaitTime = WaypointData.WaitTime;
				PathActor->Waypoints[NewWaypointIndex].SegmentSpeed = EffectiveSpeed;
			}
		}

		PathActor->RefreshPath();

		const FVector StartLocation = PathActor->GetWaypointWorldLocation(0);
		AActor* DroneActor = nullptr;
		if (bUseExistingShadowDrones)
		{
			if (!ShadowDrones.IsValidIndex(PathIndex) || !IsValid(ShadowDrones[PathIndex]))
			{
				UE_LOG(LogTemp, Warning, TEXT("DronePlaybackManager: no shadow drone for path index %d."), PathIndex);
				PathActor->Destroy();
				continue;
			}

			DroneActor = ShadowDrones[PathIndex];
			if (AMultiDroneCharacter* Multi = Cast<AMultiDroneCharacter>(DroneActor))
			{
				Multi->bFollowingMirror = false;
			}
			if (AUE5DroneControlCharacter* DroneCharacter = Cast<AUE5DroneControlCharacter>(DroneActor))
			{
				DroneCharacter->StopClickTargetSending();
			}
			DroneActor->SetActorLocation(StartLocation, false, nullptr, ETeleportType::TeleportPhysics);
		}
		else
		{
			DroneActor = SpawnPlaybackDrone(StartLocation);
		}

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
	SpawnedMultiDrones.Reset();
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

	// Keep spawned preview drones from being overridden by mirror-follow logic
	for (AActor* Multi : SpawnedMultiDrones)
	{
		if (AMultiDroneCharacter* MultiChar = Cast<AMultiDroneCharacter>(Multi))
		{
			MultiChar->bFollowingMirror = false;
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

void ADronePlaybackManager::EnsurePlaybackDrones(int32 Count)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || Count <= 0)
	{
		return;
	}

	// Remove extras
	while (SpawnedMultiDrones.Num() > Count)
	{
		AActor* Extra = SpawnedMultiDrones.Last();
		SpawnedMultiDrones.RemoveAt(SpawnedMultiDrones.Num() - 1);
		if (IsValid(Extra))
		{
			Extra->Destroy();
		}
	}

	// Spawn missing using the same DroneActorClass path as SpawnPlaybackDrone
	for (int32 i = SpawnedMultiDrones.Num(); i < Count; ++i)
	{
		AActor* NewDrone = SpawnPlaybackDrone(FVector::ZeroVector);
		if (IsValid(NewDrone))
		{
			SpawnedMultiDrones.Add(NewDrone);
		}
	}
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
		bool bWasSpawnedForPlayback = false;
		for (const TObjectPtr<AActor>& SpawnedDrone : SpawnedPlaybackDrones)
		{
			if (SpawnedDrone.Get() == DroneActor)
			{
				bWasSpawnedForPlayback = true;
				break;
			}
		}

		if (bWasSpawnedForPlayback && IsValid(DroneActor))
		{
			DroneActor->Destroy();
		}
	}

	ActivePaths.Reset();
	ActiveDrones.Reset();
	SpawnedPlaybackDrones.Reset();
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
		AActor* SpawnedActor = World->SpawnActor<AActor>(DroneActorClass, SpawnLocation, FRotator::ZeroRotator, SpawnParameters);
		if (SpawnedActor)
		{
			if (AMultiDroneCharacter* Multi = Cast<AMultiDroneCharacter>(SpawnedActor))
			{
				Multi->bFollowingMirror = false;
			}
			SpawnedPlaybackDrones.Add(SpawnedActor);
		}
		return SpawnedActor;
	}

	// Fallback: spawn a plain Actor with the drone skeletal mesh
	AActor* FallbackActor = World->SpawnActor<AActor>(AActor::StaticClass(), SpawnLocation, FRotator::ZeroRotator, SpawnParameters);
	if (!IsValid(FallbackActor))
	{
		return nullptr;
	}

	USkeletalMeshComponent* SkelMeshComp = NewObject<USkeletalMeshComponent>(FallbackActor);
	if (IsValid(SkelMeshComp))
	{
		SkelMeshComp->SetMobility(EComponentMobility::Movable);
		USkeletalMesh* DroneMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Drone_Scavenger/SK/SK_Drone_Scavenger.SK_Drone_Scavenger"));
		if (DroneMesh)
		{
			SkelMeshComp->SetSkeletalMesh(DroneMesh);
		}
		SkelMeshComp->SetWorldScale3D(FVector(0.3f));
		SkelMeshComp->RegisterComponent();
		FallbackActor->SetRootComponent(SkelMeshComp);
	}

	SpawnedPlaybackDrones.Add(FallbackActor);
	return FallbackActor;
}

TArray<AMultiDroneCharacter*> ADronePlaybackManager::FindShadowDrones() const
{
	TArray<AMultiDroneCharacter*> Result;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Result;
	}

	for (TActorIterator<AMultiDroneCharacter> It(World); It; ++It)
	{
		AMultiDroneCharacter* ShadowDrone = *It;
		if (IsValid(ShadowDrone))
		{
			Result.Add(ShadowDrone);
		}
	}

	Result.Sort([](const AMultiDroneCharacter& A, const AMultiDroneCharacter& B)
	{
		return A.DroneId < B.DroneId;
	});

	return Result;
}

FDronePathsSaveData ADronePlaybackManager::RemapDataToAnchor(const FDronePathsSaveData& Data, const FVector& NewAnchorLocation) const
{
	FDronePathsSaveData Remapped = Data;
	if (Remapped.Paths.IsEmpty() || Remapped.Paths[0].Waypoints.IsEmpty())
	{
		return Remapped;
	}

	const FVector EditAnchorLocation = Remapped.Paths[0].Waypoints[0].Location;
	for (FDronePathSaveData& PathData : Remapped.Paths)
	{
		for (FDroneWaypointSaveData& Waypoint : PathData.Waypoints)
		{
			Waypoint.Location = (Waypoint.Location - EditAnchorLocation) + NewAnchorLocation;
		}
	}

	return Remapped;
}
