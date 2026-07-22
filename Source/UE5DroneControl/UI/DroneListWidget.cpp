// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneListWidget.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "Components/ScrollBox.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetTree.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UDroneListWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    BuildRuntimeWidgetTree();
}

void UDroneListWidget::BuildRuntimeWidgetTree()
{
    if (!WidgetTree)
    {
        return;
    }

    // Replace the legacy WBP tree: it mixed absolute-positioned labels with C++ appended
    // controls, which is why the screenshot showed every row on top of each other.
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("DroneListRoot"));
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = Root;

    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("DroneListPanel"));
    Panel->SetBrushColor(FLinearColor(0.025f, 0.045f, 0.075f, 0.97f));
    Panel->SetPadding(FMargin(16.0f, 14.0f));
    const FString MapName = GetWorld() ? GetWorld()->GetMapName() : FString();
    const bool bIsMainMenu = MapName.Contains(TEXT("MainMenu"));
    const bool bIsCesiumWorld = MapName.Contains(TEXT("CesiumWorld"));
    if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
    {
        PanelSlot->SetAnchors(bIsMainMenu ? FAnchors(1.0f, 0.0f)
            : (bIsCesiumWorld ? FAnchors(0.0f, 1.0f) : FAnchors(0.0f, 0.0f)));
        PanelSlot->SetAlignment(bIsMainMenu ? FVector2D(1.0f, 0.0f)
            : (bIsCesiumWorld ? FVector2D(0.0f, 1.0f) : FVector2D::ZeroVector));
        PanelSlot->SetPosition(bIsMainMenu ? FVector2D(-32.0f, 112.0f)
            : (bIsCesiumWorld ? FVector2D(32.0f, -32.0f) : FVector2D(32.0f, 112.0f)));
        PanelSlot->SetSize(FVector2D(450.0f, 620.0f));
    }

    UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DroneListContent"));
    Panel->SetContent(Content);
    auto MakeText = [this](const FName Name, const FString& Value, const FLinearColor& Color)
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

    Add(Content, MakeText(TEXT("TitleText"), TEXT("无人机态势"), FLinearColor(0.94f, 0.98f, 1.0f, 1.0f)), FMargin(0.0f, 0.0f, 0.0f, 2.0f));
    Add(Content, MakeText(TEXT("SubtitleText"), TEXT("已注册无人机 · 实时状态同步"), FLinearColor(0.45f, 0.67f, 0.86f, 1.0f)), FMargin(0.0f, 0.0f, 0.0f, 6.0f));

    // ---- Refresh 按钮区（垂直排列：按钮在上，状态文字在下，文字自动换行避免溢出） ----
    UVerticalBox* RefreshSection = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(), TEXT("RefreshSection"));
    RefreshButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RefreshButton"));
    UTextBlock* RefreshLabel = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("RefreshLabel"));
    RefreshLabel->SetText(FText::FromString(TEXT("刷新连接")));
    RefreshLabel->SetColorAndOpacity(FLinearColor(0.90f, 0.95f, 1.0f, 1.0f));
    RefreshButton->AddChild(RefreshLabel);
    if (UVerticalBoxSlot* BtnSlot = RefreshSection->AddChildToVerticalBox(RefreshButton))
    {
        BtnSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
        BtnSlot->SetHorizontalAlignment(HAlign_Left);
    }
    RefreshStatusText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(), TEXT("RefreshStatusText"));
    RefreshStatusText->SetColorAndOpacity(FLinearColor(0.55f, 0.75f, 0.95f, 1.0f));
    RefreshStatusText->SetAutoWrapText(true);
    if (UVerticalBoxSlot* StatusSlot = RefreshSection->AddChildToVerticalBox(RefreshStatusText))
    {
        StatusSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 0.0f));
        StatusSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
    }
    Add(Content, RefreshSection, FMargin(0.0f, 0.0f, 0.0f, 8.0f));

    DroneScrollBox = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("DroneScrollBox"));
    DroneScrollBox->SetScrollBarVisibility(ESlateVisibility::Visible);
    if (UVerticalBoxSlot* ScrollSlot = Content->AddChildToVerticalBox(DroneScrollBox))
    {
        ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    }

    // Force the code-owned card class so an old WBP_DroneListItem cannot reintroduce overlap.
    ListItemClass = UDroneListItemWidget::StaticClass();
}

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

    // 销毁前保存每个条目的折叠状态，刷新后按 DroneId 还原
    if (DroneScrollBox)
    {
        for (UWidget* Child : DroneScrollBox->GetAllChildren())
        {
            if (UDroneListItemWidget* Item = Cast<UDroneListItemWidget>(Child))
            {
                CollapseStates.Add(Item->GetDroneId(), Item->IsCollapsed());
            }
        }
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

		// 展示所有已登记无人机；连接状态由单项控件独立显示为在线、离线或失联。

        if (ListItemClass && DroneScrollBox)
        {
            UDroneListItemWidget* Item = CreateWidget<UDroneListItemWidget>(GetWorld(), ListItemClass);
            if (Item)
            {
                // SetDroneFullState 覆盖：连接状态、GPS、海拔、电量、任务状态、航点、更新时间
                Item->SetDroneFullState(Desc.DroneId, Snap);
                // 名称 & 颜色：优先读用户已保存的标签设置，避免刷新后颜色恢复默认
                FDroneLabelSettings LabelSettings;
                if (Registry->GetDroneLabelSettings(Desc.DroneId, LabelSettings))
                {
                    Item->ApplyLabelSettings(LabelSettings);
                }
                else if (Item->DroneNameText)
                {
                    Item->DroneNameText->SetText(FText::FromString(Desc.Name));
                }
                // 单独更新模式显示，不覆盖连接状态等字段
				Item->SetCommandMode(Desc.DroneId, Snap.TaskMode);
                // 绑定 DroneId 并订阅多选变更委托，使选中按钮生效
                Item->SetDroneId(Desc.DroneId);
                // 还原折叠状态（跨刷新持久化）
                if (const bool* bSaved = CollapseStates.Find(Desc.DroneId))
                {
                    Item->SetCollapsed(*bSaved);
                }
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

    // ---- 读取严格本地预演模式 ----
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UDroneNetworkManager* NetMgr = GI->GetSubsystem<UDroneNetworkManager>())
        {
            bStrictLocalPreview = NetMgr->bStrictLocalPreview;
        }
    }

    // ---- 绑定 Refresh 按钮 ----
    if (RefreshButton)
    {
        if (bStrictLocalPreview)
        {
            RefreshButton->SetIsEnabled(false);
            if (RefreshStatusText)
            {
                RefreshStatusText->SetText(FText::FromString(TEXT("纯本地预演模式：已禁止请求后端 refresh")));
            }
        }
        else
        {
            RefreshButton->OnClicked.AddDynamic(this, &UDroneListWidget::OnRefreshButtonClicked);
        }
    }

    RefreshFromRegistry();

    // 启动刷新定时器
    if (RefreshInterval > 0)
    {
        GetWorld()->GetTimerManager().SetTimer(RefreshTimerHandle, this, &UDroneListWidget::OnRefreshTimer, RefreshInterval, true);
    }

    // 订阅标签设置变更，实时同步面板中的名称和颜色（避免全表重建）
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UDroneRegistrySubsystem* Registry = GI->GetSubsystem<UDroneRegistrySubsystem>())
        {
            Registry->OnDroneLabelSettingsChanged.AddDynamic(this, &UDroneListWidget::OnLabelSettingsChanged);
        }
    }
}

