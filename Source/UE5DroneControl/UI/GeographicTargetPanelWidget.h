// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DroneOps/Core/GeographicTypes.h"
#include "Types/SlateEnums.h"
#include "GeographicTargetPanelWidget.generated.h"

class UButton;
class UComboBoxString;
class UEditableTextBox;
class UMultiLineEditableTextBox;
class USizeBox;
class USpinBox;
class UTextBlock;
class UVerticalBox;
class UWidget;

/**
 * WBP_GeographicTargetPanel backing class.
 *
 * Supports either one copy-pasted (longitude, latitude, altitude MSL) vector shared by the selected
 * drones, or one exact vector per selected DroneId. Preview draws markers without sending; dispatch
 * sends exactly one logical command per drone.
 *
 * Expected (optional) bound widgets in the Blueprint:
 *   OpenButton         (UButton)         — toggles the panel body open/closed (coordinate icon button)
 *   PanelBody          (UWidget)         — container shown/hidden by OpenButton
 *   CoordSystemComboBox(UComboBoxString) — coordinate system selector (WGS84 only for now)
 *   DispatchModeComboBox(UComboBoxString)— shared formation target / exact per-drone targets
 *   CoordinateVectorInput(UEditableTextBox) — single-line (longitude, latitude, altitude MSL)
 *   PerDroneTargetsBox (UVerticalBox)    — dynamic rows sorted by selected DroneId
 *   BatchCoordinatesInput(UMultiLineEditableTextBox) — ordered tuple sequence for path editing only
 *   BatchAddButton     (UButton)         — atomically appends the batch to active editing paths
 *   PreviewButton      (UButton)         — "位置预览"
 *   DispatchButton     (UButton)         — "派发"
 *   SelectedCountText  (UTextBlock)      — shows current selected drone count
 *   StatusText         (UTextBlock)      — status / error / dispatch count message
 */
UCLASS()
class UE5DRONECONTROL_API UGeographicTargetPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> OpenButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> PanelBody;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> CoordSystemComboBox;

	/** "统一目标" keeps the stable square spiral; "逐机目标" uses exact per-DroneId coordinates. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> DispatchModeComboBox;

	/** Preferred single-line input: (longitude, latitude, altitude MSL). */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> CoordinateVectorInput;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> UniformTargetRow;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> PerDroneTargetsPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> PerDroneTargetsBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USizeBox> PerDroneTargetsScrollSize;

	/** Ordered tuple sequence used only to append local path-edit waypoints. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UMultiLineEditableTextBox> BatchCoordinatesInput;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BatchAddButton;

	/** Batch path card. It stays visible so non-edit attempts can show the isolation message. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> BatchPathSection;

	/** Legacy three-field Blueprint bindings retained for compatibility. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> LongitudeInput;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> LatitudeInput;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> AltitudeInput;

	/** Legacy Blueprint bindings retained for compatibility; text inputs take precedence. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USpinBox> LongitudeSpinBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USpinBox> LatitudeSpinBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<USpinBox> AltitudeSpinBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> PreviewButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> DispatchButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SelectedCountText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> StatusPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> AltitudeLabel;

	/** Returns true while the panel body is expanded (used to gate game clicks like the sequence panel). */
	static bool IsPanelInteractive() { return bStaticPanelInteractive; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FWgs84GeographicDispatchLatentCommand;
#endif

	static bool bStaticPanelInteractive;

	bool bExpanded = false;
	FString LastReadinessMessage;
	double BatchStatusUntilSeconds = 0.0;

	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UEditableTextBox>> PerDroneInputs;

	TMap<int32, FString> PerDroneInputCache;
	TArray<int32> RenderedPerDroneIds;

	void BuildFallbackWidgetTree();
	void SetExpanded(bool bInExpanded);
	void PopulateCoordSystemComboBox();
	void PopulateDispatchModeComboBox();
	bool IsPerDroneMode() const;
	void UpdateDispatchModeVisibility();
	void RefreshPerDroneTargetRows();
	void CachePerDroneInputs();
	void RefreshAvailability();
	bool TryReadTarget(double& OutLongitude, double& OutLatitude, double& OutAltitudeMsl, FString& OutError) const;
	bool TryReadPerDroneTargets(TArray<FDroneGeographicTarget>& OutTargets, FString& OutError) const;
	void SetStatusMessage(const FString& Message);
	void SetBatchStatusMessage(const FString& Message);
	void SetReadinessMessage(const FString& Message);

	class ADroneOpsPlayerController* GetDroneOpsController() const;
	void RunDispatch(bool bPreviewOnly);
	EGeographicCoordinateSystem GetSelectedCoordinateSystem() const;

	UFUNCTION()
	void OnOpenButtonClicked();

	UFUNCTION()
	void OnPreviewButtonClicked();

	UFUNCTION()
	void OnDispatchButtonClicked();

	UFUNCTION()
	void OnBatchAddButtonClicked();

	UFUNCTION()
	void OnInputTextChanged(const FText& Text);

	UFUNCTION()
	void OnDispatchModeChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	void RunBatchPathAdd();
};
