# 路径播放生命周期改进 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 派发路径播放结束后自动清除；新增全局停止按钮取代编辑模式停止按钮；在线真机派发时等后端集结完成再启动本地播放。

**Architecture:** 在 `ADronePlaybackManager` 上新增静态方法 `StopAndClearAllInWorld` 作为唯一的全局停止入口，供派发面板全局停止按钮与 JSON 播放面板共用。派发面板 `USequenceDispatchPanelWidget` 绑定每个路径 Actor 的 `OnExecutionStateChanged` 实现完成自清除，按在线真机判定把播放分为"立即启动"与"等待集结完成启动"两路，并订阅 `OnAssemblyComplete`/`OnAssemblyTimeout` 放行等待中的路径。

**Tech Stack:** Unreal Engine 5 C++（UMG UserWidget、Actor、GameInstanceSubsystem、非动态多播委托 `AddUObject`）。

## Global Constraints

- 引擎/语言：UE5 C++，遵循现有 UPROPERTY/UFUNCTION 与命名风格。
- 无自动化测试框架接入路径；每个任务以"编译通过 + 运行时手动验证"为验收。
- 按钮布局位于 Widget Blueprint（`.uasset`），C++ 用 `BindWidgetOptional` 绑定；未添加控件也须编译通过。
- 编译方式：通过 Visual Studio 或 UBT 编译 `UE5DroneControl` 目标（`Development Editor` / `Win64`）。
- 提交信息尾部附加：`Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>`。
- "在线真机需等待到位"判定：`Registry->GetTelemetry(DroneId).Availability == EDroneAvailability::Online` **且** `Registry->GetReceiverActor(DroneId)` 为 `ARealTimeDroneReceiver` 且其 `bHasGpsAnchor == true`。

## 文件结构

- `Source/UE5DroneControl/PathEditor/DronePlaybackManager.h` / `.cpp`
  职责：新增静态 `StopAndClearAllInWorld(UWorld*)`——全局停止并清除所有 manager 与路径 Actor。
- `Source/UE5DroneControl/UI/DronePathPlaybackWidget.cpp`
  职责：`StopCurrentPlayback()` 改为委托给静态辅助函数，去除重复扫描。
- `Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.h` / `.cpp`
  职责：全局停止按钮 + 删除编辑停止按钮；路径完成自清除；在线真机等待到位分流；集结事件订阅与放行。

---

## Task 1: 新增全局停止辅助函数 `StopAndClearAllInWorld`

**Files:**
- Modify: `Source/UE5DroneControl/PathEditor/DronePlaybackManager.h`（在类 public 段声明）
- Modify: `Source/UE5DroneControl/PathEditor/DronePlaybackManager.cpp`（实现 + include）

**Interfaces:**
- Consumes: `ADronePlaybackManager::StopPlayback()`（已存在）、`ADronePathActor::StopMovement()`（已存在）。
- Produces: `static void ADronePlaybackManager::StopAndClearAllInWorld(UWorld* World);` —— 停掉 World 内所有 `ADronePlaybackManager`，并停止且销毁所有仍存在的 `ADronePathActor`。

- [ ] **Step 1: 在头文件声明静态方法**

在 `DronePlaybackManager.h` 中 `EnsurePlaybackDrones` 声明之后（约第 81 行后）加入：

```cpp
	/**
	 * 全局停止：停掉 World 内所有 ADronePlaybackManager 的播放，并停止且销毁
	 * 所有仍存在的 ADronePathActor（含卡在循环中的路径及其可视化）。
	 * 作为统一的全局停止入口，供派发面板全局停止按钮与 JSON 播放面板共用。
	 */
	UFUNCTION(BlueprintCallable, Category = "Drone|Playback")
	static void StopAndClearAllInWorld(UWorld* World);
```

- [ ] **Step 2: 在 .cpp 实现（EngineUtils.h 已 include）**

`DronePlaybackManager.cpp` 顶部已包含 `#include "EngineUtils.h"`、`#include "DronePathActor.h"`、`#include "Engine/World.h"`，无需新增 include。在文件末尾追加：

