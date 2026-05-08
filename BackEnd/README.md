# DroneBackend

UE5DroneControl 项目的 C++ 后端服务。提供 HTTP REST 接口与 WebSocket 实时推送，通过 UDP 与 Jetson 伴飞计算机通信，管理多架无人机的状态、指令与集结流程。

---

## 目录

- [功能概述](#功能概述)
- [目录结构](#目录结构)
- [依赖](#依赖)
- [编译](#编译)
- [运行](#运行)
- [配置](#配置)
- [HTTP API](#http-api)
- [WebSocket 协议](#websocket-协议)
- [维护](#维护)
- [数据流](#数据流)

---

## 功能概述

| 模块 | 功能 |
|------|------|
| HTTP REST | 无人机注册、查询、更新、删除；集结任务下发与停止 |
| WebSocket | 遥测实时推送（10 Hz）；接收移动 / 暂停 / 恢复指令 |
| UDP 接收 | 接收 Jetson 发来的 YAML 格式遥测数据 |
| UDP 发送 | 向 Jetson 发送 24 字节控制包，2 Hz 心跳维持 PX4 Offboard 模式 |
| 状态机 | 管理每架无人机的连接状态（Offline / Online / Lost） |
| 集结控制 | 监控多机到达指定集结点，超时自动中止，自动向各机发送集结目标指令 |
| 执行引擎 | 集结完成后按模式（侦察/巡逻/攻击）驱动各机独立执行航点序列 |
| 实时避障 | 预测多机碰撞并临时调整低优先级机的目标点 |
| 持久化 | 无人机注册信息写入 `data/drones.json`，重启后自动恢复 |

---

## 目录结构

```
BackEnd/
├── main.cpp                  # 入口：模块初始化、主循环、信号处理
├── CMakeLists.txt            # 构建配置（C++17，vcpkg）
├── vcpkg.json                # 依赖声明
├── config.yaml               # 运行时配置（端口、超时、日志等）
├── build.bat                 # Windows 一键编译脚本
├── CHANGELOG.md              # 后端开发记录
│
├── core/                     # 基础设施
│   ├── types.h               # 全局数据结构与回调类型
│   ├── config_loader.h/.cpp  # YAML 配置加载与校验
│   └── logger.h              # spdlog 初始化
│
├── communication/            # 网络 I/O
│   ├── udp_receiver.h/.cpp   # 异步 UDP 遥测接收（Boost.Asio）
│   ├── udp_sender.h/.cpp     # UDP 控制包发送
│   └── ws_manager.h          # WebSocket 会话管理与广播
│
├── conversion/               # 坐标与数学工具
│   ├── coordinate_converter.h/.cpp  # NED ↔ UE 偏移坐标转换
│   ├── quaternion_utils.h/.cpp      # 四元数 → 欧拉角，速度计算
│   └── gps_anchor_manager.h/.cpp    # GPS 锚点（上电位置）管理
│
├── drone/                    # 无人机核心逻辑
│   ├── drone_context.h       # 单机运行时状态容器
│   ├── drone_manager.h/.cpp  # 多机注册表、遥测处理、指令分发
│   ├── command_queue.h/.cpp  # 线程安全 FIFO 指令队列（支持暂停）
│   ├── state_machine.h/.cpp  # 连接状态机（Offline/Online/Lost）
│   └── heartbeat_manager.h/.cpp  # 心跳线程，消费指令队列
│
├── execution/                # 任务执行
│   ├── assembly_controller.h/.cpp  # 集结流程状态机（含集结指令发送）
│   └── execution_engine.h/.cpp     # 执行引擎（侦察/巡逻/攻击模式，多机并发，实时避障）
│
├── http/                     # 服务器
│   ├── http_server.h/.cpp    # HTTP REST + WebSocket（Boost.Beast）
│   └── ...
│
└── tests/                    # C++ 单元测试（Google Test）
    ├── test_main.cpp
    ├── test_coordinate_converter.cpp
    ├── test_quaternion_utils.cpp
    ├── test_gps_anchor_manager.cpp
    ├── test_command_queue.cpp
    └── test_state_machine.cpp
```

**相关目录（BackEnd 之外）：**

| 目录 | 内容 |
|------|------|
| `../Jetson/` | Jetson 端 ROS2↔UDP 桥接脚本（`jetson_bridge.py`） |
| `../tools/` | 开发辅助工具：`mock_server.py`（模拟后端）、`mock_ue/`（模拟 UE 客户端）、集成测试脚本 |

---

## 依赖

通过 vcpkg 管理，首次编译自动安装：

| 库 | 版本要求 | 用途 |
|----|---------|------|
| Boost.Beast | ≥ 1.82 | HTTP / WebSocket 服务器 |
| Boost.Asio | ≥ 1.82 | 异步 UDP I/O |
| Boost.JSON | ≥ 1.82 | JSON 序列化 |
| yaml-cpp | ≥ 0.7 | 配置文件与遥测解析 |
| nlohmann/json | ≥ 3.11 | 持久化存储 |
| spdlog | ≥ 1.12 | 日志 |
| Google Test | ≥ 1.14 | 单元测试 |

---

## 编译

### 前提条件

- **Visual Studio 2022**（Community 或 BuildTools），含"使用 C++ 的桌面开发"工作负载
- **vcpkg** 已安装并集成：`vcpkg integrate install`

### 一键编译

```powershell
cd BackEnd
.\build.bat
```

编译产物：`BackEnd\build\Release\DroneBackend.exe`

### 手动编译

```powershell
cd BackEnd
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE="C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
cmake --build . --config Release --target DroneBackend
```

> 如果 vcpkg 安装在其他路径，修改 `-DCMAKE_TOOLCHAIN_FILE` 的值，或在 `build.bat` 中取消注释并修改 `VCPKG_TOOLCHAIN` 那一行。

### 编译并运行单元测试

```powershell
cmake --build build --config Release --target DroneBackend_tests
.\build\Release\DroneBackend_tests.exe
```

---

## 运行

```powershell
cd BackEnd\build\Release
.\DroneBackend.exe
```

或从项目根目录指定配置文件路径：

```powershell
.\BackEnd\build\Release\DroneBackend.exe .\BackEnd\config.yaml
```

正常启动输出：

```
[info] ============================================
[info]   Drone Backend v1.0.0
[info]   HTTP :8080  WS :8081
[info] ============================================
[info] [HTTP] Listening on 0.0.0.0:8080
[info] [WS]  Listening on 0.0.0.0:8081
[info] Backend started. Press Ctrl+C to stop.
```

按 `Ctrl+C` 优雅退出，等待所有线程结束后程序退出。

---

## 配置

编辑 `config.yaml`（编译时自动复制到 `build\` 目录）。若只修改配置而不重新编译，直接覆盖 `build\config.yaml`，或运行：

```powershell
Copy-Item BackEnd\config.yaml BackEnd\build\config.yaml
```

| 字段 | 默认值 | 说明 |
|------|--------|------|
| `server.http_port` | `8080` | HTTP REST 监听端口 |
| `server.ws_port` | `8081` | WebSocket 监听端口 |
| `server.debug` | `true` | 启用 `/api/debug/*` 调试接口 |
| `drone.max_count` | `6` | 最大注册无人机数量 |
| `drone.heartbeat_hz` | `2` | 心跳频率（Hz），不得低于 2 |
| `drone.lost_timeout_sec` | `10` | 无遥测超时判定失联（秒） |
| `drone.arrival_threshold_m` | `1.0` | 到达集结点的位置误差阈值（米） |
| `drone.assembly_timeout_sec` | `60` | 集结超时阈值（秒） |
| `drone.low_battery_threshold` | `20` | 低电量告警阈值（%） |
| `jetson.host` | `192.168.30.104` | Jetson 局域网 IP |
| `storage.path` | `./data/drones.json` | 无人机注册表持久化路径 |
| `log.level` | `info` | 日志级别：`debug` / `info` / `warn` / `error` |
| `log.file` | `./logs/backend.log` | 日志文件路径（留空则只输出到终端） |

**端口映射**（`port_map`）：每个 slot 编号对应固定的 UDP 端口，注册无人机时通过 `slot` 字段指定：

| Slot | 后端监听（遥测） | 后端发送（控制） | ROS2 话题前缀 |
|------|----------------|----------------|--------------|
| 1 | 8888 | 8889 | `/px4_1` |
| 2 | 8890 | 8891 | `/px4_2` |
| 3 | 8892 | 8893 | `/px4_3` |
| 4 | 8894 | 8895 | `/px4_4` |
| 5 | 8896 | 8897 | `/px4_5` |
| 6 | 8898 | 8899 | `/px4_6` |

---

## HTTP API

所有接口返回 JSON，跨域头已配置（`Access-Control-Allow-Origin: *`）。

### 无人机管理

#### `GET /api/drones`

列出所有已注册无人机及其实时状态。

**响应示例：**
```json
[
  {
    "id": 1,
    "name": "UAV1",
    "slot": 1,
    "status": "online",
    "battery": 85,
    "x": 0.0, "y": 0.0, "z": 500.0,
    "yaw": 0.0, "speed": 0.0,
    "ue_receive_port": 8888,
    "topic_prefix": "/px4_1"
  }
]
```

#### `POST /api/drones`

注册新无人机。

**请求体：**
```json
{ "name": "UAV1", "slot": 1 }
```

**响应（201）：**
```json
{ "id": 1, "id_str": "d1", "slot": 1, "name": "UAV1" }
```

#### `PUT /api/drones/{id}`

更新无人机名称、IP 等信息（不可修改 slot）。

#### `DELETE /api/drones/{id}`

删除无人机，同时停止其心跳线程并清除 GPS 锚点。

#### `GET /api/drones/{id}/anchor`

查询无人机 GPS 锚点（上电时记录的首个 GPS 坐标）。

**响应：**
```json
{ "drone_id": 1, "gps_lat": 39.9, "gps_lon": 116.3, "gps_alt": 50.0, "valid": true }
```

### 集结任务

#### `POST /api/arrays`

下发集结任务，后端开始监控各机到达初始航点。

**请求体：**
```json
{
  "array_id": "a1",
  "mode": "recon",
  "paths": [
    {
      "drone_id": "d1",
      "waypoints": [{ "x": 1000.0, "y": 2000.0, "z": -500.0 }]
    }
  ]
}
```

#### `POST /api/arrays/{id}/stop`

停止集结任务。

### 调试接口（需 `server.debug: true`）

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/debug/drone/{id}/state` | 无人机完整状态快照（含心跳统计） |
| GET | `/api/debug/drone/{id}/queue` | 指令队列内容与暂停状态 |
| GET | `/api/debug/heartbeat/{id}` | 心跳线程统计 |
| POST | `/api/debug/drone/{id}/inject` | 注入模拟遥测数据（用于无 Jetson 时测试） |
| POST | `/api/debug/cmd/{id}/move` | 发送移动指令（body: `{"x":0,"y":0,"z":0}`） |
| POST | `/api/debug/cmd/{id}/pause` | 暂停指令队列 |
| POST | `/api/debug/cmd/{id}/resume` | 恢复指令队列 |
| POST | `/api/debug/cmd/{id}/array` | 直接启动单机执行任务（跳过集结，用于测试执行模式） |
| POST | `/api/debug/cmd/{id}/target` | 注入目标识别事件（巡逻模式专用） |
| POST | `/api/debug/cmd/batch/array` | 多机并发阵列任务下发（含集结流程） |
| GET | `/api/debug/arrays/{id}/state` | 集结与执行状态快照 |

**`/api/debug/cmd/{id}/array` 请求体示例：**
```json
{
  "mode": "recon",
  "loop": false,
  "waypoints": [
    {"x": 1000, "y": 0, "z": -500},
    {"x": 2000, "y": 1000, "z": -500}
  ]
}
```
- `mode`：`"recon"`（侦察）/ `"patrol"`（巡逻）/ `"attack"`（攻击）
- `loop`：`true` 表示侦察模式循环（到末航点后回到第一个）
- `waypoints`：UE 偏移坐标（厘米），后端自动转换为 NED

**`/api/debug/cmd/{id}/target` 请求体示例（巡逻模式）：**
```json
{"x": 3000, "y": 3000, "z": -500}
```
注入后，巡逻模式无人机立即中断当前航点序列，飞向目标点，到达后停止。

---

## WebSocket 协议

连接地址：`ws://127.0.0.1:8081`（或将 `127.0.0.1` 替换为后端实际 IP）

### 后端 → 客户端（推送）

```jsonc
// 遥测（10 Hz，每架在线无人机各一条）
{ "type": "telemetry", "drone_id": 1, "x": 0.0, "y": 0.0, "z": 500.0,
  "yaw": 0.0, "pitch": 0.0, "roll": 0.0, "speed": 0.0, "battery": 85 }

// 状态事件
{ "type": "event", "drone_id": 1, "event": "power_on",
  "gps_lat": 39.9, "gps_lon": 116.3, "gps_alt": 50.0 }
{ "type": "event", "drone_id": 1, "event": "lost_connection" }
{ "type": "event", "drone_id": 1, "event": "reconnect" }

// 告警
{ "type": "alert", "drone_id": 1, "alert": "low_battery", "value": 15 }

// 集结进度
{ "type": "assembling", "array_id": "a1", "ready_count": 1, "total_count": 2 }
{ "type": "assembly_complete", "array_id": "a1" }
{ "type": "assembly_timeout", "array_id": "a1", "ready_count": 1, "total_count": 2 }
```

### 客户端 → 后端（指令）

```jsonc
// 移动（UE 偏移坐标，单位：厘米）
{ "type": "move", "drone_id": 1, "x": 1000.0, "y": 2000.0, "z": -500.0 }

// 暂停 / 恢复（支持批量）
{ "type": "pause",  "drone_ids": [1, 2] }
{ "type": "resume", "drone_ids": [1, 2] }
```

指令成功时，若请求包含 `request_id` 字段，后端返回 `command_ack`：
```json
{ "type": "command_ack", "command": "move", "request_id": "abc", "drone_id": 1 }
```

---

## 维护

### 清理日志文件

```powershell
Remove-Item BackEnd\logs\backend.log
```

或删除整个 logs 目录（运行时自动重建）：

```powershell
Remove-Item -Recurse -Force BackEnd\logs
```

### 清理编译产物

```powershell
Remove-Item -Recurse -Force BackEnd\build
```

重新编译时运行 `.\build.bat` 即可。

### 清理无人机注册数据

无人机注册信息持久化在 `BackEnd\data\drones.json`，删除后重启将清空注册表：

```powershell
Remove-Item BackEnd\data\drones.json
```

### 完整清理（恢复到源码状态）

```powershell
Remove-Item -Recurse -Force BackEnd\build
Remove-Item -Recurse -Force BackEnd\logs
Remove-Item -Recurse -Force BackEnd\data
```

---

## 数据流

```
UE5 前端（WebSocket 客户端）
        │  move / pause / resume
        ▼
  HttpServer（Boost.Beast）
        │
        ├─→ DroneManager.ProcessMoveCommand()
        │         └─→ CommandQueue.Push(mode=1)
        │                   └─→ HeartbeatLoop（config.heartbeat_hz，≥2 Hz）
        │                             └─→ UdpSender → Jetson
        │
        └─→ WsManager.broadcast(telemetry / event / alert / assembling)
                  ▲
                  │
            DroneManager（回调）
                  ▲
                  │
            UdpReceiver ← Jetson（YAML 遥测，10 Hz）

POST /api/arrays
        │
        ▼
  AssemblyController（ASSEMBLING）
        │  向各机发送集结目标点
        │  监控位置到达
        ▼
  assembly_complete → ExecutionEngine.StartTasks()
        │  每机独立线程，按模式执行航点序列
        │  侦察：循环/非循环航点序列
        │  巡逻：航点序列 + 目标识别事件中断
        │  攻击：航点序列，末航点悬停
        │
        └─→ 避障线程（200ms 轮询，预测碰撞，临时调整目标）
```

**Jetson 端**（见 `../Jetson/jetson_bridge.py`）：订阅 PX4 ROS2 话题，将遥测打包为 YAML 通过 UDP 发往后端；接收后端 24 字节控制包，转发为 ROS2 `TrajectorySetpoint`。

---

## 第四周验收状态

本节按项目架构文档与后端开发文档检查 `BackEnd/`，不包含旧目录 `BackEnd(before)/`。

| 周次 | 后端范围 | 当前状态 | 验证入口 |
|------|----------|----------|----------|
| 第一周 | C++ 后端骨架、配置加载、注册管理、持久化 | 已实现 | `DroneBackend_tests.exe`、`integration_week3.py` |
| 第二周 | UDP 收发、坐标转换、GPS 锚点、心跳维持 | 已实现 | `DroneBackend_tests.exe`、HTTP/UDP 遥测注入 |
| 第三周 | 连接状态机、WebSocket 推送、move/pause/resume、遥测/事件/告警、调试接口 | 已实现 | `integration_week3.py` |
| 第四周 | 集结流程、三种执行模式、多机并发调度、基础实时避障 | 已实现 | `integration_week4.py`、UDP 抓包/队列状态 |

重要说明：

- `heartbeat_hz`、`lost_timeout_sec`、`arrival_threshold_m`、`assembly_timeout_sec`、`avoidance_radius_m`、`avoidance_lookahead_sec`、`low_battery_threshold` 均从 `config.yaml` 读取。
- `POST /api/arrays` 会校验 `mode`、`paths`、`drone_id` 与航点，不再静默接受空路径或未注册无人机。
- `DebugBatchArray` 当前按一个 `AssemblyConfig.mode` 执行整批任务；如果请求中每条 path 都带 `mode`，最终会统一使用最后解析到的模式。正式多模式混合任务建议拆成多个任务，或后续将模式移动到 path 级别。
- 基础避障通过执行引擎线程检测在线无人机预测距离，并对 ID 较大的低优先级机临时发送偏移目标，3 秒后恢复原目标；该实现是第四周基础版，不等同于完整路径规划。

---

## 自动化验收

从项目根目录运行：

```powershell
.\BackEnd\build\Release\DroneBackend_tests.exe
python tools\integration_week3.py
python tools\integration_week4.py
```

预期结果：

```text
28 tests ... PASSED
week1-3 backend integration: PASS
week4 backend integration: ALL PASS
```

覆盖内容：

| 脚本 | 覆盖内容 |
|------|----------|
| `DroneBackend_tests.exe` | 坐标转换、四元数/Yaw、GPS 锚点、指令队列、连接状态机 |
| `tools/integration_week3.py` | 注册/去重、遥测推送、power_on/reconnect/lost、move/pause/resume、低电量、集结完成/超时 |
| `tools/integration_week4.py` | 集结流程、侦察/巡逻/攻击、循环侦察、多机并发、集结超时、执行期间暂停恢复 |

---

## 遥测模拟脚本

脚本位置：`../tools/simulate_telemetry.py`。建议从项目根目录运行。

该脚本有两种模式：

| 模式 | 参数 | 覆盖链路 | 使用场景 |
|------|------|----------|----------|
| HTTP 调试注入 | `--transport http`（默认） | `/api/debug/drone/{id}/inject` → `DroneManager` → WS | 无 Jetson、快速让 UE5 看到遥测 |
| UDP YAML 注入 | `--transport udp` | UDP YAML → `UdpReceiver` → `DroneManager` → WS | 验证真实 Jetson 遥测接收路径 |

常用命令：

```powershell
# 自动注册 d1，然后以 10Hz 通过 HTTP 调试接口注入悬停遥测
python tools\simulate_telemetry.py --register --drones 1 --hz 10

# 同时模拟 d1/d2
python tools\simulate_telemetry.py --register --drones 1 2 --hz 10

# 让 d1 做圆周运动
python tools\simulate_telemetry.py --drones 1 --move-circle

# 低电量告警测试
python tools\simulate_telemetry.py --drones 1 --battery 15

# 固定注入 NED 位置，单位米
python tools\simulate_telemetry.py --drones 1 --position 10 0 5

# 固定注入 UE 偏移目标，单位厘米；脚本会转换为 NED
python tools\simulate_telemetry.py --drones 1 --ue-target 1000 0 -500

# 直接发 UDP YAML 到后端默认遥测端口 8888/8890/...
python tools\simulate_telemetry.py --register --transport udp --drones 1 2 --hz 10

# 临时配置使用非默认 UDP 端口时，指定 slot 1 基准端口
python tools\simulate_telemetry.py --transport udp --udp-base-port 18888 --drones 1

# 后端在另一台机器
python tools\simulate_telemetry.py --base http://<后端IP>:8080 --udp-host <后端IP> --transport udp --drones 1
```

验收方法：

```powershell
curl.exe http://127.0.0.1:8080/api/debug/drone/1/state
curl.exe http://127.0.0.1:8080/api/drones/1/anchor
```

预期：

- `status` 为 `online`
- `anchor_valid` 为 `true`
- `x/y/z` 为 NED→UE 转换后的厘米坐标
- WebSocket 客户端收到 `event=power_on` 与持续 `telemetry`

---

## 手工测试指南（无 UE5 / 无 Jetson）

前提：

1. `BackEnd/config.yaml` 中 `server.debug: true`
2. 启动后端：

```powershell
.\BackEnd\build\Release\DroneBackend.exe .\BackEnd\config.yaml
```

3. WebSocket 客户端连接：`ws://127.0.0.1:8081/ws`

### 1. 注册、查询、更新、删除

```powershell
curl.exe -X POST http://127.0.0.1:8080/api/drones `
  -H "Content-Type: application/json" `
  -d "{\"name\":\"UAV1\",\"model\":\"PX4\",\"slot\":1,\"ip\":\"127.0.0.1\"}"

curl.exe http://127.0.0.1:8080/api/drones

curl.exe -X PUT http://127.0.0.1:8080/api/drones/1 `
  -H "Content-Type: application/json" `
  -d "{\"name\":\"UAV1-A\"}"

curl.exe -X DELETE http://127.0.0.1:8080/api/drones/1
```

验收点：

- 首次注册返回 `id=1`、`id_str=d1`
- 重复 `name` 或重复 `slot` 返回 `409`
- 删除后 `GET /api/drones` 中不再出现该无人机
- 重启后端后，未删除的无人机会从 `storage.path` 恢复

### 2. 遥测、锚点、坐标和姿态

```powershell
curl.exe -X POST http://127.0.0.1:8080/api/drones `
  -H "Content-Type: application/json" `
  -d "{\"name\":\"UAV1\",\"slot\":1}"

curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/1/inject `
  -H "Content-Type: application/json" `
  -d "{\"position\":[1,2,-3],\"q\":[0.965925826,0,0,0.258819045],\"velocity\":[3,4,0],\"battery\":85,\"gps_lat\":39.9042,\"gps_lon\":116.4074,\"gps_alt\":45}"

curl.exe http://127.0.0.1:8080/api/drones/1/anchor
curl.exe http://127.0.0.1:8080/api/debug/drone/1/state
```

验收点：

- WebSocket 收到 `event=power_on`，携带 `gps_lat/gps_lon/gps_alt`
- WebSocket 收到 `telemetry`
- `position=[1,2,-3]` 转换为 UE 偏移 `x=100,y=200,z=300`
- `velocity=[3,4,0]` 推送 `speed=5`
- 示例四元数对应 NED yaw 约 `30°`，UE 推送 yaw 约 `-30°`

### 3. move/pause/resume 与队列

```powershell
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/move `
  -H "Content-Type: application/json" `
  -d "{\"x\":1000,\"y\":2000,\"z\":-500}"

curl.exe http://127.0.0.1:8080/api/debug/drone/1/queue

curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/pause
curl.exe http://127.0.0.1:8080/api/debug/drone/1/state

curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/resume
```

验收点：

- 队列中出现 `mode=1`
- UE 偏移 `(1000,2000,-500)` 转换为 NED `(10,20,5)`
- pause 后 `queue_paused=true`
- resume 后 `queue_paused=false`
- WebSocket 发送 `{ "type":"move", "request_id":"m1", ... }` 时会收到 `command_ack`

### 4. 心跳和 UDP 控制包

上线后查看心跳：

```powershell
curl.exe http://127.0.0.1:8080/api/debug/heartbeat/1
```

验收点：

- `running=true`
- `sent_count` 持续增加
- 频率由 `config.yaml` 的 `drone.heartbeat_hz` 控制，且配置校验要求 `>=2`
- Wireshark 抓目标端口（slot 1 默认 `8889`）可看到 24 字节小端包，格式 `<dfffI>`
- 没有队列指令时 `Mode=0`，发送 move 后会出现 `Mode=1`

### 5. 失联、重连、告警

```powershell
# 低电量
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/1/inject `
  -H "Content-Type: application/json" `
  -d "{\"position\":[0,0,-10],\"q\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":15}"

# 停止注入遥测，等待 lost_timeout_sec 后查询
curl.exe http://127.0.0.1:8080/api/debug/drone/1/state

# 重新注入，触发 reconnect
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/1/inject `
  -H "Content-Type: application/json" `
  -d "{\"position\":[0,0,-10],\"q\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":85,\"gps_lat\":39.91,\"gps_lon\":116.41,\"gps_alt\":50}"
```

验收点：

- 电量 `<= drone.low_battery_threshold` 推送 `alert=low_battery`
- 停止遥测超过 `lost_timeout_sec` 后状态为 `lost`，推送 `event=lost_connection` 与 `alert=lost_connection`
- 重连首包推送 `event=reconnect`，并携带新 GPS 锚点

### 6. 集结流程

```powershell
curl.exe -X POST http://127.0.0.1:8080/api/drones -H "Content-Type: application/json" -d "{\"name\":\"UAV2\",\"slot\":2}"

curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[0,0,-5],\"q\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":85,\"gps_lat\":39.9,\"gps_lon\":116.3,\"gps_alt\":50}"
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/2/inject -H "Content-Type: application/json" -d "{\"position\":[5,0,-5],\"q\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":90,\"gps_lat\":39.9,\"gps_lon\":116.31,\"gps_alt\":50}"

curl.exe -X POST http://127.0.0.1:8080/api/arrays `
  -H "Content-Type: application/json" `
  -d "{\"array_id\":\"a1\",\"mode\":\"recon\",\"paths\":[{\"pathId\":1,\"drone_id\":\"d1\",\"bClosedLoop\":false,\"waypoints\":[{\"location\":{\"x\":1000,\"y\":0,\"z\":-500}},{\"location\":{\"x\":2000,\"y\":1000,\"z\":-500}}]},{\"pathId\":2,\"drone_id\":\"d2\",\"bClosedLoop\":false,\"waypoints\":[{\"location\":{\"x\":-1000,\"y\":0,\"z\":-500}},{\"location\":{\"x\":-2000,\"y\":1000,\"z\":-500}}]}]}"

# 到达首航点。UE(1000,0,-500) -> NED(10,0,5)
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[10,0,5],\"q\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":85}"
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/2/inject -H "Content-Type: application/json" -d "{\"position\":[-10,0,5],\"q\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":90}"

curl.exe http://127.0.0.1:8080/api/debug/arrays/a1/state
```

验收点：

- 下发任务后 WebSocket 收到 `assembling`
- 每架机到达首航点后 `ready_count` 增加
- 全部到达后收到 `assembly_complete`
- `/api/debug/arrays/a1/state` 中 `exec_running=true`

### 7. 执行模式

单机调试接口会跳过集结，直接启动执行引擎。

```powershell
# 侦察模式，循环
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/array `
  -H "Content-Type: application/json" `
  -d "{\"mode\":\"recon\",\"loop\":true,\"waypoints\":[{\"x\":1000,\"y\":0,\"z\":-500},{\"x\":2000,\"y\":1000,\"z\":-500}]}"

# 巡逻模式
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/array `
  -H "Content-Type: application/json" `
  -d "{\"mode\":\"patrol\",\"loop\":false,\"waypoints\":[{\"x\":1000,\"y\":0,\"z\":-500},{\"x\":2000,\"y\":0,\"z\":-500}]}"

curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/target `
  -H "Content-Type: application/json" `
  -d "{\"x\":3000,\"y\":3000,\"z\":-500}"

# 攻击模式
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/array `
  -H "Content-Type: application/json" `
  -d "{\"mode\":\"attack\",\"loop\":false,\"waypoints\":[{\"x\":500,\"y\":0,\"z\":-500},{\"x\":1000,\"y\":500,\"z\":-500},{\"x\":1500,\"y\":1000,\"z\":-500}]}"
```

验收点：

- 侦察非循环：按航点顺序发送，末航点后悬停
- 侦察循环：到末航点后回到第一个航点
- 巡逻：`target` 注入后中断当前航点序列，飞向目标，到达后停止
- 攻击：依次经过中间航点，到最后攻击目标点后悬停，不循环

### 8. 多机并发和基础避障

```powershell
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/batch/array `
  -H "Content-Type: application/json" `
  -d "[{\"drone_id\":\"d1\",\"mode\":\"recon\",\"waypoints\":[{\"x\":100,\"y\":0,\"z\":-300}]},{\"drone_id\":\"d2\",\"mode\":\"recon\",\"waypoints\":[{\"x\":-100,\"y\":0,\"z\":-300}]}]"
```

验收点：

- 两架无人机进入同一集结任务
- 集结完成后执行引擎为每架机启动独立线程
- `/api/debug/drone/1/queue` 与 `/api/debug/drone/2/queue` 互不影响
- 避障验证需要两机都处在执行阶段，持续注入相互靠近的位置与速度；使用 Wireshark 抓控制端口，确认低优先级机收到临时偏移目标，约 3 秒后恢复原目标

---

## 真实 UE5 联调

### 后端准备

1. 后端与 UE5 机器必须在同一局域网。
2. 后端启动后应监听所有网卡：

```text
[HTTP] Listening on 0.0.0.0:8080
[WS] Listening on 0.0.0.0:8081
```

3. Windows 防火墙允许 `DroneBackend.exe` 的 TCP `8080/8081`，以及需要真实 Jetson 时的 UDP `8888~8899`。

### UE5 配置

`UDroneNetworkManager` 暴露两个配置项：

| 属性 | 示例 |
|------|------|
| `BackendBaseUrl` | `http://<后端IP>:8080` |
| `WebSocketUrl` | `ws://<后端IP>:8081/ws` |

同机测试可使用默认值：

```text
BackendBaseUrl = http://127.0.0.1:8080
WebSocketUrl   = ws://127.0.0.1:8081/ws
```

跨机器联调时不要使用 `127.0.0.1`，必须改成后端机器的局域网 IP。

### UE5 操作验收

| 功能 | UE5 操作 | 后端/日志验收 |
|------|----------|---------------|
| 连接后端 | 打开默认地图 `Lvl_TopDown` 运行 | Output Log 出现 `DroneNetworkManager Initialized`、`DroneWS Connected` |
| 注册表同步 | 后端已注册无人机，UE 等待一次轮询 | Output Log 出现 `Registered drone N` |
| 遥测显示 | 启动 `simulate_telemetry.py` 或真实 Jetson | UE 收到 `telemetry`，本地 Registry 更新位置/姿态/电量 |
| 上电锚定 | 首包遥测带 GPS | UE 收到 `power_on`，缓存 GPS anchor |
| 失联重连 | 停止遥测超过超时，再恢复 | UE 收到 `lost_connection`、`reconnect` |
| 实时移动 | 左键选中无人机，再点击地图目标 | 后端日志出现 `[WS] MOVE`，`/api/debug/drone/{id}/queue` 或 UDP 抓包出现 `Mode=1` |
| 暂停/恢复 | 选中无人机按 `P` | 后端收到 `pause/resume`，`queue_paused` 切换 |
| 低电量 | 注入 `battery=15` | UE 收到 `alert=low_battery` |
| 集结事件 | 用 `curl` 或 Mock UE 下发 `/api/arrays` | UE 收到 `assembling`、`assembly_complete` 或 `assembly_timeout` |

当前 UE C++ 通讯层已支持 `GET /api/drones`、WebSocket `move/pause/resume`、遥测/事件/告警接收。正式阵列 UI 提交入口如果尚未接入，可先用 `curl`、Apifox 或 `tools/mock_ue` 下发 `/api/arrays`，UE 仍可通过 WebSocket 观察集结事件与遥测变化。

### 配合遥测模拟测试 UE5

```powershell
# 1. 启动后端
.\BackEnd\build\Release\DroneBackend.exe .\BackEnd\config.yaml

# 2. 注册并持续模拟 d1/d2
python tools\simulate_telemetry.py --register --drones 1 2 --hz 10

# 3. 打开 UE5，运行默认地图
```

观察：

- UE5 Output Log 显示 WebSocket 连接成功
- 后端 `GET /api/drones` 中无人机 `status=online`
- UE 本地 Registry 的位置、电量随遥测更新

---

## Mock UE 客户端

Mock UE 位于 `../tools/mock_ue/`，只调用正式 HTTP/WS 接口，不使用后端 debug 接口。

```powershell
cd tools\mock_ue
pip install -r requirements.txt
python -m mock_ue.main shell
```

常用命令：

```text
register name=drone-1 model=PX4 slot=1 ip=127.0.0.1 port=8889
list
move d1 1000 0 -500
pause d1
resume d1
array samples/recon_two_drones.json
array samples/patrol_single_drone.json
stop a1
events
telemetry
```

Mock UE 用于验证 UE 通讯协议和后端事件推送；要产生遥测，仍需真实 Jetson 或 `tools/simulate_telemetry.py`。

---

## 真实 Jetson / PX4 联调

1. 修改 `BackEnd/config.yaml`：

```yaml
jetson:
  host: "<Jetson局域网IP>"
```

2. 确认 `port_map` 与 Jetson slot 一致：

| Slot | 后端接收遥测 | 后端发送控制 |
|------|--------------|--------------|
| 1 | 8888 | 8889 |
| 2 | 8890 | 8891 |
| 3 | 8892 | 8893 |
| 4 | 8894 | 8895 |
| 5 | 8896 | 8897 |
| 6 | 8898 | 8899 |

3. 后端启动：

```powershell
.\BackEnd\build\Release\DroneBackend.exe .\BackEnd\config.yaml
```

4. Jetson 启动桥接：

```bash
python3 Jetson/jetson_bridge.py 1
python3 Jetson/jetson_bridge.py 2
```

5. 验收：

- Jetson 发送 YAML 遥测到后端对应 `recv_port`
- 后端收到首包后推送 `power_on`，状态变为 `online`
- 后端心跳和 move 指令以 24 字节 `<dfffI` 控制包发往 Jetson 对应 `send_port`
- Jetson 将控制包转成 ROS2 `OffboardControlMode` / `TrajectorySetpoint`

推荐抓包过滤：

```text
udp.port == 8888 || udp.port == 8889 || udp.port == 8890 || udp.port == 8891
```

---

## 验收速查表

| 功能 | 后端操作 | UE5 操作 | 通过标准 |
|------|----------|----------|----------|
| 注册 | `POST /api/drones` | 注册 UI 或 Mock UE `register` | 返回唯一 ID，slot/name 冲突返回 409 |
| 查询 | `GET /api/drones` | UE 自动轮询 | 列表字段完整，状态/电量更新 |
| 更新 | `PUT /api/drones/{id}` | 编辑 UI 或 HTTP | 再次查询字段已变 |
| 删除 | `DELETE /api/drones/{id}` | 删除 UI 或 HTTP | 注册表移除，心跳停止 |
| 持久化 | 注册后重启后端 | UE 重启后轮询 | 注册数据仍存在 |
| HTTP 遥测 | `POST /api/debug/drone/{id}/inject` | UE 观察遥测 | WS 收到 `telemetry` |
| UDP 遥测 | `simulate_telemetry.py --transport udp` | UE 观察遥测 | `UdpReceiver` 解析，状态 online |
| GPS 锚点 | 首包含 GPS | UE 收到 `power_on` | `/anchor` 有有效 GPS |
| 坐标转换 | 注入 `[1,2,-3]` | UE 位置更新 | 推送 `(100,200,300)` cm |
| 姿态转换 | 注入已知四元数 | UE 姿态更新 | Yaw 按左手系翻转 |
| 心跳 | `GET /api/debug/heartbeat/{id}` | 无需 UE | `sent_count` 持续增长 |
| move | `POST /api/debug/cmd/{id}/move` 或 WS | 选中后点地图 | 队列/UDP 出现 `Mode=1` |
| pause/resume | debug 或 WS | 按 `P` | `queue_paused` 切换 |
| 失联 | 停止遥测 | UE 等待 | `lost_connection` event/alert |
| 重连 | 重新注入遥测 | UE 观察 | `reconnect` 携带新 GPS |
| 低电量 | 注入 `battery <= threshold` | UE 观察告警 | `alert=low_battery` |
| 集结 | `POST /api/arrays` | 阵列 UI 或 HTTP | `assembling` → `assembly_complete` |
| 集结超时 | 不注入到达 | UE 观察 | `assembly_timeout` |
| 侦察 | `mode=recon` | 阵列任务 | 按航点推进，支持循环 |
| 巡逻 | `mode=patrol` + target | 目标识别事件 | 中断巡逻飞目标 |
| 攻击 | `mode=attack` | 阵列任务 | 到末航点悬停 |
| 多机并发 | 多 paths | 多机任务 | 各机队列独立推进 |
| 基础避障 | 两机接近 + 抓 UDP | UE 观察绕行 | 低优先级机临时偏移后恢复 |
