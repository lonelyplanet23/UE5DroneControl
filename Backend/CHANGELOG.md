# 后端 C++ 项目开发记录

> 项目：UE5DroneControl — 后端 C++ 服务
> 作者：陈锦垚
> 日期：2026-04-23

---

## 目录

1. [Change 1：项目骨架 —— CMake + 配置 + 数据类型](#change-1项目骨架--cmake--配置--数据类型)
2. [Change 2：坐标转换模块 —— NED↔UE + 四元数 + GPS 锚点](#change-2坐标转换模块--nedue--四元数--gps-锚点)
3. [Change 3：无人机管理核心 —— 队列 + 状态机 + 上下文](#change-3无人机管理核心--队列--状态机--上下文)
4. [Change 4：UDP 通讯模块 —— 遥测接收 + 控制发送](#change-4udp-通讯模块--遥测接收--控制发送)
5. [Change 5：心跳维持 + DroneManager 集成](#change-5心跳维持--dronemanager-集成)
6. [Change 6：Jetson 桥接脚本 —— UDP↔ROS2 透明桥](#change-6jetson-桥接脚本--udpros2-透明桥)
7. [Change 7：集结流程 —— ASSEMBLING 状态机](#change-7集结流程--assembling-状态机)
8. [Change 8：单元测试](#change-8单元测试)
9. [Change 9：代码审查修复](#change-9代码审查修复)

---

## Change 1：项目骨架 —— CMake + 配置 + 数据类型

### 设计思路

后端 C++ 服务独立于 UE5 运行，目标平台 Windows（vcpkg 管理依赖）。需要四个基础部件：**构建系统**（CMake）、**配置管理**（YAML）、**核心数据结构**（所有模块公用的类型）、**日志系统**（统一输出）。

### CMakeLists.txt

```
位置：Backend/CMakeLists.txt (90行)
```

**功能**：
- 定义主程序目标 `DroneBackend` 和测试目标 `DroneBackend_tests`
- 通过 vcpkg 查找 6 个外部库：httplib、Boost.Asio、yaml-cpp、nlohmann-json、spdlog、GTest
- 编译标准 C++17，自动复制 config.yaml 到输出目录

**设计决策**：
- 头文件路径设为源码根目录（`CMAKE_CURRENT_SOURCE_DIR`），所以 `#include "core/types.h"` 就能引用
- test 目标链接同源码目录的 HEADERS，确保类型定义一致
- 没有用 `add_subdirectory(tests)` 而是直接加到同一个 CMakeLists.txt，小项目保持简洁

### config.yaml

```
位置：Backend/config.yaml (56行)
```

**设计**：YAML 分 5 个顶级段：

| 段 | 功能 | 关键字段 |
|----|------|---------|
| `server` | HTTP/WS 服务端口 | http_port:8080, ws_port:8081 |
| `drone` | 全局无人机参数 | max_count:6, heartbeat_hz:2, lost_timeout_sec:10 |
| `port_map` | 编号↔UDP 端口映射 | slot 1~6 对应的 send_port/recv_port |
| `jetson` | Jetso n伴飞计算机 IP | host:192.168.30.104 |
| `storage`/`log` | 持久化路径、日志配置 | — |

**关键设计**：port_map 固定不可动态修改，slot 由注册时用户指定。send_port 是后端→Jetson 的端口，recv_port 是后端监听 Jetson 遥测的端口。

### core/types.h

```
位置：Backend/core/types.h (123行)
```

这是整个后端的"通用语"，所有模块都引用这个头文件。定义了以下类型：

#### 枚举

| 枚举 | 值 | 用途 |
|------|----|------|
| `DroneConnectionState` | Offline/Connecting/Online/Lost | 连接状态机四个状态 |
| `StateEvent` | PowerOn/LostConnection/Reconnect | 状态变更时触发的事件类型，WS 据此推送不同消息 |

#### 结构体

**`DroneControlPacket`** — 24 字节控制包（后端 → 无人机）
```cpp
#pragma pack(push, 1)
struct DroneControlPacket {
    double   timestamp;  // 8B, Unix 时间戳（秒）
    float    x;          // 4B, NED North（米）
    float    y;          // 4B, NED East（米）
    float    z;          // 4B, NED Down（米）
    uint32_t mode;       // 4B, 0=悬停/心跳, 1=移动
};  // 总计 24 字节
#pragma pack(pop)
```
`#pragma pack(1)` 确保 C++ 结构体在内存中的布局与 UDP 线缆上的字节序列完全一致，可以直接用 `send_to(buffer(&pkt, 24))` 发送，不需要序列化。

**`TelemetryData`** — 遥测数据（Jetson → 后端 YAML 解析后）
```cpp
struct TelemetryData {
    uint64_t  timestamp;            // PX4 系统时间（微秒）
    double    position_ned[3];      // [N, E, D] 米 — vehicle_odometry
    double    quaternion[4];        // [w, x, y, z] — vehicle_odometry
    double    velocity[3];          // [vN, vE, vD] m/s
    double    angular_velocity[3];  // rad/s
    int       battery;              // %, -1=不可用 — battery_status
    double    gps_lat, gps_lon, gps_alt;  // — vehicle_global_position
    bool      gps_fix;              // GPS 是否有效
    double    local_position[3];    // — vehicle_local_position
    uint8_t   arming_state;         // 1=DISARMED, 2=ARMED — vehicle_status_v1
    uint8_t   nav_state;            // 14=OFFBOARD — vehicle_status_v1
};
```
这里用 `position_ned[3]` 而不是 `FVector`/`Vector3D`，是为了保持零依赖（不用引入 UE 或 GLM 库）。新增了 `arming_state` 和 `nav_state` 字段，后端据此判断无人机是否解锁、是否处于 OFFBOARD 模式，取代旧 Python 桥接中的状态机。

辅助方法 `IsArmed()` 和 `IsOffboard()` 提供语义化的判断。

**`GpsAnchor`** — GPS 锚点（上电位置）
```cpp
struct GpsAnchor {
    int    drone_id;
    double latitude, longitude, altitude;
    bool   valid;        // false 表示尚未收到首包遥测
};
```
锚点由 `vehicle_global_position` 产生，是 NED 偏移的绝对参考点。

**`PortMapping`** — 端口映射
```cpp
struct PortMapping {
    int     slot;              // 编号 1~6
    int     recv_port;         // 后端监听遥测的端口
    int     send_port;         // 后端发送控制的端口
    std::string ros_topic_prefix; // e.g. "/px4_1"
};
```

**`DroneStatus`** — 无人机快照（供 HTTP GET /api/drones 返回）
包含状态、电量、UE 偏移坐标、yaw、speed、anchor。所有坐标已经过后端转换（NED→UE 厘米），UE5 收到后可直接叠加锚点。

**`AssemblyProgress`** — 集结进度
```cpp
struct AssemblyProgress {
    std::string array_id;
    int ready_count, total_count;  // 如 "2/4 架已就位"
};
```

#### 回调类型（模块对接接口）

```cpp
using TelemetryCallback   = std::function<void(int drone_id, const TelemetryData&)>;
using StateChangeCallback = std::function<void(int drone_id, StateEvent event, const GpsAnchor&)>;
using AlertCallback       = std::function<void(int drone_id, const std::string& alert_type, int value)>;
using AssemblyCallback    = std::function<void(const AssemblyProgress&)>;
```

这四个回调是 **DroneManager 与 WebSocket Server 的契约接口**。WS Server（李昊泽）通过调用 `SetXxxCallback()` 注册自己的推送逻辑，DroneManager 在事件发生时回调。这种"观察者模式"避免了 DroneManager 对 WS Server 的直接依赖。

---

### core/logger.h

```
位置：Backend/core/logger.h (34行)
```

**功能**：一行代码初始化 spdlog，支持终端输出 + 日志文件。

```cpp
inline void InitLogger(const std::string& level, const std::string& file);
```

**设计**：使用 `inline` 函数而不是单独的 .cpp，因为初始化逻辑简单。spdlog 支持 `stdout_color_sink_mt`（带颜色的终端输出）和 `basic_file_sink_mt`（文件滚动）。日志格式 `[时间] [级别] [文件名:行号] 消息`。

### core/config_loader.h/.cpp

```
位置：Backend/core/config_loader.h (41行)
      Backend/core/config_loader.cpp (98行)
```

**类/函数**：

| 函数/结构体 | 描述 |
|-------------|------|
| `struct AppConfig` | 后端全部配置的聚合。包含 server、drone、port_map、jetson、storage、log 六个子段 |
| `LoadConfig(path)` | 读取 YAML → 填充 AppConfig。每个字段都有默认值，YAML 中缺失的字段不会导致崩溃 |
| `ValidateConfig(cfg)` | 校验配置合法性：heartbeat≥2Hz、lost_timeout>0、port_map 端口唯一性等 |

**设计要点**：
- YAML 解析放在 try-catch 中，出错后记录日志并重新抛出
- 每个字段读取时都带默认值（如 `.as<int>(cfg.xxx)`），即使 YAML 缺失该段也不会 crash
- port_map 的 YAML 结构是 `1:{send_port:8889, recv_port:8888, ros_topic_prefix:"/px4_1"}`，直接在循环中遍历

---

## Change 2：坐标转换模块 —— NED↔UE + 四元数 + GPS 锚点

### conversion/coordinate_converter.h/.cpp

```
位置：Backend/conversion/coordinate_converter.h (26行)
      Backend/conversion/coordinate_converter.cpp (17行)
```

**静态类**——所有方法都是 `static`，因为坐标转换是纯数学运算，不需要状态。

| 函数 | 公式 | 输入 | 输出 |
|------|------|------|------|
| `NedToUeOffset(n,e,d, out_x,out_y,out_z)` | `×100, ×100, -×100` | NED 米 | UE 偏移厘米 |
| `UeOffsetToNed(x,y,z, out_n,out_e,out_d)` | `×0.01, ×0.01, -×0.01` | UE 偏移厘米 | NED 米 |

**设计要点**：
- 用 `constexpr` 定义比例常数 `NED_TO_UE_SCALE=100.0` 和 `UE_TO_NED_SCALE=0.01`，确保两个函数使用同一基准
- 输出参数用引用，避免拼错顺序（`NedToUeOffset(n,e,d, x,y,z)` 比 `NedToUeOffset(n,e,d)` 返回 `Vector3` 更清晰）
- Z 取反是最容易遗漏的，代码注释标明了 `下→上（取反）`

### conversion/quaternion_utils.h/.cpp

```
位置：Backend/conversion/quaternion_utils.h (33行)
      Backend/conversion/quaternion_utils.cpp (42行)
```

**静态类**，处理四元数转换和速度计算。

| 函数 | 说明 |
|------|------|
| `QuatToEuler(qw,qx,qy,qz, out_roll,out_pitch,out_yaw)` | 核心转换：NED 四元数 → UE5 欧拉角（度） |
| `GetUeYaw(qw,qx,qy,qz)` | 便捷版，只取 UE Yaw |
| `SpeedFromVelocity(vN,vE,vD)` | 标量速度：`sqrt(vN²+vE²+vD²)` |

**QuatToEuler 三步转换**：

1. **Z 分量取反**：`ue_qz = -qz`（NED Down → UE5 Up）
2. **四元数→欧拉角**：标准公式，用 `atan2` 和 `asin` 计算 roll/pitch/yaw（弧度）
3. **Yaw 取反**：`yaw = -yaw`（右手系 NED → 左手系 UE5）

最后将所有角度从弧度转为度。

**设计决策**：手动实现四元数→欧拉角转换，而不是依赖外部数学库。原因：只有这一个地方需要这个功能，引入整个数学库（如 glm）太重了。极端万向锁情况（`|sin_p| >= 1`）用 `copysign` 处理。

### conversion/gps_anchor_manager.h/.cpp

```
位置：Backend/conversion/gps_anchor_manager.h (38行)
      Backend/conversion/gps_anchor_manager.cpp (57行)
```

**类**：`GpsAnchorManager`

**功能**：记录每架无人机的 GPS 上电位置（锚点），支持首次记录和断联重连更新。

| 函数 | 功能 | 线程安全 |
|------|------|---------|
| `SetAnchor(drone_id, lat, lon, alt)` | 记录/更新锚点，返回 true 表示首次记录 | ✅ std::mutex |
| `GetAnchor(drone_id)` | 查询锚点 | ✅ |
| `HasAnchor(drone_id)` | 是否存在有效锚点 | ✅ |
| `ClearAnchor(drone_id)` | 清除（删除无人机时调用） | ✅ |
| `Count()` | 当前锚点数量 | ✅ |

**设计要点**：
- 所有方法加 `std::mutex`，因为 `DroneManager` 的 `OnTelemetryReceived` 在 Asio 线程调用，而 `GetAnchor` 可能在 HTTP Server 线程调用
- `SetAnchor` 返回 `is_new` 用于区分"首次上电"和"断联重连"，调用方据此决定推送 power_on 还是 reconnect 事件
- 错误处理：`GetAnchor` 找不到时返回 `valid=false` 的默认锚点，而不是抛异常

---

## Change 3：无人机管理核心 —— 队列 + 状态机 + 上下文

### drone/command_queue.h/.cpp

```
位置：Backend/drone/command_queue.h (48行)
      Backend/drone/command_queue.cpp (70行)
```

**类**：`CommandQueue`

**功能**：每架无人机独立的 FIFO 指令队列，线程安全，支持暂停/恢复。

| 函数 | 功能 | 线程安全 |
|------|------|---------|
| `Push(cmd)` | 队尾入队，满时丢弃最旧 | ✅ |
| `Pop(cmd)` | 队首出队，暂停时返回 false | ✅ |
| `Peek(cmd)` | 查看队首不移除 | ✅ |
| `Clear()` | 清空队列 | ✅ |
| `Size()` | 当前长度 | ✅ |
| `SetPaused(b)` | 暂停/恢复队列 | ✅ |
| `IsPaused()` | 查询暂停状态 | ✅ |

**设计决策**：
- **有界队列**：默认最大 128 条，超过时丢弃最旧指令。原因：如果 UE5 发了大量 move 指令而网络拥塞，队列不应无限膨胀，丢弃旧指令"飞向最新目标"比"依次飞行所有过期目标"更合理
- **暂停语义**：暂停时 `Pop` 返回 false，但指令保留在队列中。恢复后先从队列取指令发送
- **线程安全**：使用 `std::mutex` + `std::queue`，没有用 `moodycamel::ConcurrentQueue` 等无锁队列——因为队列操作频率低（最高 100Hz），mutex 的开销可以忽略
- **`const` 方法**：`Size()` 和 `Peek()` 是 `const`，但锁需要修改 mutex，所以 mutex 声明为 `mutable`

### drone/state_machine.h/.cpp

```
位置：Backend/drone/state_machine.h (41行)
      Backend/drone/state_machine.cpp (64行)
```

**类**：`StateMachine`

**功能**：无人机连接状态机，管理 Offline↔Online↔Lost 状态转换，注册状态变更回调。

| 函数 | 功能 | 调用者 |
|------|------|--------|
| `GetState()` | 查询当前状态 | HTTP/WS Server |
| `OnTelemetryReceived()` | 收到遥测→更新时间戳，必要时转换状态 | DroneManager |
| `CheckTimeout(timeout_sec)` | 检查超时，超过阈值→Lost | 主循环（每秒） |
| `Reset()` | 重置为 Offline | DroneManager::RemoveDrone |

**状态转移图**：
```
Offline ──(首包遥测)──→ Online ──(10s无遥测)──→ Lost
                           ↑                        │
                           └──── (新遥测) ──────────┘
```

**事件回调**：状态变更时调用 `OnStateChange callback`：
- `Offline→Online`: `StateEvent::PowerOn`（触发心跳启动 + GPS 锚点记录）
- `Online→Lost`: `StateEvent::LostConnection`（触发心跳停止 + WS 推送断联）
- `Lost→Online`: `StateEvent::Reconnect`（触发心跳重启 + 新锚点记录）

**设计要点**：
- `state_` 在第一次版本是普通 `enum`，后经代码审查改为 `std::atomic<DroneConnectionState>`，因为 `OnTelemetryReceived`（在 Asio 线程）和 `CheckTimeout`（在主线程）同时读写
- 超时检测不自己启动计时器，而是由 `DroneManager::CheckTimeouts` 在主循环中每秒调用，避免额外的线程开销
- `last_telemetry_time_` 在 `OnTelemetryReceived` 和 `Reset` 中更新，`CheckTimeout` 只在 Online 状态检查

### drone/drone_context.h

```
位置：Backend/drone/drone_context.h (51行)
```

**结构体**：`DroneContext`

**功能**：单架无人机的"档案袋"，聚合所有运行时状态。

```cpp
struct DroneContext {
    // 身份信息（生命周期内不变）
    int     drone_id, slot;
    std::string name;

    // 运行时状态（由 DroneManager 管理）
    std::unique_ptr<StateMachine>   state_machine;
    std::unique_ptr<CommandQueue>   command_queue;
    TelemetryData   latest_telemetry{};
    bool            has_telemetry = false;

    // 最后发送的 NED 位置（失联后维持悬停用）
    double  last_ned_x, last_ned_y, last_ned_z;

    // 网络配置（Jetson 目标地址）
    std::string jetson_ip;
    int         send_port;
};
```

**设计要点**：
- 使用 `unique_ptr` 管理 StateMachine 和 CommandQueue，明确所有权归 DroneManager
- `has_telemetry` 区分"从未收到遥测"和"当前遥测数据"，用于 `GetStatus` 中判断是否返回坐标
- 默认 jetson_ip 设为 "192.168.30.104"，send_port 设为 8889，实际运行中由注册模块配置

---

## Change 4：UDP 通讯模块 —— 遥测接收 + 控制发送

### 总体设计

UDP 模块是后端与 PX4 世界的"门面"。后端通过这一收一发两个口子与 Jetson 上的 ROS2 桥交互。

为什么选择 Boost.Asio：
- 跨平台（Windows/Linux/Jetson）
- 异步模型成熟（async_receive_from 不需要轮询）
- C++17 标准库网络支持不完整

### communication/udp_receiver.h/.cpp

```
位置：Backend/communication/udp_receiver.h (56行)
      Backend/communication/udp_receiver.cpp (174行)
```

**类**：`UdpReceiver`

**功能**：监听多个 UDP 端口（每架无人机一个），接收 YAML 格式遥测数据，解析后通过回调通知 DroneManager。

| 函数 | 功能 | 调用者 |
|------|------|--------|
| `AddPort(slot, port, drone_id)` | 添加监听端口 | main.cpp（启动时） |
| `SetCallback(cb)` | 注册遥测接收回调 | main.cpp（启动时） |
| `Start()` | 启动所有端口监听（异步） | main.cpp（注册完成后） |
| `Stop()` | 停止所有监听 | main.cpp（退出时） |

**内部结构**：
```cpp
struct PortListener {
    int slot, port, drone_id;
    boost::asio::ip::udp::socket socket;
    boost::asio::ip::udp::endpoint remote_endpoint;
    std::array<char, 65535> buffer{};  // 最大 UDP 包
};
```

**数据流**：
```
Jetson → UDP包 → socket.async_receive_from() → HandleReceive()
    → yaml-cpp 解析 → TelemetryData
    → callback_(drone_id, TelemetryData)
    → DroneManager::OnTelemetryReceived()
```

**YAML 解析字段**（与 jetson_bridge.py 输出格式对齐）：
```
timestamp, position[N, E, D], q[w, x, y, z], velocity[vN, vE, vD],
angular_velocity, battery, gps_lat, gps_lon, gps_alt, gps_fix,
local_position[x, y, z], arming_state, nav_state
```

**设计要点**：
- 使用 Boost.Asio 异步接收，不需要轮询线程
- 每个占口一个独立的 `async_receive_from` 循环，处理完一个包后立即调用 `StartReceive` 等待下一个
- 解析错误只记录日志不崩溃，解析失败的包直接丢弃
- 回调在 Asio worker 线程中执行，调用方（DroneManager）需确保线程安全

### communication/udp_sender.h/.cpp

```
位置：Backend/communication/udp_sender.h (51行)
      Backend/communication/udp_sender.cpp (76行)
```

**类**：`UdpSender`

**功能**：向 Jetson 发送 24 字节小端控制包。支持多机发送。

| 函数 | 功能 | 调用者 |
|------|------|--------|
| `SetTarget(drone_id, host, port)` | 设置发送目标 IP:Port | HeartbeatManager::Start |
| `Send(drone_id, packet)` | 发送 24 字节控制包 | HeartbeatLoop |
| `SendMove(drone_id, x, y, z)` | 便捷：发送移动包（Mode=1） | 测试/调试时使用 |
| `SendHover(drone_id)` | 便捷：发送悬停包（Mode=0） | 测试/调试时使用 |

**设计要点**：
- `Target` 内部结构体包含 `boost::asio::ip::udp::socket`，每架无人机独立 socket
- `send_to` 直接传入 `&packet` 指针和固定大小 `sizeof(DroneControlPacket)`（24），不需要手动计算偏移
- 失败时记录日志但不出异常，不影响主流程

---

## Change 5：心跳维持 + DroneManager 集成

### drone/heartbeat_manager.h/.cpp

```
位置：Backend/drone/heartbeat_manager.h (51行)
      Backend/drone/heartbeat_manager.cpp (112行)
```

**类**：`HeartbeatManager`

**功能**：每架 Online 无人机启动一个独立线程，以 ≥2Hz 发送 24 字节 UDP 控制包到 Jetson，防止 PX4 退出 Offboard 模式。

**为什么需要心跳**：PX4 的 Offboard 模式要求控制端持续以 ≥2Hz 发送 `OffboardControlMode` 消息，否则 PX4 自动退出 Offboard 并悬停。这个 2Hz 保活由后端的 UDP 心跳负责。

| 函数 | 功能 | 线程安全 |
|------|------|---------|
| `Start(drone_id, ip, port, cmd_provider)` | 启动心跳线程 | ✅ |
| `UpdateLastPosition(drone_id, x, y, z)` | 更新最后已知 NED 位置 | ✅ |
| `Stop(drone_id)` | 停止指定无人机心跳 | ✅ |
| `StopAll()` | 停止所有心跳 | ✅ |

**心跳线程逻辑**（`HeartbeatLoop`，运行在独立线程）：

```
循环每 500ms：
    1. 调用 get_command 回调（从 CommandQueue 取指令）
    2. 如果有指令：发送 Mode=1 移动包
    3. 如果没有指令：发送 Mode=0 悬停包（最后已知位置）
    4. sleep(500ms)
```

**线程安全设计**（经代码审查修复后）：

`Start()` 调用流程：
```
Start(drone_id):
    1. StopInternal(drone_id)         ← 无锁调用，安全
    2. 创建 HeartbeatState + thread
    3. lock(mutex_)
    4.   states_[drone_id] = state    ← 存储状态
    5.   threads_[drone_id] = thread  ← 存储线程
    6. unlock
```

`Stop()` 调用流程：
```
Stop(drone_id):
    1. lock(mutex_)
    2.   state = states_[drone_id]    ← 取出状态指针
    3.   state->running = false       ← 通知线程退出
    4.   thread = threads_[drone_id]  ← 取出线程指针
    5. unlock
    6. thread->join()                ← 锁外 join，不阻塞其他操作
```

关键修复：不要在持有锁的时候 join 线程，否则心跳线程想获取锁时会死锁。

**HeartbeatState 结构体**：
```cpp
struct HeartbeatState {
    std::atomic<bool> running{false};       // 线程运行标志
    std::atomic<double> last_ned_x{0.0};    // 最后位置 X
    std::atomic<double> last_ned_y{0.0};    // 最后位置 Y
    std::atomic<double> last_ned_z{-1.0};   // 最后位置 Z（默认-1m 安全高度）
    CommandProvider    get_command;          // 从指令队列取指令
};
```

**CommandProvider 回调类型**：
```cpp
using CommandProvider = std::function<bool(DroneControlPacket& cmd)>;
```
- 返回 `true` 表示队列有指令，`cmd` 被填充
- 返回 `false` 表示队列为空，心跳发 Mode=0 悬停

### drone/drone_manager.h/.cpp

```
位置：Backend/drone/drone_manager.h (99行)
      Backend/drone/drone_manager.cpp (295行)
```

**类**：`DroneManager`

**功能**：多无人机总管理器，核心模块。聚合了状态机、指令队列、心跳管理、GPS 锚点、坐标转换。

**构造依赖**：`HeartbeatManager&`（通过构造函数注入，不负责心跳线程的生命周期）

#### 对外接口

| 函数 | 调用方 | 功能 |
|------|--------|------|
| `SetTelemetryCallback` | WS Server (李昊泽) | 注册遥测推送回调 |
| `SetStateChangeCallback` | WS Server | 注册状态事件推送回调 |
| `SetAlertCallback` | WS Server | 注册告警推送回调 |
| `SetAssemblyCallback` | WS Server | 注册集结进度推送回调 |
| `AddDrone(id, slot, name)` | HTTP Server (关皓元) | 添加无人机（注册时调用） |
| `RemoveDrone(id)` | HTTP Server | 删除无人机 |
| `GetAllStatus()` | HTTP Server (GET /api/drones) | 获取所有无人机状态 |
| `GetStatus(id)` | HTTP Server | 获取单个无人机状态 |
| `OnTelemetryReceived(id, data)` | UdpReceiver | 收到遥测（核心入口） |
| `ProcessMoveCommand(id, x, y, z)` | WS Server | 处理移动指令 |
| `ProcessPauseCommand(id)` | WS Server | 处理暂停指令 |
| `ProcessResumeCommand(id)` | WS Server | 处理恢复指令 |
| `GetConnectionState(id)` | HTTP Server | 查询连接状态 |
| `GetLatestTelemetry(id)` | HTTP/WS Server | 查询最新遥测 |
| `GetAnchor(id)` | HTTP Server (GET /anchor) | 查询 GPS 锚点 |
| `CheckTimeouts(sec)` | main.cpp（主循环） | 超时检查（每秒调用） |

#### 内部数据流

**收到遥测**（UdpReceiver → DroneManager）：
```
OnTelemetryReceived(drone_id, data)
  ├─→ DroneContext.latest_telemetry = data          // 缓存最新遥测
  ├─→ HeartbeatManager.UpdateLastPosition(...)       // 更新心跳位置
  ├─→ StateMachine.OnTelemetryReceived()             // 推进状态机
  │     └─→ 如果 PowerOn/Reconnect:
  │           ├─→ StateChangeCallback (WS推送)
  │           └─→ HeartbeatManager.Start(id, ip, port, cmd_provider)  // 启动心跳
  ├─→ GpsAnchorManager.SetAnchor(...)                // 记录 GPS 锚点
  ├─→ AlertCallback (如果低电量)                    // 告警推送
  └─→ TelemetryCallback (WS推送)                    // 遥测推送
```

**收到控制指令**（WS Server → DroneManager）：
```
ProcessMoveCommand(drone_id, ue_x, ue_y, ue_z)
  ├─→ CoordinateConverter::UeOffsetToNed()           // UE厘米→NED米
  ├─→ DroneContext.last_ned = ned                    // 保存最后位置
  ├─→ HeartbeatManager.UpdateLastPosition(ned)       // 更新心跳位置
  └─→ CommandQueue.Push(mode=1)                     // 入队
        └─→ HeartbeatLoop 下一轮读取到 → UdpSender.Send(mode=1)
```

**超时检查**（主循环每秒调用）：
```
CheckTimeouts(10)
  └─→ StateMachine.CheckTimeout(10)
        └─→ 如果超时→ Lost:
              ├─→ StateChangeCallback (LostConnection)
              ├─→ AlertCallback (lost_connection)
              └─→ HeartbeatManager.Stop(id)
```

#### 状态机回调 lambda（AddDrone 中设置）

这个 lambda 是 DroneManager 的核心逻辑，在 StateMachine 状态变更时执行：

```cpp
[this, drone_id](StateEvent event) {
    auto* ctx = GetContext(drone_id);
    if (!ctx) return;

    switch (event) {
        case PowerOn: case Reconnect:
            // 1. 通知 WS
            state_change_cb_(drone_id, event, anchor);
            // 2. 启动心跳，传入CommandProvider从队列取指令
            hb_manager_.Start(drone_id, ctx->jetson_ip, ctx->send_port,
                [this, drone_id](DroneControlPacket& cmd) {
                    auto* q = GetContext(drone_id);
                    return q ? q->command_queue->Pop(cmd) : false;
                });
            break;
        case LostConnection:
            // 1. 停止心跳
            hb_manager_.Stop(drone_id);
            // 2. 通知 WS + 告警
            state_change_cb_(drone_id, event, {});
            alert_cb_(drone_id, "lost_connection", 0);
            break;
    }
}
```

**设计要点**：
- `GetContext` 检查 `drones_` 是否存在，避免心跳 lambda 在无人机被删除后访问已释放内存
- lambda 通过值捕获 `drone_id`，通过 `this` 捕获 DroneManager，避免悬空引用

---

## Change 6：Jetson 桥接脚本 —— UDP↔ROS2 透明桥

```
位置：Backend/jetson_bridge.py (268行)
```

### 为什么需要这个脚本

后端 C++ 跑在 Windows 上，无法直接使用 ROS2 客户端库。因此需要在 Jetson（Linux + ROS2 环境）上保留一个极简桥接，只做**协议转换**，不做任何控制逻辑。

### 架构

```
后端(Windows)            Jetson(Linux)                    PX4
 UDP 24B控制包 ─────────→ UDP Recv → ROS2 Publish ──────→ /fmu/in/*
                         ROS2 Subscribe → UDP Send ──→ YAML UDP
                                 ↑
                           MicroXRCE Agent (serial↔DDS)
                                 ↓
                              PX4 飞控
```

### 类：`JetsonBridge(Node)`

继承自 `rclpy.node.Node`，是一个 ROS2 节点。

**构造函数**接受 `slot` 参数（1~6），自动生成话题前缀 `/px4_{slot}` 和 UDP 端口号。

**发布者**（接收 UDP → 发 ROS2）：

| 话题 | 发布频率 | 说明 |
|------|---------|------|
| `{prefix}/fmu/in/offboard_control_mode` | 透传（后端决定≥2Hz） | 位置控制模式 |
| `{prefix}/fmu/in/trajectory_setpoint` | 透传 | NED 位置设定点 |
| `{prefix}/fmu/in/vehicle_command` | 仅在 Mode=1 时发送 | OFFBOARD 模式切换 |

**订阅者**（收 ROS2 → 发 UDP，10Hz 定时器）：

| 话题 | 数据用途 |
|------|---------|
| `/px4_i/fmu/out/vehicle_odometry` | position[NED]、q[wxyz]、velocity |
| `/px4_i/fmu/out/vehicle_status_v1` | arming_state、nav_state |
| `/px4_i/fmu/out/vehicle_local_position` | local_position（备用） |
| `/px4_i/fmu/out/vehicle_global_position` | GPS 坐标 |
| `/px4_i/fmu/out/battery_status` | 电量百分比 |

### 核心函数

| 函数 | 功能 |
|------|------|
| `_on_odometry(msg)` | 缓存 odometry 数据 |
| `_on_status_v1(msg)` | 缓存飞控状态（解锁/OFFBOARD） |
| `_on_global_pos(msg)` | 缓存 GPS 坐标 |
| `_on_battery(msg)` | 缓存电量 |
| `_send_telemetry()` | 10Hz 定时器：组合所有缓存数据→YAML→UDP→后端 |
| `process_control()` | UDP 接收循环：解析 24 字节控制包→发布 ROS2 |

**24 字节控制包解析**：
```python
CONTROL_FORMAT = "<dfffI"  # double, float, float, float, uint32
# timestamp, x(NED_N), y(NED_E), z(NED_D), mode
```

**设计要点**：
- 比原来的 `ue_to_px4_bridge.py`（800+ 行）精简到 268 行
- 去掉了状态机、GPS 偏移计算、坐标转换、安全限制——全部移到 C++ 后端
- ROS2 订阅的回调只做缓存不做处理，`_send_telemetry` 定时器（10Hz）统一组合发送
- 控制接收循环使用 `settimeout(0.1)` + `time.sleep(0.01)`，即 100Hz 轮询

---

## Change 7：集结流程 —— ASSEMBLING 状态机

### execution/assembly_controller.h/.cpp

```
位置：Backend/execution/assembly_controller.h (94行)
      Backend/execution/assembly_controller.cpp (149行)
```

**类**：`AssemblyController`

**功能**：无人机集结流程管理。接收 POST /api/arrays 后，进入 ASSEMBLING 状态，监控每架无人机到达初始槽位，全部到位后进入 READY 状态。

#### 状态枚举

```cpp
enum class AssemblyState : uint8_t {
    Idle,           // 空闲
    Assembling,     // 集结中（监控到位）
    Ready,          // 全部到位，准备执行
    Executing,      // 执行中（交给 ExecutionEngine）
    Timeout         // 超时
};
```

状态机：
```
Idle ──(Start)──→ Assembling ──(全部到位)──→ Ready ──→ Executing
                         ↘──── (超时) ────→ Timeout → Idle
                                    ↗ (Stop) ──┘
```

#### 结构体

**`AssemblyConfig`** — 集结任务配置

```cpp
struct AssemblyConfig {
    std::string array_id;      // e.g. "a1"
    std::string mode;          // "recon" / "patrol" / "attack"
    struct Path {
        int path_id;
        std::string drone_id;     // e.g. "d1"
        bool closed_loop;
        struct Waypoint {
            double x, y, z;      // UE 偏移坐标（厘米）
            float segment_speed, wait_time;
        };
        std::vector<Waypoint> waypoints;
    };
    std::vector<Path> paths;
};
```

**`DroneArrival`**（内部）— 每架无人机的到位监控

```cpp
struct DroneArrival {
    int drone_id;
    double target_ned_x, target_ned_y, target_ned_z;  // NED 米
    bool arrived = false;
};
```

#### 函数

| 函数 | 功能 | 调用者 |
|------|------|--------|
| `Start(config)` | 开始集结，计算各机初始目标点 | HTTP Server (POST /api/arrays) |
| `UpdateDronePosition(id, x, y, z)` | 更新无人机当前位置，判断到达 | DroneManager 遥测回调 |
| `CheckTimeout()` | 超时检查 | main.cpp 主循环 |
| `Stop()` | 停止集结 | HTTP Server |
| `GetProgress()` | 获取当前进度 | WS 推送 |
| `GetState()` | 查询状态 | main.cpp |

**Start 流程**：
```
Start(config):
    1. config_ = config
    2. 遍历 config.paths:
        a. 取第一个航点作为集结目标点
        b. UE偏移→NED坐标转换
        c. 提取 drone_id（"d1" → 1）
        d. 创建 DroneArrival，存入 arrivals_
    3. state_ = Assembling
    4. 推送初始进度（0/N）
```

**UpdateDronePosition 流程**：
```
UpdateDronePosition(id, x, y, z):
    1. 不在 Assembling 状态 → 返回
    2. 遍历 arrivals_:
        a. 跳过不匹配的 drone_id 或已到位的
        b. 计算 dist = sqrt(Δx² + Δy² + Δz²)
        c. dist < threshold(1m) → marked arrived
    3. 如有新到位的 → 推送进度
    4. 如果全部到位 → state_ = Ready
```

**设计要点**：
- 构造函数接受 `timeout_sec` 参数（从 config.yaml 读取），不在代码中硬编码
- 到达阈值当前写死 1m，后续可以改为 config 参数
- 目标点取 waypoints[0]（第一个航点），后面的航点在执行阶段由 ExecutionEngine 处理
- `UpdateDronePosition` 在 DroneManager 遥测回调中调用，频率跟随遥测（10Hz）

---

## Change 8：单元测试

```
位置：Backend/tests/
```

### 测试框架：Google Test

### 测试文件

| 文件 | 覆盖类 | 测试数 | 测试内容 |
|------|--------|--------|---------|
| `test_coordinate_converter.cpp` | CoordinateConverter | 3 | NED→UE、UE→NED、往返验证（精度 1e-9） |
| `test_quaternion_utils.cpp` | QuaternionUtils | 4 | 速度计算(3-4-5)、单位四元数(n=0)、Yaw翻转(30°→-30°)、GetUeYaw |
| `test_gps_anchor_manager.cpp` | GpsAnchorManager | 5 | 设置/获取/更新(is_new=false)/清除/多机/不存在(valid=false) |
| `test_command_queue.cpp` | CommandQueue | 7 | Push/Pop/FIFO顺序/溢出丢弃/暂停/Paused/清空/10000线程安全 |
| `test_state_machine.cpp` | StateMachine | 9 | 初始Offline/Online/超时Lost/重连Reset/PowerOn事件/Lost事件/Reconnect事件/Offline不超时 |

### 测试要点

**坐标往返验证**（`test_coordinate_converter.cpp:44-55`）：
```cpp
// 输入UE(1234, 5678, 901) → NED → UE，应回到原值
// 这个测试验证了 ×100 / ×0.01 / Z取反 是互逆的
CoordinateConverter::UeOffsetToNed(1234, 5678, 901, n, e, d);
CoordinateConverter::NedToUeOffset(n, e, d, rx, ry, rz);
// rx≈1234, ry≈5678, rz≈901（双精度，误差 1e-9）
```

**线程安全测试**（`test_command_queue.cpp:102-118`）：
```cpp
// 1 个生产者线程 Push 10000 次
// 1 个消费者线程 Pop 10000 次
// 验证最终队列为 0
```
通过这种测试确保 CommandQueue 在并发下不会丢数据或 crash。

**Yaw 翻转测试**（`test_quaternion_utils.cpp:39-49`）：
```cpp
// 绕 Z 轴旋转 30° → 四元数 [cos15°, 0, 0, sin15°]
// NED Yaw = 30°, UE Yaw 应为 -30°
double deg15 = 15.0 * M_PI / 180.0;
double yaw = QuatToEuler(cos(deg15), 0, 0, sin(deg15), ...);
EXPECT_NEAR(yaw, -30.0, 1e-3);
```
验证了右手系→左手系的 Yaw 取反逻辑。

---

## Change 9：代码审查修复

### 问题



| # | 文件:行 | 问题 |
|---|---------|------|
| 1 | `heartbeat_manager.cpp` | 指令队列无消费者，ProcessMoveCommand Push 进去的 Mode=1 指令没有人 Pop 发送 |
| 2 | `heartbeat_manager.cpp:21` | Start() 持锁调用 Stop() → Stop() 也锁 → 递归 mutex 死锁 |
| 3 | `udp_receiver.cpp:88` | string_view.data() 传给 YAML::Load，不是 null-terminated，yaml-cpp 可能读到垃圾 |
| 4 | `state_machine.h:37` | plain enum 被两个线程同时读写（Asio 线程写 state_，HTTP 线程读），UB |
| 5 | `drone_manager.cpp:173-180` | OnTelemetryReceived 和 StateMachine 回调都推送 PowerOn → WS 收到两次 |
| 6 | `assembly_controller.cpp:116` | timeout_sec = 60 写死在代码里，config.yaml 中的配置无效 |
| 7 | `config_loader.cpp` | 没有校验 heartbeat_hz≥2、端口是否冲突等 |
| 8 | `main.cpp:97-98` | signal 注册在线程启动之后，中间窗口 Ctrl+C 不优雅退出 |

### 修复方式

| # | 修改文件 | 修改方式 |
|---|---------|---------|
| 1 | `heartbeat_manager.h/.cpp` | HeartbeatLoop 增加 CommandProvider 回调，先调用回调取指令，有则 Mode=1 发送 |
| 2 | `heartbeat_manager.h/.cpp` | 拆出 `StopInternal()`（无锁），`Start()` 先调 StopInternal 再创建新线程 |
| 3 | `udp_receiver.cpp` | `string_view` → `string(listener.buffer.data(), bytes_transferred)` |
| 4 | `state_machine.h` | `state_` → `std::atomic<DroneConnectionState>` |
| 5 | `drone_manager.cpp` | 删除 OnTelemetryReceived 中 `if(is_new && state_change_cb_)` 段 |
| 6 | `assembly_controller.h/.cpp` | 构造函数接受 timeout_sec 参数，CheckTimeout 使用成员变量 |
| 7 | `config_loader.h/.cpp` | 新增 ValidateConfig() 函数 |
| 8 | `main.cpp` | signal 注册移到 asio_worker 线程启动之前 |

### 修复后的正确性

修复后，全链路数据流为：

```
UE5(WSServer) → ProcessMoveCommand(ue_x, ue_y, ue_z)
  → UeOffsetToNed(cm→m)
  → CommandQueue.Push(Mode=1)
  → HeartbeatLoop 500ms轮询:
      → get_command() → CommandQueue.Pop()
      → UdpSender.Send(Mode=1)  ← 指令真正发出

Jetson → UDP YAML → UdpReceiver.HandleReceive()
  → callback → DroneManager.OnTelemetryReceived()
  → StateMachine.OnTelemetryReceived()
      → PowerOn: StartHeartbeat + CommandProvider
      → Reconnect: RestartHeartbeat + new anchor
  → GpsAnchorManager.SetAnchor()
  → TelemetryCallback(msg) → WSServer → UE5
```

---

## 项目总统计

| 指标 | 数值 |
|------|------|
| 文件总数 | 35 |
| C++ 源文件 (.cpp) | 14 |
| C++ 头文件 (.h) | 12 |
| Python 脚本 | 1 |
| 配置文件 | 2 |
| 总代码行数 | 2840+ |
| 测试文件 | 6 |
| 测试用例 | 28 |
| 外部依赖 | 6 (cpp-httplib / Boost.Asio / yaml-cpp / nlohmann-json / spdlog / GTest) |

### 依赖关系图（简化）

```
main.cpp
  ├── core/config_loader → yaml-cpp (读取 config.yaml)
  ├── core/logger        → spdlog
  ├── communication/udp_receiver → Boost.Asio + yaml-cpp
  │     └→ 回调 → drone/drone_manager
  ├── communication/udp_sender → Boost.Asio
  │     └→ 被 drone/heartbeat_manager 调用
  ├── drone/heartbeat_manager → udp_sender
  │     └→ 通过 CommandProvider 回调 → drone_manager 取出指令
  ├── drone/drone_manager → drone_context + state_machine + command_queue
  │     ├→ conversion/coordinate_converter (纯数学)
  │     ├→ conversion/quaternion_utils (纯数学)
  │     └→ conversion/gps_anchor_manager
  └── execution/assembly_controller → coordinate_converter
```

模块间通过回调解耦（观察者模式），没有循环依赖。