```cpp
void ADronePlaybackManager::StopAndClearAllInWorld(UWorld* World)
{
	if (!IsValid(World))
	{
		return;
	}

	// 1. 停掉所有 playback manager（其 StopPlayback 会销毁自己的路径 Actor）。
	for (TActorIterator<ADronePlaybackManager> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			It->StopPlayback();
		}
	}

	// 2. 停止并销毁所有仍存在的路径 Actor（含循环路径、派发面板 spawn 的路径）。
	for (TActorIterator<ADronePathActor> It(World); It; ++It)
	{
		ADronePathActor* PathActor = *It;
		if (IsValid(PathActor))
		{
			PathActor->StopMovement();
			PathActor->Destroy();
		}
	}
}
```

- [ ] **Step 3: 编译验证**

Run: 通过 Visual Studio 编译 `UE5DroneControl`（Development Editor, Win64），或 UBT 命令行。
Expected: 编译成功，无错误。

- [ ] **Step 4: 提交**

```bash
git add Source/UE5DroneControl/PathEditor/DronePlaybackManager.h Source/UE5DroneControl/PathEditor/DronePlaybackManager.cpp
git commit -m "feat: 新增 StopAndClearAllInWorld 全局停止辅助函数

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 2: JSON 播放面板停止改用统一入口

**Files:**
- Modify: `Source/UE5DroneControl/UI/DronePathPlaybackWidget.cpp`（`StopCurrentPlayback` 实现，约第 133-170 行）

**Interfaces:**
- Consumes: `ADronePlaybackManager::StopAndClearAllInWorld(UWorld*)`（Task 1）。
- Produces: 无新对外接口；仅重构内部实现。

- [ ] **Step 1: 替换 `StopCurrentPlayback` 实现**

将 `DronePathPlaybackWidget.cpp` 中现有的 `void UDronePathPlaybackWidget::StopCurrentPlayback()` 整个函数体（第 133-170 行，从 `{` 到对应 `}`）替换为：

```cpp
void UDronePathPlaybackWidget::StopCurrentPlayback()
{
	// 统一走全局停止入口：停掉所有 manager 并销毁所有路径 Actor，
	// 覆盖"循环路径永不 Completed"与"播放/停止分属不同实例"的情况。
	ADronePlaybackManager::StopAndClearAllInWorld(GetWorld());
	SetStatusMessage(TEXT("Playback stopped"));
}
```

说明：`DronePathActor.h`、`DronePlaybackManager.h`、`EngineUtils.h`、`Engine/World.h` 已在本文件 include（第 1-15 行），无需改动 include。原实现中的 `PlaybackManager` 成员仍由其它函数使用，保留不动。

- [ ] **Step 2: 编译验证**

Run: 编译 `UE5DroneControl`。
Expected: 编译成功，无 "undeclared identifier" 等错误。

- [ ] **Step 3: 提交**

```bash
git add Source/UE5DroneControl/UI/DronePathPlaybackWidget.cpp
git commit -m "refactor: JSON 播放停止改用 StopAndClearAllInWorld

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 3: 删除派发面板编辑模式停止按钮 `StopEditPathButton`

**Files:**
- Modify: `Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.h`（删除声明）
- Modify: `Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.cpp`（删除绑定、实现、`SetEditControlsVisible` 中引用）

**Interfaces:**
- Consumes: 无。
- Produces: 无（纯删除）。此任务先删除旧按钮，Task 5 再引入全局停止按钮，避免两者混淆。

- [ ] **Step 1: 删除头文件声明**

在 `SequenceDispatchPanelWidget.h` 删除以下三行（约第 82-84 行）：

```cpp
	/** 停止按钮：终止所有影子机播放并销毁临时路径，仅编辑模式可见。 */
	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* StopEditPathButton;
```

同时删除 `OnStopEditPathClicked` 的声明（约第 185-187 行）：

```cpp
	// 停止按钮：终止影子机播放 + 销毁临时路径 + 复位
	UFUNCTION()
	void OnStopEditPathClicked();
```

- [ ] **Step 2: 删除 NativeConstruct 中的绑定**

在 `SequenceDispatchPanelWidget.cpp` `NativeConstruct` 中删除（约第 87-90 行）：

