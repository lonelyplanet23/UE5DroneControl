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
    FHttpModule& HttpModule = FHttpModule::Get(); //
    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = HttpModule.CreateRequest(); //
    Request->SetURL(TEXT("http://127.0.0.1:8080/api/drones")); // 🌟 换成文档要求的正式后端BaseUrl
    Request->SetVerb(TEXT("GET")); //
    Request->SetHeader(TEXT("Content-Type"), TEXT("application/json")); //

    // 🌟 核心改动看这里：把原先的 [this] 改成 [this] 并且注意里面的变量名对应关系
    Request->OnProcessRequestComplete().BindLambda([this](FHttpRequestPtr HttpRequest, FHttpResponsePtr HttpResponse, bool bSuccess) //
        {
            // 🌟 检查一下你这里是不是误写成了 Request，必须和上面括号里的参数名 (HttpRequest / HttpResponse) 保持一致！
            if (bSuccess && HttpResponse.IsValid() && HttpResponse->GetResponseCode() == 200) //
            {
                FString ResponseStr = HttpResponse->GetContentAsString(); //

                // 开始解析顶层 JSON 数组
                TSharedPtr<FJsonValue> JsonValue;
                TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseStr);

                if (FJsonSerializer::Deserialize(Reader, JsonValue))
                {
                    TArray<FDroneRegistrationViewData> RealDronesArray;
                    const TArray<TSharedPtr<FJsonValue>> JsonArray = JsonValue->AsArray();

                    for (const auto& Element : JsonArray)
                    {
                        TSharedPtr<FJsonObject> Obj = Element->AsObject();
                        FDroneRegistrationViewData Data;

                        Data.Id = Obj->GetIntegerField(TEXT("id"));
                        Data.IdStr = Obj->GetStringField(TEXT("id_str"));
                        Data.Name = Obj->GetStringField(TEXT("name"));
                        Data.Status = Obj->GetStringField(TEXT("status"));
                        Data.Battery = Obj->GetIntegerField(TEXT("battery"));

                        double X = Obj->GetNumberField(TEXT("x"));
                        double Y = Obj->GetNumberField(TEXT("y"));
                        double Z = Obj->GetNumberField(TEXT("z"));
                        Data.WorldLocation = FVector(X, Y, Z);

                        RealDronesArray.Add(Data);
                    }

                    // 成功抛出事件给蓝图
                    OnDroneDataReceived(RealDronesArray);
                }
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("DroneListWidget: HTTP Failed")); //
            }
        });

    Request->ProcessRequest(); //
}