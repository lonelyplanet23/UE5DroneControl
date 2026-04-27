#include "DronePathActor.h"

#include "DronePathConflictLibrary.h"
#include "Components/SceneComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "DroneWaypointActor.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogDronePathActor, Log, All);

namespace DronePathVisual
{
	static const FName ColorParameterName(TEXT("Color"));
	static const FName BaseColorParameterName(TEXT("BaseColor"));
	static const FName EmissiveColorParameterName(TEXT("EmissiveColor"));
	static const FName TintParameterName(TEXT("Tint"));
	static const FName RuntimeSplineSegmentTag(TEXT("DroneRuntimeSplineSegment"));
	constexpr float BasicShapeCylinderRadiusCm = 50.0f;
	constexpr float CentimetersPerMeter = 100.0f;
	constexpr float MinSegmentDurationSeconds = 0.01f;
}

ADronePathActor::ADronePathActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	PathSpline = CreateDefaultSubobject<USplineComponent>(TEXT("PathSpline"));
	PathSpline->SetupAttachment(SceneRoot);
	PathSpline->bDrawDebug = true;
	PathSpline->SetClosedLoop(false);

	PathMarkerMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PathMarkerMeshComponent"));
	PathMarkerMeshComponent->SetupAttachment(SceneRoot);
	PathMarkerMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PathMarkerMeshComponent->SetGenerateOverlapEvents(false);
	PathMarkerMeshComponent->SetMobility(EComponentMobility::Movable);
	PathMarkerMeshComponent->SetRelativeScale3D(FVector(0.35f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		PathMarkerMeshComponent->SetStaticMesh(SphereMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		SplineSegmentStaticMesh = CylinderMesh.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (DefaultMaterial.Succeeded())
	{
		PathVisualizationMaterial = DefaultMaterial.Object;
		PathConflictMaterial = DefaultMaterial.Object;
		PathMarkerMeshComponent->SetMaterial(0, DefaultMaterial.Object);
	}

	WaypointActorClass = ADroneWaypointActor::StaticClass();
}

int32 ADronePathActor::AddWaypoint(const FVector& Location, float SegmentSpeed)
{
	FDroneWaypoint NewWaypoint;
	NewWaypoint.Location = WorldToLocalLocation(Location);
	NewWaypoint.Index = Waypoints.Num();
	NewWaypoint.SegmentSpeed = Waypoints.IsEmpty() ? 0.0f : FMath::Clamp(SegmentSpeed, 0.0f, 15.0f);

	const int32 NewIndex = Waypoints.Add(NewWaypoint);
	SyncPathState(true);
	return NewIndex;
}

bool ADronePathActor::RemoveWaypoint(int32 Index)
{
	if (!IsValidWaypointIndex(Index))
	{
		UE_LOG(LogDronePathActor, Warning, TEXT("RemoveWaypoint failed. Invalid index %d on %s."), Index, *GetName());
		return false;
	}

	Waypoints.RemoveAt(Index);
	SyncPathState(true);
	return true;
}

bool ADronePathActor::UpdateWaypoint(int32 Index, const FVector& NewLocation)
{
	if (!IsValidWaypointIndex(Index))
	{
		UE_LOG(LogDronePathActor, Warning, TEXT("UpdateWaypoint failed. Invalid index %d on %s."), Index, *GetName());
		return false;
	}

	Waypoints[Index].Location = WorldToLocalLocation(NewLocation);
	SyncPathState(false);
	return true;
}

bool ADronePathActor::UpdateWaypointSegmentSpeed(int32 Index, float NewSegmentSpeed)
{
	if (!IsValidWaypointIndex(Index))
	{
		UE_LOG(LogDronePathActor, Warning, TEXT("UpdateWaypointSegmentSpeed failed. Invalid index %d on %s."), Index, *GetName());
		return false;
	}

	Waypoints[Index].SegmentSpeed = (Index == 0) ? 0.0f : FMath::Clamp(NewSegmentSpeed, 0.0f, 15.0f);
	SyncPathState(false);
	return true;
}

float ADronePathActor::GetWaypointSegmentSpeed(int32 Index) const
{
	if (!IsValidWaypointIndex(Index))
	{
		UE_LOG(LogDronePathActor, Warning, TEXT("GetWaypointSegmentSpeed failed. Invalid index %d on %s."), Index, *GetName());
		return 0.0f;
	}

	return Waypoints[Index].SegmentSpeed;
}

void ADronePathActor::ClearWaypoints()
{
	Waypoints.Reset();
	SyncPathState(true);
}

void ADronePathActor::SetClosedLoop(bool bInClosedLoop)
{
	bClosedLoop = bInClosedLoop;
	SyncPathState(false);
}

void ADronePathActor::RefreshPath()
{
	SyncPathState(false);
}

void ADronePathActor::SetPathNumericId(int32 NewPathNumericId)
{
	if (PathNumericId == NewPathNumericId)
	{
		SyncWaypointHandles(false);
		BroadcastPathUpdated();
		return;
	}

	PathNumericId = NewPathNumericId;
	SyncWaypointHandles(false);
	BroadcastPathUpdated();
}

int32 ADronePathActor::GetPathNumericId() const
{
	return PathNumericId;
}

int32 ADronePathActor::GetWaypointCount() const
{
	return Waypoints.Num();
}

FVector ADronePathActor::GetWaypointWorldLocation(int32 Index) const
{
	if (!IsValidWaypointIndex(Index))
	{
		return GetActorLocation();
	}

	return LocalToWorldLocation(Waypoints[Index].Location);
}

void ADronePathActor::ClearConflictVisualization()
{
	ConflictedWaypointIndices.Reset();
	ConflictedSegmentStartIndices.Reset();
	RefreshConflictVisualization();
}

void ADronePathActor::AddWaypointAtEnd()
{
	FVector NewLocation = GetActorLocation();

	if (Waypoints.Num() > 0)
	{
		const FVector LastLocation = GetWaypointWorldLocation(Waypoints.Last().Index);
		FVector Direction = GetActorForwardVector();

		if (Waypoints.Num() > 1)
		{
			const FVector PreviousLocation = GetWaypointWorldLocation(Waypoints[Waypoints.Num() - 2].Index);
			const FVector SegmentDirection = (LastLocation - PreviousLocation).GetSafeNormal();
			if (!SegmentDirection.IsNearlyZero())
			{
				Direction = SegmentDirection;
			}
		}

		NewLocation = LastLocation + (Direction * DefaultWaypointSpacing);
	}

	const float NewSegmentSpeed = Waypoints.IsEmpty() ? 0.0f : FMath::Clamp(Waypoints.Last().SegmentSpeed, 0.0f, 15.0f);
	AddWaypoint(NewLocation, NewSegmentSpeed);
}

void ADronePathActor::ScheduleExecution(float StartTime)
{
	ScheduledStartTime = StartTime;
	RemainingStartDelay = 0.0f;
	SetPathStatus(EPathStatus::Standby);

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		SetExecutionState(EDronePathExecutionState::Scheduled);
		return;
	}

	FTimerManager& TimerManager = World->GetTimerManager();
	TimerManager.ClearTimer(ScheduledExecutionTimerHandle);

	const float CurrentTime = World->GetTimeSeconds();
	const float Delay = FMath::Max(0.0f, StartTime - CurrentTime);

	if (Delay <= KINDA_SMALL_NUMBER)
	{
		HandleScheduledExecutionStart();
		return;
	}

	RemainingStartDelay = Delay;
	TimerManager.SetTimer(ScheduledExecutionTimerHandle, this, &ADronePathActor::HandleScheduledExecutionStart, Delay, false);
	SetExecutionState(EDronePathExecutionState::Scheduled);
}

void ADronePathActor::PauseExecution()
{
	UWorld* World = GetWorld();
	if (IsValid(World))
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		if (TimerManager.IsTimerActive(ScheduledExecutionTimerHandle))
		{
			RemainingStartDelay = TimerManager.GetTimerRemaining(ScheduledExecutionTimerHandle);
			TimerManager.ClearTimer(ScheduledExecutionTimerHandle);
		}
	}

	if (ExecutionState == EDronePathExecutionState::Scheduled || ExecutionState == EDronePathExecutionState::Running)
	{
		SetExecutionState(EDronePathExecutionState::Paused);
	}

	if (bIsMoving)
	{
		StopMovementInternal(false);
	}

	SetPathStatus(EPathStatus::Standby);
}

