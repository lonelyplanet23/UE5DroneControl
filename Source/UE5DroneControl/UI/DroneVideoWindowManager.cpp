// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/DroneVideoWindowManager.h"
#include "UI/DroneVideoWindowWidget.h"

#include "Blueprint/UserWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericApplication.h"
#include "Widgets/SWindow.h"

bool UDroneVideoWindowManager::OpenVideoWindow(int32 DroneId, const FString& DroneName, const FString& VideoUrl, FString& OutError)
{
	if (DroneId <= 0)
	{
		OutError = TEXT("无效的无人机");
		return false;
	}

	// Re-check on an already-open drone: only activate + bring its window to front.
	if (TSharedRef<FVideoWindow>* Existing = Windows.Find(DroneId))
	{
		if ((*Existing)->Window.IsValid())
		{
			(*Existing)->Window->BringToFront(true);
			(*Existing)->Window->ShowWindow();
			return true;
		}
		// Stale record with a dead window — drop it and fall through to recreate.
		Windows.Remove(DroneId);
	}

	if (VideoUrl.IsEmpty())
	{
		OutError = TEXT("该无人机未配置视频地址");
		return false;
	}

	if (Windows.Num() >= MaxConcurrentWindows)
	{
		OutError = FString::Printf(TEXT("最多同时播放 %d 路视频"), MaxConcurrentWindows);
		return false;
	}

	if (!WindowContentClass)
	{
		OutError = TEXT("视频窗口控件类未配置");
		return false;
	}

	if (!FSlateApplication::IsInitialized())
	{
		OutError = TEXT("Slate 未初始化，无法创建视频窗口");
		return false;
	}

	// Own the widget under the player controller so it has a valid world/owning
	// player; the manager is outered to that controller.
	APlayerController* OwningPC = Cast<APlayerController>(GetOuter());
	UDroneVideoWindowWidget* Widget = OwningPC
		? CreateWidget<UDroneVideoWindowWidget>(OwningPC, WindowContentClass)
		: CreateWidget<UDroneVideoWindowWidget>(GetWorld(), WindowContentClass);
	if (!Widget)
	{
		OutError = TEXT("无法创建视频窗口控件");
		return false;
	}

	TSharedRef<FVideoWindow> Record = MakeShared<FVideoWindow>();
	Record->Widget = Widget;
	LiveWidgets.Add(Widget); // keep rooted against GC for the window's lifetime

	const FText WindowTitle = FText::FromString(
		DroneName.IsEmpty()
			? FString::Printf(TEXT("无人机视频 - DroneId %d"), DroneId)
			: FString::Printf(TEXT("%s (DroneId %d)"), *DroneName, DroneId));

	TSharedRef<SWindow> NewWindow = SNew(SWindow)
		.Title(WindowTitle)
		.ClientSize(FVector2D(960.0f, 540.0f))
		.SupportsMaximize(true)
		.SupportsMinimize(true)
		.IsInitiallyMaximized(false);

	// TakeWidget() builds the Slate content that the SWindow hosts; the widget
	// object is separately strong-held in LiveWidgets to keep UObject and Slate
	// lifetimes consistent.
	NewWindow->SetContent(Widget->TakeWidget());

	// The OS title-bar close box must route into the same cleanup as everything
	// else. Bind before AddWindow; the lambda captures only DroneId + this.
	NewWindow->SetOnWindowClosed(FOnWindowClosed::CreateUObject(
		this, &UDroneVideoWindowManager::HandleNativeWindowClosed, DroneId));

	Record->Window = NewWindow;
	Windows.Add(DroneId, Record);

	// Widget-side controls (UMG close / fullscreen) funnel back to the manager.
	Widget->OnCloseRequested.AddUObject(this, &UDroneVideoWindowManager::HandleWidgetCloseRequested);
	Widget->OnFullscreenToggleRequested.AddUObject(this, &UDroneVideoWindowManager::HandleWidgetFullscreenToggle);

	FSlateApplication::Get().AddWindow(NewWindow);
	NewWindow->BringToFront(true);

	Widget->SetDroneContext(DroneId, DroneName, VideoUrl);
	return true;
}

bool UDroneVideoWindowManager::HasWindow(int32 DroneId) const
{
	const TSharedRef<FVideoWindow>* Found = Windows.Find(DroneId);
	return Found && (*Found)->Window.IsValid();
}

