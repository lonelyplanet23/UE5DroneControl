# DroneBackend

UE5DroneControl 的 C++17 后端服务。它向 UE5 提供 HTTP REST 与 WebSocket 接口，通过 UDP 与 Jetson 机载电脑通信，负责无人机注册、状态管理、坐标转换、可靠控制派发、集结、任务执行和基础避障。

> 本文件是后端唯一 README，已合并原 `README.md` 与 `README_backend.md` 的有效内容。详细字段以项目根目录的《接口与通讯数据规范》为准，坐标以《坐标系转换说明》为准。

## 1. 当前通信架构

```text
UE5（Windows）                 DroneBackend（Windows）                Jetson / PX4
┌──────────────┐              ┌──────────────────────┐              ┌──────────────────┐
│ HTTP Client  │──REST───────→│ HttpServer           │              │ jetson_bridge.py │
│ WS Client    │←─WS 双向────→│ WsManager            │              │ MicroXRCEAgent   │
└──────────────┘              │                      │              └────────┬─────────┘
                              │ DroneManager         │                       │ ROS2
                              │ ├─ StateMachine      │                       ▼
                              │ ├─ CommandQueue      │                     PX4 飞控
                              │ └─ GpsAnchorManager  │
                              │                      │
                              │ HeartbeatManager     │──UDP JSON 3-of-5────→ Jetson
                              │ UdpReceiver          │←─UDP YAML + ACK────── Jetson
                              │ AssemblyController   │
                              │ ExecutionEngine      │
                              └──────────────────────┘
```

控制与遥测是两条独立 UDP 链路：

- 后端 → Jetson：UDP JSON v1，目标是相对本次上电原点的 NED 米坐标。
- Jetson → 后端：UDP YAML，包含 PX4 遥测以及最后已应用控制指令的 `control_ack`。
- Jetson 独立以 50Hz 向 PX4 发布 `OffboardControlMode` 和 `TrajectorySetpoint`；后端的 `heartbeat_hz` 是后端到 Jetson 的 JSON 发送频率，不是 PX4 Offboard 心跳频率。

## 2. 关键设计约束

### 2.1 UDP 控制可靠性

默认采用 `3-of-5`，不是简单统计任意 UDP 包：

1. 后端为每次进程启动生成 `session_id`。
2. 每条逻辑指令具有唯一 `command_id` 和单调递增 `sequence`。
3. move 指令以相同执行负载重复发送 5 次，`repeat_index=1..5`。
4. Jetson 在 2.5 秒内收到 3 个不同 `repeat_index`、相同 `command_id` 且负载一致的包后才应用。
5. Jetson 拒绝旧序号、旧会话、slot 串包、坐标系或单位不符、同 ID 不同负载的数据报。
6. Jetson 应用后在遥测中返回 `control_ack`，后端日志输出 `[ControlAck]`。

`command_ack` 与 `control_ack` 含义不同：

- WebSocket `command_ack`：后端已经接受 UE 请求并入队。
- 遥测 `control_ack`：Jetson 已达到确认阈值并把目标写入 PX4 setpoint 缓存。

### 2.2 坐标与原点

```text
UE5 目标世界坐标（cm）
  - UE5 AnchorWorldLocation
        ↓
UE 相对上电锚点偏移（cm）
        ↓ 后端 UeOffsetToNed
NED North/East/Down（m，reference=power_on_origin）
        ↓ UDP JSON，Jetson 不再转换
PX4 TrajectorySetpoint
```

公式：

```text
NED_North = UE_offset_X × 0.01
NED_East  = UE_offset_Y × 0.01
NED_Down  = UE_offset_Z × -0.01

UE_offset_X = NED_North × 100
UE_offset_Y = NED_East  × 100
UE_offset_Z = NED_Down  × -100
```

必须遵守：

