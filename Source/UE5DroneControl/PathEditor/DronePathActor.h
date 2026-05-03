#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SplineComponent.h"
#include "DroneWaypointTypes.h"
#include "DronePathActor.generated.h"

class ADroneWaypointActor;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;
class USceneComponent;
class USplineMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UWorld;

UENUM(BlueprintType)
enum class EDronePathExecutionState : uint8
{
	Idle UMETA(DisplayName = "Idle"),
	Scheduled UMETA(DisplayName = "Scheduled"),
	Running UMETA(DisplayName = "Running"),
	Paused UMETA(DisplayName = "Paused"),
	Completed UMETA(DisplayName = "Completed")
};

UENUM(BlueprintType)
enum class EPathStatus : uint8
{
	Standby UMETA(DisplayName = "Standby"),
	InFlight UMETA(DisplayName = "In Flight"),
	Completed UMETA(DisplayName = "Completed")
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDronePathExecutionStateChanged, ADronePathActor*, EDronePathExecutionState);

UCLASS(BlueprintType, Blueprintable)
class UE5DRONECONTROL_API ADronePathActor : public AActor
{
	GENERATED_BODY()

public:
	ADronePathActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USplineComponent> PathSpline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> PathMarkerMeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Path")
	TArray<FDroneWaypoint> Waypoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path")
	bool bClosedLoop = false;

