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
