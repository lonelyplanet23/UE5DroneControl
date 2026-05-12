// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneListWidget.h"
#include "Components/ScrollBox.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

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

void UDroneListWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // 先添加演示数据
    AddDemoDrones();

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
    // 发送HTTP请求轮询
    FHttpModule& HttpModule = FHttpModule::Get();
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = HttpModule.CreateRequest();
    Request->SetURL(TEXT("http://localhost:8000/api/drones"));
    Request->SetVerb(TEXT("GET"));
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

    Request->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSuccess)
    {
        if (bSuccess && HttpResponse.IsValid() && HttpResponse->GetResponseCode() == 200)
        {
            FString Response = HttpResponse->GetContentAsString();
            UE_LOG(LogTemp, Log, TEXT("DroneListWidget: HTTP Success, Response: %s"), *Response);
            // TODO: 解析JSON，这里先用演示数据
            // AddDemoDrones(); // 有真实数据后替换
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("DroneListWidget: HTTP Failed, using demo data"));
            // HTTP失败继续用演示数据，不影响演示
        }
    });

    Request->ProcessRequest();
}
