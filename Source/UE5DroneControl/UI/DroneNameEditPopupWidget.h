// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DroneNameEditPopupWidget.generated.h"

class UButton;
class UTextBlock;
class UEditableTextBox;
class USlider;

/**
 * 6 preset label colours (avoiding connection-status colours green/orange/red).
 * The first entry is the default panel body text colour.
 */
namespace DroneNameLabelPresets
{
	static const FLinearColor Colors[6] = {
		FLinearColor(0.86f, 0.91f, 0.97f, 1.0f),   // 0 默认: 面板正文白蓝
		FLinearColor(1.00f, 1.00f, 1.00f, 1.0f),   // 1 纯白
		FLinearColor(0.20f, 0.55f, 1.00f, 1.0f),   // 2 亮蓝
		FLinearColor(1.00f, 0.90f, 0.10f, 1.0f),   // 3 亮黄
		FLinearColor(0.15f, 0.95f, 0.55f, 1.0f),   // 4 亮绿
		FLinearColor(0.90f, 0.30f, 1.00f, 1.0f),   // 5 亮紫
	};
}

/**
 * Delegate broadcast when the user confirms the edited label settings.
 * DroneId identifies which drone was edited.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnLabelEditConfirmed,
	int32, DroneId,
	const FString&, NewName,
	FLinearColor, NewColor,
	int32, NewFontSize);

/**
 * Popup for editing a drone's top-label: name, colour (6 presets), and font size.
 * Shown when the user clicks the drone name in the sidebar panel.
 *
 * Layout (all built at runtime so no Blueprint asset is required):
 *   Title row
 *   Name input  (EditableTextBox)
 *   Colour row  (6 colour swatch buttons)
 *   Font-size row (label + value | slider)
 *   Confirm / Cancel buttons
 *
 * The popup does NOT auto-close on change – it stays open until the user
 * presses Confirm or Cancel.
 */
UCLASS()
class UE5DRONECONTROL_API UDroneNameEditPopupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Broadcast when Confirm is pressed. */
	UPROPERTY(BlueprintAssignable, Category = "DroneLabel")
	FOnLabelEditConfirmed OnConfirmed;

	/**
	 * Open the popup pre-filled with the drone's current settings.
	 * Call this right after creating the widget with CreateWidget<>.
	 */
	UFUNCTION(BlueprintCallable, Category = "DroneLabel")
	void InitPopup(int32 InDroneId, const FString& CurrentName,
		FLinearColor CurrentColor, int32 CurrentFontSize);

	/** Allow Blueprint subclass to access the bound drone id (read-only). */
	UFUNCTION(BlueprintPure, Category = "DroneLabel")
	int32 GetBoundDroneId() const { return BoundDroneId; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;

	// --- Bindable widgets (all optional; built at runtime if absent) ---

	UPROPERTY(meta = (BindWidgetOptional))
	UEditableTextBox* NameInput = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ConfirmButton = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* CancelButton = nullptr;

	/** Current font-size value display (read-only label next to the slider). */
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* FontSizeText = nullptr;

	/** Drag slider for font size (replaces the old +/- buttons). */
	UPROPERTY(meta = (BindWidgetOptional))
	USlider* FontSizeSlider = nullptr;

	/** Swatch buttons – index 0-5 matches DroneNameLabelPresets::Colors */
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ColorSwatch0 = nullptr;
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ColorSwatch1 = nullptr;
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ColorSwatch2 = nullptr;
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ColorSwatch3 = nullptr;
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ColorSwatch4 = nullptr;
	UPROPERTY(meta = (BindWidgetOptional))
	UButton* ColorSwatch5 = nullptr;

private:
	void BuildRuntimeWidgetTree();
	void UpdateFontSizeDisplay();
	void SelectColorByIndex(int32 Index);

	UFUNCTION() void HandleConfirmClicked();
	UFUNCTION() void HandleCancelClicked();
	UFUNCTION() void HandleFontSizeChanged(float Value);
	UFUNCTION() void HandleColor0();
	UFUNCTION() void HandleColor1();
	UFUNCTION() void HandleColor2();
	UFUNCTION() void HandleColor3();
	UFUNCTION() void HandleColor4();
	UFUNCTION() void HandleColor5();

	int32 BoundDroneId = 0;
	FLinearColor SelectedColor = DroneNameLabelPresets::Colors[0];
	int32 CurrentFontSize = 16;

	/** Highlight border for the active swatch (weak – cleared on each selection) */
	UButton* SwatchButtons[6] = {};
};
