// Copyright Epic Games, Inc. All Rights Reserved.

#include "ToastWidget.h"
#include "Components/TextBlock.h"

void UToastWidget::SetMessage(const FString& Message)
{
    if (MessageText)
    {
        MessageText->SetText(FText::FromString(Message));
    }
}

void UToastWidget::ShowAndAutoClose()
{
    if (AutoCloseDuration > 0 && GetWorld())
    {
        GetWorld()->GetTimerManager().SetTimer(
            AutoCloseTimerHandle, this, &UToastWidget::OnAutoCloseTimer, AutoCloseDuration, false);
    }
}


void UToastWidget::NativeConstruct()
{
    Super::NativeConstruct();
}

void UToastWidget::OnAutoCloseTimer()
{
    RemoveFromParent();
}
