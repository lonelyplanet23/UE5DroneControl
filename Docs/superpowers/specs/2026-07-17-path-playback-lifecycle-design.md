# 路径播放生命周期改进设计

日期：2026-07-17
分支：feature/path-playback-formation

## 背景

当前路径派发与播放存在三个问题：

1. **路径不自动清除**：派发后 spawn 的 `ADronePathActor`（含样条线、航点可视化）在播放结束后仍残留在场景中。
2. **无法终止循环播放**：循环播放（`bClosedLoop`）的 JSON 路径永不进入 `Completed` 状态；现有停止按钮只在"路径编辑模式"下可见，普通派发/JSON 播放无法从全局停止。
3. **在线真机未等待到位**：派发给真机时，影子机在收到后端 HTTP 响应后立即开始本地播放，而真机可能还在飞往起点的途中，导致预演与真机脱节。

## 相关现有架构

- **影子机**（`AMultiDroneCharacter` / SenderPawn）：本地预演表现，沿路径播放动画。`bFollowingMirror=true` 时每帧跟随镜像机位置。
- **镜像机 / 接收机**（`ARealTimeDroneReceiver`）：由真机遥测驱动。持有 `AnchorWorldLocation` 和 `bHasGpsAnchor`（收到 `power_on`/`reconnect` 事件带 GPS 锚点后为 true）。
- **派发流程**：`USequenceDispatchPanelWidget::StartShadowDronePlayback()`（[SequenceDispatchPanelWidget.cpp:851](../../../Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.cpp)）为每架影子机 spawn `ADronePathActor` 存入 `ActiveDispatchPathActors`，并立即 `StartMovement()`。
- **集结机制（已存在）**：后端推 `assembling` 事件 → 影子机 `EnterAssemblyMode()`（跟随镜像机）；推 `assembly_complete` / `assembly_timeout` → `ExitAssemblyMode()`。网络管理器暴露 `OnAssemblyComplete` / `OnAssemblyTimeout` 委托（[DroneNetworkManager.h:170-176](../../../Source/UE5DroneControl/DroneOps/Network/DroneNetworkManager.h)）。当前 `StartShadowDronePlayback` 未接入这套机制。
- **JSON 文件播放**（`ADronePlaybackManager`）：已在 `Tick` 中检测全部路径 `Completed` 后自动 `StopPlayback()`（销毁自己的路径 Actor）。派发面板的路径 Actor 无此自清理。
- **可用性信号**：`Availability == Online` 来自后端 `GET /api/drones` 轮询的 `status` 或 WebSocket 遥测。`bHasGpsAnchor` 仅真机发来 GPS 锚点后为 true。

## 设计决策

### 判定"需要等待到位的在线真机"

一架无人机被视为"在线真机、需要等待到位"当且仅当：

- `Registry->GetTelemetry(DroneId)` 的 `Availability == EDroneAvailability::Online`，**且**
- `Registry->GetReceiverActor(DroneId)` 存在且为 `ARealTimeDroneReceiver`，其 `bHasGpsAnchor == true`。

理由：等待期间影子机跟随镜像机（真机），而镜像机只有拿到 GPS 锚点后才有真实世界位置可跟随；这也与现有派发锚点选择逻辑一致。任一条件不满足（离线 / mock 预演 / 无 GPS 锚点）即走"立即播放"路径。

### 问题2 与编辑停止按钮合并

原编辑模式的 `StopEditPathButton` 逻辑并入新的全局停止。全局停止会覆盖编辑模式清理，因此不再需要独立的编辑停止按钮。

## 方案设计

### 组件一：全局停止并清理辅助函数

在 `ADronePlaybackManager` 上新增静态方法：

```cpp
UFUNCTION(BlueprintCallable, Category = "Drone|Playback")
static void StopAndClearAllInWorld(UWorld* World);
```

行为（作用于传入 World）：

1. 遍历所有 `ADronePlaybackManager`，调用 `StopPlayback()`（其内部已销毁自己的路径 Actor）。
2. 遍历所有仍存在的 `ADronePathActor`，`StopMovement()` 后 `Destroy()`（清掉卡在循环中的路径及其可视化）。

此方法为统一入口，供下述所有停止场景复用，避免重复的世界清扫逻辑。

### 问题1：路径播放结束自动清除

- 在 `StartShadowDronePlayback()` 中，为每个 spawn 的 `ADronePathActor` 绑定其已有的 `OnExecutionStateChanged` 多播委托（[DronePathActor.h:144](../../../Source/UE5DroneControl/PathEditor/DronePathActor.h)）。
- 新增私有处理函数 `USequenceDispatchPanelWidget::OnDispatchPathExecutionStateChanged(ADronePathActor*, EDronePathExecutionState)`：当状态变为 `Completed` 时，从 `ActiveDispatchPathActors` 移除该 Actor 并 `Destroy()`。
- 循环路径永不 `Completed`，自然保留至全局停止——符合预期。
- JSON 文件播放已自清理，不改动。

绑定注意：`OnExecutionStateChanged` 是 `DECLARE_MULTICAST_DELEGATE`（非动态），用 `AddUObject` 绑定，`NativeDestruct` 中无需手动移除（Actor 销毁即失效），但为安全在移除/销毁 Actor 前保持委托一致。

