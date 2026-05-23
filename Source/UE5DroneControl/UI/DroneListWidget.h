// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ScrollBox.h" //
#include "DroneListItemWidget.h" //
#include "DroneListWidget.generated.h" //

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
    virtual void NativeConstruct() override; //
    virtual void NativeDestruct() override; //

private:
    FTimerHandle RefreshTimerHandle; //

    void OnRefreshTimer(); //
};