void ADronePathActor::CompleteExecution()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ScheduledExecutionTimerHandle);
	}

	if (bIsMoving)
	{
		SnapControlledDroneToPathEnd();
	}

	StopMovementInternal(false);
	RemainingStartDelay = 0.0f;
	SetPathStatus(EPathStatus::Completed);
	SetExecutionState(EDronePathExecutionState::Completed);
}

void ADronePathActor::UpdatePathStatus(EPathStatus NewStatus)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ScheduledExecutionTimerHandle);
	}

	RemainingStartDelay = 0.0f;

	switch (NewStatus)
	{
	case EPathStatus::Standby:
		StopMovementInternal(false);
		ScheduledStartTime = 0.0f;
		SetPathStatus(EPathStatus::Standby);
		SetExecutionState(EDronePathExecutionState::Idle);
		break;

	case EPathStatus::InFlight:
		if (UWorld* World = GetWorld())
		{
			ScheduledStartTime = World->GetTimeSeconds();
		}

		SetPathStatus(EPathStatus::InFlight);
		SetExecutionState(EDronePathExecutionState::Running);
		break;

	case EPathStatus::Completed:
		CompleteExecution();
		break;

	default:
		break;
	}
}

bool ADronePathActor::IsExecutionActive() const
{
	return ExecutionState == EDronePathExecutionState::Scheduled || ExecutionState == EDronePathExecutionState::Running;
}

