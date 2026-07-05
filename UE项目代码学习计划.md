# UE5DroneControl 项目代码学习计划

> 适用对象：刚开始接触 Unreal Engine、已有 C/C++ 基础、希望从本项目源码入手逐步掌握 UE 运行逻辑和无人机控制业务的同学。
>
> 建议节奏：6 到 8 周。每天 1 到 2 小时也可以推进；如果课程或比赛比较紧，可以先完成第 0 到第 4 阶段，后端和阵列系统后置。

---

## 0. 先建立全局地图

这个仓库不是单纯的 UE Demo，而是一个“UE 前端 + C++ 后端 + 无人机/Jetson/PX4 通信”的系统。不要一开始就扎进所有文件，先记住三条主线：

1. **UE 前端主线**
   - 入口：`Source/UE5DroneControl/`
   - 当前重点模块：`DroneOps`
   - 作用：显示无人机、选择无人机、编辑/下发任务、接收后端遥测、做 UI 交互。

2. **C++ 后端主线**
   - 入口：`BackEnd/main.cpp`
   - 作用：注册无人机、维护连接状态、HTTP/WS 与 UE 通信、UDP 与 Jetson/PX4 通信、执行集结/巡逻/侦察/攻击逻辑。

3. **旧链路与新链路**
   - 旧链路：UE 直接通过 UDP 收发无人机数据，核心文件是 `RealTimeDroneReceiver`、`UE5DroneControlCharacter`、部分 Python 脚本。
   - 新链路：UE 通过 HTTP + WebSocket 连接 C++ 后端，后端再通过 UDP 与 Jetson/PX4 通信。当前项目架构应优先理解这条线。

第一遍学习目标不是“每行都懂”，而是能画出：玩家点击地图后，目标点如何从 UE 传到后端，再变成无人机控制指令；无人机遥测如何从后端推回 UE，并驱动镜像机移动。

---

## 1. 推荐先读的文档

按这个顺序读，不要反过来：

| 顺序 | 文件 | 重点 |
|---|---|---|
| 1 | `README.md` | 项目目标、端口、运行方式、旧 UDP 链路和后端链路概览 |
| 2 | `架构设计.md` | 当前系统分层、UE/后端职责边界、HTTP/WS/UDP 数据流 |
| 3 | `接口与通讯数据规范.md` | HTTP、WebSocket、UDP 数据格式 |
| 4 | `坐标系转换说明.md` | UE 坐标、NED 坐标、GPS/Cesium 锚点的关系 |
| 5 | `前端开发文档.md` | UE 侧功能和 UI 约定 |
| 6 | `BackEnd/README.md` | 后端启动、接口和测试方式 |
| 7 | `BackEnd/TEST_GUIDE.md` | 不接真实无人机时如何用模拟数据验证 |

读文档时只做三件事：

- 标出所有端口：`8080`、`8081`、`8888/8889/8899` 等。
- 标出所有坐标单位：UE 是厘米，NED 是米，GPS 是经纬高。
- 标出数据方向：UE -> 后端、后端 -> UE、后端 -> Jetson、Jetson -> 后端。

---

## 2. 阶段 0：环境和 UE 基础概念

### 目标

能打开项目，知道 UE C++ 工程最基本的组成：模块、反射宏、Actor、Pawn、Controller、GameMode、Subsystem、Component、Widget。

### 先看代码

1. `UE5DroneControl.uproject`
   - 看启用的模块和插件，尤其是 Cesium、WebSocket、SimpleWebSocket 等。

2. `Source/UE5DroneControl/UE5DroneControl.Build.cs`
   - 重点看依赖模块：
     - `Core`、`CoreUObject`、`Engine`
     - `UMG`
     - `Sockets`、`Networking`
     - `HTTP`、`WebSockets`
     - `Json`、`JsonUtilities`
     - `CesiumRuntime`
   - 你要理解：这里决定了 C++ 代码可以 include 哪些 UE 模块。

3. `Source/UE5DroneControl/UE5DroneControl.cpp`
   - 这是 UE 模块注册入口。
   - 第一遍只要知道它告诉 UE：“这个游戏模块存在”。

### 必学 UE 概念

