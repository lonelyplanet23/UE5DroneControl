// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneListItemWidget.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Engine/GameInstance.h"

void UDroneListItemWidget::SetDroneData(const FString& Name, bool bOnline)
{
    SetDroneDataWithAvailability(Name, bOnline ? EDroneAvailability::Online : EDroneAvailability::Offline);
}

void UDroneListItemWidget::SetDroneDataWithAvailability(const FString& Name, EDroneAvailability Availability)
{
    if (DroneNameText)
    {
        DroneNameText->SetText(FText::FromString(Name));
    }

    if (StatusText)
    {
        switch (Availability)
        {
        case EDroneAvailability::Online:
            StatusText->SetText(FText::FromString(TEXT("连接")));
            break;
        case EDroneAvailability::Lost:
            StatusText->SetText(FText::FromString(TEXT("失联")));
            break;
        case EDroneAvailability::Offline:
        default:
            StatusText->SetText(FText::FromString(TEXT("离线")));
            break;
        }
    }

    if (StatusIndicator)
    {
        switch (Availability)
        {
        case EDroneAvailability::Online:
            StatusIndicator->SetBrushColor(FLinearColor::Green);
            break;
        case EDroneAvailability::Lost:
            StatusIndicator->SetBrushColor(FLinearColor(1.0f, 0.45f, 0.0f, 1.0f));
            break;
        case EDroneAvailability::Offline:
        default:
            StatusIndicator->SetBrushColor(FLinearColor::Red);
            break;
        }
    }

    UpdateModeText();
}

void UDroneListItemWidget::SetDroneDataWithMode(int32 InDroneId, const FString& Name, bool bOnline, EDroneCommandMode InMode)
{
    DroneId = InDroneId;
    CurrentMode = InMode;
    SetDroneData(Name, bOnline);
}

void UDroneListItemWidget::SetDroneDataWithModeAndAvailability(int32 InDroneId, const FString& Name, EDroneAvailability Availability, EDroneCommandMode InMode)
{
    DroneId = InDroneId;
    CurrentMode = InMode;
    SetDroneDataWithAvailability(Name, Availability);
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
