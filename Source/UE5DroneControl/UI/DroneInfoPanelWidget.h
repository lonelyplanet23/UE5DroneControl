// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DroneOps/Core/DroneOpsTypes.h"
#include "DroneInfoPanelWidget.generated.h"

class UButton;
class UTextBlock;
class UWidget;
class UWebBrowser;

/**
 * Typed C++ base for WBP_DroneInfoPanel.
 *
 * The Blueprint is responsible only for presentation.  It may omit any named
 * BindWidget controls while it is being migrated; the data/video lifecycle is
 * still owned here and is never driven through reflected function names.
 */
UCLASS(Abstract, Blueprintable)
class UE5DRONECONTROL_API UDroneInfoPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetDroneContext(int32 InDroneId, const FString& InVideoUrl);
	void UpdateFromSnapshot(const FDroneTelemetrySnapshot& Snapshot, const FString& InDroneName,
		const FDroneTaskStateSnapshot& TaskState);
	void ShutdownPanel();

	/** Allows a Blueprint retry button to call the same native retry path. */
	UFUNCTION(BlueprintCallable, Category = "DroneInfo|Video")
	void RetryVideo();

	UFUNCTION(BlueprintCallable, Category = "DroneInfo")
	void RequestClose();

	DECLARE_MULTICAST_DELEGATE(FOnPanelClosed);
	FOnPanelClosed OnPanelClosed;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional)) UButton* CloseButton = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* RetryVideoButton = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UWebBrowser* VideoBrowser = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UWidget* VideoStatusOverlay = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* VideoStatusText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* DroneNameText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* DroneIdText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* OnlineStatusText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* TaskStatusText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* BatteryText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* SpeedText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* LocationText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* AltitudeText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* AttitudeText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* ArmedText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* OffboardText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* LastTelemetryText = nullptr;

private:
	void StartVideo(bool bIsReconnect = false);
	void StopVideo();
	void SetVideoStatus(const FText& Status, bool bShowRetry);
	UFUNCTION()
	void OnVideoUrlChanged(const FText& NewUrl);
	void SetText(UTextBlock* TextBlock, const FText& Text) const;
	FText AvailabilityText(EDroneAvailability Availability) const;

	int32 BoundDroneId = 0;
	FString VideoUrl;
	bool bShuttingDown = false;
	bool bHasSnapshot = false;
	FDroneTelemetrySnapshot LastSnapshot;
};