| 概念 | 本项目例子 | 你要理解的问题 |
|---|---|---|
| `AActor` | `ARealTimeDroneReceiver` | 场景中的一个对象如何生成、Tick、销毁 |
| `APawn` / `ACharacter` | `AMultiDroneCharacter`、`AUE5DroneControlCharacter` | 可以被控制/表现为无人机的对象 |
| `APlayerController` | `ADroneOpsPlayerController` | 玩家输入、鼠标点击、相机切换在哪里处理 |
| `AGameModeBase` | `ADroneOpsGameMode` | 关卡启动时谁负责初始化规则和生成对象 |
| `UActorComponent` | `UDroneTelemetryComponent` | 把“遥测”“选择”“发送命令”等能力挂到 Actor 上 |
| `UGameInstanceSubsystem` | `UDroneRegistrySubsystem`、`UDroneNetworkManager` | 跨关卡存在的全局状态/服务 |
| UMG Widget | `Source/UE5DroneControl/UI/` | UI 面板如何显示无人机状态和任务 |

### 阶段产出

在自己的笔记里写出：

```text
UE 项目启动后：
模块加载 -> GameMode BeginPlay -> 初始化 Subsystem -> 生成/绑定 Actor -> PlayerController 接管输入
```

---

## 3. 阶段 1：从 UE 运行入口读起

### 目标

弄清楚 DroneOps 关卡启动时发生了什么。

### 读代码顺序

1. `Source/UE5DroneControl/DroneOps/Control/DroneOpsGameMode.h`
2. `Source/UE5DroneControl/DroneOps/Control/DroneOpsGameMode.cpp`

重点函数：

| 函数 | 你要看什么 |
|---|---|
| `ADroneOpsGameMode::ADroneOpsGameMode()` | 默认 PlayerController、Pawn、镜像机/影子机类如何设置 |
| `PreInitializeComponents()` | 为什么要提前加载蓝图版 `BP_DroneOpsPlayerController` |
| `BeginPlay()` | 关卡开始后依次做了什么 |
| `InitializeCoordinateService()` | 选择 Cesium 坐标服务还是简单坐标服务 |
| `InitializeDroneRegistry()` | 注册表什么时候准备好 |
| `SpawnReceiversFromRegistry()` | 如何根据注册表自动生成无人机镜像 Actor |
| `FindReceiverForDroneId()` / `FindShadowForDroneId()` | DroneId 如何映射到场景 Actor |

### 阅读方法

第一遍不要追所有细节。只画一张流程：

```text
DroneOpsGameMode::BeginPlay
  -> ApplyCesiumTileServerConfig
  -> 加载 BP_RealTimeDrone / BP_MultiDroneCharacter
  -> InitializeCoordinateService
  -> ApplyPendingGeoreferenceOrigin
  -> InitializeDroneRegistry
  -> SpawnReceiversFromRegistry
  -> RetryPossessPlacedPawns
```

### 必须理解的 UE 逻辑

- `GameMode` 只在服务器/本地游戏规则层存在，负责“这个关卡怎么玩”。
- `PlayerController` 负责玩家输入，不应该把大量业务状态都塞进它。
- 蓝图类可以继承 C++ 类。项目里经常用 `TSoftClassPtr` 加载 `/Game/..._C` 蓝图类。
- `BeginPlay()` 是运行时逻辑的高频入口，读 UE 代码常常先搜它。

---

## 4. 阶段 2：读全局状态中心 Registry

### 目标

理解为什么项目需要一个 `UDroneRegistrySubsystem`，以及它如何把注册表、选择状态、遥测状态、控制锁串起来。

### 读代码顺序

1. `Source/UE5DroneControl/DroneOps/Core/DroneOpsTypes.h`
2. `Source/UE5DroneControl/DroneOps/Core/DroneRegistrySubsystem.h`
3. `Source/UE5DroneControl/DroneOps/Core/DroneRegistrySubsystem.cpp`

### 先看数据结构

`DroneOpsTypes.h` 是非常重要的“项目字典”：

| 类型 | 意义 |
|---|---|
| `EDroneCameraMode` | 跟随、自由、俯视相机 |
| `EDroneAvailability` | 在线、离线、失联 |
| `EDroneCommandMode` | 移动、侦察、巡逻、攻击 |
| `FDroneDescriptor` | 无人机静态信息：名字、ID、Slot、IP、端口、BitIndex |
| `FDroneTelemetrySnapshot` | 运行时遥测快照：位置、速度、姿态、状态 |
| `FDroneTargetCommand` | 点击目标点后形成的命令 |
| `FMultiDroneControlPacket` | 旧多机 UDP 32 字节包 |

