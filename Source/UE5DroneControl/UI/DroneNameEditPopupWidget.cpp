// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/DroneNameEditPopupWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------

namespace
{
UTextBlock* MakeLabel(UWidgetTree* Tree, const FName Name, const FString& Value,
	FLinearColor Color = FLinearColor(0.86f, 0.91f, 0.97f, 1.0f))
{
	UTextBlock* T = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	T->SetText(FText::FromString(Value));
	T->SetColorAndOpacity(Color);
	return T;
}

UButton* MakeTextButton(UWidgetTree* Tree, const FName Name, const FString& Label,
	FLinearColor BgColor = FLinearColor(0.08f, 0.15f, 0.26f, 1.0f),
	FLinearColor TextColor = FLinearColor(0.90f, 0.95f, 1.0f, 1.0f))
{
	UButton* Btn = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
	Btn->SetBackgroundColor(BgColor);
	UTextBlock* Lbl = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Lbl->SetText(FText::FromString(Label));
	Lbl->SetColorAndOpacity(TextColor);
	Lbl->SetJustification(ETextJustify::Center);
	Btn->AddChild(Lbl);
	return Btn;
}

void VAdd(UVerticalBox* Box, UWidget* Child,
	FMargin Pad = FMargin(0.0f, 4.0f), EHorizontalAlignment HAlign = HAlign_Fill)
{
	if (UVerticalBoxSlot* S = Box->AddChildToVerticalBox(Child))
	{
		S->SetPadding(Pad);
		S->SetHorizontalAlignment(HAlign);
	}
}

void HFill(UHorizontalBox* Box, UWidget* Child, FMargin Pad = FMargin(2.0f, 0.0f))
{
	if (UHorizontalBoxSlot* S = Box->AddChildToHorizontalBox(Child))
	{
		S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		S->SetPadding(Pad);
	}
}

void HFixed(UHorizontalBox* Box, UWidget* Child, FMargin Pad = FMargin(2.0f, 0.0f))
{
	if (UHorizontalBoxSlot* S = Box->AddChildToHorizontalBox(Child))
	{
		S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		S->SetPadding(Pad);
	}
}
}  // namespace

// ---------------------------------------------------------------------------
//  NativeOnInitialized
// ---------------------------------------------------------------------------

void UDroneNameEditPopupWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildRuntimeWidgetTree();
}

void UDroneNameEditPopupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (ConfirmButton)
		ConfirmButton->OnClicked.AddDynamic(this, &UDroneNameEditPopupWidget::HandleConfirmClicked);
	if (CancelButton)
		CancelButton->OnClicked.AddDynamic(this, &UDroneNameEditPopupWidget::HandleCancelClicked);
	if (FontSizeSlider)
		FontSizeSlider->OnValueChanged.AddDynamic(this, &UDroneNameEditPopupWidget::HandleFontSizeChanged);
	if (SwatchButtons[0])
		SwatchButtons[0]->OnClicked.AddDynamic(this, &UDroneNameEditPopupWidget::HandleColor0);
	if (SwatchButtons[1])
		SwatchButtons[1]->OnClicked.AddDynamic(this, &UDroneNameEditPopupWidget::HandleColor1);
	if (SwatchButtons[2])
		SwatchButtons[2]->OnClicked.AddDynamic(this, &UDroneNameEditPopupWidget::HandleColor2);
	if (SwatchButtons[3])
		SwatchButtons[3]->OnClicked.AddDynamic(this, &UDroneNameEditPopupWidget::HandleColor3);
	if (SwatchButtons[4])
		SwatchButtons[4]->OnClicked.AddDynamic(this, &UDroneNameEditPopupWidget::HandleColor4);
	if (SwatchButtons[5])
		SwatchButtons[5]->OnClicked.AddDynamic(this, &UDroneNameEditPopupWidget::HandleColor5);
}

// ---------------------------------------------------------------------------
//  BuildRuntimeWidgetTree
// ---------------------------------------------------------------------------

