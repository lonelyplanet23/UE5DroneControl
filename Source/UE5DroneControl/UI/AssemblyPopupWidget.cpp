// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssemblyPopupWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

void UAssemblyPopupWidget::UpdateProgress(int32 NewCount)
{
    CurrentCount = NewCount;
    if (ProgressText)
    {
        FString Text = FString::Printf(TEXT("已就位: %d/%d"), CurrentCount, TotalCount);
        ProgressText->SetText(FText::FromString(Text));
    }
}

void UAssemblyPopupWidget::StartAutoDemo(float StepInterval)
{
    DemoStep = 0;
    UpdateProgress(0);
    GetWorld()->GetTimerManager().SetTimer(DemoTimerHandle, this, &UAssemblyPopupWidget::OnDemoStep, StepInterval, true);
}

void UAssemblyPopupWidget::OnCloseClicked()
{
    RemoveFromParent();
}

void UAssemblyPopupWidget::NativeConstruct()
{
    Super::NativeConstruct();
}

void UAssemblyPopupWidget::OnDemoStep()
{
    DemoStep++;
    UpdateProgress(DemoStep);

    if (DemoStep >= TotalCount)
    {
        GetWorld()->GetTimerManager().ClearTimer(DemoTimerHandle);
        // 完成后1秒关闭
        FTimerHandle CloseTimer;
        GetWorld()->GetTimerManager().SetTimer(CloseTimer, [this]()
        {
            RemoveFromParent();
        }, 1.0f, false);
    }
}
