// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/TimerHandle.h"
#include "DroneVideoWindowWidget.generated.h"

class UButton;
class UTextBlock;
class UWidget;
class UWebBrowser;

/**
 * Content widget for a single top-level drone video window.
 *
 * Unlike UDroneInfoPanelWidget (a viewport UMG overlay carrying 0.2 s telemetry),
 * this widget is designed to be hosted inside an OS-level SWindow via TakeWidget().
 * It owns only the WebBrowser video lifecycle plus name/id labels and the
 * close / fullscreen controls. It never queries DroneBackend; all context is
 * pushed in once from the cached drone registry.
 *
 * The SWindow itself is owned by UDroneVideoWindowManager. Button clicks are
 * surfaced as delegates carrying the bound DroneId so the manager can route
 * them to the single CloseVideoWindow(DroneId) / fullscreen path — the widget
 * never destroys or reshapes its own native window.
 */
UCLASS(Abstract, Blueprintable)
class UE5DRONECONTROL_API UDroneVideoWindowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Bind context and start loading the video page. VideoUrl is assumed non-empty (caller validates). */
	void SetDroneContext(int32 InDroneId, const FString& InDroneName, const FString& InVideoUrl);

	/** Stop and release the browser (navigate about:blank). Idempotent; called by the manager before window destroy. */
	void ShutdownWidget();

	/** Let the manager reflect the true fullscreen state into the button visuals after it reshapes the window. */
	void NotifyFullscreenChanged(bool bIsFullscreen);

	int32 GetBoundDroneId() const { return BoundDroneId; }

	/** Broadcast when the user asks to close (UMG close button). Carries the bound DroneId. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnVideoWindowCloseRequested, int32 /*DroneId*/);
	FOnVideoWindowCloseRequested OnCloseRequested;

	/** Broadcast when the user toggles fullscreen/restore. Carries the bound DroneId. */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnVideoWindowFullscreenToggle, int32 /*DroneId*/);
	FOnVideoWindowFullscreenToggle OnFullscreenToggleRequested;

	/** Retry the video connection from a Blueprint button, if bound directly. */
	UFUNCTION(BlueprintCallable, Category = "DroneVideo")
	void RetryVideo();

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional)) UWebBrowser* VideoBrowser = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UWidget* VideoStatusOverlay = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* VideoStatusText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* RetryVideoButton = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* CloseButton = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* FullscreenButton = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* RestoreButton = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* DroneNameText = nullptr;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* DroneIdText = nullptr;

	/** Seconds to wait for the video page to commit a navigation before showing a retriable error. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DroneVideo", meta = (ClampMin = "1.0"))
	float VideoLoadTimeoutSeconds = 10.0f;

private:
	UFUNCTION() void HandleCloseClicked();
	UFUNCTION() void HandleFullscreenClicked();
	UFUNCTION() void HandleRestoreClicked();
	UFUNCTION() void OnVideoUrlChanged(const FText& NewUrl);

	void StartVideo(bool bIsReconnect);
	void StopVideo();
	void SetVideoStatus(const FText& Status, bool bShowRetry);

	/** Watchdog: fired if the page never commits a navigation, so an unreachable url shows retry. */
	void HandleVideoLoadTimeout();

	int32 BoundDroneId = 0;
	FString VideoUrl;
	bool bShuttingDown = false;
	/** True once a real navigation committed; suppresses the load-timeout error. */
	bool bLoadConfirmed = false;
	FTimerHandle VideoLoadTimeoutHandle;
};