```cpp
	if (StopEditPathButton)
	{
		StopEditPathButton->OnClicked.AddDynamic(this, &USequenceDispatchPanelWidget::OnStopEditPathClicked);
	}
```

- [ ] **Step 3: 删除 `OnStopEditPathClicked` 实现**

删除整个函数（约第 1023-1037 行）：

```cpp
void USequenceDispatchPanelWidget::OnStopEditPathClicked()
{
	// 停止：终止所有影子机播放，销毁临时路径与缓存，并复位编辑状态。
	ClearActiveDispatchPaths();

	if (ADroneOpsPlayerController* PC = GetDroneOpsController())
	{
		PC->EndPathEditMode();
		PC->ClearEditingPaths();
	}

	bInPathEditMode = false;
	ResetPathEditState();
	SetStatusMessage(TEXT("已停止，临时路径已清理"));
}
```

- [ ] **Step 4: 从 `SetEditControlsVisible` 移除该按钮引用**

在 `SetEditControlsVisible` 中删除（约第 1057-1060 行）：

```cpp
	if (StopEditPathButton)
	{
		StopEditPathButton->SetVisibility(Vis);
	}
```

- [ ] **Step 5: 编译验证**

Run: 编译 `UE5DroneControl`。
Expected: 编译成功；无对 `StopEditPathButton` / `OnStopEditPathClicked` 的残留引用报错。

- [ ] **Step 6: 提交**

```bash
git add Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.h Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.cpp
git commit -m "refactor: 删除编辑模式停止按钮 StopEditPathButton

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 4: 派发路径播放结束自动清除

**Files:**
- Modify: `Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.h`（新增私有处理函数声明）
- Modify: `Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.cpp`（`StartShadowDronePlayback` 绑定委托 + 新增实现）

**Interfaces:**
- Consumes: `ADronePathActor::OnExecutionStateChanged`（`FOnDronePathExecutionStateChanged` = `DECLARE_MULTICAST_DELEGATE_TwoParams(ADronePathActor*, EDronePathExecutionState)`）；枚举值 `EDronePathExecutionState::Completed`；成员 `TArray<TObjectPtr<ADronePathActor>> ActiveDispatchPathActors`（已存在）。
- Produces: `void USequenceDispatchPanelWidget::OnDispatchPathExecutionStateChanged(ADronePathActor* PathActor, EDronePathExecutionState NewState);` —— 路径完成时销毁并从跟踪列表移除。

- [ ] **Step 1: 头文件声明处理函数**

在 `SequenceDispatchPanelWidget.h` 的 `StartShadowDronePlayback();` 声明之后（约第 139 行后）加入：

```cpp
	// 派发路径的执行状态变化回调：路径 Completed 时销毁该 Actor 并从跟踪列表移除。
	// 非动态多播委托，用 AddUObject 绑定。
	void OnDispatchPathExecutionStateChanged(ADronePathActor* PathActor, EDronePathExecutionState NewState);
```

同时确认头文件已能识别 `EDronePathExecutionState`：`SequenceDispatchPanelWidget.h` 已 `#include "PathEditor/DronePathSaveLibrary.h"`，但该枚举定义在 `PathEditor/DronePathActor.h`。为避免不完整类型，在头文件顶部 include 区加入：

```cpp
#include "PathEditor/DronePathActor.h"
```

（若已存在则跳过。）

- [ ] **Step 2: 在 `StartShadowDronePlayback` 中绑定委托**

在 `SequenceDispatchPanelWidget.cpp` 的 `StartShadowDronePlayback()` 内，`PathActor->StartMovement(ShadowPawn);` 之前、`ActiveDispatchPathActors.Add(PathActor);` 附近，为路径 Actor 绑定完成回调。定位到（约第 914-917 行）：

```cpp
		PathActor->RefreshPath();
		PathActor->StartMovement(ShadowPawn);

		ActiveDispatchPathActors.Add(PathActor);
```

改为：

```cpp
		PathActor->RefreshPath();
		PathActor->OnExecutionStateChanged.AddUObject(
			this, &USequenceDispatchPanelWidget::OnDispatchPathExecutionStateChanged);
		PathActor->StartMovement(ShadowPawn);

		ActiveDispatchPathActors.Add(PathActor);
```

- [ ] **Step 3: 实现处理函数**

