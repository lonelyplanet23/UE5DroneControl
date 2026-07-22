// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/DroneNameLabelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"

void UDroneNameLabelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureLabelText();
}

void UDroneNameLabelWidget::EnsureLabelText()
{
	if (LabelText)
	{
		return; // Blueprint already bound
	}
	if (!WidgetTree)
	{
		return;
	}

	LabelText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("LabelText"));
	if (LabelText)
	{
		LabelText->SetText(FText::FromString(TEXT("UAV")));
		// Default: bright near-white matching panel body text (avoids connection-status colours)
		LabelText->SetColorAndOpacity(FLinearColor(0.86f, 0.91f, 0.97f, 1.0f));

		FSlateFontInfo Font = LabelText->GetFont();
		Font.Size = 16;
		Font.OutlineSettings.OutlineSize = 2;
		Font.OutlineSettings.OutlineColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.7f);
		LabelText->SetFont(Font);

		WidgetTree->RootWidget = LabelText;
	}
}

void UDroneNameLabelWidget::SetLabelName(const FString& Name)
{
	EnsureLabelText();
	if (LabelText)
	{
		LabelText->SetText(FText::FromString(Name));
	}
}

void UDroneNameLabelWidget::SetLabelColor(FLinearColor Color)
{
	EnsureLabelText();
	if (LabelText)
	{
		LabelText->SetColorAndOpacity(Color);
	}
}

void UDroneNameLabelWidget::SetLabelFontSize(int32 Size)
{
	EnsureLabelText();
	if (LabelText)
	{
		FSlateFontInfo Font = LabelText->GetFont();
		Font.Size = FMath::Clamp(Size, 8, 72);
		LabelText->SetFont(Font);
	}
}

void UDroneNameLabelWidget::ApplyLabelSettings(const FString& Name, FLinearColor Color, int32 FontSize)
{
	EnsureLabelText();
	if (!LabelText)
	{
		return;
	}

	LabelText->SetText(FText::FromString(Name));
	LabelText->SetColorAndOpacity(Color);

	FSlateFontInfo Font = LabelText->GetFont();
	Font.Size = FMath::Clamp(FontSize, 8, 72);
	LabelText->SetFont(Font);
}
