// Copyright Epic Games, Inc. All Rights Reserved.

#include "PreviewConfirmPopupWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Blueprint/WidgetTree.h"

void UPreviewConfirmPopupWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildRuntimeWidgetTree();
}

void UPreviewConfirmPopupWidget::BuildRuntimeWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ConfirmRoot"));
	WidgetTree->RootWidget = Root;
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ConfirmPanel"));
	Panel->SetBrushColor(FLinearColor(0.035f, 0.06f, 0.10f, 0.98f));
	Panel->SetPadding(FMargin(20.0f, 18.0f));
    if (UCanvasPanelSlot* PanelSlot = Root->AddChildToCanvas(Panel))
    {
        PanelSlot->SetAnchors(FAnchors(0.5f, 0.5f));
        PanelSlot->SetAlignment(FVector2D(0.5f, 0.5f));
        PanelSlot->SetSize(FVector2D(380.0f, 230.0f));
	}
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ConfirmContent"));
	Panel->SetContent(Content);
	MessageText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MessageText"));
	MessageText->SetText(FText::FromString(TEXT("请选择操作")));
	MessageText->SetAutoWrapText(true);
	MessageText->SetColorAndOpacity(FLinearColor(0.94f, 0.97f, 1.0f, 1.0f));
    if (UVerticalBoxSlot* MessageSlot = Content->AddChildToVerticalBox(MessageText))
    {
        MessageSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
        MessageSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
    auto MakeButton = [this, Content](const FName Name, const FString& Label, UButton*& OutButton, const FLinearColor& Color)
	{
		OutButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		OutButton->SetBackgroundColor(Color);
		UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Text->SetText(FText::FromString(Label));
		Text->SetJustification(ETextJustify::Center);
		OutButton->AddChild(Text);
		if (UVerticalBoxSlot* ButtonSlot = Content->AddChildToVerticalBox(OutButton)) ButtonSlot->SetPadding(FMargin(0.0f, 3.0f));
	};
	MakeButton(TEXT("LocalPreviewButton"), TEXT("本地预演"), LocalPreviewButton, FLinearColor(0.08f, 0.36f, 0.56f, 1.0f));
	MakeButton(TEXT("DispatchButton"), TEXT("派发指令"), DispatchButton, FLinearColor(0.10f, 0.48f, 0.34f, 1.0f));
	MakeButton(TEXT("CancelButton"), TEXT("取消"), CancelButton, FLinearColor(0.20f, 0.24f, 0.30f, 1.0f));
}

void UPreviewConfirmPopupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (LocalPreviewButton)
	{
		LocalPreviewButton->OnClicked.AddDynamic(this, &UPreviewConfirmPopupWidget::HandleLocalPreviewClicked);
	}
	if (DispatchButton)
	{
		DispatchButton->OnClicked.AddDynamic(this, &UPreviewConfirmPopupWidget::HandleDispatchClicked);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.AddDynamic(this, &UPreviewConfirmPopupWidget::HandleCancelClicked);
	}
}

void UPreviewConfirmPopupWidget::SetMessage(const FString& Message)
{
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(Message));
	}
}

void UPreviewConfirmPopupWidget::HandleLocalPreviewClicked()
{
	if (bIsAttackConfirmMode)
	{
		HandleAttackConfirmClicked();
		return;
	}
	MakeChoice(EPreviewConfirmChoice::LocalPreview);
}

void UPreviewConfirmPopupWidget::HandleDispatchClicked()
{
	MakeChoice(EPreviewConfirmChoice::Dispatch);
}

void UPreviewConfirmPopupWidget::HandleCancelClicked()
{
	if (bIsAttackConfirmMode)
	{
		HandleAttackDeclineClicked();
		return;
	}
	MakeChoice(EPreviewConfirmChoice::Cancel);
}

void UPreviewConfirmPopupWidget::MakeChoice(EPreviewConfirmChoice Choice)
{
	// 先广播再移除，避免弹窗销毁后监听者拿不到结果
	OnChoiceMade.Broadcast(Choice);
	RemoveFromParent();
}

// 攻击确认模式

void UPreviewConfirmPopupWidget::SetAttackConfirmData(int32 InDroneId, int32 InTargetId,
	const FVector& InTargetLocation)
{
	bIsAttackConfirmMode = true;
	AttackDroneId = InDroneId;
	AttackTargetId = InTargetId;
	AttackTargetLocation = InTargetLocation;

	// 更新按钮文字和消息
	if (MessageText)
	{
		MessageText->SetText(FText::FromString(
			FString::Printf(TEXT("无人机 D%d 发现敌对目标 T%d\n是否攻击？"),
				AttackDroneId, AttackTargetId)));
	}

	SetAttackButtonText(TEXT("攻击"));
	SetDeclineButtonText(TEXT("不攻击"));

	// 隐藏第三个按钮（DispatchButton）
	if (DispatchButton)
	{
		DispatchButton->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UPreviewConfirmPopupWidget::SetAttackButtonText(const FString& Text)
{
	if (LocalPreviewButton)
	{
		if (UTextBlock* ButtonText = Cast<UTextBlock>(
			LocalPreviewButton->GetChildAt(0)))
		{
			ButtonText->SetText(FText::FromString(Text));
		}
	}
}

void UPreviewConfirmPopupWidget::SetDeclineButtonText(const FString& Text)
{
	if (CancelButton)
	{
		if (UTextBlock* ButtonText = Cast<UTextBlock>(
			CancelButton->GetChildAt(0)))
		{
			ButtonText->SetText(FText::FromString(Text));
		}
	}
}

void UPreviewConfirmPopupWidget::HandleAttackConfirmClicked()
{
	if (!bIsAttackConfirmMode)
	{
		// 非攻击模式，走原有逻辑
		HandleLocalPreviewClicked();
		return;
	}

	OnAttackConfirmMade.Broadcast(AttackDroneId, AttackTargetId, true);
	RemoveFromParent();
}

void UPreviewConfirmPopupWidget::HandleAttackDeclineClicked()
{
	if (!bIsAttackConfirmMode)
	{
		HandleCancelClicked();
		return;
	}

	OnAttackConfirmMade.Broadcast(AttackDroneId, AttackTargetId, false);
	RemoveFromParent();
}
