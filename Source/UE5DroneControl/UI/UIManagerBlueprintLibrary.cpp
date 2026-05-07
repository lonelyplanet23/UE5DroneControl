// Copyright Epic Games, Inc. All Rights Reserved.

#include "UIManagerBlueprintLibrary.h"
#include "DroneListWidget.h"
#include "ToastWidget.h"
#include "AssemblyPopupWidget.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

// 静态成员初始化
TWeakObjectPtr<UDroneListWidget> UUIManagerBlueprintLibrary::CurrentDroneList;
TWeakObjectPtr<UToastWidget> UUIManagerBlueprintLibrary::CurrentToast;
TWeakObjectPtr<UAssemblyPopupWidget> UUIManagerBlueprintLibrary::CurrentAssemblyPopup;

TSubclassOf<UDroneListWidget> UUIManagerBlueprintLibrary::GetDroneListClass()
{
    // 自动加载蓝图类（如果存在的话）
    static ConstructorHelpers::FClassFinder<UDroneListWidget> Finder(TEXT("/Game/DroneOps/UI/WBP_DroneList.WBP_DroneList_C"));
    if (Finder.Succeeded())
    {
        return TSubclassOf<UDroneListWidget>(Finder.Class);
    }
    return UDroneListWidget::StaticClass();
}

TSubclassOf<UToastWidget> UUIManagerBlueprintLibrary::GetToastClass()
{
    static ConstructorHelpers::FClassFinder<UToastWidget> Finder(TEXT("/Game/DroneOps/UI/WBP_Toast.WBP_Toast_C"));
    if (Finder.Succeeded())
    {
        return TSubclassOf<UToastWidget>(Finder.Class);
    }
    return UToastWidget::StaticClass();
}

TSubclassOf<UAssemblyPopupWidget> UUIManagerBlueprintLibrary::GetAssemblyPopupClass()
{
    static ConstructorHelpers::FClassFinder<UAssemblyPopupWidget> Finder(TEXT("/Game/DroneOps/UI/WBP_AssemblyPopup.WBP_AssemblyPopup_C"));
    if (Finder.Succeeded())
    {
        return TSubclassOf<UAssemblyPopupWidget>(Finder.Class);
    }
    return UAssemblyPopupWidget::StaticClass();
}

void UUIManagerBlueprintLibrary::ShowToast(UObject* WorldContextObject, const FString& Message, float Duration)
{
    UWorld* World = WorldContextObject->GetWorld();
    if (!World) return;

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
        // 如果蓝图类没设置ListItemClass，就设默认值
        if (!DroneList->ListItemClass)
        {
            static ConstructorHelpers::FClassFinder<UDroneListItemWidget> ItemFinder(TEXT("/Game/DroneOps/UI/WBP_DroneListItem.WBP_DroneListItem_C"));
            if (ItemFinder.Succeeded())
            {
                DroneList->ListItemClass = ItemFinder.Class;
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