EPathStatus ADronePathActor::GetCurrentPathStatus() const
{
	return CurrentPathStatus;
}

void ADronePathActor::PausePathExecution()
{
	PauseExecution();
}

void ADronePathActor::StartMovement(AActor* DroneActor)
{
	if (!IsValid(DroneActor))
	{
		UE_LOG(LogDronePathActor, Warning, TEXT("StartMovement failed. Invalid controlled drone on %s."), *GetName());
		return;
	}

	if (Waypoints.Num() < 2)
	{
		UE_LOG(LogDronePathActor, Warning, TEXT("StartMovement failed. %s requires at least 2 waypoints."), *GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		UE_LOG(LogDronePathActor, Warning, TEXT("StartMovement failed. %s has no valid world."), *GetName());
		return;
	}

	World->GetTimerManager().ClearTimer(ScheduledExecutionTimerHandle);

	ControlledDrone = DroneActor;
	PrecomputeSegmentDurations();

	CurrentSegmentIndex = 1;
	SegmentStartTime = World->GetTimeSeconds();
	SegmentDuration = SegmentDurations.IsValidIndex(CurrentSegmentIndex) ? SegmentDurations[CurrentSegmentIndex] : DronePathVisual::MinSegmentDurationSeconds;
	bIsMoving = true;

	SnapControlledDroneToPathStart();
	SetActorTickEnabled(true);
	SetPathStatus(EPathStatus::InFlight);
	SetExecutionState(EDronePathExecutionState::Running);
	BroadcastPathUpdated();
}

void ADronePathActor::StopMovement()
{
	StopMovementInternal(false);
	SetPathStatus(EPathStatus::Standby);
	SetExecutionState(EDronePathExecutionState::Idle);
}

bool ADronePathActor::ResumeMovement(AActor* DroneActor, int32 SegmentIndex, float RemainingSegmentTime)
{
	if (!IsValid(DroneActor))
	{
		UE_LOG(LogDronePathActor, Warning, TEXT("ResumeMovement failed. Invalid controlled drone on %s."), *GetName());
		return false;
	}

	if (Waypoints.Num() < 2)
	{
		UE_LOG(LogDronePathActor, Warning, TEXT("ResumeMovement failed. %s requires at least 2 waypoints."), *GetName());
		return false;
	}

	if (!Waypoints.IsValidIndex(SegmentIndex) || SegmentIndex <= 0)
	{
		UE_LOG(LogDronePathActor, Warning, TEXT("ResumeMovement failed. Invalid segment index %d on %s."), SegmentIndex, *GetName());
		return false;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		UE_LOG(LogDronePathActor, Warning, TEXT("ResumeMovement failed. %s has no valid world."), *GetName());
		return false;
	}

	World->GetTimerManager().ClearTimer(ScheduledExecutionTimerHandle);

	ControlledDrone = DroneActor;
	PrecomputeSegmentDurations();

	CurrentSegmentIndex = SegmentIndex;
	SegmentDuration = SegmentDurations.IsValidIndex(CurrentSegmentIndex) ? SegmentDurations[CurrentSegmentIndex] : DronePathVisual::MinSegmentDurationSeconds;

	const float SafeDuration = FMath::Max(SegmentDuration, DronePathVisual::MinSegmentDurationSeconds);
	const float SafeRemainingTime = FMath::Clamp(RemainingSegmentTime, 0.0f, SafeDuration);
	const float ElapsedTime = SafeDuration - SafeRemainingTime;
	SegmentStartTime = World->GetTimeSeconds() - ElapsedTime;
	bIsMoving = true;

	const FVector StartLocation = GetWaypointWorldLocation(CurrentSegmentIndex - 1);
	const FVector EndLocation = GetWaypointWorldLocation(CurrentSegmentIndex);
	const float Alpha = FMath::Clamp(ElapsedTime / SafeDuration, 0.0f, 1.0f);
	const FVector ResumeLocation = FMath::Lerp(StartLocation, EndLocation, Alpha);

	DroneActor->SetActorLocation(ResumeLocation);

	const FVector TravelDirection = EndLocation - StartLocation;
	if (bOrientControlledDroneToPath && !TravelDirection.IsNearlyZero())
	{
		DroneActor->SetActorRotation(TravelDirection.Rotation());
	}

	SetActorTickEnabled(true);
	SetPathStatus(EPathStatus::InFlight);
	SetExecutionState(EDronePathExecutionState::Running);
	BroadcastPathUpdated();
	return true;
}

bool ADronePathActor::IsMovementActive() const
{
	return bIsMoving;
}

void ADronePathActor::RemoveLastWaypoint()
{
	if (Waypoints.IsEmpty())
	{
		return;
	}

	RemoveWaypoint(Waypoints.Num() - 1);
}

void ADronePathActor::RebuildWaypointHandles()
{
	SyncPathState(true);
}

void ADronePathActor::HandleWaypointActorMoved(const ADroneWaypointActor* WaypointActor, const FVector& NewWorldLocation)
{
	if (!IsValid(WaypointActor))
	{
		return;
	}

	const int32 WaypointIndex = WaypointActor->WaypointIndex;
	if (!IsValidWaypointIndex(WaypointIndex))
	{
		return;
	}

	Waypoints[WaypointIndex].Location = WorldToLocalLocation(NewWorldLocation);
	SyncPathState(false);
}

void ADronePathActor::HandleWaypointActorDestroyed(const ADroneWaypointActor* WaypointActor)
{
	if (bIsRebuildingWaypointHandles || !IsValid(WaypointActor))
	{
		return;
	}

	RemoveWaypoint(WaypointActor->WaypointIndex);
}

void ADronePathActor::MarkConflictSegment(int32 StartWaypointIndex, int32 EndWaypointIndex)
{
	if (!IsValidWaypointIndex(StartWaypointIndex) || !IsValidWaypointIndex(EndWaypointIndex))
	{
		return;
	}

	ConflictedSegmentStartIndices.Add(StartWaypointIndex);
	ConflictedWaypointIndices.Add(StartWaypointIndex);
	ConflictedWaypointIndices.Add(EndWaypointIndex);
}

void ADronePathActor::RefreshConflictVisualization()
{
	ApplyPathVisualState();

	for (int32 HandleIndex = 0; HandleIndex < WaypointHandleActors.Num(); ++HandleIndex)
	{
		if (ADroneWaypointActor* HandleActor = WaypointHandleActors[HandleIndex])
		{
			HandleActor->SetConflictHighlighted(ConflictedWaypointIndices.Contains(HandleIndex));
		}
	}
}

void ADronePathActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	SyncPathState(false);
}