void UDroneNameEditPopupWidget::BuildRuntimeWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	// Root overlay
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("EditPopupRoot"));
	WidgetTree->RootWidget = Root;

	// Semi-transparent darkened backdrop
	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("EditBackdrop"));
	Backdrop->SetBrushColor(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Backdrop))
	{
		S->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		S->SetOffsets(FMargin(0.0f));
	}

	// Popup card – 增大到 440×380 以容纳滑动条和各行内容
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>(
		UBorder::StaticClass(), TEXT("EditCard"));
	Card->SetBrushColor(FLinearColor(0.025f, 0.045f, 0.08f, 0.98f));
	Card->SetPadding(FMargin(28.0f, 22.0f));
	if (UCanvasPanelSlot* S = Root->AddChildToCanvas(Card))
	{
		S->SetAnchors(FAnchors(0.5f, 0.5f));
		S->SetAlignment(FVector2D(0.5f, 0.5f));
		S->SetSize(FVector2D(460.0f, 430.0f));
	}

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("EditContent"));
	Card->SetContent(Content);

	// ---- Title ----
	UTextBlock* Title = MakeLabel(WidgetTree, TEXT("EditTitle"), TEXT("编辑飞机顶部标签"),
		FLinearColor(0.94f, 0.98f, 1.0f, 1.0f));
	VAdd(Content, Title, FMargin(0.0f, 0.0f, 0.0f, 16.0f));

	// ---- Name input ----
	VAdd(Content, MakeLabel(WidgetTree, TEXT("NameLabel"), TEXT("显示名称"),
		FLinearColor(0.48f, 0.67f, 0.84f, 1.0f)), FMargin(0.0f, 0.0f, 0.0f, 4.0f));

	NameInput = WidgetTree->ConstructWidget<UEditableTextBox>(
		UEditableTextBox::StaticClass(), TEXT("NameInput"));
	NameInput->SetHintText(FText::FromString(TEXT("无人机名称…")));
	{
		FEditableTextBoxStyle Style = NameInput->GetWidgetStyle();
		Style.BackgroundColor = FSlateColor(FLinearColor(0.06f, 0.10f, 0.18f, 1.0f));
		Style.ForegroundColor = FSlateColor(FLinearColor(0.94f, 0.97f, 1.0f, 1.0f));
		NameInput->SetWidgetStyle(Style);
	}
	VAdd(Content, NameInput, FMargin(0.0f, 0.0f, 0.0f, 16.0f));

	// ---- Colour swatches ----
	VAdd(Content, MakeLabel(WidgetTree, TEXT("ColorLabel"), TEXT("显示颜色（6色预设）"),
		FLinearColor(0.48f, 0.67f, 0.84f, 1.0f)), FMargin(0.0f, 0.0f, 0.0f, 4.0f));

	UHorizontalBox* SwatchRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("SwatchRow"));
	for (int32 i = 0; i < 6; ++i)
	{
		const FName BtnName(*FString::Printf(TEXT("ColorSwatch%d"), i));
		UButton* Swatch = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), BtnName);
		Swatch->SetBackgroundColor(DroneNameLabelPresets::Colors[i]);
		UTextBlock* Dot = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		Dot->SetText(FText::FromString(TEXT("  ")));
		Swatch->AddChild(Dot);
		SwatchButtons[i] = Swatch;
		if (UHorizontalBoxSlot* HS = SwatchRow->AddChildToHorizontalBox(Swatch))
		{
			HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HS->SetPadding(FMargin(2.0f, 0.0f));
			HS->SetVerticalAlignment(VAlign_Fill);
		}
	}
	ColorSwatch0 = SwatchButtons[0];
	ColorSwatch1 = SwatchButtons[1];
	ColorSwatch2 = SwatchButtons[2];
	ColorSwatch3 = SwatchButtons[3];
	ColorSwatch4 = SwatchButtons[4];
	ColorSwatch5 = SwatchButtons[5];

	VAdd(Content, SwatchRow, FMargin(0.0f, 0.0f, 0.0f, 18.0f));

	// ---- Font size – 说明标签（独占一行） ----
	VAdd(Content, MakeLabel(WidgetTree, TEXT("FontLabel"),
		TEXT("字体大小（仅影响飞机标签）"),
		FLinearColor(0.48f, 0.67f, 0.84f, 1.0f)), FMargin(0.0f, 0.0f, 0.0f, 4.0f));

	// ---- Font size – 滑条行：[slider(fill)]  [数值(fixed 32px)] ----
	UHorizontalBox* FontSliderRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("FontSliderRow"));

	FontSizeSlider = WidgetTree->ConstructWidget<USlider>(
		USlider::StaticClass(), TEXT("FontSizeSlider"));
	FontSizeSlider->SetMinValue(8.0f);
	FontSizeSlider->SetMaxValue(72.0f);
	FontSizeSlider->SetStepSize(2.0f);
	FontSizeSlider->SetValue(static_cast<float>(CurrentFontSize));
	{
		FSliderStyle Style = FontSizeSlider->GetWidgetStyle();
		Style.NormalBarImage.TintColor    = FSlateColor(FLinearColor(0.15f, 0.25f, 0.40f, 1.0f));
		Style.HoveredBarImage.TintColor   = FSlateColor(FLinearColor(0.20f, 0.35f, 0.55f, 1.0f));
		Style.NormalThumbImage.TintColor  = FSlateColor(FLinearColor(0.45f, 0.72f, 1.00f, 1.0f));
		Style.HoveredThumbImage.TintColor = FSlateColor(FLinearColor(0.65f, 0.88f, 1.00f, 1.0f));
		FontSizeSlider->SetWidgetStyle(Style);
	}
	// 滑条垂直居中，右侧留 8px 与数值文字间隔
	if (UHorizontalBoxSlot* HS = FontSliderRow->AddChildToHorizontalBox(FontSizeSlider))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HS->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	// 数值文字固定 32px 宽，右对齐，不会超出卡片
	FontSizeText = MakeLabel(WidgetTree, TEXT("FontSizeText"), TEXT("16"),
		FLinearColor(0.94f, 0.98f, 1.0f, 1.0f));
	FontSizeText->SetJustification(ETextJustify::Right);
	if (UHorizontalBoxSlot* HS = FontSliderRow->AddChildToHorizontalBox(FontSizeText))
	{
		HS->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));
		HS->SetPadding(FMargin(0.0f));
		HS->SetVerticalAlignment(VAlign_Center);
	}

	VAdd(Content, FontSliderRow, FMargin(0.0f, 0.0f, 0.0f, 18.0f));

	// ---- Confirm / Cancel ----
	UHorizontalBox* BtnRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("BtnRow"));

	CancelButton = MakeTextButton(WidgetTree, TEXT("CancelBtn"), TEXT("取消"),
		FLinearColor(0.15f, 0.20f, 0.28f, 1.0f));
	HFill(BtnRow, CancelButton, FMargin(0.0f, 0.0f, 6.0f, 0.0f));

	ConfirmButton = MakeTextButton(WidgetTree, TEXT("ConfirmBtn"), TEXT("确认"),
		FLinearColor(0.08f, 0.38f, 0.56f, 1.0f));
	HFill(BtnRow, ConfirmButton, FMargin(6.0f, 0.0f, 0.0f, 0.0f));

	VAdd(Content, BtnRow, FMargin(0.0f, 4.0f));
}

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------