- 单位：UE5 为厘米，PX4 NED 为米。
- 原点：收发坐标都以该无人机本次上电后的本地 NED 原点为零点。
- 遥测实际位置不能覆盖期望目标。后端仅在 pause 时用最新实测位置创建 hold。
- 断联重新上电后会清空旧指令，避免把旧原点下的相对目标重放到新原点。
- 多机的本地 NED 原点不同，阵列任务不能把同一参考机坐标直接发给所有飞机；见“已知边界”。

## 3. 功能与模块

| 模块 | 职责 |
|---|---|
| `http/HttpServer` | REST、WebSocket 会话、请求校验、事件推送 |
| `communication/UdpSender` | 控制指令 JSON 序列化、session/command ID、UDP 发送 |
| `communication/UdpReceiver` | YAML 遥测及 `control_ack` 解析 |
| `drone/DroneManager` | 无人机上下文、坐标转换、指令入队、暂停/恢复 |
| `drone/HeartbeatManager` | 5Hz 派发、每条 move 重发、持续 hold |
| `drone/CommandQueue` | 每机独立、有界、线程安全 FIFO |
| `drone/StateMachine` | Offline / Connecting / Online / Lost / Reconnect |
| `conversion/CoordinateConverter` | UE 相对偏移厘米 ↔ NED 米 |
| `conversion/GpsAnchorManager` | 每架无人机上电 GPS 锚点 |
| `execution/AssemblyController` | 集结目标派发、到达判断、超时 |
| `execution/ExecutionEngine` | scout / patrol / attack 航点执行与基础避障 |

关键目录：

```text
Backend/
├─ main.cpp
├─ config.yaml
├─ CMakeLists.txt
├─ vcpkg.json
├─ build_simple.bat
├─ core/
├─ communication/
├─ conversion/
├─ drone/
├─ execution/
├─ http/
└─ tests/

../Drone/
├─ jetson_bridge.py
└─ test_jetson_bridge_protocol.py
```

## 4. 依赖与编译

### 4.1 依赖

| 依赖 | 用途 |
|---|---|
| Boost.Asio / Beast / JSON | UDP、HTTP、WebSocket、JSON |
| yaml-cpp | 配置与遥测 YAML |
| nlohmann/json | 后端 → Jetson 控制 JSON |
| spdlog / fmt | 日志 |
| GoogleTest | C++ 单元测试 |

需要 Visual Studio 2022、CMake 和 vcpkg。`vcpkg.json` 会安装所需包。

### 4.2 推荐构建

当前开发机使用 `C:\dev\vcpkg`：

```powershell
cd D:\RedAlert\UE5DroneControl\Backend
.\build_simple.bat
```

手动构建：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:\dev\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release --target DroneBackend
```

输出：

```text
Backend/build/Release/DroneBackend.exe
```

控制协议已从二进制改为 JSON，不能继续使用旧可执行文件。源码更新后必须重新编译后端，并同时部署新版 `Drone/jetson_bridge.py`。

## 5. 配置

权威配置文件为 `Backend/config.yaml`：

```yaml
server:
  http_port: 8080
  ws_port: 8081
  debug: true

drone:
  max_count: 6
  heartbeat_hz: 5
  command_repeat_count: 5
  lost_timeout_sec: 10
  arrival_threshold_m: 1.0
  assembly_timeout_sec: 60
  avoidance_radius_m: 3.0
  avoidance_lookahead_sec: 2.0
  assembly_safety_cylinder_m: 2.0
  low_battery_threshold: 20

port_map:
  1: { send_port: 8889, recv_port: 8888, ros_topic_prefix: "/px4_1" }
  2: { send_port: 8891, recv_port: 8890, ros_topic_prefix: "/px4_2" }
  3: { send_port: 8893, recv_port: 8892, ros_topic_prefix: "/px4_3" }
  4: { send_port: 8895, recv_port: 8894, ros_topic_prefix: "/px4_4" }
  5: { send_port: 8897, recv_port: 8896, ros_topic_prefix: "/px4_5" }
  6: { send_port: 8899, recv_port: 8898, ros_topic_prefix: "/px4_6" }

