// Copyright Epic Games, Inc. All Rights Reserved.

#include "PreviewConfirmPopupWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

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
	// 攻击确认模式复用 LocalPreviewButton 和 CancelButton
	if (LocalPreviewButton)
	{
		LocalPreviewButton->OnClicked.AddDynamic(this, &UPreviewConfirmPopupWidget::HandleAttackConfirmClicked);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.AddDynamic(this, &UPreviewConfirmPopupWidget::HandleAttackDeclineClicked);
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
	MakeChoice(EPreviewConfirmChoice::LocalPreview);
}

void UPreviewConfirmPopupWidget::HandleDispatchClicked()
{
	MakeChoice(EPreviewConfirmChoice::Dispatch);
}

void UPreviewConfirmPopupWidget::HandleCancelClicked()
{
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
