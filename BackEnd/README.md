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
| 集结控制 | 监控多机到达指定集结点，超时自动中止 |
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
│   └── assembly_controller.h/.cpp  # 集结流程状态机
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
| GET | `/api/debug/arrays/{id}/state` | 集结状态快照 |

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
        │                   └─→ HeartbeatLoop（2 Hz）
        │                             └─→ UdpSender → Jetson
        │
        └─→ WsManager.broadcast(telemetry / event / alert)
                  ▲
                  │
            DroneManager（回调）
                  ▲
                  │
            UdpReceiver ← Jetson（YAML 遥测，10 Hz）
```

**Jetson 端**（见 `../Jetson/jetson_bridge.py`）：订阅 PX4 ROS2 话题，将遥测打包为 YAML 通过 UDP 发往后端；接收后端 24 字节控制包，转发为 ROS2 `TrajectorySetpoint`。
