// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "DroneListItemWidget.generated.h"

/**
 * 无人机列表项 C++ 基类
 * 蓝图只需继承此类，绑定 UI 变量即可，无需写逻辑
 */
UCLASS()
class UE5DRONECONTROL_API UDroneListItemWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** 无人机名称 - 蓝图绑定到 Text */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    UTextBlock* DroneNameText = nullptr;

    /** 状态颜色 - 蓝图绑定到 Border 的 Color And Opacity */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    UBorder* StatusIndicator = nullptr;

    /** 状态文字 - 蓝图绑定到 Text */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    UTextBlock* StatusText = nullptr;

    /** 设置无人机数据 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void SetDroneData(const FString& Name, bool bOnline);

protected:
    virtual void NativeConstruct() override;
};
