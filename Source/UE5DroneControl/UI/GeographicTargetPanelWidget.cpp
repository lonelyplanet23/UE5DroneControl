// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeographicTargetPanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SpinBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "DroneOps/Control/DroneOpsPlayerController.h"
#include "Engine/Texture2D.h"
#include "Misc/DefaultValueHelper.h"

bool UGeographicTargetPanelWidget::bStaticPanelInteractive = false;

namespace
{
const FLinearColor PanelBackground(0.025f, 0.045f, 0.075f, 0.98f);
const FLinearColor CardBackground(0.045f, 0.075f, 0.115f, 0.98f);
const FLinearColor SubtleCardBackground(0.035f, 0.060f, 0.095f, 0.96f);
const FLinearColor PrimaryText(0.94f, 0.98f, 1.0f, 1.0f);
const FLinearColor SecondaryText(0.48f, 0.67f, 0.84f, 1.0f);
const FLinearColor AccentText(0.38f, 0.82f, 1.0f, 1.0f);
const FLinearColor InputText(0.035f, 0.065f, 0.095f, 1.0f);
const FLinearColor InputBackground(0.94f, 0.97f, 1.0f, 1.0f);

UTextBlock* MakeText(UWidgetTree* WidgetTree, const FName Name, const FString& Text)
{
	UTextBlock* TextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	TextBlock->SetText(FText::FromString(Text));
	return TextBlock;
}

void StyleText(UTextBlock* TextBlock, int32 FontSize, const FLinearColor& Color, bool bBold = false)
{
	if (!TextBlock)
	{
		return;
	}
	FSlateFontInfo Font = TextBlock->GetFont();
	Font.Size = FontSize;
	if (bBold)
	{
		Font.TypefaceFontName = TEXT("Bold");
	}
	TextBlock->SetFont(Font);
	TextBlock->SetColorAndOpacity(FSlateColor(Color));
}

void StyleSingleLineInput(UEditableTextBox* Input)
{
	if (!Input)
	{
		return;
	}
	FEditableTextBoxStyle Style = Input->GetWidgetStyle();
	FTextBlockStyle TextStyle = Style.TextStyle;
	TextStyle.Font.Size = 14;
	TextStyle.SetColorAndOpacity(FSlateColor(InputText));
	Style
		.SetTextStyle(TextStyle)
		.SetForegroundColor(FSlateColor(InputText))
		.SetFocusedForegroundColor(FSlateColor(InputText))
		.SetReadOnlyForegroundColor(FSlateColor(InputText))
		.SetBackgroundColor(FSlateColor(InputBackground))
		.SetPadding(FMargin(12.0f, 9.0f));
	Input->SetWidgetStyle(Style);
}

void StyleMultiLineInput(UMultiLineEditableTextBox* Input)
{
	if (!Input)
	{
		return;
	}
	FTextBlockStyle TextStyle = Input->WidgetStyle.TextStyle;
	TextStyle.Font.Size = 13;
	TextStyle.SetColorAndOpacity(FSlateColor(InputText));
	Input->WidgetStyle
		.SetTextStyle(TextStyle)
		.SetForegroundColor(FSlateColor(InputText))
		.SetFocusedForegroundColor(FSlateColor(InputText))
		.SetReadOnlyForegroundColor(FSlateColor(InputText))
		.SetBackgroundColor(FSlateColor(InputBackground))
		.SetPadding(FMargin(10.0f, 8.0f));
}

UBorder* MakeCard(UWidgetTree* WidgetTree, const FName Name, const FLinearColor& Color = CardBackground)
{
	UBorder* Card = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
	Card->SetBrushColor(Color);
	Card->SetPadding(FMargin(14.0f, 12.0f));
	return Card;
}

void AddPaddedChild(UVerticalBox* Parent, UWidget* Child, const FMargin& Padding = FMargin(0.0f, 4.0f))
{
	if (UVerticalBoxSlot* Slot = Parent->AddChildToVerticalBox(Child))
	{
		Slot->SetPadding(Padding);
	}
}
}

void UGeographicTargetPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	// WBP_GeographicTargetPanel intentionally inherits this production-ready runtime tree. A designer
	// can add a bound widget tree later without changing controller or validation logic.
	BuildFallbackWidgetTree();
}