void ADronePathActor::BeginPlay()
{
	Super::BeginPlay();
	SyncPathState(true);
	SetActorTickEnabled(bIsMoving);
}

void ADronePathActor::PostLoad()
{
	Super::PostLoad();
	SyncPathState(true);
}

void ADronePathActor::Destroyed()
{
	StopMovementInternal(true);
	ResetCachedMaterialInstances();
	DestroySplineMeshes();
	DestroyWaypointHandles();
	Super::Destroyed();
}

void ADronePathActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ScheduledExecutionTimerHandle);
	}

	StopMovementInternal(true);
	ResetCachedMaterialInstances();
	DestroySplineMeshes();

	Super::EndPlay(EndPlayReason);
}

#if WITH_EDITOR
void ADronePathActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SyncPathState(true);
}
#endif

void ADronePathActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	TickMovement();
}

void ADronePathActor::NormalizeWaypointIndices()
{
	for (int32 Index = 0; Index < Waypoints.Num(); ++Index)
	{
		Waypoints[Index].Index = Index;
		Waypoints[Index].SegmentSpeed = (Index == 0) ? 0.0f : FMath::Clamp(Waypoints[Index].SegmentSpeed, 0.0f, 15.0f);
	}
}

void ADronePathActor::SyncPathState(bool bRebuildWaypointHandles)
{
	NormalizeWaypointIndices();
	PrecomputeSegmentDurations();
	RebuildSplineFromWaypoints();
	RebuildSplineMeshes();
	SyncWaypointHandles(bRebuildWaypointHandles);

	if (bIsMoving)
	{
		if (!SegmentDurations.IsValidIndex(CurrentSegmentIndex) || Waypoints.Num() < 2 || !IsValid(ControlledDrone.Get()))
		{
			StopMovementInternal(false);
			SetPathStatus(EPathStatus::Standby);
			SetExecutionState(EDronePathExecutionState::Idle);
		}
		else
		{
			SegmentDuration = SegmentDurations[CurrentSegmentIndex];
		}
	}

	if (IsValid(GetWorld()) && !HasAnyFlags(RF_ClassDefaultObject))
	{
		TArray<AActor*> FoundPathActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), ADronePathActor::StaticClass(), FoundPathActors);

		TArray<ADronePathActor*> WorldPaths;
		WorldPaths.Reserve(FoundPathActors.Num());

		for (AActor* FoundActor : FoundPathActors)
		{
			if (ADronePathActor* PathActor = Cast<ADronePathActor>(FoundActor))
			{
				WorldPaths.Add(PathActor);
			}
		}

		UDronePathConflictLibrary::CheckPathConflictsWithVolume(WorldPaths);
	}
	else
	{
		RefreshConflictVisualization();
	}

	BroadcastPathUpdated();
}

