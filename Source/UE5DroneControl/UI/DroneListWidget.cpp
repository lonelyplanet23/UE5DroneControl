// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneListWidget.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "Components/ScrollBox.h"
#include "Engine/GameInstance.h"

void UDroneListWidget::AddDroneItem(const FString& Name, bool bOnline)
{
    if (!ListItemClass || !DroneScrollBox)
    {
        UE_LOG(LogTemp, Warning, TEXT("DroneListWidget: ListItemClass or ScrollBox not set!"));
        return;
    }

    UDroneListItemWidget* Item = CreateWidget<UDroneListItemWidget>(GetWorld(), ListItemClass);
    if (Item)
    {
        Item->SetDroneData(Name, bOnline);
        DroneScrollBox->AddChild(Item);
    }
}

void UDroneListWidget::ClearList()
{
    if (DroneScrollBox)
    {
        DroneScrollBox->ClearChildren();
    }
}

void UDroneListWidget::AddDemoDrones()
{
    ClearList();
    AddDroneItem(TEXT("无人机01"), true);
    AddDroneItem(TEXT("无人机02"), false);
    AddDroneItem(TEXT("无人机03"), true);
    UE_LOG(LogTemp, Log, TEXT("DroneListWidget: Added 3 demo drones"));
}

void UDroneListWidget::RefreshFromRegistry()
{
    UGameInstance* GI = GetGameInstance();
    UDroneRegistrySubsystem* Registry = GI ? GI->GetSubsystem<UDroneRegistrySubsystem>() : nullptr;
    if (!Registry)
    {
        return;
    }

    ClearList();

    TArray<FDroneRegistrationViewData> ViewData;
    TArray<FDroneDescriptor> Descriptors = Registry->GetAllDroneDescriptors();
    Descriptors.Sort([](const FDroneDescriptor& A, const FDroneDescriptor& B)
    {
        const int32 AOrder = A.Slot > 0 ? A.Slot : A.DroneId;
        const int32 BOrder = B.Slot > 0 ? B.Slot : B.DroneId;
        return AOrder < BOrder;
    });

    for (const FDroneDescriptor& Desc : Descriptors)
    {
        FDroneTelemetrySnapshot Snap;
        if (!Registry->GetTelemetry(Desc.DroneId, Snap))
        {
            // 无快照时构造默认值，避免UI空指针
            Snap.DroneId = Desc.DroneId;
            Snap.Availability = EDroneAvailability::Lost;
            Snap.BatteryPercent = -1;
            Snap.TaskState = EDroneTaskState::Standby;
            Snap.LastUpdateTime = FPlatformTime::Seconds();
        }

        if (ListItemClass && DroneScrollBox)
        {
            UDroneListItemWidget* Item = CreateWidget<UDroneListItemWidget>(GetWorld(), ListItemClass);
            if (Item)
            {
                // SetDroneFullState 覆盖：连接状态、GPS、海拔、电量、任务状态、航点、更新时间
                Item->SetDroneFullState(Desc.DroneId, Snap);
                // 名称单独设置（FDroneTelemetrySnapshot 不含 name 字段）
                if (Item->DroneNameText)
                {
                    Item->DroneNameText->SetText(FText::FromString(Desc.Name));
                }
                // 单独更新模式显示，不覆盖连接状态等字段
                Item->SetCommandMode(Desc.DroneId, Registry->GetDroneCommandMode(Desc.DroneId));
                DroneScrollBox->AddChild(Item);
            }
        }

        // ---- 构造视图数据（供蓝图事件使用） ----
        FDroneRegistrationViewData Data;
        Data.Id = Desc.DroneId;
        Data.IdStr = Desc.BackendIdString;
        Data.Name = Desc.BackendIdString;
        Data.Battery = Snap.BatteryPercent;
        Data.WorldLocation = Snap.WorldLocation;
        switch (Snap.Availability)
        {
        case EDroneAvailability::Online:
            Data.Status = TEXT("online");
            break;
        case EDroneAvailability::Lost:
            Data.Status = TEXT("lost");
            break;
        case EDroneAvailability::Offline:
        default:
            Data.Status = TEXT("offline");
            break;
        }
        ViewData.Add(Data);
    }

    OnDroneDataReceived(ViewData);
}

void UDroneListWidget::NativeConstruct()
{
    Super::NativeConstruct();

    RefreshFromRegistry();

    // 启动刷新定时器
    if (RefreshInterval > 0)
    {
        GetWorld()->GetTimerManager().SetTimer(RefreshTimerHandle, this, &UDroneListWidget::OnRefreshTimer, RefreshInterval, true);
    }
}

void UDroneListWidget::NativeDestruct()
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(RefreshTimerHandle);
    }
    Super::NativeDestruct();
}

void UDroneListWidget::OnRefreshTimer()
{
    RefreshFromRegistry();
}