### 再看 Registry 的职责

重点函数：

| 函数 | 说明 |
|---|---|
| `Initialize()` / `Deinitialize()` | Subsystem 生命周期 |
| `RegisterDrone()` | 注册/更新无人机描述符 |
| `SaveRegisteredDrones()` / `LoadRegisteredDrones()` | 持久化到 `Saved/DroneRegistry.json` |
| `UpdateTelemetry()` | 收到遥测后更新缓存并广播事件 |
| `SetPrimarySelectedDrone()` | 单选无人机 |
| `SetMultiSelectedDrones()` | 多选无人机 |
| `GetSelectedDroneMask()` | 多机选择如何变成 bit mask |
| `ApplyControlLock()` / `ReleaseControlLock()` | 阵列播放、离线等状态如何锁定控制 |
| `SetDroneCommandMode()` | 每架机当前执行模式如何保存 |

### 关键 UE 概念

- `DECLARE_DYNAMIC_MULTICAST_DELEGATE`：UE 风格事件广播，蓝图也能绑定。
- `UPROPERTY()`：让 UE 反射、GC、蓝图或序列化系统知道这个字段。
- `UFUNCTION(BlueprintCallable)`：让函数可以被蓝图调用。
- `TMap` / `TArray` / `TObjectPtr`：UE 容器和对象引用方式。

### 阶段产出

画出这张图：

```text
HTTP 轮询 / WebSocket 遥测 / 本地 Actor
          ↓
UDroneRegistrySubsystem
          ↓
UI 列表、信息面板、镜像机 Actor、PlayerController 选中状态
```

---

## 5. 阶段 3：读网络层 HTTP + WebSocket

### 目标

理解 UE 前端如何与 C++ 后端交互。

### 读代码顺序

1. `Source/UE5DroneControl/DroneOps/Network/DroneNetworkManager.h`
2. `Source/UE5DroneControl/DroneOps/Network/DroneNetworkManager.cpp`
3. `Source/UE5DroneControl/DroneOps/Network/DroneHttpClient.h`
4. `Source/UE5DroneControl/DroneOps/Network/DroneHttpClient.cpp`
5. `Source/UE5DroneControl/DroneOps/Network/DroneWebSocketClient.h`
6. `Source/UE5DroneControl/DroneOps/Network/DroneWebSocketClient.cpp`

### 重点函数

| 函数 | 说明 |
|---|---|
| `UDroneNetworkManager::Initialize()` | 创建 HTTP/WS 客户端，启动轮询和连接 |
| `StartPolling()` / `PollDroneList()` | 定时请求 `GET /api/drones` |
| `OnDroneListResponse()` | 解析后端无人机列表 |
| `SyncDroneListToRegistry()` | 把后端状态同步进 Registry |
| `ConnectWebSocket()` | 连接后端 WebSocket |
| `OnWsMessage()` | 解析 telemetry、event、alert、assembling 等消息 |
| `SendMoveCommand()` | 玩家点击目标点后发送 move/scout/patrol/attack |
| `SendPauseCommand()` | 暂停/恢复 |
| `RegisterDroneToBackend()` | 主菜单注册无人机 |
| `SendArrayTask()` / `SendArrayTaskFromData()` | 下发阵列任务 |

### 必须弄清楚的数据方向

```text
UE -> 后端
  HTTP POST /api/drones       注册无人机
  HTTP POST /api/arrays       下发阵列任务
  WebSocket move              点击地图移动
  WebSocket pause/resume      暂停/恢复

后端 -> UE
  HTTP GET /api/drones 返回    注册表/在线状态
  WebSocket telemetry         遥测位置/姿态/电量
  WebSocket event             power_on / reconnect / lost_connection
  WebSocket alert             低电量 / 失联
  WebSocket assembling        集结进度
```

### 建议调试练习

在 `OnWsMessage()`、`SendMoveCommand()`、`SyncDroneListToRegistry()` 附近打断点或加临时日志，运行后端 Mock/真实后端，观察：

- 后端返回的 DroneId 是否和 UE 注册表一致。
- 遥测消息是否会调用 `Registry->UpdateTelemetry()`。
- 点击地图是否真的走到了 `SendMoveCommand()`。

---