void ADronePathActor::RebuildSplineFromWaypoints()
{
	if (!IsValid(PathSpline))
	{
		return;
	}

	PathSpline->ClearSplinePoints(false);

	for (int32 Index = 0; Index < Waypoints.Num(); ++Index)
	{
		PathSpline->AddSplinePoint(Waypoints[Index].Location, ESplineCoordinateSpace::Local, false);
		PathSpline->SetSplinePointType(Index, ESplinePointType::Linear, false);
	}

	DefaultSplinePointType = ESplinePointType::Linear;
	PathSpline->SetClosedLoop(bClosedLoop, false);
	PathSpline->UpdateSpline();
}

void ADronePathActor::RebuildSplineMeshes()
{
	DestroySplineMeshes();

	if (!IsValid(SplineSegmentStaticMesh) || !IsValid(PathSpline) || GetSplineSegmentCount() <= 0)
	{
		ApplyPathVisualState();
		return;
	}

	const float UniformScale = FMath::Max(PathSplineThickness, 0.05f) / DronePathVisual::BasicShapeCylinderRadiusCm;

	auto CreateSplineMeshSegment = [&](const int32 StartIndex, const int32 EndIndex)
	{
		if (!IsValidWaypointIndex(StartIndex) || !IsValidWaypointIndex(EndIndex))
		{
			return;
		}

		USplineMeshComponent* SplineMeshComponent = NewObject<USplineMeshComponent>(this);
		if (!IsValid(SplineMeshComponent))
		{
			return;
		}

		SplineMeshComponent->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		SplineMeshComponent->SetMobility(EComponentMobility::Movable);
		SplineMeshComponent->SetupAttachment(PathSpline);
		SplineMeshComponent->SetStaticMesh(SplineSegmentStaticMesh);
		SplineMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SplineMeshComponent->SetGenerateOverlapEvents(false);
		SplineMeshComponent->SetForwardAxis(ESplineMeshAxis::Z, false);
		SplineMeshComponent->ComponentTags.AddUnique(DronePathVisual::RuntimeSplineSegmentTag);

		const FVector StartLocation = PathSpline->GetLocationAtSplinePoint(StartIndex, ESplineCoordinateSpace::Local);
		const FVector EndLocation = PathSpline->GetLocationAtSplinePoint(EndIndex, ESplineCoordinateSpace::Local);
		SplineMeshComponent->SetStartAndEnd(StartLocation, FVector::ZeroVector, EndLocation, FVector::ZeroVector, false);
		SplineMeshComponent->SetStartScale(FVector2D(UniformScale, UniformScale), false);
		SplineMeshComponent->SetEndScale(FVector2D(UniformScale, UniformScale), false);
		SplineMeshComponent->RegisterComponent();
		AddInstanceComponent(SplineMeshComponent);
		SplineMeshComponents.Add(SplineMeshComponent);
		SplineVisualizationMIDs.Add(nullptr);
		SplineConflictMIDs.Add(nullptr);
		SplineVisualizationMaterialSources.Add(nullptr);
		SplineConflictMaterialSources.Add(nullptr);
	};

	for (int32 WaypointIndex = 0; WaypointIndex < Waypoints.Num() - 1; ++WaypointIndex)
	{
		CreateSplineMeshSegment(WaypointIndex, WaypointIndex + 1);
	}

	if (bClosedLoop && Waypoints.Num() > 2)
	{
		CreateSplineMeshSegment(Waypoints.Num() - 1, 0);
	}

	ApplyPathVisualState();
}

void ADronePathActor::DestroySplineMeshes()
{
	TArray<USplineMeshComponent*> OwnedSplineMeshes;
	GetComponents<USplineMeshComponent>(OwnedSplineMeshes);

	for (USplineMeshComponent* SplineMeshComponent : OwnedSplineMeshes)
	{
		if (!IsValid(SplineMeshComponent) || !SplineMeshComponent->ComponentTags.Contains(DronePathVisual::RuntimeSplineSegmentTag))
		{
			continue;
		}

		RemoveInstanceComponent(SplineMeshComponent);
		SplineMeshComponent->UnregisterComponent();
		SplineMeshComponent->DestroyComponent();
	}

	SplineMeshComponents.Reset();
	SplineVisualizationMIDs.Reset();
	SplineConflictMIDs.Reset();
	SplineVisualizationMaterialSources.Reset();
	SplineConflictMaterialSources.Reset();
}

