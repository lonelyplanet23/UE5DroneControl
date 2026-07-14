// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/DroneInfoPanelWidget.h"

#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "WebBrowser.h"

namespace
{
	FText BoolText(bool bValue) { return bValue ? FText::FromString(TEXT("是")) : FText::FromString(TEXT("否")); }

	FText TaskStateText(EDroneTaskState State)
	{
		switch (State)
		{
		case EDroneTaskState::Moving: return FText::FromString(TEXT("移动中"));
		case EDroneTaskState::Assembling: return FText::FromString(TEXT("集结中"));
		case EDroneTaskState::Scouting: return FText::FromString(TEXT("侦察中"));
		case EDroneTaskState::Patrolling: return FText::FromString(TEXT("巡逻中"));
		case EDroneTaskState::Attacking: return FText::FromString(TEXT("执行攻击"));
		case EDroneTaskState::Paused: return FText::FromString(TEXT("已暂停"));
		case EDroneTaskState::Avoiding: return FText::FromString(TEXT("避障中"));
		case EDroneTaskState::Completed: return FText::FromString(TEXT("已完成"));
		case EDroneTaskState::Error: return FText::FromString(TEXT("异常"));
		default: return FText::FromString(TEXT("待命"));
		}
	}
}

void UDroneInfoPanelWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CloseButton) CloseButton->OnClicked.AddDynamic(this, &UDroneInfoPanelWidget::RequestClose);
	if (RetryVideoButton) RetryVideoButton->OnClicked.AddDynamic(this, &UDroneInfoPanelWidget::RetryVideo);
	if (VideoBrowser) VideoBrowser->OnUrlChanged.AddDynamic(this, &UDroneInfoPanelWidget::OnVideoUrlChanged);
}

void UDroneInfoPanelWidget::NativeDestruct()
{
	ShutdownPanel();
	OnPanelClosed.Broadcast();
	Super::NativeDestruct();
}

void UDroneInfoPanelWidget::SetDroneContext(int32 InDroneId, const FString& InVideoUrl)
{
	BoundDroneId = InDroneId;
	VideoUrl = InVideoUrl;
	bShuttingDown = false;
	StartVideo();
}

void UDroneInfoPanelWidget::UpdateFromSnapshot(const FDroneTelemetrySnapshot& Snapshot, const FString& InDroneName,
	const FDroneTaskStateSnapshot& TaskState)
{
	if (Snapshot.DroneId != BoundDroneId) return;

	SetText(DroneNameText, FText::FromString(InDroneName));
	SetText(DroneIdText, FText::FromString(FString::Printf(TEXT("DroneId: %d"), Snapshot.DroneId)));
	SetText(OnlineStatusText, AvailabilityText(Snapshot.Availability));
	SetText(TaskStatusText, FText::FromString(FString::Printf(TEXT("%s / %s"),
		*DroneCommandModeToDisplayText(TaskState.Mode).ToString(), *TaskStateText(TaskState.State).ToString())));

	// A failed cache read is represented by an offline snapshot without a
	// timestamp. Never replace an already displayed real sample with zeros.
	if (Snapshot.LastUpdateTime <= 0.0 && bHasSnapshot)
	{
		SetVideoStatus(FText::FromString(TEXT("已断联 / 视频中断")), !VideoUrl.IsEmpty());
		return;
	}

	bHasSnapshot = true;
	LastSnapshot = Snapshot;
	SetText(BatteryText, Snapshot.Battery >= 0 ? FText::FromString(FString::Printf(TEXT("%d%%"), Snapshot.Battery)) : FText::FromString(TEXT("--")));
	SetText(SpeedText, FText::FromString(FString::Printf(TEXT("%.1f m/s"), Snapshot.Velocity.Size())));
	SetText(LocationText, FText::FromString(FString::Printf(TEXT("%.6f, %.6f"), Snapshot.GeographicLocation.X, Snapshot.GeographicLocation.Y)));
	SetText(AltitudeText, FText::FromString(FString::Printf(TEXT("%.1f m"), Snapshot.Altitude)));
	SetText(AttitudeText, FText::FromString(FString::Printf(TEXT("航 %.1f  俯 %.1f  滚 %.1f"), Snapshot.Attitude.Yaw, Snapshot.Attitude.Pitch, Snapshot.Attitude.Roll)));
	SetText(ArmedText, BoolText(Snapshot.bArmed));
	SetText(OffboardText, BoolText(Snapshot.bOffboard));
	SetText(LastTelemetryText, Snapshot.LastUpdateTime > 0.0
		? FText::FromString(FString::Printf(TEXT("%.1f 秒前"), FMath::Max(0.0, FPlatformTime::Seconds() - Snapshot.LastUpdateTime)))
		: FText::FromString(TEXT("尚无遥测")));

	if (Snapshot.Availability != EDroneAvailability::Online)
	{
		SetVideoStatus(FText::FromString(TEXT("已断联 / 视频中断")), !VideoUrl.IsEmpty());
	}
}

void UDroneInfoPanelWidget::RetryVideo()
{
	if (!bShuttingDown) StartVideo(true);
}

void UDroneInfoPanelWidget::RequestClose()
{
	ShutdownPanel();
	RemoveFromParent();
}

void UDroneInfoPanelWidget::ShutdownPanel()
{
	if (bShuttingDown) return;
	bShuttingDown = true;
	StopVideo();
}

void UDroneInfoPanelWidget::StartVideo(bool bIsReconnect)
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
	SetVideoStatus(FText::FromString(bIsReconnect ? TEXT("正在重新连接视频…") : TEXT("正在加载视频…")), false);
	VideoBrowser->LoadURL(VideoUrl);
}

void UDroneInfoPanelWidget::StopVideo()
{
	if (VideoBrowser) VideoBrowser->LoadURL(TEXT("about:blank"));
}

void UDroneInfoPanelWidget::SetVideoStatus(const FText& Status, bool bShowRetry)
{
	SetText(VideoStatusText, Status);
	if (VideoStatusOverlay) VideoStatusOverlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	if (RetryVideoButton) RetryVideoButton->SetVisibility(bShowRetry ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UDroneInfoPanelWidget::OnVideoUrlChanged(const FText& NewUrl)
{
	if (!bShuttingDown && !VideoUrl.IsEmpty() && VideoStatusOverlay)
	{
		VideoStatusOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UDroneInfoPanelWidget::SetText(UTextBlock* TextBlock, const FText& Text) const
{
	if (TextBlock) TextBlock->SetText(Text);
}

FText UDroneInfoPanelWidget::AvailabilityText(EDroneAvailability Availability) const
{
	switch (Availability)
	{
	case EDroneAvailability::Online: return FText::FromString(TEXT("在线"));
	case EDroneAvailability::Lost: return FText::FromString(TEXT("已断联"));
	default: return FText::FromString(TEXT("离线"));
	}
}