## 6. 阶段 4：读无人机 Actor 和遥测驱动

### 目标

理解场景里的“镜像机”如何从遥测数据移动，以及旧 UDP 模式和新 WebSocket 模式的差异。

### 读代码顺序

1. `Source/UE5DroneControl/RealTimeDroneReceiver.h`
2. `Source/UE5DroneControl/RealTimeDroneReceiver.cpp`
3. `Source/UE5DroneControl/DroneOps/Drone/DroneTelemetryComponent.h`
4. `Source/UE5DroneControl/DroneOps/Drone/DroneTelemetryComponent.cpp`
5. `Source/UE5DroneControl/DroneOps/Drone/DroneSelectionComponent.h`
6. `Source/UE5DroneControl/DroneOps/Drone/DroneSelectionComponent.cpp`

### 重点理解 `ARealTimeDroneReceiver`

这个类是“真实无人机镜像 Actor”。它做了几件事：

- 继承 `AUE5DroneControlCharacter`，可以拥有基础无人机外观/移动能力。
- 实现 `IDroneSelectableInterface`，能被点击、选中、高亮。
- 实现 `IDroneInfoProvider`，能给 UI 提供信息面板数据。
- 旧模式下监听 UDP YAML 数据。
- 新模式下通过 Registry 接收 WebSocket 遥测。
- 把 NED 坐标转换成 UE 世界坐标。
- 处理 `power_on` / `reconnect` 事件带来的 GPS 锚点。

### 重点函数

| 函数 | 说明 |
|---|---|
| `BeginPlay()` | 初始化 Socket、注册到 Registry、绑定网络事件 |
| `Tick()` | 平滑插值位置/姿态，旧 UDP 模式下收包 |
| `ParseYAMLData()` | 旧链路：解析 PX4 YAML 遥测 |
| `NEDToUE5()` | NED 米 -> UE 厘米，Z 轴取反 |
| `QuatToEuler()` | 姿态四元数转换 |
| `ProcessPacket()` | 旧链路收包后的主处理 |
| `PushTelemetry()` | 把当前数据推给 `UDroneTelemetryComponent` |
| `OnWebSocketTelemetry()` | 新链路：后端遥测驱动 Actor |
| `OnDroneWsEvent()` | 处理上电/重连 GPS 锚点 |
| `ApplyDescriptor()` | 把注册表描述符应用到 Actor |

### 坐标转换必须背下来

旧 UDP 链路里常见公式：

```text
NED -> UE
North 米 -> UE X 厘米 = North * 100
East  米 -> UE Y 厘米 = East  * 100
Down  米 -> UE Z 厘米 = Down  * -100
```

新后端链路里，后端通常已经把部分数据转换成 UE 偏移，UE 侧还要结合 Cesium/GPS 锚点生成世界坐标。

### 阶段产出

用自己的话解释：

```text
为什么无人机真实高度增加时，NED 的 Down 可能变小，而 UE 的 Z 会变大？
```

如果这个能讲清楚，坐标系就过了第一关。

---

## 7. 阶段 5：读玩家输入、选择和命令下发

### 目标

理解“鼠标点击地图/无人机、按键切换视角、暂停、下发目标点”在哪里实现。

### 读代码顺序

1. `Source/UE5DroneControl/DroneOps/Control/DroneOpsPlayerController.h`
2. `Source/UE5DroneControl/DroneOps/Control/DroneOpsPlayerController.cpp`
3. `Source/UE5DroneControl/DroneOps/Interfaces/DroneSelectableInterface.h`
4. `Source/UE5DroneControl/DroneOps/Interfaces/DroneInfoProviderInterface.h`

### 重点函数

| 函数 | 说明 |
|---|---|
| `BeginPlay()` | 获取 Registry、初始化 HUD/相机 |
| `SetupInputComponent()` | 绑定鼠标和键盘输入 |
| `OnPrimaryClick()` | 左键点击入口 |
| `HandleMapClick()` | 点击地图生成目标点 |
| `HandleDroneClick()` | 点击无人机进行选择 |
| `SendTargetCommand()` | 把目标点发给网络层 |
| `OnPauseToggle()` | P 键暂停/恢复 |
| `OnFreeCamToggle()` | 自由相机切换 |
| `OnTopDownToggle()` | 俯视相机切换 |
| `OpenDroneInfoPanel()` | 打开中键信息面板 |
| `ResetShadowDronesToMirrors()` | 校准影子机到镜像机位置 |

