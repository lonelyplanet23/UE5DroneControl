// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ScrollBox.h"
#include "DroneListItemWidget.h"
#include "DroneListWidget.generated.h"

/**
 * 无人机列表 C++ 基类
 * 蓝图只需继承，绑定 ScrollBox 即可
 */
UCLASS()
class UE5DRONECONTROL_API UDroneListWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** 滚动框 - 蓝图绑定到 ScrollBox */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    UScrollBox* DroneScrollBox = nullptr;

    /** 列表项 Widget 类 - 在蓝图中设为 WBP_DroneListItem */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    TSubclassOf<UDroneListItemWidget> ListItemClass;

    /** 刷新间隔（秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    float RefreshInterval = 5.0f;

    /** 添加无人机到列表 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void AddDroneItem(const FString& Name, bool bOnline);

    /** 清空列表 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void ClearList();

    /** 演示数据：添加3架测试无人机 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void AddDemoDrones();

protected:
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;

private:
    FTimerHandle RefreshTimerHandle;

    void OnRefreshTimer();
};