### 问题2：全局停止按钮（派发序列按钮左边）

- 在 `USequenceDispatchPanelWidget` 头文件新增：
  ```cpp
  UPROPERTY(meta = (BindWidgetOptional))
  class UButton* GlobalStopButton;
  ```
- `NativeConstruct` 绑定 `OnClicked` → 新增 `UFUNCTION() void OnGlobalStopClicked()`。
- `OnGlobalStopClicked()` 逻辑：
  1. 调用 `ADronePlaybackManager::StopAndClearAllInWorld(GetWorld())`。
  2. 清空本面板 `ActiveDispatchPathActors`（Actor 已被上面销毁，仅清引用）。
  3. 若当前 `bInPathEditMode`，执行编辑清理（`PC->EndPathEditMode()` + `PC->ClearEditingPaths()`）并 `ResetPathEditState()`。
  4. 清空 `PendingRemappedMap` 及待启动缓存（见组件三）。
  5. 状态提示"已全局停止并清除所有路径"。
- **完全删除** `StopEditPathButton`：头文件 `BindWidgetOptional` 声明、`NativeConstruct` 中的 `OnClicked` 绑定、`OnStopEditPathClicked()` 函数声明与实现全部移除；`SetEditControlsVisible` 不再引用该按钮。
- 重构 `UDronePathPlaybackWidget::StopCurrentPlayback()` 改为调用同一静态辅助函数，去除其内部重复的世界扫描逻辑。

按钮布局位于 Widget Blueprint（`.uasset`），需用户在序列派发 WBP 中添加名为 `GlobalStopButton` 的 Button，置于 `OpenButton` 左侧。C++ 用 `BindWidgetOptional`，未添加控件也能编译。

### 问题3：在线真机等待到位后再动作

改造 `StartShadowDronePlayback()`，按上述"在线真机"判定分流：

- **离线 / mock / 无锚点**：维持现状——`Shadow->bFollowingMirror = false`，spawn 路径 Actor，`RefreshPath()` 后立即 `StartMovement(ShadowPawn)`。
- **在线真机**：spawn 路径 Actor 并 `RefreshPath()`，但**不** `StartMovement`；保持 `Shadow->bFollowingMirror = true`（影子机跟随真机飞向起点）。把 `{PathActor, ShadowPawn}` 存入新的成员 `TArray<FPendingArrivalPath> PendingArrivalPaths`。

新增成员结构（面板内部，无需 USTRUCT 反射，可用普通 struct 或两个平行数组；此处用轻量 struct）：

```cpp
struct FPendingArrivalPath
{
    TWeakObjectPtr<ADronePathActor> PathActor;
    TWeakObjectPtr<APawn> ShadowPawn;
};
TArray<FPendingArrivalPath> PendingArrivalPaths;
```

到位触发：

- `NativeConstruct` 中订阅 `NetMgr->OnAssemblyComplete`（`AddUObject`）与 `OnAssemblyTimeout`（兜底：真机长时间未到位也放行，避免永久卡住），保存 `FDelegateHandle`，`NativeDestruct` 中移除。
- 新增 `void OnAssemblyCompleteForDispatch(const FString& ArrayId)` 与超时同构处理：遍历 `PendingArrivalPaths`，对有效项：将 `ShadowPawn` 的 `bFollowingMirror = false`，调用 `PathActor->StartMovement(ShadowPawn)`。清空 `PendingArrivalPaths`。
- 注：现有 `AMultiDroneCharacter::OnAssemblyComplete` 会 `ExitAssemblyMode()`，与本面板处理并行，互不冲突；本面板只负责启动路径播放。

## 影响的文件

- `Source/UE5DroneControl/PathEditor/DronePlaybackManager.h` / `.cpp`：新增静态 `StopAndClearAllInWorld`。
- `Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.h` / `.cpp`：新增 `GlobalStopButton`、`OnGlobalStopClicked`、待启动缓存、集结事件订阅与处理、路径完成自清除；移除编辑停止按钮相关逻辑。
- `Source/UE5DroneControl/UI/DronePathPlaybackWidget.cpp`：`StopCurrentPlayback` 改用静态辅助函数。

## 蓝图侧改动（用户手动）

- 序列派发 WBP：新增 Button `GlobalStopButton`（放在 `OpenButton` 左侧）。
- 序列派发 WBP：删除旧的编辑停止按钮 `StopEditPathButton`（C++ 已不再引用该控件）。

## 测试与验证

UE C++ 无独立单元测试框架接入路径，验证以编译 + 运行时手动验证为主：

1. **编译**：通过 UBT/IDE 编译 `UE5DroneControl` 目标，确认无错误。
2. **问题1**：派发非循环路径 → 影子机走完 → 确认路径 Actor 与航点可视化自动消失。
3. **问题2**：派发循环路径 → 点全局停止 → 确认所有影子机停止、所有路径 Actor 消失；编辑模式下点全局停止 → 确认临时路径也清理。
4. **问题3**：
   - mock/离线：派发后影子机立即开始播放（现状不变）。
   - 在线真机（或 mock_server 模拟 `assembling` → `assembly_complete`）：派发后影子机先跟随镜像机，收到 `assembly_complete` 后才开始沿路径播放。
