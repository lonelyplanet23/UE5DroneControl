// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/DroneVideoWindowWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "WebBrowser.h"

void UDroneVideoWindowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CloseButton) CloseButton->OnClicked.AddDynamic(this, &UDroneVideoWindowWidget::HandleCloseClicked);
	if (FullscreenButton) FullscreenButton->OnClicked.AddDynamic(this, &UDroneVideoWindowWidget::HandleFullscreenClicked);
	if (RestoreButton) RestoreButton->OnClicked.AddDynamic(this, &UDroneVideoWindowWidget::HandleRestoreClicked);
	if (RetryVideoButton) RetryVideoButton->OnClicked.AddDynamic(this, &UDroneVideoWindowWidget::RetryVideo);
	if (VideoBrowser) VideoBrowser->OnUrlChanged.AddDynamic(this, &UDroneVideoWindowWidget::OnVideoUrlChanged);
}

void UDroneVideoWindowWidget::NativeDestruct()
{
	// Safety net only. The manager always calls ShutdownWidget() before it
	// destroys the SWindow, so the browser is normally already released here.
	ShutdownWidget();
	Super::NativeDestruct();
}

void UDroneVideoWindowWidget::SetDroneContext(int32 InDroneId, const FString& InDroneName, const FString& InVideoUrl)
{
	BoundDroneId = InDroneId;
	VideoUrl = InVideoUrl;
	bShuttingDown = false;

	if (DroneNameText) DroneNameText->SetText(FText::FromString(InDroneName));
	if (DroneIdText) DroneIdText->SetText(FText::FromString(FString::Printf(TEXT("DroneId: %d"), InDroneId)));

	NotifyFullscreenChanged(false);
	StartVideo(false);
}

void UDroneVideoWindowWidget::ShutdownWidget()
{
	if (bShuttingDown) return;
	bShuttingDown = true;
	StopVideo();
}

void UDroneVideoWindowWidget::NotifyFullscreenChanged(bool bIsFullscreen)
{
	// Single toggle presentation: show whichever button applies, if both exist.
	if (FullscreenButton) FullscreenButton->SetVisibility(bIsFullscreen ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	if (RestoreButton) RestoreButton->SetVisibility(bIsFullscreen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UDroneVideoWindowWidget::RetryVideo()
{
	if (!bShuttingDown) StartVideo(true);
}

void UDroneVideoWindowWidget::HandleCloseClicked()
{
	OnCloseRequested.Broadcast(BoundDroneId);
}

void UDroneVideoWindowWidget::HandleFullscreenClicked()
{
	OnFullscreenToggleRequested.Broadcast(BoundDroneId);
}

void UDroneVideoWindowWidget::HandleRestoreClicked()
{
	OnFullscreenToggleRequested.Broadcast(BoundDroneId);
}

void UDroneVideoWindowWidget::StartVideo(bool bIsReconnect)
{
	if (VideoUrl.IsEmpty())
	{
		SetVideoStatus(FText::FromString(TEXT("无视频源")), false);
		return;
	}
	if (!VideoBrowser)
	{
		SetVideoStatus(FText::FromString(TEXT("视频控件未配置")), true);
		return;
	}

	// UWebBrowser only surfaces OnUrlChanged (no OnLoadError on the UMG wrapper), so an
	// unreachable / hanging MediaMTX page would otherwise sit on "正在加载…" forever with no
	// way to recover. Arm a watchdog: a committed navigation (OnVideoUrlChanged) confirms the
	// load and cancels it; if nothing commits in time we surface a retriable error state.
	bLoadConfirmed = false;
	SetVideoStatus(FText::FromString(bIsReconnect ? TEXT("正在重新连接视频…") : TEXT("正在加载视频…")), false);
	VideoBrowser->LoadURL(VideoUrl);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			VideoLoadTimeoutHandle, this, &UDroneVideoWindowWidget::HandleVideoLoadTimeout,
			FMath::Max(VideoLoadTimeoutSeconds, 1.0f), false);
	}
}

void UDroneVideoWindowWidget::StopVideo()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(VideoLoadTimeoutHandle);
	}
	if (VideoBrowser) VideoBrowser->LoadURL(TEXT("about:blank"));
}

void UDroneVideoWindowWidget::HandleVideoLoadTimeout()
{
	if (bShuttingDown || bLoadConfirmed)
	{
		return;
	}
	// The page never committed a navigation within the timeout — treat the source as
	// unreachable and offer a retry (RetryVideoButton -> RetryVideo -> StartVideo(reconnect)).
	SetVideoStatus(FText::FromString(TEXT("视频加载失败，请点“重试”")), true);
}

void UDroneVideoWindowWidget::SetVideoStatus(const FText& Status, bool bShowRetry)
{
	if (VideoStatusText) VideoStatusText->SetText(Status);
	if (VideoStatusOverlay) VideoStatusOverlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (RetryVideoButton) RetryVideoButton->SetVisibility(bShowRetry ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UDroneVideoWindowWidget::OnVideoUrlChanged(const FText& NewUrl)
{
	// about:blank during shutdown must not re-show or confirm anything.
	if (bShuttingDown || VideoUrl.IsEmpty())
	{
		return;
	}

	const FString UrlString = NewUrl.ToString();
	if (UrlString.Equals(TEXT("about:blank"), ESearchCase::IgnoreCase))
	{
		return;
	}

	// CEF reports load failures as an in-page error document (e.g. chrome-error://) rather
	// than through a delegate; treat those as a retriable error instead of hiding the overlay.
	if (UrlString.Contains(TEXT("chrome-error")) || UrlString.Contains(TEXT("chromewebdata")))
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(VideoLoadTimeoutHandle);
		}
		SetVideoStatus(FText::FromString(TEXT("视频加载失败，请点“重试”")), true);
		return;
	}

	// A real navigation committed: cancel the watchdog and reveal the video.
	bLoadConfirmed = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(VideoLoadTimeoutHandle);
	}
	if (VideoStatusOverlay)
	{
		VideoStatusOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
}