### 推荐跟踪的一条完整调用链

```text
用户左键点击地图
  -> ADroneOpsPlayerController::OnPrimaryClick
  -> GetSelectableDroneUnderCursor / GetWorldLocationUnderCursor
  -> HandleMapClick
  -> SendTargetCommand
  -> UDroneNetworkManager::SendMoveCommand
  -> WebSocket 发给后端
  -> 后端 DroneManager / ExecutionEngine
  -> UDP 控制包发给 Jetson/PX4
```

### 必须理解的设计点

- `PlayerController` 不直接保存所有无人机状态，而是查 `UDroneRegistrySubsystem`。
- 点击无人机和点击地图是两种路径：前者改变选择，后者下发目标。
- 控制命令模式来自 `EDroneCommandMode`，最后会转成协议字符串：`move`、`scout`、`patrol`、`attack`。

---

## 8. 阶段 6：读 UI 和蓝图边界

### 目标

理解 C++ 如何给 UMG Widget 提供数据，蓝图又如何承接显示。

### 读代码顺序

1. `Source/UE5DroneControl/UI/DroneListWidget.h/.cpp`
2. `Source/UE5DroneControl/UI/DroneListItemWidget.h/.cpp`
3. `Source/UE5DroneControl/UI/AssemblyPopupWidget.h/.cpp`
4. `Source/UE5DroneControl/UI/SequenceDispatchPanelWidget.h/.cpp`
5. `Source/UE5DroneControl/UI/ToastWidget.h/.cpp`
6. `Source/UE5DroneControl/UI/UIManagerBlueprintLibrary.h/.cpp`
7. `Source/UE5DroneControl/MainMenu/`

### 阅读重点

- Widget 从哪里拿无人机列表？
- Widget 是否绑定了 Registry 的事件？
- 点击 UI 按钮后调用了哪个 C++ 函数？
- 哪些属性是 `BindWidget`，需要蓝图里同名控件配合？
- 哪些函数是 `BlueprintCallable`，说明它们是给蓝图用的桥。

### UE 入门注意

很多 UI 行为可能不完全在 C++ 里，蓝图资源在 `Content/` 下。读源码时如果发现某个 `WidgetClass`、`BP_...`、`WBP_...`，要去 UE 编辑器里打开对应蓝图看绑定关系。

---

## 9. 阶段 7：读航点、路径和阵列任务

### 目标

理解路径编辑、航点保存、阵列下发、任务预演和调度的代码结构。

### 读代码顺序

1. `Source/UE5DroneControl/PathEditor/DroneWaypointTypes.h`
2. `Source/UE5DroneControl/PathEditor/DroneWaypointActor.h/.cpp`
3. `Source/UE5DroneControl/PathEditor/DronePathActor.h/.cpp`
4. `Source/UE5DroneControl/PathEditor/DronePathSaveLibrary.h/.cpp`
5. `Source/UE5DroneControl/PathEditor/DronePathConflictLibrary.h/.cpp`
6. `Source/UE5DroneControl/PathEditor/DronePlaybackManager.h/.cpp`
7. `Source/UE5DroneControl/TaskSystem/DroneTaskManager.h/.cpp`

### 重点问题

- 航点在 UE 世界坐标中如何表示？
- 路径如何保存成文件？
- 下发给后端前是否转换坐标或序列化 JSON？
- 阵列任务中“路径”和“无人机”的绑定关系在哪里形成？
- 碰撞检测/冲突检测是前端做预演，还是后端执行时避障？

### 和后端的连接点

回到 `UDroneNetworkManager::SendArrayTask()` 和 `SendArrayTaskFromData()`，看路径数据如何被包装成 `/api/arrays` 请求。

---

## 10. 阶段 8：读后端框架

### 目标

理解后端作为“权威控制中心”的模块划分和主循环。

### 读代码顺序