void UDroneListWidget::NativeDestruct()
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(RefreshTimerHandle);
    }
    if (UGameInstance* GI = GetGameInstance())
    {
        if (UDroneRegistrySubsystem* Registry = GI->GetSubsystem<UDroneRegistrySubsystem>())
        {
            Registry->OnDroneLabelSettingsChanged.RemoveAll(this);
        }
    }
    Super::NativeDestruct();
}

void UDroneListWidget::OnRefreshTimer()
{
    RefreshFromRegistry();
}

void UDroneListWidget::OnRefreshButtonClicked()
{
    if (bStrictLocalPreview)
    {
        if (RefreshStatusText)
        {
            RefreshStatusText->SetText(FText::FromString(TEXT("严格本地预演模式：无法请求刷新")));
        }
        return;
    }

    if (bRefreshing)
    {
        return;
    }

    bRefreshing = true;
    if (RefreshButton)
    {
        RefreshButton->SetIsEnabled(false);
    }
    if (RefreshStatusText)
    {
        RefreshStatusText->SetText(FText::FromString(TEXT("正在探测断连无人机...")));
    }

    UDroneNetworkManager* NetMgr = GetGameInstance()
        ? GetGameInstance()->GetSubsystem<UDroneNetworkManager>()
        : nullptr;
    if (!NetMgr)
    {
        HandleRefreshResponse(false, {});
        return;
    }

    NetMgr->RefreshDroneConnections([this](bool bSuccess, const TArray<int32>& RefreshedIds)
    {
        HandleRefreshResponse(bSuccess, RefreshedIds);
    });
}

void UDroneListWidget::HandleRefreshResponse(bool bSuccess, const TArray<int32>& RefreshedIds)
{
    bRefreshing = false;
    if (RefreshButton)
    {
        RefreshButton->SetIsEnabled(true);
    }

    FString Message;
    if (!bSuccess)
    {
        Message = TEXT("刷新请求失败，请检查网络或后端状态");
    }
    else if (RefreshedIds.Num() == 0)
    {
        Message = TEXT("当前无断连无人机需要探测");
    }
    else
    {
        Message = FString::Printf(TEXT("已请求探测 %d 架断连无人机"), RefreshedIds.Num());
    }

    if (RefreshStatusText)
    {
        RefreshStatusText->SetText(FText::FromString(Message));
    }

    // 立即刷新一次列表，展示注册表最新状态（不会将 refreshed_drone_ids 直接置为 online）
    RefreshFromRegistry();
}

void UDroneListWidget::OnLabelSettingsChanged(int32 InDroneId, const FDroneLabelSettings& Settings)
{
    // 找到对应条目，直接刷新其名称和颜色，避免重建整个列表
    if (!DroneScrollBox)
    {
        return;
    }
    for (UWidget* Child : DroneScrollBox->GetAllChildren())
    {
        if (UDroneListItemWidget* Item = Cast<UDroneListItemWidget>(Child))
        {
            if (Item->GetDroneId() == InDroneId)
            {
                Item->ApplyLabelSettings(Settings);
                break;
            }
        }
    }
}