void UGeographicTargetPanelWidget::BuildFallbackWidgetTree()
{
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}

	UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
		UCanvasPanel::StaticClass(), TEXT("RootCanvas"));
	RootCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	WidgetTree->RootWidget = RootCanvas;

	OpenButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("OpenButton"));
	OpenButton->SetBackgroundColor(FLinearColor(0.08f, 0.42f, 0.64f, 0.95f));
	UImage* CoordinateIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("CoordinateIcon"));
	if (UTexture2D* LocationTexture = LoadObject<UTexture2D>(
			nullptr, TEXT("/Game/SpaceGUI/textures/Icons/64/t_Location_64.t_Location_64")))
	{
		CoordinateIcon->SetBrushFromTexture(LocationTexture, true);
		CoordinateIcon->SetColorAndOpacity(FLinearColor(0.40f, 0.85f, 1.0f, 1.0f));
	}
	else
	{
		// Keep a visible fallback if the optional SpaceGUI pack is removed from a build.
		CoordinateIcon->SetVisibility(ESlateVisibility::Collapsed);
		OpenButton->AddChild(MakeText(WidgetTree, TEXT("OpenButtonText"), TEXT("⌖")));
	}
	if (!OpenButton->GetContent())
	{
		OpenButton->AddChild(CoordinateIcon);
	}
	if (UCanvasPanelSlot* OpenSlot = RootCanvas->AddChildToCanvas(OpenButton))
	{
		OpenSlot->SetAnchors(FAnchors(1.0f, 0.0f));
		OpenSlot->SetAlignment(FVector2D(1.0f, 0.0f));
		OpenSlot->SetPosition(FVector2D(-24.0f, 24.0f));
		OpenSlot->SetSize(FVector2D(48.0f, 48.0f));
	}

	UBorder* BodyBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelBody"));
	BodyBorder->SetBrushColor(PanelBackground);
	BodyBorder->SetPadding(FMargin(18.0f, 16.0f));
	PanelBody = BodyBorder;
	if (UCanvasPanelSlot* BodySlot = RootCanvas->AddChildToCanvas(BodyBorder))
	{
		BodySlot->SetAnchors(FAnchors(1.0f, 0.0f));
		BodySlot->SetAlignment(FVector2D(1.0f, 0.0f));
		BodySlot->SetPosition(FVector2D(-28.0f, 84.0f));
		BodySlot->SetSize(FVector2D(600.0f, 600.0f));
	}

	UScrollBox* PanelScroll = WidgetTree->ConstructWidget<UScrollBox>(
		UScrollBox::StaticClass(), TEXT("PanelScroll"));
	PanelScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	BodyBorder->AddChild(PanelScroll);
	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("PanelContent"));
	PanelScroll->AddChild(Content);
	UHorizontalBox* HeaderRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("HeaderRow"));
	UVerticalBox* HeaderCopy = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("HeaderCopy"));
	UTextBlock* TitleText = MakeText(WidgetTree, TEXT("TitleText"), TEXT("地理坐标目标"));
	StyleText(TitleText, 21, PrimaryText, true);
	AddPaddedChild(HeaderCopy, TitleText, FMargin(0.0f, 0.0f, 0.0f, 2.0f));
	UTextBlock* SubtitleText = MakeText(WidgetTree, TEXT("SubtitleText"),
		TEXT("粘贴经纬高，一次预览或派发到所选无人机"));
	StyleText(SubtitleText, 12, SecondaryText);
	if (UHorizontalBoxSlot* HeaderCopySlot = HeaderRow->AddChildToHorizontalBox(HeaderCopy))
	{
		HeaderCopySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	UBorder* DatumBadge = MakeCard(WidgetTree, TEXT("DatumBadge"), FLinearColor(0.05f, 0.20f, 0.29f, 1.0f));
	DatumBadge->SetPadding(FMargin(10.0f, 6.0f));
	UTextBlock* DatumBadgeText = MakeText(WidgetTree, TEXT("DatumBadgeText"), TEXT("WGS84  ·  MSL"));
	StyleText(DatumBadgeText, 11, AccentText, true);
	DatumBadge->AddChild(DatumBadgeText);
	if (UHorizontalBoxSlot* BadgeSlot = HeaderRow->AddChildToHorizontalBox(DatumBadge))
	{
		BadgeSlot->SetVerticalAlignment(VAlign_Center);
	}
	AddPaddedChild(Content, HeaderRow, FMargin(0.0f, 0.0f, 0.0f, 14.0f));

	UTextBlock* SettingsHeading = MakeText(WidgetTree, TEXT("SettingsHeading"), TEXT("目标设置"));
	StyleText(SettingsHeading, 12, AccentText, true);
	AddPaddedChild(Content, SettingsHeading, FMargin(2.0f, 0.0f, 0.0f, 5.0f));

	UBorder* SettingsCard = MakeCard(WidgetTree, TEXT("SettingsCard"));
	UHorizontalBox* SettingsRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("SettingsRow"));
	SettingsCard->AddChild(SettingsRow);

	UVerticalBox* CoordColumn = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("CoordinateSystemColumn"));
	UTextBlock* CoordinateSystemLabel = MakeText(WidgetTree, TEXT("CoordinateSystemLabel"), TEXT("坐标系"));
	StyleText(CoordinateSystemLabel, 12, SecondaryText);
	AddPaddedChild(CoordColumn, CoordinateSystemLabel, FMargin(0.0f, 0.0f, 0.0f, 5.0f));
	CoordSystemComboBox = WidgetTree->ConstructWidget<UComboBoxString>(
		UComboBoxString::StaticClass(), TEXT("CoordSystemComboBox"));
	CoordSystemComboBox->SetContentPadding(FMargin(10.0f, 7.0f));
	AddPaddedChild(CoordColumn, CoordSystemComboBox, FMargin(0.0f));
	if (UHorizontalBoxSlot* CoordColumnSlot = SettingsRow->AddChildToHorizontalBox(CoordColumn))
	{
		CoordColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		CoordColumnSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
	}

	UVerticalBox* ModeColumn = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("DispatchModeColumn"));
	UTextBlock* ModeLabel = MakeText(WidgetTree, TEXT("DispatchModeLabel"), TEXT("派发方式"));
	StyleText(ModeLabel, 12, SecondaryText);
	AddPaddedChild(ModeColumn, ModeLabel, FMargin(0.0f, 0.0f, 0.0f, 5.0f));
	DispatchModeComboBox = WidgetTree->ConstructWidget<UComboBoxString>(
		UComboBoxString::StaticClass(), TEXT("DispatchModeComboBox"));
	DispatchModeComboBox->SetContentPadding(FMargin(10.0f, 7.0f));
	AddPaddedChild(ModeColumn, DispatchModeComboBox, FMargin(0.0f));
	if (UHorizontalBoxSlot* ModeColumnSlot = SettingsRow->AddChildToHorizontalBox(ModeColumn))
	{
		ModeColumnSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ModeColumnSlot->SetPadding(FMargin(6.0f, 0.0f, 0.0f, 0.0f));
	}
	AddPaddedChild(Content, SettingsCard, FMargin(0.0f, 0.0f, 0.0f, 10.0f));

	UBorder* UniformCard = MakeCard(WidgetTree, TEXT("UniformTargetCard"));
	UniformTargetRow = UniformCard;
	UVerticalBox* UniformContent = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("UniformTargetContent"));
	UniformCard->AddChild(UniformContent);
	UHorizontalBox* UniformTitleRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("UniformTargetTitleRow"));
	UTextBlock* VectorLabel = MakeText(WidgetTree, TEXT("CoordinateVectorLabel"), TEXT("统一目标坐标"));
	StyleText(VectorLabel, 13, PrimaryText, true);
	if (UHorizontalBoxSlot* VectorLabelSlot = UniformTitleRow->AddChildToHorizontalBox(VectorLabel))
	{
		VectorLabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	UTextBlock* AxisHint = MakeText(WidgetTree, TEXT("CoordinateAxisHint"), TEXT("经度  /  纬度  /  海拔 m"));
	StyleText(AxisHint, 11, SecondaryText);
	UniformTitleRow->AddChildToHorizontalBox(AxisHint);
	AddPaddedChild(UniformContent, UniformTitleRow, FMargin(0.0f, 0.0f, 0.0f, 7.0f));
	CoordinateVectorInput = WidgetTree->ConstructWidget<UEditableTextBox>(
		UEditableTextBox::StaticClass(), TEXT("CoordinateVectorInput"));
	CoordinateVectorInput->SetHintText(FText::FromString(TEXT("例如  (116.347, 39.981, 63)")));
	CoordinateVectorInput->SetToolTipText(FText::FromString(
		TEXT("格式：（经度, 纬度, 海拔）；海拔单位为米，基准为平均海平面")));
	StyleSingleLineInput(CoordinateVectorInput);
	AddPaddedChild(UniformContent, CoordinateVectorInput, FMargin(0.0f));
	AltitudeLabel = MakeText(WidgetTree, TEXT("AltitudeLabel"),
		TEXT("支持中英文逗号或空格分隔  ·  海拔（m，平均海平面）"));
	StyleText(AltitudeLabel, 11, SecondaryText);
	AddPaddedChild(UniformContent, AltitudeLabel, FMargin(0.0f, 6.0f, 0.0f, 0.0f));
	AddPaddedChild(Content, UniformCard, FMargin(0.0f, 0.0f, 0.0f, 10.0f));

	UBorder* PerDroneCard = MakeCard(WidgetTree, TEXT("PerDroneTargetsPanel"));
	PerDroneTargetsPanel = PerDroneCard;
	UVerticalBox* PerDronePanel = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("PerDroneTargetsContent"));
	PerDroneCard->AddChild(PerDronePanel);
	UTextBlock* PerDroneTitle = MakeText(WidgetTree, TEXT("PerDroneTitle"), TEXT("逐机目标坐标"));
	StyleText(PerDroneTitle, 13, PrimaryText, true);
	AddPaddedChild(PerDronePanel, PerDroneTitle, FMargin(0.0f, 0.0f, 0.0f, 2.0f));
	UTextBlock* PerDroneHint = MakeText(WidgetTree, TEXT("PerDroneHint"),
		TEXT("按 DroneId 稳定排序；每架无人机精确到自己的坐标"));
	StyleText(PerDroneHint, 11, SecondaryText);
	AddPaddedChild(PerDronePanel, PerDroneHint, FMargin(0.0f, 0.0f, 0.0f, 7.0f));
	USizeBox* PerDroneScrollSize = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("PerDroneScrollSize"));
	PerDroneTargetsScrollSize = PerDroneScrollSize;
	PerDroneScrollSize->SetHeightOverride(190.0f);
	UScrollBox* PerDroneScroll = WidgetTree->ConstructWidget<UScrollBox>(
		UScrollBox::StaticClass(), TEXT("PerDroneTargetsScroll"));
	PerDroneScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
	PerDroneTargetsBox = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("PerDroneTargetsBox"));
	PerDroneScroll->AddChild(PerDroneTargetsBox);
	PerDroneScrollSize->AddChild(PerDroneScroll);
	AddPaddedChild(PerDronePanel, PerDroneScrollSize, FMargin(0.0f));
	AddPaddedChild(Content, PerDroneCard, FMargin(0.0f, 0.0f, 0.0f, 10.0f));

	UBorder* BatchCard = MakeCard(WidgetTree, TEXT("BatchPathSection"));
	BatchPathSection = BatchCard;
	UVerticalBox* BatchContent = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("BatchPathContent"));
	BatchCard->AddChild(BatchContent);
	UHorizontalBox* BatchTitleRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("BatchTitleRow"));
	UTextBlock* BatchLabel = MakeText(WidgetTree, TEXT("BatchCoordinatesLabel"), TEXT("批量添加路径点"));
	StyleText(BatchLabel, 13, PrimaryText, true);
	if (UHorizontalBoxSlot* BatchLabelSlot = BatchTitleRow->AddChildToHorizontalBox(BatchLabel))
	{
		BatchLabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	UBorder* EditModeBadge = MakeCard(WidgetTree, TEXT("EditModeBadge"), FLinearColor(0.06f, 0.28f, 0.32f, 1.0f));
	EditModeBadge->SetPadding(FMargin(8.0f, 4.0f));
	UTextBlock* EditModeBadgeText = MakeText(WidgetTree, TEXT("EditModeBadgeText"), TEXT("路径编辑"));
	StyleText(EditModeBadgeText, 10, AccentText, true);
	EditModeBadge->AddChild(EditModeBadgeText);
	BatchTitleRow->AddChildToHorizontalBox(EditModeBadge);
	AddPaddedChild(BatchContent, BatchTitleRow, FMargin(0.0f, 0.0f, 0.0f, 4.0f));
	UTextBlock* BatchHint = MakeText(WidgetTree, TEXT("BatchCoordinatesHint"),
		TEXT("每行一个 (经度, 纬度, 海拔)，按输入顺序追加；海拔为平均海平面 MSL，不是离地高度"));
	StyleText(BatchHint, 11, SecondaryText);
	BatchHint->SetAutoWrapText(true);
	AddPaddedChild(BatchContent, BatchHint, FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	USizeBox* BatchInputSize = WidgetTree->ConstructWidget<USizeBox>(
		USizeBox::StaticClass(), TEXT("BatchCoordinatesInputSize"));
	BatchInputSize->SetHeightOverride(92.0f);
	BatchCoordinatesInput = WidgetTree->ConstructWidget<UMultiLineEditableTextBox>(
		UMultiLineEditableTextBox::StaticClass(), TEXT("BatchCoordinatesInput"));
	BatchCoordinatesInput->SetHintText(FText::FromString(
		TEXT("(116.397, 39.908, 50)\n(116.398, 39.909, 52)")));
	BatchCoordinatesInput->SetToolTipText(FText::FromString(
		TEXT("字段顺序：经度、纬度、海拔；海拔单位为米，基准为平均海平面（MSL），不是距地面高度")));
	StyleMultiLineInput(BatchCoordinatesInput);
	BatchInputSize->AddChild(BatchCoordinatesInput);
	AddPaddedChild(BatchContent, BatchInputSize, FMargin(0.0f, 0.0f, 0.0f, 7.0f));
	BatchAddButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("BatchAddButton"));
	BatchAddButton->SetBackgroundColor(FLinearColor(0.07f, 0.40f, 0.50f, 1.0f));
	UTextBlock* BatchButtonText = MakeText(WidgetTree, TEXT("BatchAddButtonText"), TEXT("添加到全部活动路径"));
	StyleText(BatchButtonText, 13, PrimaryText, true);
	BatchAddButton->AddChild(BatchButtonText);
	AddPaddedChild(BatchContent, BatchAddButton, FMargin(0.0f));
	AddPaddedChild(Content, BatchCard, FMargin(0.0f, 0.0f, 0.0f, 10.0f));

	UBorder* SelectionSummary = MakeCard(WidgetTree, TEXT("SelectionSummary"), SubtleCardBackground);
	SelectionSummary->SetPadding(FMargin(12.0f, 8.0f));
	SelectedCountText = MakeText(WidgetTree, TEXT("SelectedCountText"), TEXT("已选中无人机  ·  0 架"));
	StyleText(SelectedCountText, 13, AccentText, true);
	SelectionSummary->AddChild(SelectedCountText);
	AddPaddedChild(Content, SelectionSummary, FMargin(0.0f, 0.0f, 0.0f, 8.0f));

	UHorizontalBox* ActionRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("ActionRow"));
	PreviewButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("PreviewButton"));
	PreviewButton->SetBackgroundColor(FLinearColor(0.08f, 0.28f, 0.40f, 1.0f));
	UTextBlock* PreviewButtonText = MakeText(WidgetTree, TEXT("PreviewButtonText"), TEXT("位置预览"));
	StyleText(PreviewButtonText, 14, PrimaryText, true);
	PreviewButton->AddChild(PreviewButtonText);
	if (UHorizontalBoxSlot* PreviewSlot = ActionRow->AddChildToHorizontalBox(PreviewButton))
	{
		PreviewSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PreviewSlot->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
	}
	DispatchButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("DispatchButton"));
	DispatchButton->SetBackgroundColor(FLinearColor(0.04f, 0.48f, 0.54f, 1.0f));
	UTextBlock* DispatchButtonText = MakeText(WidgetTree, TEXT("DispatchButtonText"), TEXT("派发目标"));
	StyleText(DispatchButtonText, 14, PrimaryText, true);
	DispatchButton->AddChild(DispatchButtonText);
	if (UHorizontalBoxSlot* DispatchSlot = ActionRow->AddChildToHorizontalBox(DispatchButton))
	{
		DispatchSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		DispatchSlot->SetPadding(FMargin(4.0f, 0.0f, 0.0f, 0.0f));
	}
	AddPaddedChild(Content, ActionRow, FMargin(0.0f, 0.0f, 0.0f, 8.0f));

	UBorder* StatusCard = MakeCard(WidgetTree, TEXT("StatusCard"), FLinearColor(0.025f, 0.040f, 0.065f, 0.96f));
	StatusPanel = StatusCard;
	StatusCard->SetPadding(FMargin(12.0f, 9.0f));
	StatusText = MakeText(WidgetTree, TEXT("StatusText"), TEXT("请先选择无人机"));
	StyleText(StatusText, 12, FLinearColor(0.72f, 0.80f, 0.88f, 1.0f));
	StatusText->SetAutoWrapText(true);
	StatusCard->AddChild(StatusText);
	AddPaddedChild(Content, StatusCard, FMargin(0.0f));
}

void UGeographicTargetPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (OpenButton)
	{
		OpenButton->OnClicked.AddUniqueDynamic(this, &UGeographicTargetPanelWidget::OnOpenButtonClicked);
	}
	if (PreviewButton)
	{
		PreviewButton->OnClicked.AddUniqueDynamic(this, &UGeographicTargetPanelWidget::OnPreviewButtonClicked);
	}
	if (DispatchButton)
	{
		DispatchButton->OnClicked.AddUniqueDynamic(this, &UGeographicTargetPanelWidget::OnDispatchButtonClicked);
	}
	if (BatchAddButton)
	{
		BatchAddButton->OnClicked.AddUniqueDynamic(this, &UGeographicTargetPanelWidget::OnBatchAddButtonClicked);
	}
	if (DispatchModeComboBox)
	{
		DispatchModeComboBox->OnSelectionChanged.AddUniqueDynamic(
			this, &UGeographicTargetPanelWidget::OnDispatchModeChanged);
	}
	if (CoordinateVectorInput)
	{
		CoordinateVectorInput->OnTextChanged.AddUniqueDynamic(
			this, &UGeographicTargetPanelWidget::OnInputTextChanged);
	}
	if (BatchCoordinatesInput)
	{
		BatchCoordinatesInput->OnTextChanged.AddUniqueDynamic(
			this, &UGeographicTargetPanelWidget::OnInputTextChanged);
	}
	if (LongitudeInput)
	{
		LongitudeInput->OnTextChanged.AddUniqueDynamic(this, &UGeographicTargetPanelWidget::OnInputTextChanged);
	}
	if (LatitudeInput)
	{
		LatitudeInput->OnTextChanged.AddUniqueDynamic(this, &UGeographicTargetPanelWidget::OnInputTextChanged);
	}
	if (AltitudeInput)
	{
		AltitudeInput->OnTextChanged.AddUniqueDynamic(this, &UGeographicTargetPanelWidget::OnInputTextChanged);
	}

	// Legacy SpinBox Blueprints use slider bounds only, so a typed out-of-range value remains
	// observable and can be rejected instead of silently being clamped to a different coordinate.
	if (LongitudeSpinBox)
	{
		LongitudeSpinBox->ClearMinValue();
		LongitudeSpinBox->ClearMaxValue();
		LongitudeSpinBox->SetMinSliderValue(-180.0f);
		LongitudeSpinBox->SetMaxSliderValue(180.0f);
		LongitudeSpinBox->SetMaxFractionalDigits(8);
	}
	if (LatitudeSpinBox)
	{
		LatitudeSpinBox->ClearMinValue();
		LatitudeSpinBox->ClearMaxValue();
		LatitudeSpinBox->SetMinSliderValue(-90.0f);
		LatitudeSpinBox->SetMaxSliderValue(90.0f);
		LatitudeSpinBox->SetMaxFractionalDigits(8);
	}
	if (AltitudeLabel && !CoordinateVectorInput)
	{
		AltitudeLabel->SetText(FText::FromString(TEXT("海拔（m，平均海平面）")));
	}

	PopulateCoordSystemComboBox();
	PopulateDispatchModeComboBox();
	UpdateDispatchModeVisibility();
	RefreshPerDroneTargetRows();
	SetExpanded(false);
	LastReadinessMessage.Reset();
	RefreshAvailability();
}

