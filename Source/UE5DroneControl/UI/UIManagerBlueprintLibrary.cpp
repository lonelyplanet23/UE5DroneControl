// Copyright Epic Games, Inc. All Rights Reserved.

#include "UIManagerBlueprintLibrary.h"
#include "DroneListWidget.h"
#include "ToastWidget.h"
#include "AssemblyPopupWidget.h"
#include "SequenceDispatchPanelWidget.h"
#include "GeographicTargetPanelWidget.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

// 静态成员初始化
TWeakObjectPtr<UDroneListWidget> UUIManagerBlueprintLibrary::CurrentDroneList;
TWeakObjectPtr<UToastWidget> UUIManagerBlueprintLibrary::CurrentToast;
TWeakObjectPtr<UAssemblyPopupWidget> UUIManagerBlueprintLibrary::CurrentAssemblyPopup;
TWeakObjectPtr<USequenceDispatchPanelWidget> UUIManagerBlueprintLibrary::CurrentSequenceDispatchPanel;
TWeakObjectPtr<UGeographicTargetPanelWidget> UUIManagerBlueprintLibrary::CurrentGeographicTargetPanel;

TSubclassOf<UDroneListWidget> UUIManagerBlueprintLibrary::GetDroneListClass()
{
    UClass* Class = LoadClass<UDroneListWidget>(nullptr, TEXT("/Game/DroneOps/UI/WBP_DroneList.WBP_DroneList_C"));
    return Class ? Class : UDroneListWidget::StaticClass();
}

TSubclassOf<UToastWidget> UUIManagerBlueprintLibrary::GetToastClass()
{
    UClass* Class = LoadClass<UToastWidget>(nullptr, TEXT("/Game/DroneOps/UI/WBP_Toast.WBP_Toast_C"));
    return Class ? Class : UToastWidget::StaticClass();
}

TSubclassOf<UAssemblyPopupWidget> UUIManagerBlueprintLibrary::GetAssemblyPopupClass()
{
    UClass* Class = LoadClass<UAssemblyPopupWidget>(nullptr, TEXT("/Game/DroneOps/UI/WBP_AssemblyPopup.WBP_AssemblyPopup_C"));
    return Class ? Class : UAssemblyPopupWidget::StaticClass();
}

void UUIManagerBlueprintLibrary::ShowToast(UObject* WorldContextObject, const FString& Message, float Duration)
{
    UWorld* World = WorldContextObject->GetWorld();
    if (!World) return;

    // 关闭上一条还在显示的 Toast，避免叠加
    if (CurrentToast.IsValid())
    {
        CurrentToast->RemoveFromParent();
    }

    UToastWidget* Toast = CreateWidget<UToastWidget>(World, GetToastClass());
    if (Toast)
    {
        Toast->SetMessage(Message);
        Toast->AutoCloseDuration = Duration;
        Toast->AddToViewport();
        Toast->ShowAndAutoClose();
        CurrentToast = Toast;
        UE_LOG(LogTemp, Log, TEXT("UIManager: Toast shown - %s"), *Message);
    }
}

void UUIManagerBlueprintLibrary::ShowDroneList(UObject* WorldContextObject)
{
    UWorld* World = WorldContextObject->GetWorld();
    if (!World) return;

    if (CurrentDroneList.IsValid())
    {
        CurrentDroneList->SetVisibility(ESlateVisibility::Visible);
        UE_LOG(LogTemp, Log, TEXT("UIManager: DroneList already exists, shown"));
        return;
    }

    UDroneListWidget* DroneList = CreateWidget<UDroneListWidget>(World, GetDroneListClass());
    if (DroneList)
    {
        if (!DroneList->ListItemClass)
        {
            UClass* ItemClass = LoadClass<UDroneListItemWidget>(nullptr, TEXT("/Game/DroneOps/UI/WBP_DroneListItem.WBP_DroneListItem_C"));
            if (ItemClass)
            {
                DroneList->ListItemClass = ItemClass;
                UE_LOG(LogTemp, Log, TEXT("UIManager: Auto-set ListItemClass"));
            }
        }
        DroneList->AddToViewport();
        CurrentDroneList = DroneList;
        UE_LOG(LogTemp, Log, TEXT("UIManager: DroneList created and shown"));
    }
}