jetson:
  host: "192.168.30.104"

storage:
  path: "./data/drones.json"

log:
  level: "debug"
  file: "./logs/backend.log"
```

`jetson.host` 是后端控制数据报的真实目标 IP。注册接口里的 `ip` 不是当前控制链路的最终发送地址。

## 6. 运行与部署

### 6.1 后端

从项目根目录运行，确保相对配置、日志和存储路径一致：

```powershell
.\Backend\build\Release\DroneBackend.exe .\Backend\config.yaml
```

启动后：

- HTTP：`http://<backend-ip>:8080`
- WebSocket：`ws://<backend-ip>:8081/ws`
- `Ctrl+C`：停止 HTTP/WS、UDP 和心跳线程。

### 6.2 Jetson

先启动 MicroXRCE Agent，再启动桥接脚本：

```bash
source /opt/ros/<distro>/setup.bash
source ~/ros2_ws/install/setup.bash

python3 jetson_bridge.py 1 2>&1 | tee jetson_bridge_slot1.log
```

默认配置适用于无 ROS 命名空间的话题。如果 PX4 话题是 `/px4_1/fmu/...`：

```bash
ROS_TOPIC_PREFIX=/px4_1 python3 jetson_bridge.py 1
```

常用环境变量：

| 变量 | 默认值 | 用途 |
|---|---:|---|
| `BACKEND_HOST` | `192.168.30.100` | 遥测发送目标 |
| `CONTROL_PORT` | slot1=`8889` | JSON 控制监听端口 |
| `TELEMETRY_PORT` | slot1=`8888` | 后端 YAML 遥测端口 |
| `MAVLINK_SYSTEM_ID` | `slot + 1` | PX4 目标系统 ID |
| `ROS_TOPIC_PREFIX` | 空 | ROS2 命名空间 |
| `COMMAND_CONFIRM_COUNT` | `3` | 执行确认阈值 |
| `COMMAND_CONFIRM_WINDOW_SEC` | `2.5` | 确认时间窗 |
| `MAX_ABS_TARGET_M` | `5000` | 单轴绝对坐标安全上限 |
| `ARM_NOW` | 未设置 | 设为 `1` 时跳过人工 ARM 确认，仅限受控测试 |

### 6.3 防火墙

- 后端 Windows：TCP `8080/8081`，UDP `8888/8890/...` 入站。
- Jetson：UDP `8889/8891/...` 入站。
- 抓包示例：`udp.port == 8888 || udp.port == 8889`。

## 7. HTTP API 速查

完整字段见根目录《接口与通讯数据规范》；当前代码路由如下。

### 7.1 正式接口

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/` | 服务信息、HTTP/WS 端口 |
| GET | `/api/drones` | 注册表与实时状态 |
| POST | `/api/drones` | 注册无人机 |
| PUT | `/api/drones/{id}` | 更新注册信息 |
| DELETE | `/api/drones/{id}` | 删除无人机并停止发送线程 |
| GET | `/api/drones/{id}/anchor` | 查询 GPS 上电锚点 |
| POST | `/api/arrays` | 下发阵列任务 |
| POST | `/api/arrays/{id}/stop` | 停止阵列任务 |

### 7.2 Debug 接口

仅在 `server.debug: true` 时启用：

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/api/debug/drone/{id}/state` | 状态、心跳、最后 `control_ack` |
| GET | `/api/debug/drone/{id}/queue` | 队列中的 NED 目标及 sequence |
| GET | `/api/debug/heartbeat/{id}` | 发送成功/失败、活动序号、重发进度 |
| POST | `/api/debug/drone/{id}/inject` | 注入模拟遥测 |
| POST | `/api/debug/cmd/{id}/move` | 注入 UE 相对偏移目标（cm） |
| POST | `/api/debug/cmd/{id}/pause` | 暂停并在实测位置 hold |
| POST | `/api/debug/cmd/{id}/resume` | 恢复队列 |
| POST | `/api/debug/cmd/{id}/array` | 单机阵列调试 |
| POST | `/api/debug/cmd/batch/array` | 多机阵列调试 |
| POST | `/api/debug/cmd/{id}/target` | 目标识别事件 |
| GET | `/api/debug/arrays/{id}/state` | 阵列状态 |