void UDroneVideoWindowManager::CloseVideoWindow(int32 DroneId)
{
	TSharedRef<FVideoWindow>* FoundPtr = Windows.Find(DroneId);
	if (!FoundPtr)
	{
		return;
	}

	// Take a local ref and remove the record up front so any re-entrant close
	// (native OnWindowClosed firing during RequestDestroyWindow) is a no-op.
	TSharedRef<FVideoWindow> Record = *FoundPtr;
	Windows.Remove(DroneId);

	// 1) Stop video / navigate about:blank.
	if (Record->Widget)
	{
		Record->Widget->ShutdownWidget();
	}

	// 2) Unbind callbacks (widget delegates + native window closed handler).
	if (Record->Widget)
	{
		Record->Widget->OnCloseRequested.RemoveAll(this);
		Record->Widget->OnFullscreenToggleRequested.RemoveAll(this);
	}
	if (Record->Window.IsValid())
	{
		Record->Window->SetOnWindowClosed(FOnWindowClosed());
	}

	// 3) Destroy the native window.
	if (Record->Window.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().RequestDestroyWindow(Record->Window.ToSharedRef());
	}
	Record->Window.Reset();

	// 4) Remove records / release the widget root so it can be GC'd.
	if (Record->Widget)
	{
		LiveWidgets.Remove(Record->Widget);
		Record->Widget = nullptr;
	}

	// 5) Tell listeners (the BP row's checkbox) this drone's window is gone, so it can
	//    reflect the closed state. Broadcast last, after the record is fully torn down.
	OnWindowClosed.Broadcast(DroneId);
}

void UDroneVideoWindowManager::CloseAllVideoWindows()
{
	TArray<int32> OpenIds;
	Windows.GetKeys(OpenIds);
	for (int32 DroneId : OpenIds)
	{
		CloseVideoWindow(DroneId);
	}
	Windows.Empty();
	LiveWidgets.Empty();
}

void UDroneVideoWindowManager::ToggleFullscreen(int32 DroneId)
{
	TSharedRef<FVideoWindow>* FoundPtr = Windows.Find(DroneId);
	if (!FoundPtr || !(*FoundPtr)->Window.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	TSharedRef<FVideoWindow> Record = *FoundPtr;
	const TSharedRef<SWindow> Window = Record->Window.ToSharedRef();

	if (!Record->bIsFullscreen)
	{
		// Capture the exact pre-fullscreen geometry so restore is faithful.
		Record->RestoreScreenPosition = Window->GetPositionInScreen();
		Record->RestoreSize = Window->GetSizeInScreen();

		// Find the monitor that currently contains the window's center and fill it.
		const FVector2D Center = Record->RestoreScreenPosition + Record->RestoreSize * 0.5f;

		FDisplayMetrics Metrics;
		FSlateApplication::Get().GetInitialDisplayMetrics(Metrics);

		FVector2D TargetPos(Metrics.PrimaryDisplayWorkAreaRect.Left, Metrics.PrimaryDisplayWorkAreaRect.Top);
		FVector2D TargetSize(
			Metrics.PrimaryDisplayWorkAreaRect.Right - Metrics.PrimaryDisplayWorkAreaRect.Left,
			Metrics.PrimaryDisplayWorkAreaRect.Bottom - Metrics.PrimaryDisplayWorkAreaRect.Top);

		for (const FMonitorInfo& Monitor : Metrics.MonitorInfo)
		{
			const FPlatformRect& R = Monitor.DisplayRect;
			if (Center.X >= R.Left && Center.X < R.Right && Center.Y >= R.Top && Center.Y < R.Bottom)
			{
				TargetPos = FVector2D(R.Left, R.Top);
				TargetSize = FVector2D(R.Right - R.Left, R.Bottom - R.Top);
				break;
			}
		}

		Window->ReshapeWindow(TargetPos, TargetSize);
		Record->bIsFullscreen = true;
	}
	else
	{
		// Restore to the pre-fullscreen screen position and size.
		if (Record->RestoreSize.X > 0.0f && Record->RestoreSize.Y > 0.0f)
		{
			Window->ReshapeWindow(Record->RestoreScreenPosition, Record->RestoreSize);
		}
		Record->bIsFullscreen = false;
	}

	Window->BringToFront(true);
	if (Record->Widget)
	{
		Record->Widget->NotifyFullscreenChanged(Record->bIsFullscreen);
	}
}

void UDroneVideoWindowManager::HandleWidgetCloseRequested(int32 DroneId)
{
	CloseVideoWindow(DroneId);
}

void UDroneVideoWindowManager::HandleWidgetFullscreenToggle(int32 DroneId)
{
	ToggleFullscreen(DroneId);
}

void UDroneVideoWindowManager::HandleNativeWindowClosed(const TSharedRef<SWindow>& ClosedWindow, int32 DroneId)
{
	// OS title-bar X (or Alt+F4). Funnel into the single cleanup path. If the
	// close originated from CloseVideoWindow itself, the record is already gone
	// and this is a no-op.
	CloseVideoWindow(DroneId);
}