void UUIManagerBlueprintLibrary::HideDroneList(UObject* WorldContextObject)
{
    if (CurrentDroneList.IsValid())
    {
        CurrentDroneList->SetVisibility(ESlateVisibility::Hidden);
        UE_LOG(LogTemp, Log, TEXT("UIManager: DroneList hidden"));
    }
}

void UUIManagerBlueprintLibrary::ShowAssemblyDemo(UObject* WorldContextObject, int32 TotalCount, float StepInterval)
{
    UWorld* World = WorldContextObject->GetWorld();
    if (!World) return;

    UAssemblyPopupWidget* Popup = CreateWidget<UAssemblyPopupWidget>(World, GetAssemblyPopupClass());
    if (Popup)
    {
        Popup->TotalCount = TotalCount;
        Popup->AddToViewport();
        Popup->StartAutoDemo(StepInterval);
        CurrentAssemblyPopup = Popup;
        UE_LOG(LogTemp, Log, TEXT("UIManager: Assembly demo started, TotalCount=%d"), TotalCount);
    }
}

TSubclassOf<USequenceDispatchPanelWidget> UUIManagerBlueprintLibrary::GetSequenceDispatchPanelClass()
{
    UClass* Class = LoadClass<USequenceDispatchPanelWidget>(nullptr, TEXT("/Game/DroneOps/UI/WBP_SequenceDispatchPanel.WBP_SequenceDispatchPanel_C"));
    return Class ? Class : USequenceDispatchPanelWidget::StaticClass();
}

void UUIManagerBlueprintLibrary::ShowSequenceDispatchPanel(UObject* WorldContextObject)
{
    UWorld* World = WorldContextObject->GetWorld();
    if (!World) return;

    if (CurrentSequenceDispatchPanel.IsValid())
    {
        CurrentSequenceDispatchPanel->SetVisibility(ESlateVisibility::Visible);
        return;
    }

    USequenceDispatchPanelWidget* Panel = CreateWidget<USequenceDispatchPanelWidget>(World, GetSequenceDispatchPanelClass());
    if (Panel)
    {
        Panel->AddToViewport();
        CurrentSequenceDispatchPanel = Panel;
        UE_LOG(LogTemp, Log, TEXT("UIManager: SequenceDispatchPanel created"));
    }
}

void UUIManagerBlueprintLibrary::HideSequenceDispatchPanel(UObject* WorldContextObject)
{
    if (CurrentSequenceDispatchPanel.IsValid())
    {
        CurrentSequenceDispatchPanel->RemoveFromParent();
        CurrentSequenceDispatchPanel = nullptr;
    }
}

TSubclassOf<UGeographicTargetPanelWidget> UUIManagerBlueprintLibrary::GetGeographicTargetPanelClass()
{
    UClass* Class = LoadClass<UGeographicTargetPanelWidget>(nullptr, TEXT("/Game/DroneOps/UI/WBP_GeographicTargetPanel.WBP_GeographicTargetPanel_C"));
    return Class ? Class : UGeographicTargetPanelWidget::StaticClass();
}

void UUIManagerBlueprintLibrary::ShowGeographicTargetPanel(UObject* WorldContextObject)
{
    UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
    if (!World) return;

    if (CurrentGeographicTargetPanel.IsValid())
    {
        CurrentGeographicTargetPanel->SetVisibility(ESlateVisibility::Visible);
        return;
    }

    APlayerController* OwningPlayer = World->GetFirstPlayerController();
    UGeographicTargetPanelWidget* Panel = OwningPlayer
        ? CreateWidget<UGeographicTargetPanelWidget>(OwningPlayer, GetGeographicTargetPanelClass())
        : CreateWidget<UGeographicTargetPanelWidget>(World, GetGeographicTargetPanelClass());
    if (Panel)
    {
        Panel->AddToViewport();
        CurrentGeographicTargetPanel = Panel;
        UE_LOG(LogTemp, Log, TEXT("UIManager: GeographicTargetPanel created"));
    }
}

void UUIManagerBlueprintLibrary::HideGeographicTargetPanel(UObject* WorldContextObject)
{
    if (CurrentGeographicTargetPanel.IsValid())
    {
        CurrentGeographicTargetPanel->RemoveFromParent();
        CurrentGeographicTargetPanel = nullptr;
    }
}
