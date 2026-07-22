// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DroneVideoWindowManager.generated.h"

class SWindow;
class UDroneVideoWindowWidget;

/** Broadcast (native) whenever a drone's window is fully closed, from ANY path. Carries the DroneId. */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnVideoWindowClosedNative, int32 /*DroneId*/);

/**
 * Owns and manages the set of OS-level (SWindow) drone video windows for the
 * preview level. Held by a strong UPROPERTY on ADroneOpsPlayerController so this
 * UObject and every UUserWidget it references are kept alive against GC for as
 * long as the controller lives; on level leave / EndPlay the controller calls
 * CloseAllVideoWindows().
 *
 * Invariants:
 *  - At most one window per DroneId. OpenVideoWindow on an already-open drone
 *    only activates and brings its existing window to front.
 *  - At most MaxConcurrentWindows (4) windows at once; further opens are refused.
 *  - Every close path — the native window's close box, the widget's UMG close
 *    button, an un-check request, or level teardown — funnels into the single
 *    CloseVideoWindow(DroneId). Cleanup order there is fixed: stop video
 *    (about:blank) -> unbind callbacks -> destroy SWindow -> remove record.
 *
 * The manager never touches DroneBackend: OpenVideoWindow receives the cached
 * name + video_url from the caller (which reads the isolated drone registry).
 */
UCLASS()
class UE5DRONECONTROL_API UDroneVideoWindowManager : public UObject
{
	GENERATED_BODY()

public:
	/** Max simultaneous video windows for the initial version. */
	static constexpr int32 MaxConcurrentWindows = 4;

	/**
	 * Open (or re-focus) the video window for a drone.
	 * @return true if a window is open for DroneId after the call (newly created
	 *         or already existed and was brought to front); false if refused
	 *         (empty url / at cap / bad params). OutError carries a user-facing
	 *         reason on refusal.
	 */
	bool OpenVideoWindow(int32 DroneId, const FString& DroneName, const FString& VideoUrl, FString& OutError);

	/** True if a live window exists for this drone. */
	bool HasWindow(int32 DroneId) const;

	/** Number of currently open video windows. */
	int32 GetOpenWindowCount() const { return Windows.Num(); }

	/** The single cleanup entry point for one drone's window. Safe if none exists. */
	void CloseVideoWindow(int32 DroneId);

	/** Close every open video window (level leave, global stop, shutdown). */
	void CloseAllVideoWindows();

	/** Native/OS-level fullscreen toggle for a drone's own window, on its current monitor. */
	void ToggleFullscreen(int32 DroneId);

	/** Widget class used for window content; set by the controller from a BP class. */
	void SetWindowContentClass(TSubclassOf<UDroneVideoWindowWidget> InClass) { WindowContentClass = InClass; }

	/** Fires after a window is closed via any path (native X, UMG close, uncheck, teardown). */
	FOnVideoWindowClosedNative OnWindowClosed;

private:
	struct FVideoWindow
	{
		TSharedPtr<SWindow> Window;
		// Strong ref: keeps the UUserWidget alive alongside its Slate content.
		TObjectPtr<UDroneVideoWindowWidget> Widget = nullptr;

		// Pre-fullscreen native geometry, captured on the way into fullscreen so
		// restore returns to the exact screen/size the user had.
		bool bIsFullscreen = false;
		FVector2D RestoreScreenPosition = FVector2D::ZeroVector;
		FVector2D RestoreSize = FVector2D::ZeroVector;
	};

	/** Records for open windows. Both the SWindow and the widget are strong-held. */
	TMap<int32, TSharedRef<FVideoWindow>> Windows;

	UPROPERTY(Transient)
	TSubclassOf<UDroneVideoWindowWidget> WindowContentClass;

	/** Keeps every live content widget rooted so none is GC'd while its window lives. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDroneVideoWindowWidget>> LiveWidgets;

	void HandleWidgetCloseRequested(int32 DroneId);
	void HandleWidgetFullscreenToggle(int32 DroneId);
	void HandleNativeWindowClosed(const TSharedRef<SWindow>& ClosedWindow, int32 DroneId);
};