在 `SequenceDispatchPanelWidget.cpp` 中 `ClearActiveDispatchPaths()` 实现之后追加：

```cpp
void USequenceDispatchPanelWidget::OnDispatchPathExecutionStateChanged(ADronePathActor* PathActor, EDronePathExecutionState NewState)
{
	// 仅在路径播放完成时自动清除（循环路径永不 Completed，保留至全局停止）。
	if (NewState != EDronePathExecutionState::Completed || !IsValid(PathActor))
	{
		return;
	}

	ActiveDispatchPathActors.Remove(PathActor);
	PathActor->StopMovement();
	PathActor->Destroy();
}
```

- [ ] **Step 4: 编译验证**

Run: 编译 `UE5DroneControl`。
Expected: 编译成功。

- [ ] **Step 5: 运行时验证（手动）**

在编辑器中派发一条非循环路径，等影子机走到终点。
Expected: 路径样条线与航点可视化在到达终点后自动消失。

- [ ] **Step 6: 提交**

```bash
git add Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.h Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.cpp
git commit -m "feat: 派发路径播放结束后自动清除

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 5: 全局停止按钮

**Files:**
- Modify: `Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.h`（新增 `GlobalStopButton` + `OnGlobalStopClicked`）
- Modify: `Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.cpp`（绑定 + 实现）

**Interfaces:**
- Consumes: `ADronePlaybackManager::StopAndClearAllInWorld(UWorld*)`（Task 1）；成员 `ActiveDispatchPathActors`、`PendingRemappedMap`、`bInPathEditMode`；`GetDroneOpsController()`、`ResetPathEditState()`（已存在）。
- Produces: `UButton* GlobalStopButton`；`UFUNCTION() void OnGlobalStopClicked();`

- [ ] **Step 1: 头文件声明按钮与回调**

在 `SequenceDispatchPanelWidget.h` 的 `OpenButton` 声明之后（约第 34-35 行后）加入：

```cpp
	/** 全局停止按钮：始终可见，停止并清除全世界所有播放与路径 Actor。 */
	UPROPERTY(meta = (BindWidgetOptional))
	class UButton* GlobalStopButton;
```

在私有 `UFUNCTION()` 回调区（如 `OnOpenButtonClicked` 附近，约第 153-154 行后）加入：

```cpp
	UFUNCTION()
	void OnGlobalStopClicked();
```

- [ ] **Step 2: NativeConstruct 绑定**

在 `SequenceDispatchPanelWidget.cpp` `NativeConstruct` 中 `OpenButton` 绑定之后（约第 57 行后）加入：

```cpp
	if (GlobalStopButton)
	{
		GlobalStopButton->OnClicked.AddDynamic(this, &USequenceDispatchPanelWidget::OnGlobalStopClicked);
	}
```

- [ ] **Step 3: 实现 `OnGlobalStopClicked`**

在 `SequenceDispatchPanelWidget.cpp` 中 `ClearActiveDispatchPaths()` 实现之后追加（需确保 `#include "PathEditor/DronePlaybackManager.h"`，若无则在 include 区加入）：

```cpp
void USequenceDispatchPanelWidget::OnGlobalStopClicked()
{
	// 1. 全局停止并销毁所有 manager 与路径 Actor（含卡在循环中的 JSON 播放）。
	ADronePlaybackManager::StopAndClearAllInWorld(GetWorld());

	// 2. 本面板派发路径 Actor 已被上面销毁，仅清引用。
	ActiveDispatchPathActors.Empty();

	// 3. 若在编辑模式：清理临时路径并复位编辑状态与 UI。
	if (bInPathEditMode)
	{
		if (ADroneOpsPlayerController* PC = GetDroneOpsController())
		{
			PC->EndPathEditMode();
			PC->ClearEditingPaths();
		}
		ResetPathEditState();
	}

	// 4. 清空待发送缓存。（Task 6 会在此追加清空待启动列表 PendingArrivalPaths）
	PendingRemappedMap.Empty();

	SetStatusMessage(TEXT("已全局停止并清除所有路径"));
}
```

