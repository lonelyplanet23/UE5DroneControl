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
UTextBlock* MakeText(UWidgetTree* WidgetTree, const FName Name, const FString& Text)
{
	UTextBlock* TextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	TextBlock->SetText(FText::FromString(Text));
	return TextBlock;
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
	BodyBorder->SetBrushColor(FLinearColor(0.035f, 0.055f, 0.085f, 0.97f));
	BodyBorder->SetPadding(FMargin(20.0f, 18.0f));
	PanelBody = BodyBorder;
	if (UCanvasPanelSlot* BodySlot = RootCanvas->AddChildToCanvas(BodyBorder))
	{
		BodySlot->SetAnchors(FAnchors(1.0f, 0.0f));
		BodySlot->SetAlignment(FVector2D(1.0f, 0.0f));
		BodySlot->SetPosition(FVector2D(-28.0f, 84.0f));
		BodySlot->SetSize(FVector2D(470.0f, 430.0f));
	}

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
		UVerticalBox::StaticClass(), TEXT("PanelContent"));
	BodyBorder->AddChild(Content);
	UTextBlock* TitleText = MakeText(WidgetTree, TEXT("TitleText"), TEXT("WGS84 目标派发"));
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.93f, 0.97f, 1.0f, 1.0f)));
	AddPaddedChild(Content, TitleText, FMargin(0.0f, 0.0f, 0.0f, 2.0f));
	UTextBlock* SubtitleText = MakeText(WidgetTree, TEXT("SubtitleText"), TEXT("本地影子机立即执行；后端在线时同步发送指令"));
	SubtitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.50f, 0.72f, 0.88f, 1.0f)));
	SubtitleText->SetAutoWrapText(true);
	AddPaddedChild(Content, SubtitleText, FMargin(0.0f, 0.0f, 0.0f, 12.0f));

	auto AddInputRow = [this, Content](
		const FName RowName,
		const FName LabelName,
		const FString& Label,
		const FName InputName,
		const FString& Hint,
		TObjectPtr<UEditableTextBox>& OutInput,
		TObjectPtr<UTextBlock>* OutLabel = nullptr)
	{
		UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), RowName);
		UTextBlock* LabelText = MakeText(WidgetTree, LabelName, Label);
		if (OutLabel)
		{
			*OutLabel = LabelText;
		}

		LabelText->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.85f, 0.93f, 1.0f)));
		USizeBox* LabelSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		LabelSize->SetWidthOverride(150.0f);
		LabelSize->AddChild(LabelText);
		Row->AddChildToHorizontalBox(LabelSize);

		OutInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), InputName);
		OutInput->SetHintText(FText::FromString(Hint));
		OutInput->SetForegroundColor(FLinearColor(0.94f, 0.97f, 1.0f, 1.0f));
		if (UHorizontalBoxSlot* InputSlot = Row->AddChildToHorizontalBox(OutInput))
		{
			InputSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		AddPaddedChild(Content, Row, FMargin(0.0f, 3.0f));
	};

	UHorizontalBox* CoordRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("CoordinateSystemRow"));
	USizeBox* CoordLabelSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	CoordLabelSize->SetWidthOverride(150.0f);
	UTextBlock* CoordinateSystemLabel = MakeText(WidgetTree, TEXT("CoordinateSystemLabel"), TEXT("坐标系"));
	CoordinateSystemLabel->SetColorAndOpacity(FSlateColor(FLinearColor(0.78f, 0.85f, 0.93f, 1.0f)));
	CoordLabelSize->AddChild(CoordinateSystemLabel);
	CoordRow->AddChildToHorizontalBox(CoordLabelSize);
	CoordSystemComboBox = WidgetTree->ConstructWidget<UComboBoxString>(
		UComboBoxString::StaticClass(), TEXT("CoordSystemComboBox"));
	if (UHorizontalBoxSlot* ComboSlot = CoordRow->AddChildToHorizontalBox(CoordSystemComboBox))
	{
		ComboSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	}
	AddPaddedChild(Content, CoordRow, FMargin(0.0f, 3.0f));

	AddInputRow(TEXT("LongitudeRow"), TEXT("LongitudeLabel"), TEXT("经度（°）"),
		TEXT("LongitudeInput"), TEXT("-180 ~ 180"), LongitudeInput);
	AddInputRow(TEXT("LatitudeRow"), TEXT("LatitudeLabel"), TEXT("纬度（°）"),
		TEXT("LatitudeInput"), TEXT("-90 ~ 90"), LatitudeInput);
	AddInputRow(TEXT("AltitudeRow"), TEXT("AltitudeLabel"), TEXT("海拔（m，平均海平面）"),
		TEXT("AltitudeInput"), TEXT("米"), AltitudeInput, &AltitudeLabel);

	SelectedCountText = MakeText(WidgetTree, TEXT("SelectedCountText"), TEXT("已选中无人机：0"));
	SelectedCountText->SetColorAndOpacity(FSlateColor(FLinearColor(0.43f, 0.80f, 1.0f, 1.0f)));
	AddPaddedChild(Content, SelectedCountText, FMargin(0.0f, 12.0f, 0.0f, 6.0f));

	UHorizontalBox* ActionRow = WidgetTree->ConstructWidget<UHorizontalBox>(
		UHorizontalBox::StaticClass(), TEXT("ActionRow"));
	PreviewButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("PreviewButton"));
	PreviewButton->SetBackgroundColor(FLinearColor(0.11f, 0.30f, 0.43f, 1.0f));
	PreviewButton->AddChild(MakeText(WidgetTree, TEXT("PreviewButtonText"), TEXT("位置预览")));
	if (UHorizontalBoxSlot* PreviewSlot = ActionRow->AddChildToHorizontalBox(PreviewButton))
	{
		PreviewSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		PreviewSlot->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
	}
	DispatchButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("DispatchButton"));
	DispatchButton->SetBackgroundColor(FLinearColor(0.06f, 0.46f, 0.58f, 1.0f));
	DispatchButton->AddChild(MakeText(WidgetTree, TEXT("DispatchButtonText"), TEXT("派发")));
	if (UHorizontalBoxSlot* DispatchSlot = ActionRow->AddChildToHorizontalBox(DispatchButton))
	{
		DispatchSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		DispatchSlot->SetPadding(FMargin(4.0f, 0.0f, 0.0f, 0.0f));
	}
	AddPaddedChild(Content, ActionRow, FMargin(0.0f, 6.0f));

	StatusText = MakeText(WidgetTree, TEXT("StatusText"), TEXT("请先选择无人机"));
	StatusText->SetColorAndOpacity(FSlateColor(FLinearColor(0.70f, 0.78f, 0.86f, 1.0f)));
	StatusText->SetAutoWrapText(true);
	AddPaddedChild(Content, StatusText);
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
	if (AltitudeLabel)
	{
		AltitudeLabel->SetText(FText::FromString(TEXT("海拔（m，平均海平面）")));
	}

	PopulateCoordSystemComboBox();
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