void ADronePathActor::SyncWaypointHandles(bool bForceRebuild)
{
	if (!ShouldSpawnWaypointHandles())
	{
		DestroyWaypointHandles();
		return;
	}

	bool bNeedsRebuild = bForceRebuild || WaypointHandleActors.Num() != Waypoints.Num();

	if (!bNeedsRebuild)
	{
		for (const TObjectPtr<ADroneWaypointActor>& HandleActor : WaypointHandleActors)
		{
			if (!IsValid(HandleActor))
			{
				bNeedsRebuild = true;
				break;
			}
		}
	}

	if (bNeedsRebuild)
	{
		DestroyWaypointHandles();

		UWorld* World = GetWorld();
		if (!IsValid(World))
		{
			return;
		}

		bIsRebuildingWaypointHandles = true;

		for (const FDroneWaypoint& Waypoint : Waypoints)
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Owner = this;
			SpawnParameters.OverrideLevel = GetLevel();
			SpawnParameters.ObjectFlags |= RF_Transient;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			UClass* HandleClass = IsValid(WaypointActorClass) ? WaypointActorClass.Get() : ADroneWaypointActor::StaticClass();
			ADroneWaypointActor* HandleActor = World->SpawnActor<ADroneWaypointActor>(HandleClass, LocalToWorldLocation(Waypoint.Location), FRotator::ZeroRotator, SpawnParameters);
			if (!IsValid(HandleActor))
			{
				continue;
			}

			HandleActor->AttachToActor(this, FAttachmentTransformRules::KeepWorldTransform);
			HandleActor->SetPathBinding(this, Waypoint.Index);
			HandleActor->SyncFromWaypointData(Waypoint, LocalToWorldLocation(Waypoint.Location));
			HandleActor->SetConflictHighlighted(ConflictedWaypointIndices.Contains(Waypoint.Index));
			WaypointHandleActors.Add(HandleActor);
		}

		bIsRebuildingWaypointHandles = false;
		RefreshConflictVisualization();
		return;
	}

	for (int32 Index = 0; Index < WaypointHandleActors.Num(); ++Index)
	{
		if (ADroneWaypointActor* HandleActor = WaypointHandleActors[Index])
		{
			HandleActor->SetPathBinding(this, Index);
			HandleActor->SyncFromWaypointData(Waypoints[Index], LocalToWorldLocation(Waypoints[Index].Location));
			HandleActor->SetConflictHighlighted(ConflictedWaypointIndices.Contains(Index));
		}
	}
}

void ADronePathActor::DestroyWaypointHandles()
{
	bIsRebuildingWaypointHandles = true;

	for (TObjectPtr<ADroneWaypointActor>& HandleActor : WaypointHandleActors)
	{
		if (IsValid(HandleActor))
		{
			HandleActor->PrepareForDestroyByPath();
			HandleActor->Destroy();
		}
	}

	WaypointHandleActors.Reset();
	bIsRebuildingWaypointHandles = false;
}

void ADronePathActor::BroadcastPathUpdated()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		ReceivePathUpdated();
	}
}

void ADronePathActor::BroadcastExecutionStateChanged()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		ReceiveExecutionStateChanged(ExecutionState, ScheduledStartTime);
	}
}

void ADronePathActor::ApplyPathVisualState()
{
	const bool bHasConflict = !ConflictedSegmentStartIndices.IsEmpty();
	const FLinearColor MarkerColor = bHasConflict ? PathConflictColor : PathDefaultColor;

	if (IsValid(PathMarkerMeshComponent))
	{
		UMaterialInstanceDynamic* PathMarkerMID = nullptr;
		if (bHasConflict)
		{
			PathMarkerMID = ResolveMaterialInstance(PathMarkerMeshComponent, PathConflictMaterial, PathMarkerConflictMID, PathMarkerConflictMaterialSource);
		}
		else
		{
			PathMarkerMID = ResolveMaterialInstance(PathMarkerMeshComponent, PathVisualizationMaterial, PathMarkerVisualizationMID, PathMarkerVisualizationMaterialSource);
		}

		ApplyColorToMaterial(PathMarkerMID, MarkerColor);
	}

	for (int32 SegmentIndex = 0; SegmentIndex < SplineMeshComponents.Num(); ++SegmentIndex)
	{
		if (USplineMeshComponent* SplineMeshComponent = SplineMeshComponents[SegmentIndex])
		{
			const bool bSegmentInConflict = ConflictedSegmentStartIndices.Contains(SegmentIndex);
			UMaterialInstanceDynamic* SegmentMID = nullptr;
			if (bSegmentInConflict)
			{
				SegmentMID = ResolveMaterialInstance(SplineMeshComponent, PathConflictMaterial, SplineConflictMIDs[SegmentIndex], SplineConflictMaterialSources[SegmentIndex]);
			}
			else
			{
				SegmentMID = ResolveMaterialInstance(SplineMeshComponent, PathVisualizationMaterial, SplineVisualizationMIDs[SegmentIndex], SplineVisualizationMaterialSources[SegmentIndex]);
			}

			ApplyColorToMaterial(SegmentMID, bSegmentInConflict ? PathConflictColor : PathDefaultColor);
		}
	}
}

bool ADronePathActor::ShouldSpawnWaypointHandles() const
{
	if (IsTemplate() || !IsValid(GetWorld()))
	{
		return false;
	}

	return GetWorld()->IsGameWorld() ? bSpawnWaypointHandlesAtRuntime : bSpawnWaypointHandlesInEditor;
}

bool ADronePathActor::IsValidWaypointIndex(int32 Index) const
{
	return Waypoints.IsValidIndex(Index);
}