说明：`ResetPathEditState()` 内部已把 `bInPathEditMode` 置 false 并复位 Toggle/编辑控件；此处调用顺序安全。`ADroneOpsPlayerController` 头文件已在 .cpp include（第 16 行）。

- [ ] **Step 4: 编译验证**

Run: 编译 `UE5DroneControl`。
Expected: 编译成功。

- [ ] **Step 5: 蓝图接线（手动，编译后）**

在序列派发 WBP 中新增名为 `GlobalStopButton` 的 Button，放在 `OpenButton` 左侧。

- [ ] **Step 6: 运行时验证（手动）**

派发一条循环路径 → 点全局停止。
Expected: 所有影子机停止、所有路径 Actor 与可视化消失。编辑模式下点全局停止 → 临时路径也被清理，编辑控件复位。

- [ ] **Step 7: 提交**

```bash
git add Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.h Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.cpp
git commit -m "feat: 新增全局停止按钮 GlobalStopButton

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Task 6: 在线真机等待集结完成后再启动播放

**Files:**
- Modify: `Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.h`（新增待启动缓存结构、成员、集结事件句柄与处理函数、`StartPendingArrivalPaths` 辅助）
- Modify: `Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.cpp`（`StartShadowDronePlayback` 分流、`NativeConstruct` 订阅、`NativeDestruct` 退订、新增实现、include）

**Interfaces:**
- Consumes: `UDroneNetworkManager::OnAssemblyComplete`（`DECLARE_MULTICAST_DELEGATE_OneParam(const FString&)`）与 `OnAssemblyTimeout`（`DECLARE_MULTICAST_DELEGATE_ThreeParams(const FString&, int32, int32)`）；`UDroneRegistrySubsystem::GetTelemetry(int32, FDroneTelemetrySnapshot&)`、`GetReceiverActor(int32)`、`GetSenderPawn(int32)`；`ARealTimeDroneReceiver::bHasGpsAnchor`；`AMultiDroneCharacter::bFollowingMirror`；`ADronePathActor::StartMovement(AActor*)`、`OnExecutionStateChanged`（Task 4 已绑定路径需保持一致）。
- Produces: 面板内部结构 `FPendingArrivalPath`；成员 `TArray<FPendingArrivalPath> PendingArrivalPaths`、`FDelegateHandle AssemblyCompleteHandle`、`FDelegateHandle AssemblyTimeoutHandle`；`void OnAssemblyCompleteForDispatch(const FString&)`、`void OnAssemblyTimeoutForDispatch(const FString&, int32, int32)`、`void StartPendingArrivalPaths()`、`bool IsOnlineRealDrone(int32 DroneId) const`。

- [ ] **Step 1: 头文件新增结构、成员与声明**

在 `SequenceDispatchPanelWidget.h` 类的 private 段（`ActiveDispatchPathActors` 声明附近，约第 132-133 行后）加入：

```cpp
	// 在线真机派发：spawn 但未启动的路径，等后端 assembly_complete 后再启动。
	struct FPendingArrivalPath
	{
		TWeakObjectPtr<ADronePathActor> PathActor;
		TWeakObjectPtr<APawn> ShadowPawn;
	};
	TArray<FPendingArrivalPath> PendingArrivalPaths;

	// 集结事件订阅句柄（NativeDestruct 中移除）
	FDelegateHandle AssemblyCompleteHandle;
	FDelegateHandle AssemblyTimeoutHandle;
```

在私有方法声明区（`StartShadowDronePlayback();` 附近）加入：

```cpp
	// 判定一架无人机是否为"在线真机、需等待到位"：Online 且镜像机已有 GPS 锚点。
	bool IsOnlineRealDrone(int32 DroneId) const;

	// 启动所有待到位路径（解除跟随镜像 + StartMovement），并清空待启动列表。
	void StartPendingArrivalPaths();

	// 后端集结完成 / 超时：放行等待中的路径。
	void OnAssemblyCompleteForDispatch(const FString& ArrayId);
	void OnAssemblyTimeoutForDispatch(const FString& ArrayId, int32 ReadyCount, int32 TotalCount);
