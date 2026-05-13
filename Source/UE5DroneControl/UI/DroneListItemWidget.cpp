// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneListItemWidget.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"

void UDroneListItemWidget::SetDroneData(const FString& Name, bool bOnline)
{
    if (DroneNameText)
    {
        DroneNameText->SetText(FText::FromString(Name));
    }

    if (StatusText)
    {
        StatusText->SetText(bOnline ? FText::FromString(TEXT("在线")) : FText::FromString(TEXT("离线")));
    }

    if (StatusIndicator)
    {
        StatusIndicator->SetBrushColor(bOnline ? FLinearColor::Green : FLinearColor::Red);
    }
}

void UDroneListItemWidget::NativeConstruct()
{
    Super::NativeConstruct();
}