FVector ADronePathActor::LocalToWorldLocation(const FVector& LocalLocation) const
{
	return GetActorTransform().TransformPosition(LocalLocation);
}

FVector ADronePathActor::WorldToLocalLocation(const FVector& WorldLocation) const
{
	return GetActorTransform().InverseTransformPosition(WorldLocation);
}

void ADronePathActor::SetExecutionState(EDronePathExecutionState NewExecutionState)
{
	if (ExecutionState == NewExecutionState)
	{
		BroadcastExecutionStateChanged();
		OnExecutionStateChanged.Broadcast(this, ExecutionState);
		return;
	}

	ExecutionState = NewExecutionState;
	OnExecutionStateChanged.Broadcast(this, ExecutionState);
	BroadcastExecutionStateChanged();
	BroadcastPathUpdated();
}

void ADronePathActor::SetPathStatus(EPathStatus NewPathStatus)
{
	if (UWorld* World = GetWorld())
	{
		LastStatusUpdateTime = World->GetTimeSeconds();
	}

	CurrentPathStatus = NewPathStatus;

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		ReceivePathStatusChanged(CurrentPathStatus, LastStatusUpdateTime);
	}
}

void ADronePathActor::HandleScheduledExecutionStart()
{
	RemainingStartDelay = 0.0f;
	SetPathStatus(EPathStatus::InFlight);
	SetExecutionState(EDronePathExecutionState::Running);
}

void ADronePathActor::PrecomputeSegmentDurations()
{
	SegmentDurations.Reset();
	SegmentDurations.SetNumZeroed(Waypoints.Num());

	for (int32 WaypointIndex = 1; WaypointIndex < Waypoints.Num(); ++WaypointIndex)
	{
		const float DistanceCm = FVector::Distance(GetWaypointWorldLocation(WaypointIndex - 1), GetWaypointWorldLocation(WaypointIndex));
		const float DistanceMeters = DistanceCm / DronePathVisual::CentimetersPerMeter;
		const float SpeedMetersPerSecond = Waypoints[WaypointIndex].SegmentSpeed;

		float ComputedDuration = KINDA_SMALL_NUMBER;
		if (SpeedMetersPerSecond > KINDA_SMALL_NUMBER)
		{
			ComputedDuration = DistanceMeters / SpeedMetersPerSecond;
		}

		SegmentDurations[WaypointIndex] = FMath::Max(ComputedDuration, DronePathVisual::MinSegmentDurationSeconds);
	}
}

void ADronePathActor::TickMovement()
{
	if (!bIsMoving)
	{
		return;
	}

	AActor* DroneActor = ControlledDrone.Get();
	UWorld* World = GetWorld();
	if (!IsValid(DroneActor) || !IsValid(World))
	{
		StopMovementInternal(true);
		SetPathStatus(EPathStatus::Standby);
		SetExecutionState(EDronePathExecutionState::Idle);
		return;
	}

	if (!Waypoints.IsValidIndex(CurrentSegmentIndex) || CurrentSegmentIndex <= 0)
	{
		StopMovementInternal(false);
		SetPathStatus(EPathStatus::Standby);
		SetExecutionState(EDronePathExecutionState::Idle);
		return;
	}

	const float Now = World->GetTimeSeconds();
	const float SafeDuration = FMath::Max(SegmentDuration, DronePathVisual::MinSegmentDurationSeconds);
	const float Alpha = FMath::Clamp((Now - SegmentStartTime) / SafeDuration, 0.0f, 1.0f);

	const FVector StartLocation = GetWaypointWorldLocation(CurrentSegmentIndex - 1);
	const FVector EndLocation = GetWaypointWorldLocation(CurrentSegmentIndex);
	const FVector NewLocation = FMath::Lerp(StartLocation, EndLocation, Alpha);
	DroneActor->SetActorLocation(NewLocation);

	const FVector TravelDirection = EndLocation - StartLocation;
	if (bOrientControlledDroneToPath && !TravelDirection.IsNearlyZero())
	{
		DroneActor->SetActorRotation(TravelDirection.Rotation());
	}

	if (bDrawMovementDebug)
	{
		DrawDebugString(
			World,
			NewLocation + MovementDebugTextOffset,
			FString::Printf(TEXT("Seg: %d Time: %.2f / %.2f"), CurrentSegmentIndex, Now - SegmentStartTime, SafeDuration),
			nullptr,
			FColor::Green,
			0.0f,
			false);
	}

	if (Alpha < 1.0f)
	{
		return;
	}

	++CurrentSegmentIndex;
	if (CurrentSegmentIndex >= Waypoints.Num())
	{
		SnapControlledDroneToPathEnd();
		CompleteExecution();
		ReceivePathMovementFinished(DroneActor);
		return;
	}

	SegmentStartTime = Now;
	SegmentDuration = SegmentDurations.IsValidIndex(CurrentSegmentIndex) ? SegmentDurations[CurrentSegmentIndex] : DronePathVisual::MinSegmentDurationSeconds;
}