```

- [ ] **Step 2: .cpp 新增 include**

在 `SequenceDispatchPanelWidget.cpp` 顶部 include 区加入（`RealTimeDroneReceiver.h` 已在第 7 行 include；确认以下存在，缺则补）：

```cpp
#include "PathEditor/DronePlaybackManager.h"
```

（`DroneNetworkManager.h`、`DroneRegistrySubsystem.h`、`RealTimeDroneReceiver.h`、`MultiDroneCharacter.h`、`PathEditor/DronePathActor.h` 均已 include。）

- [ ] **Step 3: `NativeConstruct` 订阅集结事件**

在 `NativeConstruct` 中已有的 `NetMgr->OnAssignmentResult.AddUObject(...)` 订阅块内（约第 99-106 行，取得 `NetMgr` 的同一作用域）追加：

```cpp
			AssemblyCompleteHandle = NetMgr->OnAssemblyComplete.AddUObject(
				this, &USequenceDispatchPanelWidget::OnAssemblyCompleteForDispatch);
			AssemblyTimeoutHandle = NetMgr->OnAssemblyTimeout.AddUObject(
				this, &USequenceDispatchPanelWidget::OnAssemblyTimeoutForDispatch);
```

- [ ] **Step 4: `NativeDestruct` 退订**

在 `NativeDestruct` 中已有的移除 `AssignmentResultHandle` 的块内（取得 `NetMgr` 的同一作用域，约第 126-135 行）追加：

```cpp
			if (AssemblyCompleteHandle.IsValid())
			{
				NetMgr->OnAssemblyComplete.Remove(AssemblyCompleteHandle);
				AssemblyCompleteHandle.Reset();
			}
			if (AssemblyTimeoutHandle.IsValid())
			{
				NetMgr->OnAssemblyTimeout.Remove(AssemblyTimeoutHandle);
				AssemblyTimeoutHandle.Reset();
			}
```

- [ ] **Step 5: `StartShadowDronePlayback` 按在线真机分流**

在 `StartShadowDronePlayback()` 中，将 Step（Task 4 已改过的）区块——从 `// 停止跟随镜像机` 到 `ActiveDispatchPathActors.Add(PathActor);`——替换为按在线真机分流的逻辑。定位当前代码（Task 4 后应为）：

```cpp
		// 停止跟随镜像机，否则 Tick 会每帧把影子机拉回镜像机位置
		if (AMultiDroneCharacter* Shadow = Cast<AMultiDroneCharacter>(ShadowPawn))
		{
			Shadow->bFollowingMirror = false;
		}

		// Spawn DronePathActor
```

替换为：

```cpp
		const bool bWaitForArrival = IsOnlineRealDrone(DroneId);

		// 离线 / mock：立即停止跟随镜像机；在线真机：保持跟随，等集结完成再解除。
		if (!bWaitForArrival)
		{
			if (AMultiDroneCharacter* Shadow = Cast<AMultiDroneCharacter>(ShadowPawn))
			{
				Shadow->bFollowingMirror = false;
			}
		}

		// Spawn DronePathActor
```

然后将该循环末尾（Task 4 后）：

```cpp
		PathActor->RefreshPath();
		PathActor->OnExecutionStateChanged.AddUObject(
			this, &USequenceDispatchPanelWidget::OnDispatchPathExecutionStateChanged);
		PathActor->StartMovement(ShadowPawn);

		ActiveDispatchPathActors.Add(PathActor);

		UE_LOG(LogTemp, Log, TEXT("[SequenceDispatch] Shadow drone DroneId=%d started playback along path %d"),
			DroneId, PathData.PathId);
```

替换为：

```cpp
		PathActor->RefreshPath();
		PathActor->OnExecutionStateChanged.AddUObject(
			this, &USequenceDispatchPanelWidget::OnDispatchPathExecutionStateChanged);

		ActiveDispatchPathActors.Add(PathActor);

		if (bWaitForArrival)
		{
			// 在线真机：先不启动，等 assembly_complete 放行。
			FPendingArrivalPath Pending;
			Pending.PathActor = PathActor;
			Pending.ShadowPawn = ShadowPawn;
			PendingArrivalPaths.Add(Pending);

			UE_LOG(LogTemp, Log, TEXT("[SequenceDispatch] Online drone DroneId=%d path %d spawned, waiting for assembly_complete"),
				DroneId, PathData.PathId);
		}
		else
		{
			PathActor->StartMovement(ShadowPawn);
			UE_LOG(LogTemp, Log, TEXT("[SequenceDispatch] Shadow drone DroneId=%d started playback along path %d"),
				DroneId, PathData.PathId);
		}
```

