#include "DroneWaypointActor.h"

#include "Components/ArrowComponent.h"
#include "Components/BillboardComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "DronePathActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogDroneWaypointActor, Log, All);

namespace DroneWaypointVisual
{
	static const FName ColorParameterName(TEXT("Color"));
	static const FName BaseColorParameterName(TEXT("BaseColor"));
	static const FName EmissiveColorParameterName(TEXT("EmissiveColor"));
	static const FName TintParameterName(TEXT("Tint"));
	constexpr float BaseWaypointMeshScale = 0.15f;
	constexpr float GizmoHandleBaseOffsetCm = 12.0f;
	constexpr float ActiveGizmoScaleMultiplier = 1.2f;
}

ADroneWaypointActor::ADroneWaypointActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(SceneRoot);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	MeshComponent->SetGenerateOverlapEvents(false);
	MeshComponent->SetRelativeScale3D(FVector(0.15f));
	MeshComponent->SetMobility(EComponentMobility::Movable);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(SphereMesh.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (DefaultMaterial.Succeeded())
	{
		WaypointVisualizationMaterial = DefaultMaterial.Object;
		WaypointConflictMaterial = DefaultMaterial.Object;
		GizmoVisualizationMaterial = DefaultMaterial.Object;
		MeshComponent->SetMaterial(0, DefaultMaterial.Object);
	}

	GizmoXAxisHandle = CreateDefaultSubobject<UArrowComponent>(TEXT("GizmoXAxisHandle"));
	GizmoXAxisHandle->SetupAttachment(SceneRoot);
	ConfigureGizmoHandle(GizmoXAxisHandle, TEXT("GizmoXAxisHandle"), FVector::ForwardVector);

	GizmoYAxisHandle = CreateDefaultSubobject<UArrowComponent>(TEXT("GizmoYAxisHandle"));
	GizmoYAxisHandle->SetupAttachment(SceneRoot);
	ConfigureGizmoHandle(GizmoYAxisHandle, TEXT("GizmoYAxisHandle"), FVector::RightVector);

	GizmoZAxisHandle = CreateDefaultSubobject<UArrowComponent>(TEXT("GizmoZAxisHandle"));
	GizmoZAxisHandle->SetupAttachment(SceneRoot);
	ConfigureGizmoHandle(GizmoZAxisHandle, TEXT("GizmoZAxisHandle"), FVector::UpVector);

	GizmoXAxisHitTarget = CreateDefaultSubobject<UBoxComponent>(TEXT("GizmoXAxisHitTarget"));
	GizmoXAxisHitTarget->SetupAttachment(SceneRoot);
	ConfigureGizmoHitTarget(GizmoXAxisHitTarget, TEXT("GizmoXAxisHitTarget"), FVector::ForwardVector);

	GizmoYAxisHitTarget = CreateDefaultSubobject<UBoxComponent>(TEXT("GizmoYAxisHitTarget"));
	GizmoYAxisHitTarget->SetupAttachment(SceneRoot);
	ConfigureGizmoHitTarget(GizmoYAxisHitTarget, TEXT("GizmoYAxisHitTarget"), FVector::RightVector);

	GizmoZAxisHitTarget = CreateDefaultSubobject<UBoxComponent>(TEXT("GizmoZAxisHitTarget"));
	GizmoZAxisHitTarget->SetupAttachment(SceneRoot);
	ConfigureGizmoHitTarget(GizmoZAxisHitTarget, TEXT("GizmoZAxisHitTarget"), FVector::UpVector);

	BillboardComponent = CreateDefaultSubobject<UBillboardComponent>(TEXT("BillboardComponent"));
	BillboardComponent->SetupAttachment(SceneRoot);
	BillboardComponent->SetHiddenInGame(true);

	TextRenderComponent = CreateDefaultSubobject<UTextRenderComponent>(TEXT("TextRenderComponent"));
	TextRenderComponent->SetupAttachment(SceneRoot);
	TextRenderComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 70.0f));
	TextRenderComponent->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	TextRenderComponent->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	TextRenderComponent->SetWorldSize(48.0f);
	TextRenderComponent->SetTextRenderColor(NormalTextColor.ToFColor(true));
	TextRenderComponent->SetText(FText::FromString(TEXT("D1|S:0")));
}