void UDroneNameEditPopupWidget::InitPopup(int32 InDroneId, const FString& CurrentName,
	FLinearColor CurrentColor, int32 InFontSize)
{
	BoundDroneId    = InDroneId;
	SelectedColor   = CurrentColor;
	CurrentFontSize = FMath::Clamp(InFontSize, 8, 72);

	if (NameInput)
	{
		NameInput->SetText(FText::FromString(CurrentName));
	}

	// 同步滑动条位置
	if (FontSizeSlider)
	{
		FontSizeSlider->SetValue(static_cast<float>(CurrentFontSize));
	}
	UpdateFontSizeDisplay();

	// Highlight the swatch that matches current colour (if any)
	for (int32 i = 0; i < 6; ++i)
	{
		if (SwatchButtons[i] &&
			DroneNameLabelPresets::Colors[i].Equals(CurrentColor, 0.01f))
		{
			SelectColorByIndex(i);
			return;
		}
	}
	// Custom colour not in presets – just store it, no highlight
}

// ---------------------------------------------------------------------------
//  Private helpers
// ---------------------------------------------------------------------------

void UDroneNameEditPopupWidget::UpdateFontSizeDisplay()
{
	if (FontSizeText)
	{
		FontSizeText->SetText(FText::FromString(FString::Printf(TEXT("%d"), CurrentFontSize)));
	}
}

void UDroneNameEditPopupWidget::SelectColorByIndex(int32 Index)
{
	if (Index < 0 || Index >= 6)
	{
		return;
	}
	SelectedColor = DroneNameLabelPresets::Colors[Index];

	for (int32 i = 0; i < 6; ++i)
	{
		if (!SwatchButtons[i])
		{
			continue;
		}
		FLinearColor Base = DroneNameLabelPresets::Colors[i];
		SwatchButtons[i]->SetBackgroundColor(
			i == Index ? FLinearColor(Base.R, Base.G, Base.B, 1.0f)
					   : FLinearColor(Base.R * 0.5f, Base.G * 0.5f, Base.B * 0.5f, 0.65f));
	}
}

// ---------------------------------------------------------------------------
//  Button / Slider handlers
// ---------------------------------------------------------------------------

void UDroneNameEditPopupWidget::HandleConfirmClicked()
{
	const FString Name = NameInput ? NameInput->GetText().ToString() : FString();
	OnConfirmed.Broadcast(BoundDroneId, Name, SelectedColor, CurrentFontSize);
	RemoveFromParent();
}

void UDroneNameEditPopupWidget::HandleCancelClicked()
{
	RemoveFromParent();
}

void UDroneNameEditPopupWidget::HandleFontSizeChanged(float Value)
{
	// Slider 以 StepSize=2 离散输出，直接四舍五入到偶数以保持与 [8,72] 范围一致
	CurrentFontSize = FMath::Clamp(FMath::RoundToInt(Value / 2.0f) * 2, 8, 72);
	UpdateFontSizeDisplay();
}

void UDroneNameEditPopupWidget::HandleColor0() { SelectColorByIndex(0); }
void UDroneNameEditPopupWidget::HandleColor1() { SelectColorByIndex(1); }
void UDroneNameEditPopupWidget::HandleColor2() { SelectColorByIndex(2); }
void UDroneNameEditPopupWidget::HandleColor3() { SelectColorByIndex(3); }
void UDroneNameEditPopupWidget::HandleColor4() { SelectColorByIndex(4); }
void UDroneNameEditPopupWidget::HandleColor5() { SelectColorByIndex(5); }