void ADronePathActor::StopMovementInternal(bool bClearControlledDrone)
{
	bIsMoving = false;
	CurrentSegmentIndex = INDEX_NONE;
	SegmentStartTime = 0.0f;
	SegmentDuration = 0.0f;
	SetActorTickEnabled(false);

	if (bClearControlledDrone)
	{
		ControlledDrone = nullptr;
	}
}

void ADronePathActor::SnapControlledDroneToPathStart() const
{
	if (!IsValid(ControlledDrone.Get()) || Waypoints.IsEmpty())
	{
		return;
	}

	AActor* DroneActor = ControlledDrone.Get();
	const FVector StartLocation = GetWaypointWorldLocation(0);
	DroneActor->SetActorLocation(StartLocation);

	if (bOrientControlledDroneToPath && Waypoints.Num() > 1)
	{
		const FVector ForwardDirection = GetWaypointWorldLocation(1) - StartLocation;
		if (!ForwardDirection.IsNearlyZero())
		{
			DroneActor->SetActorRotation(ForwardDirection.Rotation());
		}
	}
}

void ADronePathActor::SnapControlledDroneToPathEnd() const
{
	if (!IsValid(ControlledDrone.Get()) || Waypoints.IsEmpty())
	{
		return;
	}

	AActor* DroneActor = ControlledDrone.Get();
	const FVector EndLocation = GetWaypointWorldLocation(Waypoints.Num() - 1);
	DroneActor->SetActorLocation(EndLocation);

	if (bOrientControlledDroneToPath && Waypoints.Num() > 1)
	{
		const FVector EndDirection = EndLocation - GetWaypointWorldLocation(Waypoints.Num() - 2);
		if (!EndDirection.IsNearlyZero())
		{
			DroneActor->SetActorRotation(EndDirection.Rotation());
		}
	}
}

void ADronePathActor::ResetCachedMaterialInstances()
{
	PathMarkerVisualizationMID = nullptr;
	PathMarkerConflictMID = nullptr;
	PathMarkerVisualizationMaterialSource = nullptr;
	PathMarkerConflictMaterialSource = nullptr;
	SplineVisualizationMIDs.Reset();
	SplineConflictMIDs.Reset();
	SplineVisualizationMaterialSources.Reset();
	SplineConflictMaterialSources.Reset();
}

UMaterialInstanceDynamic* ADronePathActor::ResolveMaterialInstance(UMeshComponent* PrimitiveComponent, UMaterialInterface* BaseMaterial, TObjectPtr<UMaterialInstanceDynamic>& CachedMID, TObjectPtr<UMaterialInterface>& CachedSource)
{
	if (!IsValid(PrimitiveComponent))
	{
		return nullptr;
	}

	if (!IsValid(BaseMaterial))
	{
		return Cast<UMaterialInstanceDynamic>(PrimitiveComponent->GetMaterial(0));
	}

	if (!IsValid(CachedMID) || CachedSource != BaseMaterial)
	{
		CachedMID = UMaterialInstanceDynamic::Create(BaseMaterial, PrimitiveComponent);
		CachedSource = BaseMaterial;
	}

	if (!IsValid(CachedMID))
	{
		return nullptr;
	}

	if (PrimitiveComponent->GetMaterial(0) != CachedMID)
	{
		PrimitiveComponent->SetMaterial(0, CachedMID);
	}

	return CachedMID;
}

void ADronePathActor::ApplyColorToMaterial(UMaterialInstanceDynamic* MaterialInstance, const FLinearColor& InColor) const
{
	if (!IsValid(MaterialInstance))
	{
		return;
	}

	const FVector ColorVector(InColor.R, InColor.G, InColor.B);
	const FVector EmissiveColorVector = ColorVector * 6.0f;

	MaterialInstance->SetVectorParameterValue(DronePathVisual::ColorParameterName, FLinearColor(ColorVector.X, ColorVector.Y, ColorVector.Z));
	MaterialInstance->SetVectorParameterValue(DronePathVisual::BaseColorParameterName, FLinearColor(ColorVector.X, ColorVector.Y, ColorVector.Z));
	MaterialInstance->SetVectorParameterValue(DronePathVisual::EmissiveColorParameterName, FLinearColor(EmissiveColorVector.X, EmissiveColorVector.Y, EmissiveColorVector.Z));
	MaterialInstance->SetVectorParameterValue(DronePathVisual::TintParameterName, FLinearColor(ColorVector.X, ColorVector.Y, ColorVector.Z));
}

int32 ADronePathActor::GetSplineSegmentCount() const
{
	if (Waypoints.Num() < 2)
	{
		return 0;
	}

	return bClosedLoop && Waypoints.Num() > 2 ? Waypoints.Num() : Waypoints.Num() - 1;
}