1. `BackEnd/main.cpp`
2. `BackEnd/core/types.h`
3. `BackEnd/core/config_loader.h/.cpp`
4. `BackEnd/http/http_server.h/.cpp`
5. `BackEnd/drone/drone_manager.h/.cpp`
6. `BackEnd/drone/heartbeat_manager.h/.cpp`
7. `BackEnd/communication/udp_receiver.h/.cpp`
8. `BackEnd/communication/udp_sender.h/.cpp`
9. `BackEnd/communication/ws_manager.h`
10. `BackEnd/execution/assembly_controller.h/.cpp`
11. `BackEnd/execution/execution_engine.h/.cpp`
12. `BackEnd/conversion/coordinate_converter.h/.cpp`
13. `BackEnd/conversion/gps_anchor_manager.h/.cpp`
14. `BackEnd/conversion/quaternion_utils.h/.cpp`

### 先读 `main.cpp` 的初始化顺序

```text
LoadConfig / ValidateConfig
  -> InitLogger
  -> 创建 io_context
  -> 创建 UdpSender / HeartbeatManager / DroneManager
  -> 创建 AssemblyController / UdpReceiver / WsManager / ExecutionEngine / HttpServer
  -> 绑定集结和执行引擎回调
  -> 配置 UDP 接收端口
  -> 配置 UDP 发送目标
  -> 遥测回调进入 DroneManager / AssemblyController / ExecutionEngine
  -> 启动 UDP / Asio / HTTP+WS / Debug CLI
  -> 主循环检查超时
```

### 后端模块职责

| 模块 | 你要理解什么 |
|---|---|
| `HttpServer` | REST 接口如何处理注册、数组任务、调试接口 |
| `WsManager` | 如何向 UE 推送 telemetry/event/alert |
| `DroneManager` | 无人机注册、slot 映射、遥测缓存、状态机、move 命令 |
| `HeartbeatManager` | 为什么 PX4 Offboard 需要持续发送 setpoint |
| `UdpReceiver` | Jetson/PX4 遥测如何进入后端 |
| `UdpSender` | 后端如何发 24 字节控制包 |
| `AssemblyController` | 集结过程如何判断到位/超时 |
| `ExecutionEngine` | 侦察、巡逻、攻击、避障如何推进 |
| `conversion` | 坐标、GPS、四元数转换 |

### 推荐跟踪两条链路

**遥测链路：**

```text
Jetson UDP YAML
  -> UdpReceiver::SetCallback
  -> DroneManager::OnTelemetryReceivedBySlot
  -> WsManager 推送 telemetry/event/alert
  -> UE UDroneNetworkManager::OnWsMessage
  -> UDroneRegistrySubsystem::UpdateTelemetry
  -> ARealTimeDroneReceiver::OnWebSocketTelemetry
```

**控制链路：**

```text
UE 点击地图
  -> WebSocket move
  -> HttpServer / WS handler
  -> DroneManager::ProcessMoveCommandNed 或 ExecutionEngine
  -> HeartbeatManager / UdpSender
  -> Jetson/PX4
```

---

## 11. 不同代码区域的优先级

| 优先级 | 目录/文件 | 原因 |
|---|---|---|
| P0 | `DroneOps/Core` | 全局类型和注册表，所有功能都依赖 |
| P0 | `DroneOps/Network` | 当前主通信链路 |
| P0 | `DroneOps/Control` | 输入、相机、命令下发 |
| P0 | `RealTimeDroneReceiver.*` | 遥测驱动镜像机，理解坐标系核心 |
| P1 | `DroneOps/Drone` | 组件化能力：遥测、选择、发送 |
| P1 | `UI`、`MainMenu` | 注册、列表、弹窗、任务面板 |
| P1 | `PathEditor`、`TaskSystem` | 阵列/航点任务 |
| P1 | `BackEnd` | 后端控制中心，建议 UE 主线清楚后再读 |
| P2 | `Variant_Strategy`、`Variant_TwinStick` | UE 模板/示例变体，非当前无人机主线 |
| P2 | 旧 Python/UDP 工具 | 理解历史链路和调试可以读，但不要作为第一主线 |

---

## 12. 每周学习安排

### 第 1 周：能打开项目，知道 UE 基础对象

任务：

- 打开 `UE5DroneControl.uproject`。
- 浏览 `Build.cs`、`UE5DroneControl.cpp`、`DroneOpsGameMode`。
- 在 UE 编辑器中找到 `BP_DroneOpsPlayerController`、`BP_RealTimeDrone`、`BP_MultiDroneCharacter`。
- 学会查看 Output Log。

验收：

- 能说出 `GameMode`、`PlayerController`、`Actor`、`Subsystem` 分别负责什么。
- 能找到 DroneOps 关卡启动后创建无人机 Actor 的代码。

