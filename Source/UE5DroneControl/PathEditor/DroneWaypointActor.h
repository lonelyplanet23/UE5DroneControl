#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DroneWaypointTypes.h"
#include "DroneWaypointActor.generated.h"

class ADronePathActor;
class UArrowComponent;
class UBillboardComponent;
class UBoxComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;
class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EGizmoAxis : uint8
{
	None UMETA(DisplayName = "None"),
	X UMETA(DisplayName = "X"),
	Y UMETA(DisplayName = "Y"),
	Z UMETA(DisplayName = "Z")
};

UCLASS(BlueprintType, Blueprintable)
class UE5DRONECONTROL_API ADroneWaypointActor : public AActor
{
	GENERATED_BODY()

public:
	ADroneWaypointActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBillboardComponent> BillboardComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTextRenderComponent> TextRenderComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> GizmoXAxisHandle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> GizmoYAxisHandle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UArrowComponent> GizmoZAxisHandle;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> GizmoXAxisHitTarget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> GizmoYAxisHitTarget;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UBoxComponent> GizmoZAxisHitTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint")
	TObjectPtr<ADronePathActor> PathActor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waypoint")
	int32 WaypointIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Waypoint")
	int32 DisplayPathNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization")
	FLinearColor NormalTextColor = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization")
	FLinearColor ConflictTextColor = FLinearColor::Red;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization", meta = (ClampMin = "0.05", UIMin = "0.05"))
	float ConflictBlinkInterval = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization")
	TObjectPtr<UMaterialInterface> WaypointVisualizationMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization")
	TObjectPtr<UMaterialInterface> WaypointConflictMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization")
	FLinearColor SelectedTextColor = FLinearColor::Yellow;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float SelectedScaleMultiplier = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float GizmoHandleLength = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization", meta = (ClampMin = "0.01", UIMin = "0.01"))
	float GizmoHandleThickness = 0.045f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization")
	FLinearColor GizmoXAxisColor = FLinearColor(1.0f, 0.1f, 0.1f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization")
	FLinearColor GizmoYAxisColor = FLinearColor(0.1f, 1.0f, 0.1f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization")
	FLinearColor GizmoZAxisColor = FLinearColor(0.1f, 0.45f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float ActiveGizmoBrightnessMultiplier = 1.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waypoint|Visualization")
	TObjectPtr<UMaterialInterface> GizmoVisualizationMaterial;

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Waypoint")
	void SetPathBinding(ADronePathActor* InPathActor, int32 InWaypointIndex);

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Waypoint")
	void SyncFromWaypointData(const FDroneWaypoint& WaypointData, const FVector& WorldLocation);

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Waypoint")
	void UpdateDisplayText(int32 PathId, float SegmentSpeed);

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Waypoint")
	void ApplyCurrentLocationToPath();

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Waypoint")
	void SetConflictHighlighted(bool bInConflictHighlighted);

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Waypoint")
	void SetSelected(bool bInSelected);

	UFUNCTION(BlueprintPure, Category = "Drone Path|Waypoint")
	bool IsSelected() const;

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Waypoint")
	void SetActiveGizmoAxis(EGizmoAxis NewActiveAxis);

	UFUNCTION(BlueprintPure, Category = "Drone Path|Waypoint")
	EGizmoAxis GetActiveGizmoAxis() const;

	UFUNCTION(BlueprintPure, Category = "Drone Path|Waypoint")
	EGizmoAxis GetGizmoAxisFromComponent(const UPrimitiveComponent* PrimitiveComponent) const;

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Waypoint")
	void BeginDeferredPathUpdate();

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Waypoint")
	void EndDeferredPathUpdate(bool bCommitPathUpdate);

	UFUNCTION(BlueprintPure, Category = "Drone Path|Waypoint")
	bool IsDeferringPathUpdate() const;

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Waypoint")
	void MoveAlongGizmoAxis(EGizmoAxis Axis, float DeltaDistance);

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Waypoint")
	void SetWaypointWorldLocation(const FVector& NewLocation);

	UFUNCTION(BlueprintCallable, Category = "Drone Path|Waypoint")
	bool SetSegmentSpeed(float NewSegmentSpeed);

	UFUNCTION(CallInEditor, Category = "Drone Path|Editor")
	void RemoveFromPath();

	void PrepareForDestroyByPath();

protected:
	virtual void Destroyed() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void UpdateEditorLabel();
	void UpdateTextFacingCamera();
	void UpdateDisplayedPathNumber();
	void ApplyVisualState();
	void ToggleConflictBlink();
	void ResetCachedMaterialInstances();
	UMaterialInstanceDynamic* ResolveMeshMaterialInstance(bool bUseConflictMaterial);
	void ConfigureGizmoHandle(UArrowComponent* GizmoHandle, const FName& ComponentName, const FVector& AxisDirection);
	void ConfigureGizmoHitTarget(UBoxComponent* GizmoHitTarget, const FName& ComponentName, const FVector& AxisDirection);
	void ApplyColorToMaterial(UMaterialInstanceDynamic* MaterialInstance, const FLinearColor& InColor);
	void ApplyColorToMesh(const FLinearColor& InColor);

	bool bSyncingFromPath = false;
	bool bSuppressPathNotifications = false;
	bool bDeferredPathUpdate = false;
	bool bConflictHighlighted = false;
	bool bConflictBlinkVisible = false;
	bool bSelected = false;
	EGizmoAxis ActiveGizmoAxis = EGizmoAxis::None;
	float DisplaySegmentSpeed = 0.0f;
	FVector LastObservedWorldLocation = FVector::ZeroVector;
	FTimerHandle ConflictBlinkTimerHandle;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WaypointVisualizationMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> WaypointConflictMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WaypointVisualizationMaterialSource;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> WaypointConflictMaterialSource;
};