void ADroneWaypointActor::SetPathBinding(ADronePathActor* InPathActor, int32 InWaypointIndex)
{
	PathActor = InPathActor;
	WaypointIndex = InWaypointIndex;
	LastObservedWorldLocation = GetActorLocation();

	if (UWorld* World = GetWorld())
	{
		SetActorTickEnabled(World->IsGameWorld() && IsValid(PathActor));
	}

	UpdateDisplayedPathNumber();
	UpdateDisplayText(DisplayPathNumber, IsValid(PathActor) ? PathActor->GetWaypointSegmentSpeed(WaypointIndex) : 0.0f);
	UpdateEditorLabel();
	ApplyVisualState();
}

void ADroneWaypointActor::SyncFromWaypointData(const FDroneWaypoint& WaypointData, const FVector& WorldLocation)
{
	bSyncingFromPath = true;
	WaypointIndex = WaypointData.Index;
	SetActorLocation(WorldLocation, false, nullptr, ETeleportType::TeleportPhysics);
	LastObservedWorldLocation = WorldLocation;
	bSyncingFromPath = false;

	UpdateDisplayedPathNumber();
	UpdateDisplayText(DisplayPathNumber, WaypointData.SegmentSpeed);
	UpdateEditorLabel();
	ApplyVisualState();
}

void ADroneWaypointActor::UpdateDisplayText(int32 PathId, float SegmentSpeed)
{
	DisplayPathNumber = PathId;
	DisplaySegmentSpeed = (WaypointIndex == 0) ? 0.0f : FMath::Max(0.0f, SegmentSpeed);

	FString SpeedText = FString::Printf(TEXT("%.2f"), DisplaySegmentSpeed);
	while (SpeedText.Contains(TEXT(".")) && (SpeedText.EndsWith(TEXT("0")) || SpeedText.EndsWith(TEXT("."))))
	{
		SpeedText.LeftChopInline(1, EAllowShrinking::No);
	}

	if (SpeedText.IsEmpty())
	{
		SpeedText = TEXT("0");
	}

	if (IsValid(TextRenderComponent))
	{
		TextRenderComponent->SetText(FText::FromString(FString::Printf(TEXT("D%d|S:%s"), DisplayPathNumber, *SpeedText)));
	}
}

void ADroneWaypointActor::ApplyCurrentLocationToPath()
{
	if (bSuppressPathNotifications || !IsValid(PathActor))
	{
		return;
	}

	LastObservedWorldLocation = GetActorLocation();
	PathActor->HandleWaypointActorMoved(this, GetActorLocation());
}

void ADroneWaypointActor::SetConflictHighlighted(bool bInConflictHighlighted)
{
	if (bConflictHighlighted == bInConflictHighlighted)
	{
		ApplyVisualState();
		return;
	}

	bConflictHighlighted = bInConflictHighlighted;
	bConflictBlinkVisible = bConflictHighlighted;

	if (UWorld* World = GetWorld())
	{
		FTimerManager& TimerManager = World->GetTimerManager();
		TimerManager.ClearTimer(ConflictBlinkTimerHandle);

		if (bConflictHighlighted)
		{
			TimerManager.SetTimer(ConflictBlinkTimerHandle, this, &ADroneWaypointActor::ToggleConflictBlink, ConflictBlinkInterval, true);
		}
	}

	ApplyVisualState();
}

void ADroneWaypointActor::SetSelected(bool bInSelected)
{
	if (bSelected == bInSelected)
	{
		ApplyVisualState();
		return;
	}

	bSelected = bInSelected;
	if (!bSelected)
	{
		ActiveGizmoAxis = EGizmoAxis::None;
	}
	ApplyVisualState();
}

bool ADroneWaypointActor::IsSelected() const
{
	return bSelected;
}

