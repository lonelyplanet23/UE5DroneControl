// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneListItemWidget.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Engine/GameInstance.h"

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

    UpdateModeText();
}

void UDroneListItemWidget::SetDroneDataWithMode(int32 InDroneId, const FString& Name, bool bOnline, EDroneCommandMode InMode)
{
    DroneId = InDroneId;
    CurrentMode = InMode;
    SetDroneData(Name, bOnline);
}

void UDroneListItemWidget::NativeConstruct()
{
    Super::NativeConstruct();

    if (ModeButton)
    {
        ModeButton->OnClicked.AddDynamic(this, &UDroneListItemWidget::OnModeButtonClicked);
    }
    UpdateModeText();
}

void UDroneListItemWidget::OnModeButtonClicked()
{
    UGameInstance* GI = GetGameInstance();
    UDroneRegistrySubsystem* Registry = GI ? GI->GetSubsystem<UDroneRegistrySubsystem>() : nullptr;

    if (Registry && DroneId > 0)
    {
        Registry->CycleDroneCommandMode(DroneId);
        CurrentMode = Registry->GetDroneCommandMode(DroneId);
    }
    else
    {
        switch (CurrentMode)
        {
        case EDroneCommandMode::Scout:
            CurrentMode = EDroneCommandMode::Patrol;
            break;
        case EDroneCommandMode::Patrol:
            CurrentMode = EDroneCommandMode::Attack;
            break;
        case EDroneCommandMode::Attack:
            CurrentMode = EDroneCommandMode::Scout;
            break;
        case EDroneCommandMode::Move:
        default:
            CurrentMode = EDroneCommandMode::Scout;
            break;
        }
    }

    UpdateModeText();
}

void UDroneListItemWidget::UpdateModeText()
{
    if (ModeText)
    {
        ModeText->SetText(DroneCommandModeToDisplayText(CurrentMode));
    }
}