### 第 2 周：掌握 Registry 和数据结构

任务：

- 精读 `DroneOpsTypes.h`。
- 精读 `DroneRegistrySubsystem.h/.cpp`。
- 打开 `Saved/DroneRegistry.json`，看真实保存格式。
- 尝试注册/删除无人机，观察 Registry 文件变化。

验收：

- 能解释 `FDroneDescriptor` 和 `FDroneTelemetrySnapshot` 的区别。
- 能解释为什么 Registry 是状态中心。

### 第 3 周：掌握 HTTP/WS 通信

任务：

- 精读 `DroneNetworkManager`。
- 对照 `接口与通讯数据规范.md` 理解每类消息。
- 启动后端或 Mock Server，用日志观察 `GET /api/drones` 和 WS 消息。

验收：

- 能画出 UE 注册无人机的调用链。
- 能画出 WebSocket telemetry 更新 UI 和 Actor 的调用链。

### 第 4 周：掌握无人机 Actor、坐标和遥测

任务：

- 精读 `RealTimeDroneReceiver`。
- 精读 `DroneTelemetryComponent`。
- 对照 `坐标系转换说明.md` 手算几个 NED <-> UE 坐标例子。
- 在 `OnWebSocketTelemetry()` 或 `PushTelemetry()` 处观察数据。

验收：

- 能解释 UE 厘米、NED 米、GPS 经纬高之间的关系。
- 能解释为什么 Z 轴要取反。

### 第 5 周：掌握玩家输入和命令下发

任务：

- 精读 `DroneOpsPlayerController`。
- 跟踪左键点击地图到 `SendMoveCommand()` 的路径。
- 跟踪点击无人机到 Registry 选中状态变化的路径。
- 测试暂停、自由相机、俯视相机。

验收：

- 能自己加一个临时按键，比如打印当前选中无人机 ID。
- 能解释点击地图为什么需要先知道当前选中的无人机。

### 第 6 周：掌握 UI 和航点任务

任务：

- 阅读 `UI` 目录下的列表、弹窗、任务面板。
- 阅读 `PathEditor` 目录下的航点/路径保存代码。
- 跟踪 `SendArrayTask()` 如何把路径提交给后端。

验收：

- 能解释“前端预演”和“后端真实执行”的区别。
- 能找到阵列任务 JSON 生成的位置。

### 第 7 周：掌握后端主框架

任务：

- 精读 `BackEnd/main.cpp`。
- 阅读 `DroneManager`、`HeartbeatManager`、`UdpReceiver`、`UdpSender`。
- 跑 `BackEnd/TEST_GUIDE.md` 里的无 Jetson 测试。

验收：

- 能解释后端为什么需要心跳。
- 能解释 slot、drone_id、mavlink_system_id 的区别。

### 第 8 周：掌握任务执行和系统联调

任务：

- 阅读 `AssemblyController` 和 `ExecutionEngine`。
- 阅读 `conversion` 目录。
- 用测试脚本跑一次 Week 3/4/5 集成测试。
- 总结一份自己的项目架构图。

验收：

- 能完整讲清楚“注册 -> 上电 -> 锚定 -> 遥测 -> 点击控制 -> 集结/执行”的全流程。

---

## 13. 读 UE C++ 的固定套路

每读一个类，都按下面顺序：

1. 看 `.h` 里的继承关系。
2. 看 `UCLASS`、`USTRUCT`、`UENUM`、`UPROPERTY`、`UFUNCTION`。
3. 看构造函数。
4. 看 `BeginPlay()`、`Tick()`、`EndPlay()`。
5. 看输入绑定或事件绑定。
6. 看对其他系统的引用，比如 Registry、NetworkManager、World、TimerManager。
7. 看日志 `UE_LOG`，它们通常暗示运行流程。
8. 最后才看复杂算法和边界处理。

不要从 `.cpp` 第一行一路读到最后。UE 代码如果这样读，很容易迷路。

---

## 14. 调试建议

### UE 侧

- 用 Output Log 搜：
  - `DroneOpsGameMode`
  - `DroneNetworkManager`
  - `RealTimeDrone`
  - `Registry`
- 常用断点位置：
  - `ADroneOpsGameMode::BeginPlay`
  - `UDroneNetworkManager::OnWsMessage`
  - `UDroneRegistrySubsystem::UpdateTelemetry`
  - `ARealTimeDroneReceiver::OnWebSocketTelemetry`
  - `ADroneOpsPlayerController::SendTargetCommand`

