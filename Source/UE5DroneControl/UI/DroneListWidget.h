// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ScrollBox.h"
#include "DroneListItemWidget.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneListWidget.generated.h"

// 🌟 1. 在类外面定义文档要求的“真数据结构体”，打通蓝图读取属性的通道
USTRUCT(BlueprintType)
struct FDroneRegistrationViewData
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Drone Data")
    int32 Id = 0; //

    UPROPERTY(BlueprintReadOnly, Category = "Drone Data")
    FString IdStr; //

    UPROPERTY(BlueprintReadOnly, Category = "Drone Data")
    FString Name; //

    UPROPERTY(BlueprintReadOnly, Category = "Drone Data")
    FString Status;    // 后端返回的 online / connecting / lost / offline

    UPROPERTY(BlueprintReadOnly, Category = "Drone Data")
    int32 Battery = -1;  // 后端返回的电量

    UPROPERTY(BlueprintReadOnly, Category = "Drone Data")
    FVector WorldLocation = FVector::ZeroVector; // 物理坐标 x/y/z
};

/**
 * 无人机列表 C++ 基类
 * 蓝图只需继承，绑定 ScrollBox 即可
 */
UCLASS()
class UE5DRONECONTROL_API UDroneListWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    // 🌟 2. 新增蓝图事件：C++ 解析完后端 JSON 数组后，用这个事件把真数据直接扔给蓝图
    UFUNCTION(BlueprintImplementableEvent, Category = "UI|Drone")
    void OnDroneDataReceived(const TArray<FDroneRegistrationViewData>& Drones);

    /** 滚动框 - 蓝图绑定到 ScrollBox */ //
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional)) //
        UScrollBox* DroneScrollBox = nullptr; //

    /** 列表项 Widget 类 - 在蓝图中设为 WBP_DroneListItem */ //
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") //
        TSubclassOf<UDroneListItemWidget> ListItemClass; //

    /** 刷新间隔（秒） */ //
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI") //
        float RefreshInterval = 5.0f; //

    /** 添加无人机到列表 */ //
    UFUNCTION(BlueprintCallable, Category = "UI") //
        void AddDroneItem(const FString& Name, bool bOnline); //

    UFUNCTION(BlueprintCallable, Category = "UI")
        void RefreshFromRegistry();

    /** 清空列表 */ //
    UFUNCTION(BlueprintCallable, Category = "UI") //
        void ClearList(); //

    /** 演示数据：添加3架测试无人机 */ //
    UFUNCTION(BlueprintCallable, Category = "UI") //
        void AddDemoDrones(); //

protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override; //
    virtual void NativeDestruct() override; //

private:
    FTimerHandle RefreshTimerHandle; //

    void OnRefreshTimer(); //
    void BuildRuntimeWidgetTree();

    /** Refresh 按钮（BuildRuntimeWidgetTree 中创建） */
    UPROPERTY(meta = (BindWidgetOptional))
    class UButton* RefreshButton = nullptr;

    /** Refresh 操作状态文字（BuildRuntimeWidgetTree 中创建） */
    UPROPERTY(meta = (BindWidgetOptional))
    class UTextBlock* RefreshStatusText = nullptr;

    /** 严格本地预演模式：从 DroneNetworkManager 读取，true 时禁用 Refresh 按钮 */
    bool bStrictLocalPreview = false;

    /** 防止 Refresh 重复点击 */
    bool bRefreshing = false;

    /** 跨刷新持久化每架无人机的折叠状态（key = DroneId） */
    TMap<int32, bool> CollapseStates;

    /** Refresh 按钮点击回调 */
    UFUNCTION()
    void OnRefreshButtonClicked();

    /** 处理 POST /api/drones/refresh 响应 */
    void HandleRefreshResponse(bool bSuccess, const TArray<int32>& RefreshedIds);

    /** 隔离状态变化回调：Toggle 切换时同步禁用/启用 Refresh 按钮 */
    UFUNCTION()
    void OnIsolationStateChanged(bool bIsolated);

    /** 标签设置变更时实时同步面板名称 & 颜色（无需整列表重建） */
    UFUNCTION()
    void OnLabelSettingsChanged(int32 InDroneId, const FDroneLabelSettings& Settings);
};

