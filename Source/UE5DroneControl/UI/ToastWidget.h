// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "ToastWidget.generated.h"

/**
 * Toast 弹窗 C++ 基类
 */
UCLASS()
class UE5DRONECONTROL_API UToastWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Toast 消息文字 - 蓝图绑定到 Text */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    UTextBlock* MessageText = nullptr;

    /** 自动关闭时间（秒），0 = 不自动关闭 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
    float AutoCloseDuration = 2.0f;

    /** 设置消息内容 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void SetMessage(const FString& Message);

    /** 显示并启动自动关闭计时器 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void ShowAndAutoClose();

protected:
    virtual void NativeConstruct() override;

private:
    FTimerHandle AutoCloseTimerHandle;

    void OnAutoCloseTimer();
};