void UGeographicTargetPanelWidget::NativeDestruct()
{
	if (bExpanded)
	{
		bStaticPanelInteractive = false;
	}
	Super::NativeDestruct();
}

void UGeographicTargetPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	RefreshPerDroneTargetRows();
	RefreshAvailability();
}

void UGeographicTargetPanelWidget::SetExpanded(bool bInExpanded)
{
	bExpanded = bInExpanded;
	bStaticPanelInteractive = bInExpanded;
	if (PanelBody)
	{
		PanelBody->SetVisibility(bInExpanded ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UGeographicTargetPanelWidget::PopulateCoordSystemComboBox()
{
	if (!CoordSystemComboBox)
	{
		return;
	}
	CoordSystemComboBox->ClearOptions();
	CoordSystemComboBox->AddOption(TEXT("WGS84"));
	CoordSystemComboBox->SetSelectedOption(TEXT("WGS84"));
}

void UGeographicTargetPanelWidget::PopulateDispatchModeComboBox()
{
	if (!DispatchModeComboBox)
	{
		return;
	}
	DispatchModeComboBox->ClearOptions();
	DispatchModeComboBox->AddOption(TEXT("统一目标"));
	DispatchModeComboBox->AddOption(TEXT("逐机目标"));
	DispatchModeComboBox->SetSelectedOption(TEXT("统一目标"));
}

bool UGeographicTargetPanelWidget::IsPerDroneMode() const
{
	return DispatchModeComboBox && DispatchModeComboBox->GetSelectedOption() == TEXT("逐机目标");
}

void UGeographicTargetPanelWidget::UpdateDispatchModeVisibility()
{
	const bool bPerDrone = IsPerDroneMode();
	if (UniformTargetRow)
	{
		UniformTargetRow->SetVisibility(bPerDrone ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}
	if (PerDroneTargetsPanel)
	{
		PerDroneTargetsPanel->SetVisibility(bPerDrone ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UGeographicTargetPanelWidget::CachePerDroneInputs()
{
	for (const TPair<int32, TObjectPtr<UEditableTextBox>>& Pair : PerDroneInputs)
	{
		if (Pair.Value)
		{
			PerDroneInputCache.Add(Pair.Key, Pair.Value->GetText().ToString());
		}
	}
}

void UGeographicTargetPanelWidget::RefreshPerDroneTargetRows()
{
	if (!PerDroneTargetsBox || !WidgetTree)
	{
		return;
	}

	ADroneOpsPlayerController* Controller = GetDroneOpsController();
	const TArray<int32> SelectedIds = Controller
		? Controller->GetSelectedDroneIdsForDispatch()
		: TArray<int32>();
	if (SelectedIds == RenderedPerDroneIds)
	{
		return;
	}

	CachePerDroneInputs();
	PerDroneTargetsBox->ClearChildren();
	PerDroneInputs.Reset();
	RenderedPerDroneIds = SelectedIds;
	if (PerDroneTargetsScrollSize)
	{
		const float RowsHeight = FMath::Clamp(static_cast<float>(SelectedIds.Num()) * 46.0f, 70.0f, 190.0f);
		PerDroneTargetsScrollSize->SetHeightOverride(RowsHeight);
	}

	const FString SharedText = CoordinateVectorInput
		? CoordinateVectorInput->GetText().ToString()
		: FString();
	for (const int32 DroneId : SelectedIds)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		USizeBox* LabelSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		LabelSize->SetWidthOverride(104.0f);
		UTextBlock* Label = MakeText(WidgetTree, NAME_None, FString::Printf(TEXT("无人机 %d"), DroneId));
		StyleText(Label, 12, AccentText, true);
		LabelSize->AddChild(Label);
		Row->AddChildToHorizontalBox(LabelSize);

		UEditableTextBox* Input = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass());
		Input->SetHintText(FText::FromString(TEXT("(经度, 纬度, 海拔 m)")));
		StyleSingleLineInput(Input);
		if (const FString* CachedText = PerDroneInputCache.Find(DroneId))
		{
			Input->SetText(FText::FromString(*CachedText));
		}
		else if (!SharedText.TrimStartAndEnd().IsEmpty())
		{
			Input->SetText(FText::FromString(SharedText));
		}
		Input->OnTextChanged.AddUniqueDynamic(this, &UGeographicTargetPanelWidget::OnInputTextChanged);
		if (UHorizontalBoxSlot* InputSlot = Row->AddChildToHorizontalBox(Input))
		{
			InputSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			InputSlot->SetVerticalAlignment(VAlign_Center);
		}

		PerDroneInputs.Add(DroneId, Input);
		AddPaddedChild(PerDroneTargetsBox, Row, FMargin(0.0f, 3.0f));
	}
}

bool UGeographicTargetPanelWidget::TryReadTarget(
	double& OutLongitude,
	double& OutLatitude,
	double& OutAltitudeMsl,
	FString& OutError) const
{
	auto ParseValue = [&OutError](
		const UEditableTextBox* TextInput,
		const USpinBox* SpinInput,
		const TCHAR* FieldName,
		double& OutValue)
	{
		if (TextInput)
		{
			const FString Text = TextInput->GetText().ToString().TrimStartAndEnd();
			if (Text.IsEmpty() || !FDefaultValueHelper::ParseDouble(Text, OutValue))
			{
				OutError = FString::Printf(TEXT("请输入有效的%s"), FieldName);
				return false;
			}
		}
		else if (SpinInput)
		{
			OutValue = static_cast<double>(SpinInput->GetValue());
		}
		else
		{
			OutError = FString::Printf(TEXT("%s输入控件未配置"), FieldName);
			return false;
		}

		if (!FMath::IsFinite(OutValue))
		{
			OutError = FString::Printf(TEXT("请输入有效的%s"), FieldName);
			return false;
		}
		return true;
	};

	if (CoordSystemComboBox && CoordSystemComboBox->GetSelectedOption() != TEXT("WGS84"))
	{
		OutError = TEXT("当前仅支持 WGS84 坐标系");
		return false;
	}
	if (CoordinateVectorInput)
	{
		FGeographicCoordinate3D Coordinate;
		if (!FGeographicCoordinateTextParser::Parse(
			CoordinateVectorInput->GetText().ToString(), Coordinate, OutError))
		{
			return false;
		}
		OutLongitude = Coordinate.Longitude;
		OutLatitude = Coordinate.Latitude;
		OutAltitudeMsl = Coordinate.AltitudeMslMeters;
		return true;
	}
	if (!ParseValue(LongitudeInput, LongitudeSpinBox, TEXT("经度"), OutLongitude)
		|| !ParseValue(LatitudeInput, LatitudeSpinBox, TEXT("纬度"), OutLatitude)
		|| !ParseValue(AltitudeInput, AltitudeSpinBox, TEXT("海拔"), OutAltitudeMsl))
	{
		return false;
	}
	if (OutLongitude < -180.0 || OutLongitude > 180.0)
	{
		OutError = TEXT("经度范围应为 [-180, 180]");
		return false;
	}
	if (OutLatitude < -90.0 || OutLatitude > 90.0)
	{
		OutError = TEXT("纬度范围应为 [-90, 90]");
		return false;
	}
	return true;
}

bool UGeographicTargetPanelWidget::TryReadPerDroneTargets(
	TArray<FDroneGeographicTarget>& OutTargets,
	FString& OutError) const
{
	OutTargets.Reset();
	if (CoordSystemComboBox && CoordSystemComboBox->GetSelectedOption() != TEXT("WGS84"))
	{
		OutError = TEXT("当前仅支持 WGS84 坐标系");
		return false;
	}
	if (RenderedPerDroneIds.IsEmpty())
	{
		OutError = TEXT("请先选择无人机");
		return false;
	}

	for (const int32 DroneId : RenderedPerDroneIds)
	{
		const TObjectPtr<UEditableTextBox>* Input = PerDroneInputs.Find(DroneId);
		if (!Input || !Input->Get())
		{
			OutError = FString::Printf(TEXT("无人机 %d 的坐标输入控件不可用"), DroneId);
			return false;
		}

		FDroneGeographicTarget Target;
		Target.DroneId = DroneId;
		FString ParseError;
		if (!FGeographicCoordinateTextParser::Parse(
			(*Input)->GetText().ToString(), Target.Coordinate, ParseError))
		{
			OutError = FString::Printf(TEXT("无人机 %d：%s"), DroneId, *ParseError);
			return false;
		}
		OutTargets.Add(Target);
	}
	return true;
}

void UGeographicTargetPanelWidget::RefreshAvailability()
{
	ADroneOpsPlayerController* Controller = GetDroneOpsController();
	const bool bShowingBatchPathTools = Controller && Controller->IsPathEditMode();
	if (PanelBody)
	{
		if (UCanvasPanelSlot* BodySlot = Cast<UCanvasPanelSlot>(PanelBody->Slot))
		{
			// Keep the normal dispatch panel compact; reserve the taller viewport only
			// for scrollable per-drone rows or the path-edit batch tool.
			float DesiredHeight = 465.0f;
			if (bShowingBatchPathTools)
			{
				DesiredHeight = 600.0f;
			}
			else if (IsPerDroneMode())
			{
				const float RowsHeight = FMath::Clamp(static_cast<float>(RenderedPerDroneIds.Num()) * 46.0f, 70.0f, 190.0f);
				DesiredHeight = FMath::Clamp(410.0f + RowsHeight, 480.0f, 600.0f);
			}
			else if (!StatusText || StatusText->GetText().IsEmpty())
			{
				DesiredHeight = 410.0f;
			}
			BodySlot->SetSize(FVector2D(600.0f, DesiredHeight));
		}
	}
	if (BatchPathSection)
	{
		BatchPathSection->SetVisibility(
			bShowingBatchPathTools
				? ESlateVisibility::Visible
				: ESlateVisibility::Collapsed);
	}
	if (BatchAddButton)
	{
		// Keep this operation clickable outside edit mode so it can explain its strict isolation.
		BatchAddButton->SetIsEnabled(Controller != nullptr);
	}
	const int32 SelectedCount = Controller ? Controller->GetSelectedDroneCountForDispatch() : 0;
	if (SelectedCountText)
	{
		SelectedCountText->SetText(FText::FromString(
			FString::Printf(TEXT("已选中无人机  ·  %d 架"), SelectedCount)));
	}

	bool bCanPreview = false;
	bool bCanDispatch = false;
	FString BlockingMessage;
	if (!Controller)
	{
		BlockingMessage = TEXT("控制器不可用");
	}
	else if (SelectedCount <= 0)
	{
		BlockingMessage = TEXT("请先选择无人机");
	}
	else if (IsPerDroneMode())
	{
		TArray<FDroneGeographicTarget> Targets;
		if (TryReadPerDroneTargets(Targets, BlockingMessage))
		{
			const FGeographicDispatchResult PreviewReadiness = Controller->ValidatePerDroneGeographicTargets(
				GetSelectedCoordinateSystem(), Targets, false);
			bCanPreview = PreviewReadiness.bSuccess;
			if (!bCanPreview)
			{
				BlockingMessage = PreviewReadiness.Message;
			}
			else if (Controller->IsPathEditMode())
			{
				BlockingMessage = TEXT("路径编辑模式暂不支持逐机独立目标");
			}
			else
			{
				bCanDispatch = true;
			}
		}
	}
	else
	{
		double Longitude = 0.0;
		double Latitude = 0.0;
		double AltitudeMsl = 0.0;
		if (!TryReadTarget(Longitude, Latitude, AltitudeMsl, BlockingMessage))
		{
			// The parse error is already suitable for display.
		}
		else
		{
			const EGeographicCoordinateSystem CoordinateSystem = GetSelectedCoordinateSystem();
			const FGeographicDispatchResult PreviewReadiness = Controller->ValidateGeographicTarget(
				CoordinateSystem, Longitude, Latitude, AltitudeMsl, false);
			bCanPreview = PreviewReadiness.bSuccess;
			if (!bCanPreview)
			{
				BlockingMessage = PreviewReadiness.Message;
			}
			else
			{
				// A local shadow-drone dispatch is always meaningful. Backend connection and GPS anchors
				// only decide whether the same target can also be sent right now, not whether the button works.
				bCanDispatch = true;
			}
		}
	}

	if (PreviewButton)
	{
		PreviewButton->SetIsEnabled(bCanPreview);
	}
	if (DispatchButton)
	{
		DispatchButton->SetIsEnabled(bCanDispatch);
	}
	if (FPlatformTime::Seconds() >= BatchStatusUntilSeconds)
	{
		SetReadinessMessage(BlockingMessage);
	}
}

void UGeographicTargetPanelWidget::SetStatusMessage(const FString& Message)
{
	if (StatusPanel)
	{
		StatusPanel->SetVisibility(Message.IsEmpty()
			? ESlateVisibility::Collapsed
			: ESlateVisibility::Visible);
	}
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Message));
	}
}

void UGeographicTargetPanelWidget::SetBatchStatusMessage(const FString& Message)
{
	LastReadinessMessage.Reset();
	BatchStatusUntilSeconds = FPlatformTime::Seconds() + 5.0;
	SetStatusMessage(Message);
}

void UGeographicTargetPanelWidget::SetReadinessMessage(const FString& Message)
{
	if (!Message.IsEmpty())
	{
		LastReadinessMessage = Message;
		SetStatusMessage(Message);
		return;
	}

	if (!LastReadinessMessage.IsEmpty() && StatusText
		&& StatusText->GetText().ToString() == LastReadinessMessage)
	{
		SetStatusMessage(TEXT(""));
	}
	LastReadinessMessage.Reset();
}

ADroneOpsPlayerController* UGeographicTargetPanelWidget::GetDroneOpsController() const
{
	return Cast<ADroneOpsPlayerController>(GetOwningPlayer());
}

EGeographicCoordinateSystem UGeographicTargetPanelWidget::GetSelectedCoordinateSystem() const
{
	// Only WGS84 is exposed today. Keeping this conversion in one place makes adding a second enum
	// value and combo-box option a local UI change later.
	return EGeographicCoordinateSystem::WGS84;
}

void UGeographicTargetPanelWidget::OnOpenButtonClicked()
{
	SetExpanded(!bExpanded);
}

void UGeographicTargetPanelWidget::OnPreviewButtonClicked()
{
	RunDispatch(true);
}

void UGeographicTargetPanelWidget::OnDispatchButtonClicked()
{
	RunDispatch(false);
}

void UGeographicTargetPanelWidget::OnBatchAddButtonClicked()
{
	RunBatchPathAdd();
}

void UGeographicTargetPanelWidget::OnInputTextChanged(const FText& Text)
{
	BatchStatusUntilSeconds = 0.0;
	CachePerDroneInputs();
	RefreshAvailability();
}

void UGeographicTargetPanelWidget::OnDispatchModeChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	UpdateDispatchModeVisibility();
	RefreshPerDroneTargetRows();
	if (IsPerDroneMode() && CoordinateVectorInput)
	{
		const FString SharedText = CoordinateVectorInput->GetText().ToString().TrimStartAndEnd();
		if (!SharedText.IsEmpty())
		{
			for (const TPair<int32, TObjectPtr<UEditableTextBox>>& Pair : PerDroneInputs)
			{
				if (Pair.Value && Pair.Value->GetText().IsEmpty())
				{
					Pair.Value->SetText(FText::FromString(SharedText));
				}
			}
		}
	}
	RefreshAvailability();
}

void UGeographicTargetPanelWidget::RunBatchPathAdd()
{
	ADroneOpsPlayerController* Controller = GetDroneOpsController();
	if (!Controller)
	{
		SetBatchStatusMessage(TEXT("控制器不可用"));
		return;
	}
	if (!Controller->IsPathEditMode())
	{
		SetBatchStatusMessage(TEXT("批量坐标仅用于路径编辑"));
		return;
	}
	if (!BatchCoordinatesInput)
	{
		SetBatchStatusMessage(TEXT("批量坐标输入控件未配置"));
		return;
	}

	TArray<FGeographicCoordinate3D> Coordinates;
	FString ParseError;
	if (!FGeographicCoordinateTextParser::ParseBatch(
		BatchCoordinatesInput->GetText().ToString(), Coordinates, ParseError))
	{
		SetBatchStatusMessage(ParseError);
		return;
	}

	const FGeographicDispatchResult Result = Controller->AddGeographicWaypointsInEditMode(
		GetSelectedCoordinateSystem(), Coordinates);
	SetBatchStatusMessage(Result.Message);
}

void UGeographicTargetPanelWidget::RunDispatch(bool bPreviewOnly)
{
	ADroneOpsPlayerController* Controller = GetDroneOpsController();
	if (!Controller)
	{
		SetStatusMessage(TEXT("控制器不可用"));
		return;
	}

	if (IsPerDroneMode())
	{
		TArray<FDroneGeographicTarget> Targets;
		FString InputError;
		if (!TryReadPerDroneTargets(Targets, InputError))
		{
			SetStatusMessage(InputError);
			return;
		}
		if (!bPreviewOnly && Controller->IsPathEditMode())
		{
			SetStatusMessage(TEXT("路径编辑模式暂不支持逐机独立目标"));
			return;
		}

		const FGeographicDispatchResult Result = Controller->DispatchPerDroneGeographicTargets(
			GetSelectedCoordinateSystem(), Targets, bPreviewOnly);
		LastReadinessMessage.Reset();
		SetStatusMessage(Result.Message);
		return;
	}

	double Longitude = 0.0;
	double Latitude = 0.0;
	double AltitudeMsl = 0.0;
	FString InputError;
	if (!TryReadTarget(Longitude, Latitude, AltitudeMsl, InputError))
	{
		SetStatusMessage(InputError);
		return;
	}

	// 编辑模式下：坐标输入不派发无人机飞过去，而是在所有在编路径上加一个航点。
	// 预览(bPreviewOnly)仍走常规坐标预览（画标记，不改路径）。
	if (!bPreviewOnly && Controller->IsPathEditMode())
	{
		const FGeographicDispatchResult EditResult = Controller->AddGeographicWaypointInEditMode(
			GetSelectedCoordinateSystem(), Longitude, Latitude, AltitudeMsl);
		LastReadinessMessage.Reset();
		SetStatusMessage(EditResult.Message);
		return;
	}

	const FGeographicDispatchResult Result = Controller->DispatchGeographicTarget(
		GetSelectedCoordinateSystem(), Longitude, Latitude, AltitudeMsl, bPreviewOnly);
	LastReadinessMessage.Reset();
	SetStatusMessage(Result.Message);
	// Input text is intentionally left untouched after preview or successful dispatch.
}