	// Kept for asset compatibility; runtime rebuild always forces linear segments.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Visualization")
	TEnumAsByte<ESplinePointType::Type> DefaultSplinePointType = ESplinePointType::Linear;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Visualization")
	bool bSpawnWaypointHandlesInEditor = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Visualization", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DefaultWaypointSpacing = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Visualization")
	TSubclassOf<ADroneWaypointActor> WaypointActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Visualization")
	bool bSpawnWaypointHandlesAtRuntime = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Visualization")
	TObjectPtr<UStaticMesh> SplineSegmentStaticMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Visualization")
	TObjectPtr<UMaterialInterface> PathVisualizationMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Visualization")
	TObjectPtr<UMaterialInterface> PathConflictMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Visualization")
	FLinearColor PathDefaultColor = FLinearColor(0.0f, 0.35f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Visualization")
	FLinearColor PathConflictColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Visualization", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float PathSplineThickness = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Extensibility")
	FName PathId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Extensibility")
	bool bParticipatesInConflictChecks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Extensibility")
	int32 PathNumericId = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path|Execution")
	EDronePathExecutionState ExecutionState = EDronePathExecutionState::Idle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path|Execution")
	float ScheduledStartTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path|Execution")
	float RemainingStartDelay = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path|Execution")
	EPathStatus CurrentPathStatus = EPathStatus::Standby;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path|Execution")
	float LastStatusUpdateTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path|Movement")
	int32 CurrentSegmentIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path|Movement")
	float SegmentStartTime = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path|Movement")
	float SegmentDuration = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Path|Movement")
	bool bIsMoving = false;

	UPROPERTY(Transient, VisibleAnywhere, BlueprintReadOnly, Category = "Path|Movement")
	TWeakObjectPtr<AActor> ControlledDrone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Movement")
	bool bOrientControlledDroneToPath = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Movement")
	bool bDrawMovementDebug = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Path|Movement")
	FVector MovementDebugTextOffset = FVector(0.0f, 0.0f, 120.0f);

	FOnDronePathExecutionStateChanged OnExecutionStateChanged;

	UFUNCTION(BlueprintCallable, Category = "Drone Path")
	int32 AddWaypoint(const FVector& Location, float SegmentSpeed = 0.0f);

	UFUNCTION(BlueprintCallable, Category = "Drone Path")
	bool RemoveWaypoint(int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Drone Path")
	bool UpdateWaypoint(int32 Index, const FVector& NewLocation);

	UFUNCTION(BlueprintCallable, Category = "Drone Path")
	bool UpdateWaypointSegmentSpeed(int32 Index, float NewSegmentSpeed);

	UFUNCTION(BlueprintPure, Category = "Drone Path")
	float GetWaypointSegmentSpeed(int32 Index) const;

	UFUNCTION(BlueprintCallable, Category = "Drone Path")
	void ClearWaypoints();

	UFUNCTION(BlueprintCallable, Category = "Drone Path")
	void SetClosedLoop(bool bInClosedLoop);

	UFUNCTION(BlueprintCallable, Category = "Drone Path")
	void RefreshPath();

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Extensibility")
	void SetPathNumericId(int32 NewPathNumericId);

	UFUNCTION(BlueprintPure, Category = "Drone Path|Extensibility")
	int32 GetPathNumericId() const;

	UFUNCTION(BlueprintPure, Category = "Drone Path")
	int32 GetWaypointCount() const;

	UFUNCTION(BlueprintPure, Category = "Drone Path")
	FVector GetWaypointWorldLocation(int32 Index) const;

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Visualization")
	void ClearConflictVisualization();

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Execution")
	void ScheduleExecution(float StartTime);

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Execution")
	void PauseExecution();

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Execution")
	void CompleteExecution();

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Execution")
	void UpdatePathStatus(EPathStatus NewStatus);

	UFUNCTION(BlueprintPure, Category = "Drone Path|Execution")
	bool IsExecutionActive() const;

	UFUNCTION(BlueprintPure, Category = "Drone Path|Execution")
	EPathStatus GetCurrentPathStatus() const;

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Execution")
	void PausePathExecution();

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Movement")
	void StartMovement(AActor* DroneActor);

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Movement")
	void StopMovement();

	bool ResumeMovement(AActor* DroneActor, int32 SegmentIndex, float RemainingSegmentTime);

	UFUNCTION(BlueprintPure, Category = "Drone Path|Movement")
	bool IsMovementActive() const;

	UFUNCTION(BlueprintImplementableEvent, Category = "Drone Path|Events")
	void ReceivePathUpdated();

	UFUNCTION(BlueprintImplementableEvent, Category = "Drone Path|Events")
	void ReceiveExecutionStateChanged(EDronePathExecutionState NewExecutionState, float InScheduledStartTime);

	UFUNCTION(BlueprintImplementableEvent, Category = "Drone Path|Events")
	void ReceivePathStatusChanged(EPathStatus NewPathStatus, float InLastStatusUpdateTime);

	UFUNCTION(BlueprintImplementableEvent, Category = "Drone Path|Events")
	void ReceivePathMovementFinished(AActor* DroneActor);

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Drone Path|Editor")
	void AddWaypointAtEnd();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Drone Path|Editor")
	void RemoveLastWaypoint();

	UFUNCTION(CallInEditor, Category = "Drone Path|Editor")
	void RebuildWaypointHandles();

	void HandleWaypointActorMoved(const ADroneWaypointActor* WaypointActor, const FVector& NewWorldLocation);
	void HandleWaypointActorDestroyed(const ADroneWaypointActor* WaypointActor);
	void MarkConflictSegment(int32 StartWaypointIndex, int32 EndWaypointIndex);
	void RefreshConflictVisualization();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void PostLoad() override;
	virtual void Destroyed() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<ADroneWaypointActor>> WaypointHandleActors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> SplineMeshComponents;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PathMarkerVisualizationMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> PathMarkerConflictMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PathMarkerVisualizationMaterialSource;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> PathMarkerConflictMaterialSource;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SplineVisualizationMIDs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SplineConflictMIDs;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> SplineVisualizationMaterialSources;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> SplineConflictMaterialSources;

	UPROPERTY(Transient)
	TArray<float> SegmentDurations;

	void NormalizeWaypointIndices();
	void SyncPathState(bool bRebuildWaypointHandles);
	void RebuildSplineFromWaypoints();
	void RebuildSplineMeshes();
	void DestroySplineMeshes();
	void SyncWaypointHandles(bool bForceRebuild);
	void DestroyWaypointHandles();
	void BroadcastPathUpdated();
	void BroadcastExecutionStateChanged();
	void ApplyPathVisualState();
	bool ShouldSpawnWaypointHandles() const;
	bool IsValidWaypointIndex(int32 Index) const;
	FVector LocalToWorldLocation(const FVector& LocalLocation) const;
	FVector WorldToLocalLocation(const FVector& WorldLocation) const;
	void SetExecutionState(EDronePathExecutionState NewExecutionState);
	void SetPathStatus(EPathStatus NewPathStatus);
	void HandleScheduledExecutionStart();
	void PrecomputeSegmentDurations();
	void TickMovement();
	void StopMovementInternal(bool bClearControlledDrone);
	void SnapControlledDroneToPathStart() const;
	void SnapControlledDroneToPathEnd() const;
	void ResetCachedMaterialInstances();
	UMaterialInstanceDynamic* ResolveMaterialInstance(UMeshComponent* PrimitiveComponent, UMaterialInterface* BaseMaterial, TObjectPtr<UMaterialInstanceDynamic>& CachedMID, TObjectPtr<UMaterialInterface>& CachedSource);
	void ApplyColorToMaterial(UMaterialInstanceDynamic* MaterialInstance, const FLinearColor& InColor) const;
	int32 GetSplineSegmentCount() const;

	bool bIsRebuildingWaypointHandles = false;

	FTimerHandle ScheduledExecutionTimerHandle;
	TSet<int32> ConflictedWaypointIndices;
	TSet<int32> ConflictedSegmentStartIndices;
};
