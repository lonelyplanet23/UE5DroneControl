// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneListItemWidget.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Blueprint/WidgetTree.h"
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

void UDroneListItemWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    BuildRuntimeCard();
}

void UDroneListItemWidget::NativeConstruct()
{
    Super::NativeConstruct();
    UpdateModeText();
}

void UDroneListItemWidget::BuildRuntimeCard()
{
    if (WidgetTree)
    {
        UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DroneCard"));
        Card->SetBrushColor(FLinearColor(0.045f, 0.075f, 0.115f, 0.96f));
        Card->SetPadding(FMargin(12.0f, 10.0f));
        WidgetTree->RootWidget = Card;

        UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CardContent"));
        Card->SetContent(Content);
        auto MakeText = [this](const FName Name, const FString& Value, const FLinearColor& Color = FLinearColor(0.86f, 0.91f, 0.97f, 1.0f))
        {
            UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
            Text->SetText(FText::FromString(Value));
            Text->SetColorAndOpacity(Color);
            return Text;
        };
        auto Add = [](UVerticalBox* Box, UWidget* Child, const FMargin& ChildPadding = FMargin(0.0f, 3.0f))
        {
            if (UVerticalBoxSlot* ChildSlot = Box->AddChildToVerticalBox(Child)) ChildSlot->SetPadding(ChildPadding);
        };

        UHorizontalBox* Header = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("HeaderRow"));
        DroneNameText = MakeText(TEXT("DroneNameText"), TEXT("无人机"), FLinearColor(0.96f, 0.98f, 1.0f, 1.0f));
        if (UHorizontalBoxSlot* ChildSlot = Header->AddChildToHorizontalBox(DroneNameText)) ChildSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        ConnectionStatusText = MakeText(TEXT("ConnectionStatusText"), TEXT("离线"), FLinearColor(0.95f, 0.38f, 0.34f, 1.0f));
        Header->AddChildToHorizontalBox(ConnectionStatusText);
        StatusText = ConnectionStatusText;
        Add(Content, Header, FMargin(0.0f, 0.0f, 0.0f, 5.0f));

        UHorizontalBox* TaskRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TaskRow"));
        TaskRow->AddChildToHorizontalBox(MakeText(TEXT("ModeLabel"), TEXT("模式  "), FLinearColor(0.48f, 0.67f, 0.84f, 1.0f)));
        ModeText = MakeText(TEXT("ModeText"), TEXT("移动"));
        if (UHorizontalBoxSlot* ChildSlot = TaskRow->AddChildToHorizontalBox(ModeText)) ChildSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        TaskRow->AddChildToHorizontalBox(MakeText(TEXT("TaskLabel"), TEXT("任务  "), FLinearColor(0.48f, 0.67f, 0.84f, 1.0f)));
        TaskStatusText = MakeText(TEXT("TaskStatusText"), TEXT("待命"));
        if (UHorizontalBoxSlot* ChildSlot = TaskRow->AddChildToHorizontalBox(TaskStatusText)) ChildSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        Add(Content, TaskRow);

        StatusSourceText = MakeText(TEXT("StatusSourceText"), TEXT("状态来源：后端同步"), FLinearColor(0.48f, 0.67f, 0.84f, 1.0f));
        Add(Content, StatusSourceText, FMargin(0.0f, 1.0f, 0.0f, 5.0f));
        GpsText = MakeText(TEXT("GpsText"), TEXT("经度 --    纬度 --"));
        Add(Content, GpsText);

        UHorizontalBox* MetricsRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("MetricsRow"));
        AltitudeText = MakeText(TEXT("AltitudeText"), TEXT("海拔 -- m"));
        if (UHorizontalBoxSlot* ChildSlot = MetricsRow->AddChildToHorizontalBox(AltitudeText)) ChildSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        BatteryText = MakeText(TEXT("BatteryText"), TEXT("电量 --"));
        MetricsRow->AddChildToHorizontalBox(BatteryText);
        Add(Content, MetricsRow);

        UHorizontalBox* ProgressRow = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("ProgressRow"));
        WaypointText = MakeText(TEXT("WaypointText"), TEXT("航点 0 / 0"));
        if (UHorizontalBoxSlot* ChildSlot = ProgressRow->AddChildToHorizontalBox(WaypointText)) ChildSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        UpdateTimeText = MakeText(TEXT("UpdateTimeText"), TEXT("刚刚"), FLinearColor(0.55f, 0.62f, 0.70f, 1.0f));
        ProgressRow->AddChildToHorizontalBox(UpdateTimeText);
        Add(Content, ProgressRow);

        TargetIndexText = MakeText(TEXT("TargetIndexText"), TEXT(""));
        TargetIndexText->SetVisibility(ESlateVisibility::Collapsed);
        Content->AddChildToVerticalBox(TargetIndexText);
        StatusIndicator = nullptr;
        ModeButton = nullptr;
        return;
    }

    // 如果所有新增字段均已由蓝图绑定，无需任何创建
    if (ConnectionStatusText && TaskStatusText && GpsText &&
        AltitudeText && BatteryText && WaypointText &&
        TargetIndexText && UpdateTimeText)
    {
        return;
    }

    if (!WidgetTree)
    {
        return;
    }

    // 找到最顶层的 Panel 容器用于挂载新控件
    // 优先复用蓝图中已有的 VerticalBox；找不到则直接挂到根
    UPanelWidget* Container = nullptr;
    if (UWidget* Root = WidgetTree->RootWidget)
    {
        Container = Cast<UPanelWidget>(Root);
        if (!Container)
        {
            // 根不是 Panel（例如是 Border），向下找第一个 VerticalBox
            WidgetTree->ForEachWidget([&](UWidget* W)
            {
                if (!Container)
                {
                    Container = Cast<UVerticalBox>(W);
                }
            });
        }
    }

    // 创建缺失的 TextBlock 辅助 lambda
    auto MakeText = [this](UTextBlock*& Ptr, const FString& Label, FLinearColor Color = FLinearColor::White)
    {
        if (Ptr)
        {
            return; // 已由蓝图绑定，不覆盖
        }
        Ptr = WidgetTree->ConstructWidget<UTextBlock>();
        if (Ptr)
        {
            Ptr->SetText(FText::FromString(Label));
            Ptr->SetColorAndOpacity(Color);
        }
    };

    MakeText(ConnectionStatusText, TEXT("连接: --"));
    MakeText(TaskStatusText,       TEXT("任务: --"));
    MakeText(GpsText,              TEXT("经度: --  纬度: --"));
    MakeText(AltitudeText,         TEXT("海拔: -- m"));
    MakeText(BatteryText,          TEXT("电量: --"));
    MakeText(WaypointText,         TEXT("航点: 0 / 0"));
    MakeText(TargetIndexText,      TEXT("目标点: 0"));
    MakeText(UpdateTimeText,       TEXT("-- s 前"));

    // 将新建的控件追加到容器
    if (Container)
    {
        auto TryAdd = [&](UWidget* W)
        {
            if (W && !W->GetParent())
            {
                Container->AddChild(W);
            }
        };
        TryAdd(ConnectionStatusText);
        TryAdd(TaskStatusText);
        TryAdd(GpsText);
        TryAdd(AltitudeText);
        TryAdd(BatteryText);
        TryAdd(WaypointText);
        TryAdd(TargetIndexText);
        TryAdd(UpdateTimeText);
    }
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

