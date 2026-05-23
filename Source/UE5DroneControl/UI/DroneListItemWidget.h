// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DroneOps/Core/DroneOpsTypes.h"
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

    /** 当前移动模式 - 蓝图绑定到 Text */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    UTextBlock* ModeText = nullptr;

    /** 模式切换按钮 - 蓝图绑定到 Button */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    class UButton* ModeButton = nullptr;

    /** 设置无人机数据 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void SetDroneData(const FString& Name, bool bOnline);

    UFUNCTION(BlueprintCallable, Category = "UI")
    void SetDroneDataWithAvailability(const FString& Name, EDroneAvailability Availability);

    /** 设置真实无人机数据，并显示/切换当前指令模式 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void SetDroneDataWithMode(int32 InDroneId, const FString& Name, bool bOnline, EDroneCommandMode InMode);

    UFUNCTION(BlueprintCallable, Category = "UI")
    void SetDroneDataWithModeAndAvailability(int32 InDroneId, const FString& Name, EDroneAvailability Availability, EDroneCommandMode InMode);

protected:
    virtual void NativeConstruct() override;

private:
    int32 DroneId = 0;
    EDroneCommandMode CurrentMode = EDroneCommandMode::Move;

    UFUNCTION()
    void OnModeButtonClicked();

    void UpdateModeText();
};
