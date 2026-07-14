// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DroneOps/Core/GeographicTypes.h"
#include "GeographicTargetPanelWidget.generated.h"

class UButton;
class UComboBoxString;
class UEditableTextBox;
class USpinBox;
class UTextBlock;
class UWidget;

/**
 * WBP_GeographicTargetPanel backing class.
 *
 * Lets the user enter a WGS84 longitude / latitude / altitude (MSL) target and dispatch it to the
 * currently selected drone(s). The primary drone is placed exactly at the point; the rest keep the
 * existing 1 m square spiral formation. Preview draws markers without sending; dispatch sends exactly
 * one logical command per drone.
 *
 * Expected (optional) bound widgets in the Blueprint:
 *   OpenButton         (UButton)         — toggles the panel body open/closed (coordinate icon button)
 *   PanelBody          (UWidget)         — container shown/hidden by OpenButton
 *   CoordSystemComboBox(UComboBoxString) — coordinate system selector (WGS84 only for now)
 *   LongitudeInput     (UEditableTextBox)— double-precision longitude input, range [-180, 180]
 *   LatitudeInput      (UEditableTextBox)— double-precision latitude input, range [-90, 90]
 *   AltitudeInput      (UEditableTextBox)— double-precision altitude input (m, MSL)
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

	/** Preferred double-precision text inputs. */
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

	void BuildFallbackWidgetTree();
	void SetExpanded(bool bInExpanded);
	void PopulateCoordSystemComboBox();
	void RefreshAvailability();
	bool TryReadTarget(double& OutLongitude, double& OutLatitude, double& OutAltitudeMsl, FString& OutError) const;
	void SetStatusMessage(const FString& Message);
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
	void OnInputTextChanged(const FText& Text);
};
