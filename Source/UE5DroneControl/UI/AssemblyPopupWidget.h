// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/TextBlock.h"
#include "AssemblyPopupWidget.generated.h"

/**
 * 集结进度弹窗 C++ 基类
 */
UCLASS()
class UE5DRONECONTROL_API UAssemblyPopupWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** 进度文字 - 蓝图绑定到 Text */
    UPROPERTY(BlueprintReadWrite, meta = (BindWidgetOptional))
    UTextBlock* ProgressText = nullptr;

    /** 当前数量 */
    UPROPERTY(BlueprintReadWrite, Category = "UI")
    int32 CurrentCount = 0;

    /** 总数 */
    UPROPERTY(BlueprintReadWrite, Category = "UI")
    int32 TotalCount = 3;

    /** 更新进度 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void UpdateProgress(int32 NewCount);

    /** 启动自动演示：0→1→2→3 然后自动关闭 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void StartAutoDemo(float StepInterval = 1.0f);

    /** 关闭按钮点击 */
    UFUNCTION(BlueprintCallable, Category = "UI")
    void OnCloseClicked();

protected:
    virtual void NativeConstruct() override;

private:
    FTimerHandle DemoTimerHandle;
    int32 DemoStep = 0;

    void OnDemoStep();
};
