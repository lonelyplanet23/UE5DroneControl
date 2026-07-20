// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DroneGroundProjectionComponent.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;

/**
 * 无人机地面轨迹幕帘组件
 *
 * 功能：
 * 1. 记录无人机飞行轨迹点及其对应的地面投影点
 * 2. 在无人机路径与地面之间生成垂直的半透明幕帘面（curtain surface）
 *    —— 飞直线 → 长方形，飞曲线 → 曲面
 * 3. 超时后幕帘面从旧到新逐渐淡出消失
 *
 * 全局开关通过 GConfig [DroneDisplay].bShowGroundProjectionRay 控制，默认开启。
 */
UCLASS(ClassGroup = "DroneOps", meta = (BlueprintSpawnableComponent))
class UE5DRONECONTROL_API UDroneGroundProjectionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDroneGroundProjectionComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

	// ---- 可配置属性 ----

	/** 幕帘面颜色 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GroundProjection")
	FLinearColor CurtainColor = FLinearColor(0.0f, 1.0f, 0.0f, 0.8f); // 绿色

	/** 向下检测最大距离（厘米） */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GroundProjection", meta = (ClampMin = "100.0"))
	float MaxTraceDistance = 500000.0f;

	/** 记录轨迹点的最小间距（厘米），避免密集重复记录 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GroundProjection|Trail", meta = (ClampMin = "1.0"))
	float MinRecordDistance = 50.0f;

	/** 轨迹存活时间（秒），超时后消失 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GroundProjection|Trail", meta = (ClampMin = "1.0"))
	float TrailLifetime = 30.0f;

	/** 淡出过渡时间（秒），在消失前逐渐变透明 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GroundProjection|Trail", meta = (ClampMin = "0.5"))
	float FadeOutDuration = 5.0f;

	// ---- 静态全局开关 ----

	/** 从 GConfig 读取全局开关状态（默认 true） */
	UFUNCTION(BlueprintCallable, Category = "GroundProjection")
	static bool IsGroundProjectionEnabled();

	/** 清除已记录的轨迹 */
	UFUNCTION(BlueprintCallable, Category = "GroundProjection")
	void ClearTrail();

private:
	/** 单个轨迹点：同时保存空中位置和地面投影位置 */
	struct FTrailPoint
	{
		FVector AirPosition;    // 无人机位置
		FVector GroundPosition; // 地面投影位置
		float Timestamp;        // 记录时的世界时间
	};

	/** 轨迹点队列 */
	TArray<FTrailPoint> TrailPoints;

	/** 程序化网格组件，用于渲染幕帘面 */
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> CurtainMesh;

	/** 动态材质实例 */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> CurtainMaterial;

	/** 缓存的开关状态 */
	bool bCachedEnabled = true;

	/** 帧计数器 */
	int32 FrameCounter = 0;

	/** 刷新间隔 */
	static constexpr int32 CacheRefreshInterval = 30;

	/** Mesh 是否需要重建 */
	bool bMeshDirty = false;

	/** 对地面做射线检测 */
	bool TraceToGround(const FVector& Start, FVector& OutHitPoint) const;

	/** 清除超时轨迹点 */
	void PruneExpiredPoints(float CurrentTime);

	/** 重建幕帘网格 */
	void RebuildCurtainMesh(float CurrentTime);

	/** 创建半透明材质 */
	void CreateCurtainMaterial();
};