void ADroneWaypointActor::SetActiveGizmoAxis(EGizmoAxis NewActiveAxis)
{
	if (ActiveGizmoAxis == NewActiveAxis)
	{
		ApplyVisualState();
		return;
	}

	ActiveGizmoAxis = NewActiveAxis;
	ApplyVisualState();
}

EGizmoAxis ADroneWaypointActor::GetActiveGizmoAxis() const
{
	return ActiveGizmoAxis;
}

EGizmoAxis ADroneWaypointActor::GetGizmoAxisFromComponent(const UPrimitiveComponent* PrimitiveComponent) const
{
	if (PrimitiveComponent == GizmoXAxisHandle)
	{
		return EGizmoAxis::X;
	}

	if (PrimitiveComponent == GizmoXAxisHitTarget)
	{
		return EGizmoAxis::X;
	}

	if (PrimitiveComponent == GizmoYAxisHandle)
	{
		return EGizmoAxis::Y;
	}

	if (PrimitiveComponent == GizmoYAxisHitTarget)
	{
		return EGizmoAxis::Y;
	}

	if (PrimitiveComponent == GizmoZAxisHandle)
	{
		return EGizmoAxis::Z;
	}

	if (PrimitiveComponent == GizmoZAxisHitTarget)
	{
		return EGizmoAxis::Z;
	}

	return EGizmoAxis::None;
}

void ADroneWaypointActor::BeginDeferredPathUpdate()
{
	bDeferredPathUpdate = true;
	LastObservedWorldLocation = GetActorLocation();
}

void ADroneWaypointActor::EndDeferredPathUpdate(bool bCommitPathUpdate)
{
	const bool bShouldCommit = bCommitPathUpdate && IsValid(PathActor) && !bSuppressPathNotifications;
	bDeferredPathUpdate = false;
	ActiveGizmoAxis = EGizmoAxis::None;

	if (bShouldCommit)
	{
		ApplyCurrentLocationToPath();
	}
	else
	{
		LastObservedWorldLocation = GetActorLocation();
		ApplyVisualState();
	}
}

bool ADroneWaypointActor::IsDeferringPathUpdate() const
{
	return bDeferredPathUpdate;
}

void ADroneWaypointActor::MoveAlongGizmoAxis(EGizmoAxis Axis, float DeltaDistance)
{
	if (Axis == EGizmoAxis::None || !IsValid(PathActor) || PathActor->IsMovementActive())
	{
		return;
	}

	FVector NewLocation = GetActorLocation();
	switch (Axis)
	{
	case EGizmoAxis::X:
		NewLocation.X += DeltaDistance;
		break;

	case EGizmoAxis::Y:
		NewLocation.Y += DeltaDistance;
		break;

	case EGizmoAxis::Z:
		NewLocation.Z += DeltaDistance;
		break;

	case EGizmoAxis::None:
	default:
		return;
	}

	SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);

	if (!bDeferredPathUpdate)
	{
		ApplyCurrentLocationToPath();
	}
}

void ADroneWaypointActor::SetWaypointWorldLocation(const FVector& NewLocation)
{
	if (!IsValid(PathActor) || PathActor->IsMovementActive())
	{
		return;
	}

	bDeferredPathUpdate = false;
	SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);
	ApplyCurrentLocationToPath();
}

bool ADroneWaypointActor::SetSegmentSpeed(float NewSegmentSpeed)
{
	if (!IsValid(PathActor))
	{
		return false;
	}

	return PathActor->UpdateWaypointSegmentSpeed(WaypointIndex, NewSegmentSpeed);
}

void ADroneWaypointActor::RemoveFromPath()
{
	if (!IsValid(PathActor))
	{
		return;
	}

	PathActor->RemoveWaypoint(WaypointIndex);
}

void ADroneWaypointActor::PrepareForDestroyByPath()
{
	bSuppressPathNotifications = true;
	SetActorTickEnabled(false);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ConflictBlinkTimerHandle);
	}
	PathActor = nullptr;
}

