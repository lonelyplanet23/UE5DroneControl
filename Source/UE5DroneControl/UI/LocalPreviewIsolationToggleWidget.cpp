#include "LocalPreviewIsolationToggleWidget.h"
#include "DroneOps/Network/DroneNetworkManager.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"

void ULocalPreviewIsolationToggleWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void ULocalPreviewIsolationToggleWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	// ---- 根 CanvasPanel ----
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("IsolationToggleRoot"));
	Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = Root;

	// ---- 外层容器（右上角，不被普通面板遮挡）----
	UVerticalBox* Container = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("IsolationContainer"));
	if (UCanvasPanelSlot* ContainerSlot = Root->AddChildToCanvas(Container))
	{
		// 锚点：左下角，放在无人机态势面板(DroneListWidget)正上方。
		// 原本在右上角会盖住派发面板的“编辑模式”开关；左上角是视频流，故移到左下。
		// 态势面板在 CesiumWorld 为左下锚点、距底 32、高 620，故本控件底边取 32+620+12=664 上移，留 12px 间隙。
		ContainerSlot->SetAnchors(FAnchors(0.0f, 1.0f));
		ContainerSlot->SetAlignment(FVector2D(0.0f, 1.0f));
		ContainerSlot->SetPosition(FVector2D(32.0f, -664.0f));
		ContainerSlot->SetAutoSize(true);
	}

	// ---- Toggle 行：CheckBox + 标签 ----
	UBorder* ToggleBg = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("ToggleBg"));
	ToggleBg->SetBrushColor(FLinearColor(0.08f, 0.08f, 0.12f, 0.92f));
	ToggleBg->SetPadding(FMargin(12.0f, 8.0f));
	if (UVerticalBoxSlot* BgSlot = Container->AddChildToVerticalBox(ToggleBg))
	{
		BgSlot->SetPadding(FMargin(0.0f));
	}

	UHorizontalBox* ToggleRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("ToggleRow"));
	ToggleBg->SetContent(ToggleRow);

	IsolationCheckBox = WidgetTree->ConstructWidget<UCheckBox>(
		UCheckBox::StaticClass(), TEXT("IsolationCheckBox"));
	if (UHorizontalBoxSlot* CbSlot = ToggleRow->AddChildToHorizontalBox(IsolationCheckBox))
	{
		CbSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		CbSlot->SetVerticalAlignment(VAlign_Center);
	}

	LabelText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("IsolationLabel"));
	LabelText->SetText(FText::FromString(TEXT("\u7EAF\u672C\u5730\u9884\u6F14\uFF08\u9694\u79BB\u540E\u7AEF\uFF09")));
	// "纯本地预演（隔离后端）"
	LabelText->SetColorAndOpacity(FLinearColor(0.95f, 0.95f, 0.95f, 1.0f));
	FSlateFontInfo Font = LabelText->GetFont();
	Font.Size = 14;
	LabelText->SetFont(Font);
	if (UHorizontalBoxSlot* LblSlot = ToggleRow->AddChildToHorizontalBox(LabelText))
	{
		LblSlot->SetVerticalAlignment(VAlign_Center);
	}

	// ---- 状态横幅（隔离激活时显示，醒目红色）----
	StatusBanner = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("StatusBanner"));
	StatusBanner->SetBrushColor(FLinearColor(0.85f, 0.12f, 0.12f, 0.95f));
	StatusBanner->SetPadding(FMargin(12.0f, 6.0f));
	StatusBanner->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* BannerSlot = Container->AddChildToVerticalBox(StatusBanner))
	{
		BannerSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
	}

	BannerText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(), TEXT("BannerText"));
	// "⚠ 后端通信已隔离 — 所有出站请求已阻止"
	BannerText->SetText(FText::FromString(
		TEXT("\u26A0 \u540E\u7AEF\u901A\u4FE1\u5DF2\u9694\u79BB \u2014 \u6240\u6709\u51FA\u7AD9\u8BF7\u6C42\u5DF2\u963B\u6B62")));
	BannerText->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 1.0f));
	FSlateFontInfo BannerFont = BannerText->GetFont();
	BannerFont.Size = 13;
	BannerText->SetFont(BannerFont);
	StatusBanner->SetContent(BannerText);
}

void ULocalPreviewIsolationToggleWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 获取 DroneNetworkManager
	if (UGameInstance* GI = GetGameInstance())
	{
		CachedNetworkManager = GI->GetSubsystem<UDroneNetworkManager>();
	}

	// 绑定 CheckBox 回调
	if (IsolationCheckBox)
	{
		IsolationCheckBox->OnCheckStateChanged.AddDynamic(this, &ULocalPreviewIsolationToggleWidget::OnToggleChanged);
	}

	// 绑定隔离状态变化回调（处理外部复位，如关卡退出）
	if (CachedNetworkManager)
	{
		CachedNetworkManager->OnIsolationStateChanged.AddDynamic(this, &ULocalPreviewIsolationToggleWidget::OnIsolationStateChanged);

		// 同步初始状态
		const bool bCurrentlyIsolated = CachedNetworkManager->IsStrictLocalPreviewIsolation();
		if (IsolationCheckBox)
		{
			bSuppressCallback = true;
			IsolationCheckBox->SetIsChecked(bCurrentlyIsolated);
			bSuppressCallback = false;
		}
		if (StatusBanner)
		{
			StatusBanner->SetVisibility(bCurrentlyIsolated ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}
}

void ULocalPreviewIsolationToggleWidget::NativeDestruct()
{
	if (CachedNetworkManager)
	{
		CachedNetworkManager->OnIsolationStateChanged.RemoveDynamic(this, &ULocalPreviewIsolationToggleWidget::OnIsolationStateChanged);
	}

	Super::NativeDestruct();
}

void ULocalPreviewIsolationToggleWidget::OnToggleChanged(bool bIsChecked)
{
	if (bSuppressCallback)
	{
		return;
	}

	if (CachedNetworkManager)
	{
		CachedNetworkManager->SetStrictLocalPreviewIsolation(bIsChecked);
	}

	// 横幅可见性由 OnIsolationStateChanged 回调统一更新
}

void ULocalPreviewIsolationToggleWidget::OnIsolationStateChanged(bool bIsolated)
{
	// 同步 CheckBox 状态（处理外部触发的复位）
	if (IsolationCheckBox)
	{
		bSuppressCallback = true;
		IsolationCheckBox->SetIsChecked(bIsolated);
		bSuppressCallback = false;
	}

	// 更新状态横幅
	if (StatusBanner)
	{
		StatusBanner->SetVisibility(bIsolated ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}
