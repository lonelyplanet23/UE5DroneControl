// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "HostileTargetManager.generated.h"

class AHostileTargetActor;
class AMultiDroneCharacter;
class UDroneRegistrySubsystem;

/**
 * 敌对目标点管理器
 * 管理所有目标点的注册、发现检测和分配
 */
UCLASS()
class UE5DRONECONTROL_API UHostileTargetManager : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ----- 目标点管理 -----

	/** 注册目标点 */
	void RegisterTarget(AHostileTargetActor* Target);

	/** 注销目标点 */
	void UnregisterTarget(AHostileTargetActor* Target);
	void UnregisterTargetById(int32 TargetId);

	/** 获取目标点 */
	AHostileTargetActor* GetTarget(int32 TargetId) const;
	TArray<AHostileTargetActor*> GetAllTargets() const;

	/** 获取所有未发现的目标点 */
	TArray<AHostileTargetActor*> GetUndiscoveredTargets() const;

	/** 获取所有已发现的目标点 */
	TArray<AHostileTargetActor*> GetDiscoveredTargets() const;

	// ----- 发现检测 -----

	/**
	 * 检测指定无人机是否发现了任何目标点
	 * @param DroneId 无人机ID
	 * @param DroneLocation 无人机世界位置
	 * @param OutTarget 检测到的目标点（如果有）
	 * @param OutDistance 距离
	 * @return 是否发现目标
	 */
	bool CheckDroneDetection(int32 DroneId, const FVector& DroneLocation, AHostileTargetActor*& OutTarget, float& OutDistance) const;

	/**
	 * 检测所有巡逻无人机，触发发现事件（由PlayerController的Tick调用）
	 * 返回发生发现事件的 (DroneId, TargetId) 列表
	 */
	TArray<TPair<int32, int32>> CheckAllPatrollingDrones();

	// ----- 分配逻辑 -----

	/**
	 * 为目标点分配攻击无人机
	 * 规则：选择距离最近的巡逻无人机，距离相同时选DroneId较小的
	 * @param TargetId 目标点ID
	 * @param OutDroneId 分配的无人机ID（输出）
	 * @param OutDistance 距离（输出）
	 * @return 是否成功分配
	 */
	bool AssignDroneToTarget(int32 TargetId, int32& OutDroneId, float& OutDistance) const;

	/**
	 * 尝试分配目标（防止重复弹窗）
	 * @param TargetId 目标点ID
	 * @param DroneId 建议的无人机ID
	 * @return 是否分配成功（未被其他无人机占用）
	 */
	bool TryAssignTarget(int32 TargetId, int32 DroneId);

	/** 释放目标分配（用于"不攻击"或重置） */
	void UnassignTarget(int32 TargetId);

	/** 检查目标是否已被分配 */
	bool IsTargetAssigned(int32 TargetId) const;

	/** 获取目标当前的分配无人机 */
	int32 GetAssignedDroneId(int32 TargetId) const;

	// ----- 查询 -----

	/** 获取所有正在巡逻的在线无人机 */
	TArray<int32> GetPatrollingDrones() const;

	/** 获取无人机的影子机Actor */
	AMultiDroneCharacter* GetShadowDrone(int32 DroneId) const;

    /** 重置已离开检测范围的目标（需求4末尾：允许再次发现） */
	void ResetUndetectedTargets();
protected:
	UPROPERTY()
	TArray<TObjectPtr<AHostileTargetActor>> RegisteredTargets;

	// TargetId -> AssignedDroneId (防重复弹窗)
	TMap<int32, int32> AssignedMap;

	// 缓存 Registry 引用
	UPROPERTY()
	TObjectPtr<UDroneRegistrySubsystem> CachedRegistry;

	mutable FCriticalSection TargetsLock;

	void RefreshRegistry();
};