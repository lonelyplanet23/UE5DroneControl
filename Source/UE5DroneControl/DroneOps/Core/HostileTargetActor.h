// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HostileTargetActor.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/**
 * 敌对目标点 Actor
 * 用户可在场景中放置，静止不动，有唯一编号
 */
UCLASS(BlueprintType, Blueprintable)
class UE5DRONECONTROL_API AHostileTargetActor : public AActor
{
	GENERATED_BODY()

public:
	AHostileTargetActor();

	// ----- 核心属性 -----

	/** 唯一编号（自动分配或手动指定） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HostileTarget")
	int32 TargetId = 0;

	/** 发现半径（厘米） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HostileTarget", meta = (ClampMin = "100.0", UIMin = "100.0"))
	float DiscoveryRadius = 800.0f;

	/** 是否已被发现（任何无人机发现过） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HostileTarget")
	bool bIsDiscovered = false;

	/** 已分配去攻击的无人机ID（0表示未分配） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HostileTarget")
	int32 AssignedDroneId = 0;

	/** 是否已分配（弹窗已弹出） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HostileTarget")
	bool bIsAssigned = false;

	// ----- 组件 -----

	/** 根组件 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> RootScene;

	/** 可视网格（标记目标位置） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	/** 发现半径可视化（仅在编辑器中可见） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> DiscoverySphere;

	/** 编号标签 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UTextRenderComponent> LabelComponent;

	// ----- 方法 -----

	/** 获取目标位置（世界坐标） */
	UFUNCTION(BlueprintPure, Category = "HostileTarget")
	FVector GetHostileTargetLocation() const;

	/** 标记为已发现 */
	UFUNCTION(BlueprintCallable, Category = "HostileTarget")
	void MarkDiscovered(int32 DroneId);

	/** 重置目标状态（允许重新发现） */
	UFUNCTION(BlueprintCallable, Category = "HostileTarget")
	void ResetTarget();

	/** 生成唯一ID（自动分配） */
	UFUNCTION(BlueprintCallable, Category = "HostileTarget")
	static int32 GenerateUniqueTargetId(UWorld* World);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLoad() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void UpdateVisuals();
	void UpdateLabel();

	static int32 NextTargetId;
};