void UDroneListItemWidget::SetCommandMode(int32 InDroneId, EDroneCommandMode InMode)
{
    DroneId = InDroneId;
    CurrentMode = InMode;
    UpdateModeText();
}

// 完整状态设置方法
void UDroneListItemWidget::SetDroneFullState(int32 InDroneId, const FDroneTelemetrySnapshot& Snap)
{
    DroneId = InDroneId;

    // ---- 连接状态独立显示 ----
    // 对应 EDroneAvailability：Online / Lost / Offline
    // 同时写入 ConnectionStatusText（新字段）和 StatusText（Blueprint 实际绑定的旧字段）
    {
        FString ConnText;
        FLinearColor Color;
        switch (Snap.Availability)
        {
        case EDroneAvailability::Online:
            ConnText = TEXT("在线");
            Color = FLinearColor::Green;
            break;
        case EDroneAvailability::Lost:
            ConnText = TEXT("失联");
            Color = FLinearColor(1.0f, 0.45f, 0.0f, 1.0f);  // 橙色
            break;
        case EDroneAvailability::Offline:
        default:
            ConnText = TEXT("离线");
            Color = FLinearColor::Red;
            break;
        }
        if (ConnectionStatusText)
        {
            ConnectionStatusText->SetText(FText::FromString(ConnText));
            ConnectionStatusText->SetColorAndOpacity(Color);
        }
        if (StatusText)
        {
            StatusText->SetText(FText::FromString(ConnText));
            StatusText->SetColorAndOpacity(Color);
        }
        if (StatusIndicator)
        {
            StatusIndicator->SetBrushColor(Color);
        }
    }

    // ---- 经纬度 ----
    // 数据来源：DroneNetworkManager 从 WS telemetry/event 解析后写入 Registry
    if (GpsText)
    {
        GpsText->SetText(FText::FromString(FString::Printf(
            TEXT("经度: %.6f  纬度: %.6f"), Snap.GpsLongitude, Snap.GpsLatitude)));
    }

    // ---- 海拔 ----
    // 单位：米（WGS84椭球高）
    if (AltitudeText)
    {
        AltitudeText->SetText(FText::FromString(FString::Printf(
            TEXT("海拔: %.1f m"), Snap.GpsAltitude)));
    }

    // ---- 电量 ----
    // -1 表示数据不可用（后端未推送或未知）
    if (BatteryText)
    {
        FString BatText;
        if (Snap.BatteryPercent < 0)
        {
            BatText = TEXT("未知");
        }
        else
        {
            BatText = FString::Printf(TEXT("%d%%"), Snap.BatteryPercent);
        }
        BatteryText->SetText(FText::FromString(BatText));
    }

    // ---- 任务状态 ----
    if (TaskStatusText)
    {
        FString DisplayText;
        FLinearColor Color = FLinearColor::White;

        // ===== UE-only 本地状态优先覆盖 =====
        // 这些状态只存在于 UE，不写入后端协议，不调用 Registry->UpdateTaskState
        // 对应需求4：TargetDetectedPending / LocalAttacking / LocalAttackCompleted / TargetDeclined
        if (Snap.LocalState == EUELocalDroneState::LocalAttacking)
        {
            DisplayText = TEXT("攻击中（UE预演）");
            Color = FLinearColor(1.0f, 0.5f, 0.0f);  // 橙色
        }
        else if (Snap.LocalState == EUELocalDroneState::TargetDetectedPending)
        {
            DisplayText = TEXT("目标确认待命");
            Color = FLinearColor::Yellow;
        }
        else if (Snap.LocalState == EUELocalDroneState::LocalAttackCompleted)
        {
            DisplayText = TEXT("UE攻击完成");
            Color = FLinearColor::Gray;
        }
        else if (Snap.LocalState == EUELocalDroneState::TargetDeclined)
        {
            DisplayText = TEXT("已拒绝攻击");
            Color = FLinearColor::Gray;
        }
        else
        {
            // ===== 后端状态 =====
            // 数据来源：后端通过 drone_task_state WS 推送，或 GET /api/drones 初次加载
            switch (Snap.TaskState)
            {
            case EDroneTaskState::Standby:
                DisplayText = TEXT("待命");
                Color = FLinearColor::Gray;
                break;
            case EDroneTaskState::Assembling:
                DisplayText = TEXT("集结中");
                Color = FLinearColor(0.0f, 0.7f, 1.0f);  // 亮蓝
                break;
            case EDroneTaskState::Moving:
                DisplayText = TEXT("移动中");
                Color = FLinearColor::Yellow;
                break;
            case EDroneTaskState::Scouting:
                DisplayText = TEXT("侦察中");
                Color = FLinearColor(0.0f, 1.0f, 0.5f);  // 青绿
                break;
            case EDroneTaskState::Patrolling:
                DisplayText = TEXT("巡逻中");
                Color = FLinearColor(0.0f, 0.8f, 0.0f);  // 绿色
                break;
            case EDroneTaskState::Attacking:
                DisplayText = TEXT("攻击中");
                Color = FLinearColor::Red;
                break;
            case EDroneTaskState::Paused:
                DisplayText = TEXT("暂停");
                Color = FLinearColor::Yellow;
                break;
            case EDroneTaskState::Avoiding:
                DisplayText = TEXT("避障中");
                Color = FLinearColor(1.0f, 0.8f, 0.0f);  // 黄橙色
                break;
            case EDroneTaskState::Completed:
                DisplayText = TEXT("已完成");
                Color = FLinearColor::Gray;
                break;
            case EDroneTaskState::Error:
                // 显示 detail 字段中的错误原因
                DisplayText = Snap.TaskErrorDetail.IsEmpty() 
                    ? TEXT("异常") 
                    : FString::Printf(TEXT("异常: %s"), *Snap.TaskErrorDetail);
                Color = FLinearColor::Red;
                break;
            default:
                DisplayText = TEXT("未知");
                Color = FLinearColor::Gray;
                break;
            }
        }

        TaskStatusText->SetText(FText::FromString(DisplayText));
        TaskStatusText->SetColorAndOpacity(Color);
    }

    if (StatusSourceText)
    {
        const bool bUeLocal = Snap.LocalState != EUELocalDroneState::None;
        StatusSourceText->SetText(FText::FromString(
            bUeLocal ? TEXT("状态来源：UE 本地预演") : TEXT("状态来源：后端同步")));
        StatusSourceText->SetColorAndOpacity(bUeLocal
            ? FLinearColor(1.0f, 0.66f, 0.18f, 1.0f)
            : FLinearColor(0.48f, 0.67f, 0.84f, 1.0f));
    }

    // ---- 当前路径点 / 当前目标点编号 ----
    // CurrentWaypointIndex：当前正在飞往的航点索引（从0开始）
    // TotalWaypoints：总航点数
    // 显示为 "航点: 3 / 10" 格式
    // ---- 航点进度（总览） ----
    // 显示 "航点: 3 / 10"，告知整体进度
    if (WaypointText)
    {
        WaypointText->SetText(FText::FromString(FString::Printf(
            TEXT("航点: %d / %d"), Snap.CurrentWaypointIndex, Snap.TotalWaypoints)));
    }

    // ---- 当前目标点编号（独立显示） ----
    // 与 WaypointText 分开，单独显示正在飞往的航点序号
    if (TargetIndexText)
    {
        TargetIndexText->SetText(FText::FromString(FString::Printf(
            TEXT("目标点: %d"), Snap.CurrentWaypointIndex)));
    }

    // ---- 最近一次状态更新时间（格式化为 xx.xs 前） ----
    // LastUpdateTime 使用 FPlatformTime::Seconds()（系统启动后的秒数）
    if (UpdateTimeText)
    {
        const double Now = FPlatformTime::Seconds();
        const double Delta = Now - Snap.LastUpdateTime;
        FString TimeStr;
        if (Delta < 0)
        {
            TimeStr = TEXT("0.0s 前");
        }
        else if (Delta < 60)
        {
            TimeStr = FString::Printf(TEXT("%.1fs 前"), Delta);
        }
        else
        {
            TimeStr = FString::Printf(TEXT("%.0fm 前"), Delta / 60.0);
        }
        UpdateTimeText->SetText(FText::FromString(TimeStr));
    }
}