void UGeographicTargetPanelWidget::RefreshAvailability()
{
	ADroneOpsPlayerController* Controller = GetDroneOpsController();
	const int32 SelectedCount = Controller ? Controller->GetSelectedDroneCountForDispatch() : 0;
	if (SelectedCountText)
	{
		SelectedCountText->SetText(FText::FromString(
			FString::Printf(TEXT("已选中无人机：%d"), SelectedCount)));
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
	SetReadinessMessage(BlockingMessage);
}

void UGeographicTargetPanelWidget::SetStatusMessage(const FString& Message)
{
	if (StatusText)
	{
		StatusText->SetText(FText::FromString(Message));
	}
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

void UGeographicTargetPanelWidget::OnInputTextChanged(const FText& Text)
{
	RefreshAvailability();
}

void UGeographicTargetPanelWidget::RunDispatch(bool bPreviewOnly)
{
	ADroneOpsPlayerController* Controller = GetDroneOpsController();
	if (!Controller)
	{
		SetStatusMessage(TEXT("控制器不可用"));
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

	const FGeographicDispatchResult Result = Controller->DispatchGeographicTarget(
		GetSelectedCoordinateSystem(), Longitude, Latitude, AltitudeMsl, bPreviewOnly);
	LastReadinessMessage.Reset();
	SetStatusMessage(Result.Message);
	// Input text is intentionally left untouched after preview or successful dispatch.
}
