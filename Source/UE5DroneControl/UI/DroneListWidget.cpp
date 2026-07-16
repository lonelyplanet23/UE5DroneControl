// Copyright Epic Games, Inc. All Rights Reserved.

#include "DroneListWidget.h"
#include "DroneOps/Core/DroneRegistrySubsystem.h"
#include "Components/ScrollBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
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
    Add(Content, MakeText(TEXT("SubtitleText"), TEXT("已注册无人机 · 实时状态同步"), FLinearColor(0.45f, 0.67f, 0.86f, 1.0f)), FMargin(0.0f, 0.0f, 0.0f, 10.0f));

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
                // 名称单独设置（FDroneTelemetrySnapshot 不含 name 字段）
                if (Item->DroneNameText)
                {
                    Item->DroneNameText->SetText(FText::FromString(Desc.Name));
                }
                // 单独更新模式显示，不覆盖连接状态等字段
				Item->SetCommandMode(Desc.DroneId, Snap.TaskMode);
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
