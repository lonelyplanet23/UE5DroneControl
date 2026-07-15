#include "FormationRotationGizmoActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace FormationGizmo
{
	static const FName RingSegmentTag(TEXT("FormationRotationRingSegment"));
	static const FName ColorParameterName(TEXT("Color"));
	// /Engine/BasicShapes/Cylinder 半径为 50cm、半高 50cm。
	constexpr float BasicCylinderRadiusCm = 50.0f;
	constexpr float BasicCylinderHalfHeightCm = 50.0f;
}

AFormationRotationGizmoActor::AFormationRotationGizmoActor()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		SegmentMesh = CylinderMesh.Object;
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultMaterial(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (DefaultMaterial.Succeeded())
	{
		SegmentMaterial = DefaultMaterial.Object;
	}
}

void AFormationRotationGizmoActor::BeginPlay()
{
	Super::BeginPlay();
	RebuildRing();
}

void AFormationRotationGizmoActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildRing();
}

void AFormationRotationGizmoActor::SetGizmoWorldLocation(const FVector& WorldLocation)
{
	SetActorLocation(WorldLocation);
}

void AFormationRotationGizmoActor::RebuildRing()
{
	// 清理旧的环段。
	for (UStaticMeshComponent* Segment : RingSegments)
	{
		if (IsValid(Segment))
		{
			Segment->DestroyComponent();
		}
	}
	RingSegments.Reset();
	RingSegmentMIDs.Reset();

	if (!IsValid(SegmentMesh))
	{
		return;
	}

	const int32 SegmentCount = FMath::Clamp(RingSegmentCount, 8, 128);
	const float Radius = FMath::Max(RingRadiusCm, 50.0f);
	const float Thickness = FMath::Max(RingThicknessCm, 1.0f);

	// 每个环段是一小段圆柱，沿环的切线方向水平摆放，拼成一个水平圆环。
	const float AngleStepDeg = 360.0f / static_cast<float>(SegmentCount);
	// 相邻两段圆心之间的弦长，作为每段圆柱的长度。
	const float ChordLength = 2.0f * Radius * FMath::Sin(FMath::DegreesToRadians(AngleStepDeg * 0.5f));

	// 圆柱默认沿 Z 轴、半高 50cm、半径 50cm。缩放到目标尺寸。
	const float LengthScale = ChordLength / (2.0f * FormationGizmo::BasicCylinderHalfHeightCm);
	const float RadiusScale = (Thickness * 0.5f) / FormationGizmo::BasicCylinderRadiusCm;

	for (int32 i = 0; i < SegmentCount; ++i)
	{
		const float AngleDeg = AngleStepDeg * static_cast<float>(i);
		const float AngleRad = FMath::DegreesToRadians(AngleDeg);

		UStaticMeshComponent* Segment = NewObject<UStaticMeshComponent>(this);
		if (!IsValid(Segment))
		{
			continue;
		}

		Segment->SetupAttachment(SceneRoot);
		Segment->SetStaticMesh(SegmentMesh);
		Segment->SetMobility(EComponentMobility::Movable);
		Segment->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Segment->SetCollisionResponseToAllChannels(ECR_Block);
		Segment->SetGenerateOverlapEvents(false);
		Segment->ComponentTags.AddUnique(FormationGizmo::RingSegmentTag);

		// 段在环上的中心位置。
		const FVector SegmentLocation(Radius * FMath::Cos(AngleRad), Radius * FMath::Sin(AngleRad), 0.0f);

		// 圆柱轴(Z)对齐到环的切线方向；切线在水平面上，与半径垂直 => 朝向角 = AngleDeg + 90。
		const FRotator SegmentRotation(90.0f, AngleDeg + 90.0f, 0.0f);

		Segment->RegisterComponent();
		Segment->SetRelativeLocation(SegmentLocation);
		Segment->SetRelativeRotation(SegmentRotation);
		Segment->SetRelativeScale3D(FVector(RadiusScale, RadiusScale, LengthScale));
		AddInstanceComponent(Segment);

		UMaterialInstanceDynamic* MID = nullptr;
		if (IsValid(SegmentMaterial))
		{
			MID = UMaterialInstanceDynamic::Create(SegmentMaterial, Segment);
			if (IsValid(MID))
			{
				MID->SetVectorParameterValue(FormationGizmo::ColorParameterName, RingColor);
				Segment->SetMaterial(0, MID);
			}
		}

		RingSegments.Add(Segment);
		RingSegmentMIDs.Add(MID);
	}
}

bool AFormationRotationGizmoActor::IsRingComponent(const UPrimitiveComponent* Component) const
{
	if (!IsValid(Component))
	{
		return false;
	}

	return Component->ComponentTags.Contains(FormationGizmo::RingSegmentTag);
}

void AFormationRotationGizmoActor::SetRingHighlighted(bool bHighlighted)
{
	const FLinearColor Color = bHighlighted ? RingActiveColor : RingColor;
	for (UMaterialInstanceDynamic* MID : RingSegmentMIDs)
	{
		if (IsValid(MID))
		{
			MID->SetVectorParameterValue(FormationGizmo::ColorParameterName, Color);
		}
	}
}