示例：

```powershell
curl.exe -X POST http://127.0.0.1:8080/api/drones `
  -H "Content-Type: application/json" `
  -d '{"name":"UAV1","model":"PX4","slot":1}'

curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/1/inject `
  -H "Content-Type: application/json" `
  -d '{"position":[0,0,0],"q":[1,0,0,0],"battery":90,"gps_lat":39.9042,"gps_lon":116.4074,"gps_alt":50}'

curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/move `
  -H "Content-Type: application/json" `
  -d '{"x":1000,"y":2000,"z":500}'

curl.exe http://127.0.0.1:8080/api/debug/heartbeat/1
curl.exe http://127.0.0.1:8080/api/debug/drone/1/state
```

上述 move 表示 UE 相对锚点 `(1000,2000,500)cm`，转换后为 NED `(10,20,-5)m`。

## 8. WebSocket 协议速查

连接：`ws://<backend-ip>:8081/ws`。

UE5 → 后端：

```json
{"mode":"move","drone_id":"d1","x":1000,"y":2000,"z":500,"request_id":"req-1"}
{"type":"pause","drone_ids":["d1","d2"],"request_id":"req-2"}
{"type":"resume","drone_ids":["d1","d2"],"request_id":"req-3"}
```

后端接受请求后可返回：

```json
{"type":"command_ack","command":"move","request_id":"req-1","drone_id":1,"drone_id_str":"d1"}
```

后端 → UE5 主要推送：

| type | 说明 |
|---|---|
| `telemetry` | UE 相对偏移、姿态、速度、电量 |
| `event` | `power_on`、`lost_connection`、`reconnect` |
| `alert` | 低电量、断联等告警 |
| `assembling` | 集结进度 |
| `assembly_complete` | 集结完成并启动执行引擎 |
| `assembly_timeout` | 集结超时 |
| `assignment_result` | 自动分配结果 |

## 9. UDP JSON 控制协议

示例：

```json
{
  "protocol": "ue5_drone_control",
  "version": 1,
  "type": "control",
  "session_id": "backend-1783650000000000-ab12cd34",
  "command_id": "backend-1783650000000000-ab12cd34-d1-s1783650000000100",
  "sequence": 1783650000000100,
  "drone_id": 1,
  "slot": 1,
  "mode": "move",
  "issued_at_unix_s": 1783650000.0,
  "sent_at_unix_s": 1783650000.2,
  "target": {
    "frame": "NED",
    "reference": "power_on_origin",
    "unit": "m",
    "north": 10.0,
    "east": 20.0,
    "down": -5.0
  },
  "delivery": {"repeat_index": 1, "repeat_total": 5}
}
```

hold 使用新的 sequence/command ID，目标仍是最后期望位置，`repeat_total=0` 表示持续发送。Jetson 只在确认后更新 setpoint，并通过 YAML 返回：

```yaml
control_ack:
  session_id: backend-...
  command_id: backend-...-d1-s...
  sequence: 1783650000000100
  mode: move
  confirmed_packets: 3
  applied_at_unix_s: 1783650000.6
```

## 10. 测试

### 10.1 Jetson 协议测试（不要求安装 ROS2）

```powershell
cd D:\RedAlert\UE5DroneControl
python -m unittest Drone\test_jetson_bridge_protocol.py -v
```

覆盖：JSON 解析、坐标单位拒绝、3 包确认、重复索引去重、旧序号拒绝、同 ID 负载冲突和后端会话切换。

### 10.2 C++ 单元测试