### 后端侧

- 先不接真实无人机，用 `BackEnd/TEST_GUIDE.md` 的 debug/inject 接口。
- 常用断点位置：
  - `main.cpp` 的 `udp_receiver.SetCallback`
  - `DroneManager::OnTelemetryReceivedBySlot`
  - `DroneManager::ProcessMoveCommandNed`
  - `HttpServer` 中处理 `/api/drones`、`/api/arrays`、WebSocket 命令的位置
  - `HeartbeatManager` 发心跳的位置

### 网络侧

- HTTP：用浏览器、curl 或 Apifox 测 `http://localhost:8080/api/drones`。
- WebSocket：看后端日志和 UE Output Log。
- UDP：必要时用 Wireshark 过滤端口。

---

## 15. 最容易混淆的点

1. **UE 坐标单位是厘米，后端/PX4 常用米。**
2. **NED 的 Z 轴向下为正，UE 的 Z 轴向上为正。**
3. **`DroneId`、`Slot`、`MavlinkSystemId` 不是同一个东西。**
   - `DroneId`：项目内部无人机 ID。
   - `Slot`：后端端口映射槽位。
   - `MavlinkSystemId`：PX4/MAVLink 系统 ID。
4. **镜像机和影子机不是同一个概念。**
   - 镜像机：显示真实遥测位置。
   - 影子机：前端预演/任务模拟中的本地对象。
5. **旧 UDP 链路和新 HTTP/WS 后端链路同时存在。**
   - 第一主线学新链路。
   - 旧链路用于理解历史代码和调试备用。
6. **蓝图和 C++ 是配合关系。**
   - C++ 定义能力和数据。
   - 蓝图常负责资源、UI 绑定、外观和部分流程连接。

---

## 16. 建议你做的 5 个小练习

1. **日志练习**
   - 在 `DroneOpsGameMode::BeginPlay()` 加一条临时日志，确认关卡启动顺序。

2. **Registry 练习**
   - 写一个临时蓝图按钮或 C++ Exec 命令，打印所有注册无人机的 `DroneId`、`Slot`、`Availability`。

3. **坐标练习**
   - 手动构造一个 NED 坐标 `(10, 20, -5)`，算出 UE 坐标应该是 `(1000, 2000, 500)`，再在代码里验证。

4. **点击链路练习**
   - 在 `SendTargetCommand()` 打日志，输出选中的 DroneId 和点击点坐标。

5. **Mock 后端练习**
   - 不接无人机，只用后端 debug 接口注入一条 telemetry，看 UE 中镜像机是否移动。

---

## 17. 最终掌握标准

当你能独立回答下面这些问题，就说明你已经不是“只会打开项目”，而是真的理解了框架：

- UE 项目启动后，哪个类最先负责 DroneOps 关卡初始化？
- 无人机注册信息保存在哪里？后端和 UE 本地缓存谁是权威？
- WebSocket 收到 `telemetry` 后，经过哪些类最终移动了镜像机？
- 鼠标点击地图后，目标点如何变成后端能理解的命令？
- 为什么坐标转换里 Z 要取反？
- `DroneId`、`Slot`、`MavlinkSystemId` 分别用于什么？
- 断联、重连、上电事件分别如何影响 UE 中的无人机状态？
- 阵列任务是前端执行，还是后端执行？前端预演和后端真实控制有什么区别？
- 如果 UE 中无人机不动，你会从哪 5 个位置开始排查？

---

## 18. 你的第一遍阅读路线总结

建议严格按这个顺序走：

```text
README.md
  -> 架构设计.md
  -> UE5DroneControl.Build.cs
  -> DroneOpsTypes.h
  -> DroneRegistrySubsystem
  -> DroneOpsGameMode
  -> DroneNetworkManager
  -> RealTimeDroneReceiver
  -> DroneTelemetryComponent
  -> DroneOpsPlayerController
  -> UI / MainMenu
  -> PathEditor / TaskSystem
  -> BackEnd/main.cpp
  -> DroneManager / HttpServer / ExecutionEngine
```

第一遍看“谁调用谁”，第二遍看“数据结构怎么变”，第三遍再看“边界条件和异常处理”。这样会稳很多。