void ADroneWaypointActor::Destroyed()
{
	ADronePathActor* CachedPathActor = PathActor.Get();
	const int32 CachedWaypointIndex = WaypointIndex;

	if (!bSuppressPathNotifications && IsValid(CachedPathActor))
	{
		WaypointIndex = CachedWaypointIndex;
		CachedPathActor->HandleWaypointActorDestroyed(this);
	}

	ResetCachedMaterialInstances();
	Super::Destroyed();
}

void ADroneWaypointActor::BeginPlay()
{
	Super::BeginPlay();
	LastObservedWorldLocation = GetActorLocation();
	SetActorTickEnabled(IsValid(PathActor));
	UpdateTextFacingCamera();
	ApplyVisualState();
}

void ADroneWaypointActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ConflictBlinkTimerHandle);
	}

	ResetCachedMaterialInstances();
	Super::EndPlay(EndPlayReason);
}

void ADroneWaypointActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	UpdateTextFacingCamera();

	if (bSyncingFromPath || bSuppressPathNotifications || bDeferredPathUpdate || !IsValid(PathActor))
	{
		return;
	}

	const FVector CurrentLocation = GetActorLocation();
	if (!CurrentLocation.Equals(LastObservedWorldLocation, 0.5f))
	{
		LastObservedWorldLocation = CurrentLocation;
		ApplyCurrentLocationToPath();
	}
}

#if WITH_EDITOR
void ADroneWaypointActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	if (!bSyncingFromPath)
	{
		ApplyCurrentLocationToPath();
	}
}

void ADroneWaypointActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateEditorLabel();
	ApplyVisualState();
}
#endif

void ADroneWaypointActor::UpdateEditorLabel()
{
	UpdateDisplayText(DisplayPathNumber, DisplaySegmentSpeed);

#if WITH_EDITOR
	SetActorLabel(FString::Printf(TEXT("Path_%02d_Waypoint_%02d"), DisplayPathNumber, WaypointIndex));
#endif
}

void ADroneWaypointActor::UpdateTextFacingCamera()
{
	if (!IsValid(TextRenderComponent))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (!IsValid(PlayerController) || !IsValid(PlayerController->PlayerCameraManager))
	{
		return;
	}

	const float CameraYaw = PlayerController->PlayerCameraManager->GetCameraRotation().Yaw;
	TextRenderComponent->SetWorldRotation(FRotator(0.0f, CameraYaw + 180.0f, 0.0f));
}

void ADroneWaypointActor::UpdateDisplayedPathNumber()
{
	if (IsValid(PathActor) && PathActor->PathNumericId != INDEX_NONE)
	{
		DisplayPathNumber = PathActor->PathNumericId;
		return;
	}

	DisplayPathNumber = 1;
}

