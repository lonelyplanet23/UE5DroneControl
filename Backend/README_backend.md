# DroneBackend — UE5DroneControl 后端服务

UE5DroneControl 项目的 C++ 后端服务。提供 HTTP REST 接口与 WebSocket 实时推送，通过 UDP 与 Jetson 伴飞计算机通信，管理多架无人机的状态、指令、集结流程与执行引擎。

---

## 目录

- [架构概览](#架构概览)
- [目录结构](#目录结构)
- [依赖](#依赖)
- [编译（Windows + vcpkg）](#编译windows--vcpkg)
- [配置](#配置)
- [运行](#运行)
- [部署](#部署)
- [HTTP API](#http-api)
- [WebSocket 协议](#websocket-协议)
- [测试](#测试)
- [数据流](#数据流)
- [代码审查速查](#已知问题)

---

## 架构概览

```
UE5 (Windows)                     Backend (Windows)                Jetson (Linux)
 HTTP Client ──HTTP──→  HttpServer (Boost.Beast)                   MicroXRCE Agent
 WS Client   ──WS────→  WsManager                                jetson_bridge.py
                        │                                              │
                        ├→ DroneManager ──→ HeartbeatManager           │
                        │       │              └→ UdpSender ──UDP──→   │
                        │       ├→ StateMachine                        │
                        │       ├→ CommandQueue                        │
                        │       └→ GpsAnchorManager                    │
                        │                                              │
                        ├→ AssemblyController                          │
                        │       └→ AssemblyPlanner                     │
                        │                                              │
                        └→ ExecutionEngine                             │
                                └→ CheckAvoidance                      │
                                                                       │
                        UdpReceiver ←──UDP YAML telemetry──────────────┘
```

---

## 目录结构

```
Backend/
├── CMakeLists.txt              # CMake 构建 (C++17, Boost, vcpkg)
├── build.bat                   # Windows 一键编译脚本
├── config.yaml                 # 运行时配置
├── main.cpp                    # 入口点
├── vcpkg.json                  # vcpkg 依赖声明
├── jetson_bridge.py            # Jetson 端 ROS2 ↔ UDP 桥接
│
├── core/
│   ├── types.h                 # 公有数据结构 (DroneControlPacket, TelemetryData, 等等)
│   ├── config_loader.h/.cpp    # YAML 配置加载 + 校验
│   └── logger.h                # spdlog 日志初始化
│
├── conversion/
│   ├── coordinate_converter.h/.cpp  # NED ↔ UE 偏移坐标转换
│   ├── quaternion_utils.h/.cpp      # 四元数 → 欧拉角 + 速度计算
│   ├── gps_anchor_manager.h/.cpp    # GPS 锚点管理
│   └── assignment_solver.h/.cpp     # 匈牙利算法 (Kuhn-Munkres)
│
├── communication/
│   ├── udp_receiver.h/.cpp     # UDP YAML 遥测接收 (Boost.Asio)
│   ├── udp_sender.h/.cpp       # UDP 24 字节控制包发送
│   └── ws_manager.h            # WebSocket 会话管理
│
├── drone/
│   ├── drone_manager.h/.cpp    # 多无人机管理器
│   ├── command_queue.h/.cpp    # 线程安全 FIFO 指令队列
│   ├── state_machine.h/.cpp    # 连接状态机 (Offline/Online/Lost)
│   ├── heartbeat_manager.h/.cpp # 心跳维持 (≥2Hz)
│   └── drone_context.h         # 单无人机运行时状态
│
├── execution/
│   ├── assembly_controller.h/.cpp   # 集结流程
│   ├── assembly_planner.h/.cpp      # 集结路径规划 (匈牙利 + 安全柱)
│   └── execution_engine.h/.cpp      # 执行引擎 (侦察/巡逻/攻击/避障)
│
├── http/
│   └── http_server.h/.cpp      # HTTP REST + WebSocket (Boost.Beast)
│
├── tools/
│   └── mock_server.py          # Mock HTTP+WS 测试服务器
│
└── tests/
    ├── test_main.cpp
    ├── test_coordinate_converter.cpp
    ├── test_quaternion_utils.cpp
    ├── test_gps_anchor_manager.cpp
    ├── test_command_queue.cpp
    ├── test_state_machine.cpp
    ├── test_assembly_planner.cpp
    ├── integration_week3.py
    └── mock_ue/                # Mock UE5 客户端
```

---

## 依赖

| 库 | 用途 | vcpkg 包名 |
|----|------|-----------|
| Boost (system, json, beast) | HTTP/WS 服务器, JSON 解析 | `boost-system boost-json boost-beast` |
| yaml-cpp | 配置文件和遥测解析 | `yaml-cpp` |
| nlohmann/json | JSON 序列化 (持久化) | `nlohmann-json` |
| spdlog | 日志 | `spdlog` |
| Google Test | 单元测试 | `gtest` |

---

## 编译（Windows + vcpkg）

### 前置条件

1. Visual Studio 2022 (Community 或 BuildTools)
2. [vcpkg](https://github.com/microsoft/vcpkg) 已安装并集成

### 安装依赖

```powershell
vcpkg install boost-system boost-json boost-beast yaml-cpp nlohmann-json spdlog gtest --triplet x64-windows
vcpkg integrate install
```

### 编译

```batch
cd Backend
build.bat
```

或在 Visual Studio 中：

```powershell
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_TOOLCHAIN_FILE=<vcpkg_path>/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release
```

输出：`build/Release/DroneBackend.exe`

### 编译测试并运行

```batch
cmake --build build --config Release --target DroneBackend_tests
ctest --test-dir build -C Release -V
```

---

## 配置

所有运行时参数在 `config.yaml` 中：

```yaml
server:
  http_port: 8080             # HTTP REST 端口
  ws_port: 8081               # WebSocket 端口
  debug: true                 # 启用 /api/debug/* 路由

drone:
  max_count: 6
  heartbeat_hz: 2             # PX4 offboard 要求 ≥2
  lost_timeout_sec: 10        # 失联超时
  arrival_threshold_m: 1.0    # 到达航点判断阈值
  assembly_timeout_sec: 60    # 集结超时
  avoidance_radius_m: 3.0     # 避障安全距离
  assembly_safety_cylinder_m: 2.0  # 集结安全圆柱半径

port_map:
  1: { send_port: 8889, recv_port: 8888, ros_topic_prefix: "/px4_1" }
  2: { send_port: 8891, recv_port: 8890, ros_topic_prefix: "/px4_2" }
  # ...

jetson:
  host: "192.168.30.104"      # Jetson IP

storage:
  path: "./data/drones.json"

log:
  level: "info"
  file: "./logs/backend.log"
```

---

## 运行

```batch
cd Backend\build\Release
DroneBackend.exe ..\..\config.yaml
```

后端启动后：
- HTTP: `http://localhost:8080/`
- WebSocket: `ws://localhost:8081/`
- Debug 接口: `http://localhost:8080/api/debug/drone/d1/state`

按 `Ctrl+C` 优雅停止。

---

## 部署

### 场景：独立 Windows 后端 + UE5 + Jetson

```
┌─────────────────┐     HTTP/WS     ┌──────────────────┐     UDP      ┌────────┐
│   UE5 (Windows)  │ ←───────────→ │  Backend (Windows) │ ←─────────→ │ Jetson │
│   局域网 IP: .10  │                 │  局域网 IP: .100    │              │ .104   │
└─────────────────┘                 └──────────────────┘              └────────┘
```

**Jetso n 端**需要运行两个进程：

1. MicroXRCE Agent（PX4 固件自带）：
```bash
MicroXRCEAgent serial --dev /dev/ttyTHS1 -b 921600
```

2. jetson_bridge.py（UDP ↔ ROS2 透明桥）：
```bash
python3 jetson_bridge.py 1    # slot=1, px4_1
python3 jetson_bridge.py 2    # slot=2, px4_2
python3 jetson_bridge.py 3    # slot=3, px4_3
```

**后端**：将 `config.yaml` 中的 `jetson.host` 改为 Jetson 的局域网 IP，启动 DroneBackend.exe。

**UE5**：将连接地址配置为后端的局域网 IP（默认 `ws://<backend_ip>:8081/` 和 `http://<backend_ip>:8080/`）。

---

## HTTP API

### 无人机管理

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/drones` | 获取所有已注册无人机 + 实时状态 |
| POST | `/api/drones` | 注册新无人机 |
| PUT | `/api/drones/{id}` | 更新无人机信息 |
| DELETE | `/api/drones/{id}` | 删除无人机 |
| GET | `/api/drones/{id}/anchor` | 获取 GPS 锚点 |

### 阵列任务

| 方法 | 路径 | 说明 |
|------|------|------|
| POST | `/api/arrays` | 下发阵列任务（进入集结流程） |
| POST | `/api/arrays/{id}/stop` | 停止阵列任务 |

### Debug 接口（`debug: true` 时可用）

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/api/debug/drone/{id}/state` | 查询无人机完整状态 |
| GET | `/api/debug/drone/{id}/queue` | 查询指令队列内容 |
| GET | `/api/debug/heartbeat/{id}` | 查询心跳统计 |
| POST | `/api/debug/drone/{id}/inject` | 注入模拟遥测数据 |
| POST | `/api/debug/cmd/{id}/move` | 模拟 move 指令 |
| POST | `/api/debug/cmd/{id}/pause` | 模拟暂停 |
| POST | `/api/debug/cmd/{id}/resume` | 模拟恢复 |
| POST | `/api/debug/cmd/{id}/array` | 模拟下发阵列任务 |
| POST | `/api/debug/cmd/{id}/target` | 注入目标识别事件 |
| POST | `/api/debug/cmd/batch/array` | 多机并发阵列 |
| GET | `/api/debug/arrays/{id}/state` | 阵列状态查询 |

示例：
```bash
# 注入模拟遥测
curl -X POST http://localhost:8080/api/debug/drone/d1/inject \
  -H 'Content-Type: application/json' \
  -d '{"position":[1,2,-10],"q":[1,0,0,0],"battery":85,"gps_lat":39.9,"gps_lon":116.3,"gps_alt":50}'

# 发送移动指令
curl -X POST http://localhost:8080/api/debug/cmd/d1/move \
  -H 'Content-Type: application/json' \
  -d '{"x":1000,"y":2000,"z":500}'

# 发起多机集结
curl -X POST http://localhost:8080/api/debug/cmd/batch/array \
  -H 'Content-Type: application/json' \
  -d '[
    {"drone_id":"d1","waypoints":[{"x":500,"y":0,"z":1000}]},
    {"drone_id":"d2","waypoints":[{"x":0,"y":0,"z":1000}]}
  ]'
```

---

## WebSocket 协议

连接：`ws://<host>:8081/`

### UE5 → 后端

```json
{"mode":"move",    "drone_id":"d1", "x":1000.0, "y":2000.0, "z":500.0}
{"type":"pause",   "drone_ids":["d1","d2"]}
{"type":"resume",  "drone_ids":["d1","d2"]}
```

### 后端 → UE5

```json
{"type":"telemetry",   "drone_id":"d1", "x":10000, "y":20000, "z":5000, "yaw":45, "pitch":0, "roll":0, "speed":3.5, "battery":85}
{"type":"event",       "drone_id":"d1", "event":"power_on", "gps_lat":39.9, "gps_lon":116.3, "gps_alt":50}
{"type":"event",       "drone_id":"d1", "event":"lost_connection"}
{"type":"alert",       "drone_id":"d1", "alert":"low_battery", "value":18}
{"type":"assembling",  "array_id":"a1", "ready_count":2, "total_count":4}
{"type":"assembly_complete", "array_id":"a1"}
{"type":"assembly_timeout",  "array_id":"a1", "ready_count":2, "total_count":4}
```

---

## 测试

### C++ 单元测试（28 个用例）

```bash
cd Backend
cmake --build build --target DroneBackend_tests
ctest --test-dir build -V
```

| 测试文件 | 覆盖模块 |
|---------|---------|
| `test_coordinate_converter` | NED↔UE 转换、往返验证 |
| `test_quaternion_utils` | 速度、单位四元数、Yaw 翻转 |
| `test_gps_anchor_manager` | 锚点设置/更新/清除/多机 |
| `test_command_queue` | 入队出队/FIFO/溢出/暂停/线程安全 |
| `test_state_machine` | Offline/Online/Lost 状态转移 |
| `test_assembly_planner` | 匈牙利分配、线段距离、冲突检测、高度分离 |

### 集成测试（Python）

```bash
# Mock UE5 客户端（独立测试后端）
cd Backend/tools
python mock_server.py    # 启动 Mock HTTP+WS 服务器

# 全链路集成测试（模拟 3 机集结 + 执行）
cd Backend/tests
python integration_week3.py
```

### 手动调试

```bash
# 启动后端
DroneBackend.exe config.yaml

# 查询状态
curl http://localhost:8080/api/drones
curl http://localhost:8080/api/debug/drone/d1/state

# 模拟遥测 → 移动指令 → 验证队列
curl -X POST http://localhost:8080/api/debug/drone/d1/inject \
  -H 'Content-Type: application/json' \
  -d '{"position":[1,2,-10],"q":[1,0,0,0],"battery":85}'
curl -X POST http://localhost:8080/api/debug/cmd/d1/move \
  -H 'Content-Type: application/json' \
  -d '{"x":1000,"y":0,"z":500}'
curl http://localhost:8080/api/debug/drone/d1/queue
```

---

## 数据流

```
UE5                          Backend                         Jetson/PX4
 │                              │                               │
 │ WS: move ──────────────────→ │                               │
 │                              ├→ UeOffsetToNed()              │
 │                              ├→ CommandQueue.Push(mode=1)    │
 │                              └→ HeartbeatManager ──UDP──→   │→ ROS2 → PX4
 │                              │                               │
 │                              │              ←──UDP YAML──── │← ROS2 odometry
 │                              ├→ UdpReceiver                 │
 │                              ├→ DroneManager                │
 │                              │   ├→ NedToUeOffset()         │
 │                              │   ├→ QuatToEuler()           │
 │                              │   ├→ StateMachine             │
 │                              │   └→ GpsAnchorManager        │
 │ ← WS: telemetry ─────────── │                               │
 │                              │                               │
 │ POST /api/arrays ──────────→ │                               │
 │                              ├→ AssemblyController.Start()   │
 │                              │   └→ AssemblyPlanner.Plan()   │
 │                              │       ├→ HungarianMinCost()   │
 │                              │       └→ 高度分离             │
 │ ← WS: assembling ────────── │                               │
 │                              │                               │
 │                              │ (全部就位)                    │
 │                              ├→ ExecutionEngine.StartTasks() │
 │ ← WS: assembly_complete ─── │                               │
```

---

建议在首次联调前优先修复 High 级别的线程安全问题。
