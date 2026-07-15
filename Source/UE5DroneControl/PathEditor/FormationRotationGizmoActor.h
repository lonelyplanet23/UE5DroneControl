#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FormationRotationGizmoActor.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

/**
 * 编队旋转 Gizmo：在运行锚点位置显示一个水平面上的 Z 轴旋转环。
 * 用户左键拖动旋转环即可绕世界 Z 轴整体旋转编队路径。只影响水平朝向，不改变高度。
 *
 * 该 Actor 只提供可视化与可点击命中体；实际旋转角度由 PlayerController 计算并驱动。
 */
UCLASS()
class UE5DRONECONTROL_API AFormationRotationGizmoActor : public AActor
{
	GENERATED_BODY()

public:
	AFormationRotationGizmoActor();

	/** 环的半径（cm）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo", meta = (ClampMin = "50.0"))
	float RingRadiusCm = 400.0f;

	/** 组成旋转环的分段数。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo", meta = (ClampMin = "8", ClampMax = "128"))
	int32 RingSegmentCount = 48;

	/** 环的粗细（cm）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo", meta = (ClampMin = "1.0"))
	float RingThicknessCm = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo")
	FLinearColor RingColor = FLinearColor(0.1f, 0.6f, 1.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Gizmo")
	FLinearColor RingActiveColor = FLinearColor(1.0f, 0.85f, 0.1f, 1.0f);

	/** 返回该组件是否属于本 Gizmo 的旋转环命中体（供 PlayerController 判定点击）。 */
	bool IsRingComponent(const UPrimitiveComponent* Component) const;

	/** 设置高亮（拖拽中）状态。 */
	void SetRingHighlighted(bool bHighlighted);

	/** 以世界坐标设置 Gizmo 中心（锚点位置）。 */
	void SetGizmoWorldLocation(const FVector& WorldLocation);

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	void RebuildRing();

	UPROPERTY(VisibleAnywhere, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> RingSegments;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> RingSegmentMIDs;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> SegmentMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> SegmentMaterial;
};