- [ ] **Step 6: 实现辅助与事件处理函数**

在 `SequenceDispatchPanelWidget.cpp` 中 `OnDispatchPathExecutionStateChanged`（Task 4）实现之后追加：

```cpp
bool USequenceDispatchPanelWidget::IsOnlineRealDrone(int32 DroneId) const
{
	UGameInstance* GI = GetGameInstance();
	UDroneRegistrySubsystem* Registry = GI ? GI->GetSubsystem<UDroneRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		return false;
	}

	FDroneTelemetrySnapshot Snap;
	if (!Registry->GetTelemetry(DroneId, Snap) || Snap.Availability != EDroneAvailability::Online)
	{
		return false;
	}

	// 镜像机存在且已拿到 GPS 锚点，才有真实位置可供影子机跟随等待。
	if (ARealTimeDroneReceiver* Receiver = Cast<ARealTimeDroneReceiver>(Registry->GetReceiverActor(DroneId)))
	{
		return Receiver->bHasGpsAnchor;
	}
	return false;
}

void USequenceDispatchPanelWidget::StartPendingArrivalPaths()
{
	for (const FPendingArrivalPath& Pending : PendingArrivalPaths)
	{
		ADronePathActor* PathActor = Pending.PathActor.Get();
		APawn* ShadowPawn = Pending.ShadowPawn.Get();
		if (!IsValid(PathActor) || !IsValid(ShadowPawn))
		{
			continue;
		}

		if (AMultiDroneCharacter* Shadow = Cast<AMultiDroneCharacter>(ShadowPawn))
		{
			Shadow->bFollowingMirror = false;
		}
		PathActor->StartMovement(ShadowPawn);
	}
	PendingArrivalPaths.Empty();
}

void USequenceDispatchPanelWidget::OnAssemblyCompleteForDispatch(const FString& ArrayId)
{
	if (PendingArrivalPaths.IsEmpty())
	{
		return;
	}
	StartPendingArrivalPaths();
	SetStatusMessage(TEXT("集结完成，真机已到位，开始沿路径播放"));
}

void USequenceDispatchPanelWidget::OnAssemblyTimeoutForDispatch(const FString& ArrayId, int32 ReadyCount, int32 TotalCount)
{
	if (PendingArrivalPaths.IsEmpty())
	{
		return;
	}
	// 兜底：集结超时也放行，避免路径永久卡在等待。
	StartPendingArrivalPaths();
	SetStatusMessage(FString::Printf(TEXT("集结超时（%d/%d），仍启动路径播放"), ReadyCount, TotalCount));
}
```

- [ ] **Step 7: 在全局停止中清空待启动列表**

在 Task 5 的 `OnGlobalStopClicked` 中，`PendingRemappedMap.Empty();` 之后加入一行，确保全局停止也清掉待启动缓存：

```cpp
	PendingArrivalPaths.Empty();
```

- [ ] **Step 8: 编译验证**

Run: 编译 `UE5DroneControl`。
Expected: 编译成功。

- [ ] **Step 9: 运行时验证（手动）**

- mock/离线：派发后影子机立即开始播放（现状不变）。
- 在线真机（或用 mock_server 依次推送 `assembling` → `assembly_complete`）：派发后影子机先跟随镜像机，收到 `assembly_complete` 后才开始沿路径播放；`assembly_timeout` 也能放行。

- [ ] **Step 10: 提交**

```bash
git add Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.h Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.cpp
git commit -m "feat: 在线真机等待集结完成后再启动本地播放

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## 验收总览

- **问题1**：Task 4 —— 非循环派发路径走完自动清除可视化。
- **问题2**：Task 1 + Task 3 + Task 5 —— 全局停止按钮停止并清除全世界播放/路径，取代编辑停止按钮；Task 2 让 JSON 播放停止共用同一入口。
- **问题3**：Task 6 —— 在线真机派发后等 `assembly_complete` 再启动；离线/mock 立即启动。