```powershell
cmake --build Backend\build --config Release --target DroneBackend_tests
.\Backend\build\Release\DroneBackend_tests.exe
```

覆盖坐标转换、四元数、GPS 锚点、指令队列、状态机和集结规划。

### 10.3 集成测试和遥测模拟

```powershell
python tools\integration_week3.py
python tools\integration_week4.py
python tools\integration_week5.py

python tools\simulate_telemetry.py --register --drones 1 --hz 10
python tools\simulate_telemetry.py --register --transport udp --drones 1 --hz 10
```

完整人工步骤见 `Backend/TEST_GUIDE.md`。涉及真实控制 JSON 时，必须使用新版后端和新版 Jetson 脚本；旧集成脚本若直接解析 24 字节控制包，需要先更新测试逻辑。

## 11. 日志与故障定位

### 11.1 后端关键日志

| 日志 | 含义 |
|---|---|
| `[UdpSender] JSON protocol session_id=...` | 本次后端会话 |
| `[Heartbeat] begin move sequence=...` | 开始有限重发 |
| `[Heartbeat] ... sent 5/5 times` | move 重发完成，进入同目标 hold |
| `[ControlAck] ... confirmed_packets=3` | Jetson 已应用 |
| `reconnected: cleared commands tied to old power-on NED origin` | 重连后旧坐标指令已清除 |

### 11.2 Jetson 关键日志

| 日志 | 含义 |
|---|---|
| `[UDP-RX] valid` | JSON、slot、坐标元数据通过 |
| `[COMMAND-PENDING] ... unique=2/3` | 等待确认阈值 |
| `[COMMAND-EXECUTE]` | setpoint 已更新 |
| `[COMMAND-STALE]` | 迟到旧序号被拒绝 |
| `[COMMAND-CONFLICT]` | 同 ID 不同负载，整组拒绝 |
| `[ONE-WAY-LINK]` | 遥测能发，但控制未到 Jetson |
| `[ROS-LINK]` | PX4 输入话题没有订阅者 |
| `[PX4-ACK]` | ARM/OFFBOARD 的飞控确认结果 |

常见判断：

- 后端 `sent_count` 增长、Jetson 没有 `[UDP-RX]`：检查 `jetson.host`、控制端口、路由和防火墙。
- Jetson 有 `[COMMAND-EXECUTE]`、后端没有 `[ControlAck]`：检查 YAML 遥测回程和 `control_ack` 解析。
- Jetson 有 `[ROS-TX]`、没有 `[PX4-ACK]`：检查 MAVLink System ID、ROS 命名空间和 MicroXRCEAgent。
- `[PX4-ACK] result != 0`：PX4 已收到命令，但因预检、模式或解锁条件拒绝。

## 12. 已知边界

1. `SequenceDispatchPanelWidget` 当前阵列重映射公式把参考锚点加回航点，得到的是 Cesium UE 世界坐标；后端却按“UE 相对偏移”解释。手动匹配需再减去目标机锚点，自动分配需由后端依据各机 GPS 上电锚点补偿不同 NED 原点。当前单机 move 链路不受此问题影响。
2. 基础避障是执行层保护逻辑，不等同于完整全局路径规划。
3. UDP 方案保证“在允许丢包范围内的最新目标可靠应用”，不承诺网络分区期间的必达；新 sequence 会覆盖迟到旧目标。
4. 修改 `command_repeat_count` 时，应保证它不小于 Jetson 的 `COMMAND_CONFIRM_COUNT`。

## 13. 相关文档

- `../接口与通讯数据规范.md`：HTTP、WebSocket、UDP JSON/YAML 字段定义。
- `../坐标系转换说明.md`：NED、UE5、GPS、Cesium 与上电原点。
- `../架构设计.md`：系统分层、时序与职责边界。
- `TEST_GUIDE.md`：逐项测试流程。
- `WEEK1_3_BACKEND_ACCEPTANCE.md`：早期验收记录，涉及旧二进制协议的部分仅作历史参考。