void ADroneWaypointActor::ApplyVisualState()
{
	const bool bShowConflictColor = bConflictHighlighted && bConflictBlinkVisible;
	const FLinearColor DisplayColor = bShowConflictColor ? ConflictTextColor : (bSelected ? SelectedTextColor : NormalTextColor);

	if (IsValid(TextRenderComponent))
	{
		TextRenderComponent->SetTextRenderColor(DisplayColor.ToFColor(true));
	}

	if (IsValid(MeshComponent))
	{
		const float VisualScale = bSelected ? SelectedScaleMultiplier : 1.0f;
		MeshComponent->SetRelativeScale3D(FVector(DroneWaypointVisual::BaseWaypointMeshScale * VisualScale));
	}

	ApplyColorToMesh(DisplayColor);

	const bool bShowGizmo = bSelected;
	const float ArrowSize = FMath::Max(GizmoHandleThickness * 20.0f, 0.75f);
	const float CollisionRadius = FMath::Max(GizmoHandleLength * GizmoHandleThickness, 8.0f);
	const float CollisionHalfLength = FMath::Max(GizmoHandleLength * 0.5f, 12.0f);
	const float CollisionCenterOffset = DroneWaypointVisual::GizmoHandleBaseOffsetCm + CollisionHalfLength;

	auto UpdateGizmoHandle = [&](UArrowComponent* GizmoHandle, UBoxComponent* GizmoHitTarget, const FLinearColor& BaseColor, const FVector& AxisDirection, const EGizmoAxis Axis)
	{
		if (!IsValid(GizmoHandle) || !IsValid(GizmoHitTarget))
		{
			return;
		}

		const bool bIsActiveAxis = ActiveGizmoAxis == Axis;
		const float AxisScale = bIsActiveAxis ? DroneWaypointVisual::ActiveGizmoScaleMultiplier : 1.0f;

		GizmoHandle->SetVisibility(bShowGizmo, true);
		GizmoHandle->SetHiddenInGame(!bShowGizmo, true);
		GizmoHandle->SetCollisionEnabled(bShowGizmo ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		GizmoHandle->SetRelativeLocation(AxisDirection * DroneWaypointVisual::GizmoHandleBaseOffsetCm);
		GizmoHandle->SetRelativeRotation(AxisDirection.Rotation());
		GizmoHandle->SetRelativeScale3D(FVector(AxisScale));
		GizmoHandle->ArrowLength = GizmoHandleLength;
		GizmoHandle->ArrowSize = ArrowSize * AxisScale;

		FLinearColor HandleColor = BaseColor;
		if (bIsActiveAxis)
		{
			HandleColor *= ActiveGizmoBrightnessMultiplier;
			HandleColor.A = 1.0f;
		}

		GizmoHandle->ArrowColor = HandleColor.ToFColor(true);
		GizmoHandle->MarkRenderStateDirty();

		GizmoHitTarget->SetHiddenInGame(true, true);
		GizmoHitTarget->SetVisibility(false, true);
		GizmoHitTarget->SetCollisionEnabled(bShowGizmo ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		GizmoHitTarget->SetRelativeLocation(AxisDirection * CollisionCenterOffset);
		GizmoHitTarget->SetRelativeRotation(AxisDirection.Rotation());
		GizmoHitTarget->SetBoxExtent(FVector(CollisionHalfLength, CollisionRadius * AxisScale, CollisionRadius * AxisScale));
	};

	UpdateGizmoHandle(GizmoXAxisHandle, GizmoXAxisHitTarget, GizmoXAxisColor, FVector::ForwardVector, EGizmoAxis::X);
	UpdateGizmoHandle(GizmoYAxisHandle, GizmoYAxisHitTarget, GizmoYAxisColor, FVector::RightVector, EGizmoAxis::Y);
	UpdateGizmoHandle(GizmoZAxisHandle, GizmoZAxisHitTarget, GizmoZAxisColor, FVector::UpVector, EGizmoAxis::Z);
}

void ADroneWaypointActor::ToggleConflictBlink()
{
	bConflictBlinkVisible = !bConflictBlinkVisible;
	ApplyVisualState();
}

void ADroneWaypointActor::ResetCachedMaterialInstances()
{
	WaypointVisualizationMID = nullptr;
	WaypointConflictMID = nullptr;
	WaypointVisualizationMaterialSource = nullptr;
	WaypointConflictMaterialSource = nullptr;
}

UMaterialInstanceDynamic* ADroneWaypointActor::ResolveMeshMaterialInstance(bool bUseConflictMaterial)
{
	if (!IsValid(MeshComponent))
	{
		return nullptr;
	}

	UMaterialInterface* BaseMaterial = bUseConflictMaterial ? WaypointConflictMaterial.Get() : WaypointVisualizationMaterial.Get();
	TObjectPtr<UMaterialInstanceDynamic>& CachedMID = bUseConflictMaterial ? WaypointConflictMID : WaypointVisualizationMID;
	TObjectPtr<UMaterialInterface>& CachedSource = bUseConflictMaterial ? WaypointConflictMaterialSource : WaypointVisualizationMaterialSource;

	if (!IsValid(BaseMaterial))
	{
		return Cast<UMaterialInstanceDynamic>(MeshComponent->GetMaterial(0));
	}

	if (!IsValid(CachedMID) || CachedSource != BaseMaterial)
	{
		CachedMID = UMaterialInstanceDynamic::Create(BaseMaterial, MeshComponent);
		CachedSource = BaseMaterial;
	}

	if (!IsValid(CachedMID))
	{
		return nullptr;
	}

	if (MeshComponent->GetMaterial(0) != CachedMID)
	{
		MeshComponent->SetMaterial(0, CachedMID);
	}

	return CachedMID;
}

void ADroneWaypointActor::ConfigureGizmoHandle(UArrowComponent* GizmoHandle, const FName& ComponentName, const FVector& AxisDirection)
{
	if (!IsValid(GizmoHandle))
	{
		return;
	}

	GizmoHandle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GizmoHandle->SetCollisionResponseToAllChannels(ECR_Ignore);
	GizmoHandle->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GizmoHandle->SetGenerateOverlapEvents(false);
	GizmoHandle->SetMobility(EComponentMobility::Movable);
	GizmoHandle->SetRelativeRotation(AxisDirection.Rotation());
	GizmoHandle->SetRelativeLocation(AxisDirection * DroneWaypointVisual::GizmoHandleBaseOffsetCm);
	GizmoHandle->SetHiddenInGame(true);
	GizmoHandle->SetVisibility(false, true);
	GizmoHandle->SetComponentTickEnabled(false);
	GizmoHandle->ArrowLength = GizmoHandleLength;
	GizmoHandle->ArrowSize = FMath::Max(GizmoHandleThickness * 20.0f, 0.75f);
	GizmoHandle->ComponentTags.Add(ComponentName);
}

void ADroneWaypointActor::ConfigureGizmoHitTarget(UBoxComponent* GizmoHitTarget, const FName& ComponentName, const FVector& AxisDirection)
{
	if (!IsValid(GizmoHitTarget))
	{
		return;
	}

	GizmoHitTarget->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GizmoHitTarget->SetCollisionResponseToAllChannels(ECR_Ignore);
	GizmoHitTarget->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GizmoHitTarget->SetGenerateOverlapEvents(false);
	GizmoHitTarget->SetMobility(EComponentMobility::Movable);
	GizmoHitTarget->SetHiddenInGame(true);
	GizmoHitTarget->SetVisibility(false, true);
	GizmoHitTarget->SetRelativeRotation(AxisDirection.Rotation());
	GizmoHitTarget->SetRelativeLocation(AxisDirection * (DroneWaypointVisual::GizmoHandleBaseOffsetCm + (GizmoHandleLength * 0.5f)));
	GizmoHitTarget->SetBoxExtent(FVector(FMath::Max(GizmoHandleLength * 0.5f, 12.0f), 8.0f, 8.0f));
	GizmoHitTarget->SetCanEverAffectNavigation(false);
	GizmoHitTarget->ComponentTags.Add(ComponentName);
}

void ADroneWaypointActor::ApplyColorToMaterial(UMaterialInstanceDynamic* MaterialInstance, const FLinearColor& InColor)
{
	if (!IsValid(MaterialInstance))
	{
		return;
	}

	const FVector ColorVector(InColor.R, InColor.G, InColor.B);
	const FVector EmissiveColorVector = ColorVector * 4.0f;

	MaterialInstance->SetVectorParameterValue(DroneWaypointVisual::ColorParameterName, FLinearColor(ColorVector.X, ColorVector.Y, ColorVector.Z));
	MaterialInstance->SetVectorParameterValue(DroneWaypointVisual::BaseColorParameterName, FLinearColor(ColorVector.X, ColorVector.Y, ColorVector.Z));
	MaterialInstance->SetVectorParameterValue(DroneWaypointVisual::EmissiveColorParameterName, FLinearColor(EmissiveColorVector.X, EmissiveColorVector.Y, EmissiveColorVector.Z));
	MaterialInstance->SetVectorParameterValue(DroneWaypointVisual::TintParameterName, FLinearColor(ColorVector.X, ColorVector.Y, ColorVector.Z));
}

void ADroneWaypointActor::ApplyColorToMesh(const FLinearColor& InColor)
{
	ApplyColorToMaterial(ResolveMeshMaterialInstance(bConflictHighlighted), InColor);
}
