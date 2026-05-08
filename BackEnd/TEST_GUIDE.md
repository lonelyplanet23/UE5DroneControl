# UE5DroneControl 后端测试指南

> 版本：Week 3 & 4 功能测试  
> 适用人群：非技术人员也可按步骤操作  
> 日期：2026-05-08

---

## 目录

1. [环境准备](#1-环境准备)
2. [单元测试](#2-单元测试)
3. [无人机注册与管理](#3-无人机注册与管理)
4. [遥测数据注入与推送](#4-遥测数据注入与推送)
5. [坐标与姿态转换](#5-坐标与姿态转换)
6. [心跳与指令队列](#6-心跳与指令队列)
7. [连接状态与告警](#7-连接状态与告警)
8. [UDP 遥测通道](#8-udp-遥测通道)
9. [编队集结流程](#9-编队集结流程)
10. [侦察/巡逻/攻击模式](#10-侦察巡逻攻击模式)
11. [多机并发与避障](#11-多机并发与避障)
12. [集成测试](#12-集成测试)
13. [录屏建议](#13-录屏建议)

---

## 1. 环境准备

> 在开始任何测试之前，请先完成本节所有步骤。

### 1.1 启动后端服务器

1. 打开文件资源管理器，进入项目目录：
   ```
   f:\UE5DroneControl\BackEnd
   ```
2. 双击运行 `build.bat`，等待编译完成（终端显示 `Build succeeded` 或类似字样）。
3. 编译完成后，在同一目录下找到 `DroneBackend.exe`，双击运行，或在终端中执行：
   ```bash
   cd /f/UE5DroneControl/BackEnd
   ./DroneBackend.exe
   ```
4. 看到如下输出说明服务器已就绪：
   ```
   [INFO] HTTP server listening on port 8080
   [INFO] WebSocket server listening on port 9090
   ```
   > 如果端口被占用，请关闭其他占用 8080/9090 端口的程序后重试。

### 1.2 安装并打开 WebSocket 客户端

**方法 A：使用 wscat（推荐，需要 Node.js）**

1. 打开命令提示符（Win+R，输入 `cmd`，回车）。
2. 安装 wscat：
   ```bash
   npm install -g wscat
   ```
3. 连接到后端 WebSocket：
   ```bash
   wscat -c ws://localhost:9090
   ```
4. 连接成功后终端会显示 `Connected (press CTRL+C to quit)`。

**方法 B：使用浏览器在线工具（无需安装）**

1. 打开浏览器，访问：https://www.piesocket.com/websocket-tester
2. 在 "WebSocket URL" 输入框中填入：`ws://localhost:9090`
3. 点击 "Connect" 按钮。

### 1.3 在 Windows 上使用 curl 命令

Windows 10/11 已内置 curl，无需额外安装。

1. 打开命令提示符（Win+R → 输入 `cmd` → 回车）。
2. 验证 curl 可用：
   ```bash
   curl --version
   ```
   看到版本号即表示可用。
3. 本指南中所有 curl 命令均可直接复制粘贴到命令提示符中运行。

> **提示**：如果使用 PowerShell，`curl` 是 `Invoke-WebRequest` 的别名，行为不同。建议使用"命令提示符（cmd）"而非 PowerShell 运行本指南中的 curl 命令。

### 1.4 运行 Python 脚本

1. 确认已安装 Python 3（访问 https://www.python.org/downloads/ 下载安装）。
2. 验证安装：
   ```bash
   python --version
   ```
3. 安装所需依赖（仅需执行一次）：
   ```bash
   cd /f/UE5DroneControl/BackEnd
   pip install requests websocket-client
   ```
4. 运行脚本示例：
   ```bash
   python simulate_telemetry.py
   ```

### 1.5 录屏工具推荐

**方法 A：Windows 内置 Xbox Game Bar（最简单）**

1. 按下 `Win + G` 打开 Xbox Game Bar。
2. 点击"捕获"面板中的红色圆形录制按钮开始录屏。
3. 再次点击停止，视频保存在 `C:\Users\你的用户名\Videos\Captures\`。

**方法 B：OBS Studio（功能更强大，推荐用于完整演示视频）**

1. 下载安装：https://obsproject.com/
2. 打开 OBS，在"来源"面板点击 `+` → 选择"显示器采集"。
3. 点击"开始录制"按钮。
4. 录制完成后点击"停止录制"，视频默认保存在桌面。

---

## 2. 单元测试

### 测试目的

验证后端核心逻辑的 28 个单元测试全部通过，确保基础功能正确。

### 操作步骤

1. 确保后端已编译（已执行过 `build.bat`）。
2. 打开命令提示符，进入后端目录：
   ```bash
   cd /f/UE5DroneControl/BackEnd
   ```
3. 运行单元测试程序：
   ```bash
   ./DroneBackend_tests.exe
   ```
4. 等待测试运行完毕（通常几秒内完成）。

### 预期结果

终端输出类似以下内容：
```
[==========] Running 28 tests from X test suites.
[----------] Global test environment set-up.
...
[  PASSED  ] 28 tests.
[  FAILED  ] 0 tests.
```
所有 28 个测试显示 `PASSED`，`FAILED` 数量为 0。

### 录屏要点

- 录制整个终端窗口，确保最终的 `PASSED 28 / FAILED 0` 行清晰可见。
- 可以在测试运行前先展示一下目录结构，说明测试文件位置。

---

## 3. 无人机注册与管理

### 测试 3.1：注册无人机

**测试目的**：验证可以通过 HTTP POST 请求注册新无人机，系统返回唯一 ID。

**操作步骤**：

1. 确保后端服务器正在运行（参见 1.1 节）。
2. 打开命令提示符，执行以下命令：
   ```bash
   curl -X POST http://localhost:8080/api/drones -H "Content-Type: application/json" -d "{\"name\":\"Drone_Alpha\",\"slot\":1}"
   ```
3. 观察终端输出。

**预期结果**：

HTTP 状态码为 `201 Created`，响应体类似：
```json
{
  "id": 1,
  "id_str": "1",
  "slot": 1,
  "name": "Drone_Alpha"
}
```
关键字段：`id`（数字 ID）、`id_str`（字符串 ID）、`slot`（槽位号）均存在。

### 测试 3.2：防止重复注册

**测试目的**：验证系统拒绝重复的无人机名称或槽位，返回 409 错误。

**操作步骤**：

1. 先完成测试 3.1（已注册 Drone_Alpha，slot=1）。
2. 尝试注册相同名称：
   ```bash
   curl -X POST http://localhost:8080/api/drones -H "Content-Type: application/json" -d "{\"name\":\"Drone_Alpha\",\"slot\":2}"
   ```
3. 再尝试注册相同槽位：
   ```bash
   curl -X POST http://localhost:8080/api/drones -H "Content-Type: application/json" -d "{\"name\":\"Drone_Beta\",\"slot\":1}"
   ```

**预期结果**：

两次请求均返回 HTTP `409 Conflict`，响应体包含错误信息，例如：
```json
{"error": "name already exists"}
```

### 测试 3.3：查询无人机列表

**测试目的**：验证可以获取所有已注册无人机的列表。

**操作步骤**：

1. 确保已注册至少一架无人机（参见测试 3.1）。
2. 执行查询命令：
   ```bash
   curl http://localhost:8080/api/drones
   ```

**预期结果**：

返回 HTTP `200 OK`，响应体为 JSON 数组，包含所有已注册无人机：
```json
[
  {"id": 1, "id_str": "1", "slot": 1, "name": "Drone_Alpha"}
]
```

### 测试 3.4：更新无人机信息

**测试目的**：验证可以修改已注册无人机的名称。

**操作步骤**：

1. 确保 ID 为 1 的无人机已注册。
2. 执行更新命令（将名称改为 Drone_Alpha_V2）：
   ```bash
   curl -X PUT http://localhost:8080/api/drones/1 -H "Content-Type: application/json" -d "{\"name\":\"Drone_Alpha_V2\"}"
   ```
3. 再次查询列表确认更改：
   ```bash
   curl http://localhost:8080/api/drones
   ```

**预期结果**：

PUT 请求返回 `200 OK`，随后的 GET 请求显示该无人机名称已更新为 `Drone_Alpha_V2`。

### 测试 3.5：删除无人机

**测试目的**：验证可以删除已注册的无人机，且删除后无法再查询到该无人机。

**操作步骤**：

1. 执行删除命令：
   ```bash
   curl -X DELETE http://localhost:8080/api/drones/1
   ```
2. 查询列表确认已删除：
   ```bash
   curl http://localhost:8080/api/drones
   ```

**预期结果**：

DELETE 请求返回 `200 OK` 或 `204 No Content`，随后的 GET 请求返回的列表中不再包含 ID 为 1 的无人机。

### 测试 3.6：数据持久化

**测试目的**：验证注册的无人机数据在后端重启后仍然保留。

**操作步骤**：

1. 注册一架新无人机：
   ```bash
   curl -X POST http://localhost:8080/api/drones -H "Content-Type: application/json" -d "{\"name\":\"Persist_Test\",\"slot\":5}"
   ```
2. 记录返回的 `id` 值。
3. 关闭后端服务器（在运行服务器的终端按 `Ctrl+C`）。
4. 重新启动后端服务器：
   ```bash
   ./DroneBackend.exe
   ```
5. 查询无人机列表：
   ```bash
   curl http://localhost:8080/api/drones
   ```

**预期结果**：

重启后查询列表，`Persist_Test` 无人机仍然存在，数据未丢失。

### 录屏要点（测试 3.1-3.6）

- 将命令提示符窗口最大化，确保 JSON 响应完整可见。
- 每次执行命令前，可以先用鼠标指向命令，停顿 1-2 秒，再按回车。
- 持久化测试时，录制关闭服务器、重启服务器、再次查询的完整过程。

---

## 4. 遥测数据注入与推送

### 测试 4.1：HTTP 遥测注入

**测试目的**：验证可以通过 HTTP 接口向指定无人机注入遥测数据（位置、姿态、速度、电量、GPS）。

**前提条件**：已注册 ID 为 1 的无人机（参见测试 3.1）。

**操作步骤**：

1. 执行遥测注入命令：
   ```bash
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[1.0,2.0,-3.0],\"quaternion\":[0.965,0.0,0.0,0.258],\"velocity\":[3.0,4.0,0.0],\"battery\":85,\"gps\":{\"lat\":39.9042,\"lon\":116.4074,\"alt\":50.0}}"
   ```
2. 观察返回结果。

**预期结果**：

返回 HTTP `200 OK`，响应体包含注入成功的确认信息。

### 测试 4.2：WebSocket 遥测推送（10Hz）

**测试目的**：验证后端以 10Hz（每秒 10 次）频率通过 WebSocket 向客户端推送遥测数据。

**操作步骤**：

1. 打开一个新的命令提示符窗口，连接 WebSocket：
   ```bash
   wscat -c ws://localhost:9090
   ```
2. 在另一个命令提示符窗口，执行遥测注入（参见测试 4.1）。
3. 观察 wscat 窗口中的消息流。

**预期结果**：

wscat 窗口中每秒收到约 10 条 JSON 消息，格式类似：
```json
{
  "type": "telemetry",
  "drone_id": 1,
  "position": {"x": 100, "y": 200, "z": 300},
  "yaw": -30.0,
  "speed": 5.0,
  "battery": 85
}
```
消息以稳定频率持续推送。

### 测试 4.3：首次注入触发 power_on 事件

**测试目的**：验证无人机首次收到遥测数据时，WebSocket 会推送 `power_on` 事件，并包含 GPS 坐标。

**操作步骤**：

1. 重新注册一架全新的无人机（确保之前没有注入过遥测）：
   ```bash
   curl -X POST http://localhost:8080/api/drones -H "Content-Type: application/json" -d "{\"name\":\"NewDrone\",\"slot\":2}"
   ```
   记录返回的 `id`（假设为 2）。
2. 连接 WebSocket 客户端（wscat 或浏览器工具）。
3. 向新无人机注入第一次遥测：
   ```bash
   curl -X POST http://localhost:8080/api/debug/drone/2/inject -H "Content-Type: application/json" -d "{\"position\":[0,0,0],\"quaternion\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":100,\"gps\":{\"lat\":39.9042,\"lon\":116.4074,\"alt\":50.0}}"
   ```
4. 观察 WebSocket 客户端收到的消息。

**预期结果**：

WebSocket 收到一条 `power_on` 事件消息，包含 GPS 信息：
```json
{
  "type": "event",
  "event": "power_on",
  "drone_id": 2,
  "gps": {"lat": 39.9042, "lon": 116.4074, "alt": 50.0}
}
```

---

## 5. 坐标与姿态转换

### 测试 5.1：NED 坐标转换为 UE 坐标（厘米）

**测试目的**：验证后端将 NED（北东地）坐标系的位置数据正确转换为 UE5 引擎使用的坐标系（单位：厘米）。

**转换规则**：NED [x, y, z] → UE [x*100, y*100, -z*100]

**操作步骤**：

1. 注入位置 [1, 2, -3]（NED 坐标，单位：米）：
   ```bash
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[1.0,2.0,-3.0],\"quaternion\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":80,\"gps\":{\"lat\":39.9,\"lon\":116.4,\"alt\":50}}"
   ```
2. 观察 WebSocket 推送的遥测消息中的 `position` 字段。

**预期结果**：

WebSocket 消息中 `position` 字段应为：
```json
"position": {"x": 100, "y": 200, "z": 300}
```
即 x=1×100=100，y=2×100=200，z=(-(-3))×100=300。

### 测试 5.2：四元数转偏航角（Yaw）

**测试目的**：验证后端将四元数姿态数据正确转换为偏航角（Yaw，单位：度）。

**操作步骤**：

1. 注入四元数 [0.965, 0, 0, 0.258]（对应偏航约 -30°）：
   ```bash
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[0,0,0],\"quaternion\":[0.965,0.0,0.0,0.258],\"velocity\":[0,0,0],\"battery\":80,\"gps\":{\"lat\":39.9,\"lon\":116.4,\"alt\":50}}"
   ```
2. 观察 WebSocket 推送消息中的 `yaw` 字段。

**预期结果**：

WebSocket 消息中 `yaw` 字段约为 `-30.0`（允许 ±1° 误差）。

### 测试 5.3：速度计算

**测试目的**：验证后端根据三轴速度向量正确计算合速度（标量速度）。

**计算公式**：speed = √(vx² + vy² + vz²)

**操作步骤**：

1. 注入速度 [3, 4, 0]：
   ```bash
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[0,0,0],\"quaternion\":[1,0,0,0],\"velocity\":[3.0,4.0,0.0],\"battery\":80,\"gps\":{\"lat\":39.9,\"lon\":116.4,\"alt\":50}}"
   ```
2. 观察 WebSocket 推送消息中的 `speed` 字段。

**预期结果**：

`speed` 字段值为 `5.0`（即 √(9+16+0) = 5）。

### 测试 5.4：查询 GPS 锚点

**测试目的**：验证首次注入遥测后，系统记录了 GPS 锚点（用于后续坐标转换的参考原点）。

**操作步骤**：

1. 确保已向 ID 为 1 的无人机注入过至少一次遥测（含 GPS 数据）。
2. 查询 GPS 锚点：
   ```bash
   curl http://localhost:8080/api/drones/1/anchor
   ```

**预期结果**：

返回 `200 OK`，响应体包含首次注入时的 GPS 坐标：
```json
{
  "lat": 39.9042,
  "lon": 116.4074,
  "alt": 50.0
}
```

### 录屏要点（测试 4-5）

- 建议将终端（curl 命令）和 WebSocket 客户端窗口并排显示。
- 注入命令执行后，立即切换到 WebSocket 窗口，展示收到的消息。
- 对于坐标转换测试，可以在屏幕上用便签或注释标注输入值和预期输出值，方便观看者理解。

---

## 6. 心跳与指令队列

### 测试 6.1：心跳检测

**测试目的**：验证后端持续向无人机发送心跳包，且发送计数随时间增加。

**操作步骤**：

1. 确保 ID 为 1 的无人机已注册并已注入遥测。
2. 第一次查询心跳状态：
   ```bash
   curl http://localhost:8080/api/debug/heartbeat/1
   ```
   记录 `sent_count` 值。
3. 等待 5 秒后再次查询：
   ```bash
   curl http://localhost:8080/api/debug/heartbeat/1
   ```

**预期结果**：

第二次查询的 `sent_count` 值大于第一次，说明心跳包在持续发送：
```json
{"drone_id": 1, "sent_count": 15, "last_sent": "..."}
```

### 测试 6.2：发送移动指令

**测试目的**：验证可以向无人机发送移动指令，指令进入队列并设置模式为 1（移动模式）。

**操作步骤**：

1. 发送移动指令：
   ```bash
   curl -X POST http://localhost:8080/api/debug/cmd/1/move -H "Content-Type: application/json" -d "{\"target\":[10.0,20.0,-5.0],\"speed\":5.0}"
   ```
2. 查询指令队列状态（如有相关接口）：
   ```bash
   curl http://localhost:8080/api/debug/cmd/1/queue
   ```

**预期结果**：

指令队列中包含该移动指令，`mode` 字段值为 `1`。

### 测试 6.3：暂停与恢复指令队列

**测试目的**：验证可以暂停和恢复无人机的指令队列执行。

**操作步骤**：

1. 暂停指令队列：
   ```bash
   curl -X POST http://localhost:8080/api/debug/cmd/1/pause
   ```
2. 查询队列状态，确认 `queue_paused` 为 `true`：
   ```bash
   curl http://localhost:8080/api/debug/cmd/1/queue
   ```
3. 恢复指令队列：
   ```bash
   curl -X POST http://localhost:8080/api/debug/cmd/1/resume
   ```
4. 再次查询队列状态，确认 `queue_paused` 为 `false`。

**预期结果**：

- 暂停后：`queue_paused: true`
- 恢复后：`queue_paused: false`

### 录屏要点（测试 6）

- 心跳测试时，录制两次查询之间的等待过程，展示计数增长。
- 暂停/恢复测试时，每次操作后立即查询状态，展示字段变化。

---

## 7. 连接状态与告警

### 测试 7.1：连接丢失检测

**测试目的**：验证当无人机停止发送遥测超过 10 秒后，系统将其状态标记为 `lost`，并通过 WebSocket 推送 `lost_connection` 事件。

**操作步骤**：

1. 确保 WebSocket 客户端已连接。
2. 向 ID 为 1 的无人机注入一次遥测数据（参见测试 4.1）。
3. 停止注入（不再执行任何注入命令）。
4. 等待约 10-15 秒。
5. 观察 WebSocket 客户端收到的消息。

**预期结果**：

约 10 秒后，WebSocket 收到 `lost_connection` 事件：
```json
{
  "type": "event",
  "event": "lost_connection",
  "drone_id": 1
}
```
同时，查询无人机状态时 `status` 字段变为 `lost`。

### 测试 7.2：重新连接检测

**测试目的**：验证连接丢失后重新注入遥测，系统推送 `reconnect` 事件。

**操作步骤**：

1. 完成测试 7.1（无人机处于 `lost` 状态）。
2. 重新注入遥测数据：
   ```bash
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[0,0,0],\"quaternion\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":80,\"gps\":{\"lat\":39.9,\"lon\":116.4,\"alt\":50}}"
   ```
3. 观察 WebSocket 客户端收到的消息。

**预期结果**：

WebSocket 收到 `reconnect` 事件：
```json
{
  "type": "event",
  "event": "reconnect",
  "drone_id": 1
}
```

### 测试 7.3：低电量告警

**测试目的**：验证当电量低于阈值（15%）时，WebSocket 推送低电量告警。

**操作步骤**：

1. 确保 WebSocket 客户端已连接。
2. 注入电量为 15% 的遥测数据：
   ```bash
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[0,0,0],\"quaternion\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":15,\"gps\":{\"lat\":39.9,\"lon\":116.4,\"alt\":50}}"
   ```
3. 观察 WebSocket 客户端收到的消息。

**预期结果**：

WebSocket 收到低电量告警消息，`alert` 字段值为 `low_battery`：
```json
{
  "type": "alert",
  "alert": "low_battery",
  "drone_id": 1,
  "battery": 15
}
```

### 录屏要点（测试 7）

- 连接丢失测试时，录制等待的过程（可以加速播放），展示 10 秒后事件触发。
- 建议在屏幕上显示计时器（可用手机计时），方便观看者了解等待时长。

---

## 8. UDP 遥测通道

### 测试 8.1：UDP 遥测模拟

**测试目的**：验证后端可以通过 UDP 协议接收遥测数据（UdpReceiver 路径）。

**操作步骤**：

1. 确保后端服务器正在运行。
2. 确保 WebSocket 客户端已连接。
3. 进入后端目录，运行 UDP 遥测模拟脚本：
   ```bash
   cd /f/UE5DroneControl/BackEnd
   python simulate_telemetry.py --transport udp
   ```
4. 观察 WebSocket 客户端收到的消息。

**预期结果**：

- 脚本运行后，终端显示 UDP 数据发送日志。
- WebSocket 客户端收到遥测推送消息，与 HTTP 注入方式的消息格式相同。
- 后端日志中可以看到 `UdpReceiver` 相关的接收记录。

### 录屏要点（测试 8）

- 同时录制脚本运行终端和 WebSocket 客户端窗口。
- 展示 UDP 路径与 HTTP 路径产生相同格式的 WebSocket 推送消息。

---

## 9. 编队集结流程

### 测试 9.1：双机编队集结（正常完成）

**测试目的**：验证两架无人机可以完成完整的编队集结流程，从 `assembling` 状态到 `assembly_complete`。

**操作步骤**：

1. 注册两架无人机（如果尚未注册）：
   ```bash
   curl -X POST http://localhost:8080/api/drones -H "Content-Type: application/json" -d "{\"name\":\"Drone_1\",\"slot\":1}"
   curl -X POST http://localhost:8080/api/drones -H "Content-Type: application/json" -d "{\"name\":\"Drone_2\",\"slot\":2}"
   ```
2. 向两架无人机注入初始遥测数据：
   ```bash
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[0,0,0],\"quaternion\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":90,\"gps\":{\"lat\":39.9,\"lon\":116.4,\"alt\":50}}"
   curl -X POST http://localhost:8080/api/debug/drone/2/inject -H "Content-Type: application/json" -d "{\"position\":[10,0,0],\"quaternion\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":90,\"gps\":{\"lat\":39.901,\"lon\":116.4,\"alt\":50}}"
   ```
3. 发起编队集结请求：
   ```bash
   curl -X POST http://localhost:8080/api/arrays -H "Content-Type: application/json" -d "{\"drone_ids\":[1,2],\"formation\":\"line\"}"
   ```
4. 观察 WebSocket 推送的状态变化（应出现 `assembling` 状态）。
5. 注入两架无人机的到达位置（模拟它们飞到集结点）：
   ```bash
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[5,0,-10],\"quaternion\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":88,\"gps\":{\"lat\":39.9,\"lon\":116.4,\"alt\":60}}"
   curl -X POST http://localhost:8080/api/debug/drone/2/inject -H "Content-Type: application/json" -d "{\"position\":[5,2,-10],\"quaternion\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":88,\"gps\":{\"lat\":39.9,\"lon\":116.401,\"alt\":60}}"
   ```
6. 观察 WebSocket 消息，等待 `assembly_complete` 事件。

**预期结果**：

- 步骤 3 后，WebSocket 收到状态变为 `assembling` 的消息。
- 步骤 5 后，WebSocket 收到 `assembly_complete` 事件：
  ```json
  {"type": "event", "event": "assembly_complete", "array_id": 1}
  ```

### 测试 9.2：编队集结超时

**测试目的**：验证当无人机未能在规定时间内到达集结点时，系统触发 `assembly_timeout` 事件。

**操作步骤**：

1. 发起编队集结请求（参见测试 9.1 步骤 1-3）。
2. 不注入到达位置（不执行步骤 5）。
3. 等待约 60 秒。
4. 观察 WebSocket 消息。

**预期结果**：

约 60 秒后，WebSocket 收到超时事件：
```json
{"type": "event", "event": "assembly_timeout", "array_id": 1}
```

### 录屏要点（测试 9）

- 集结流程测试时，建议将 WebSocket 客户端窗口放大，清晰展示状态变化消息。
- 超时测试时，可以在屏幕上显示计时器，展示 60 秒等待过程（可加速播放）。

---

## 10. 侦察/巡逻/攻击模式

### 测试 10.1：侦察模式（非循环）

**测试目的**：验证侦察模式下，无人机按顺序执行航点，到达最后一个航点后停止。

**操作步骤**：

1. 确保 ID 为 1 的无人机已注册并已注入遥测。
2. 发送侦察模式指令（非循环，loop=false）：
   ```bash
   curl -X POST http://localhost:8080/api/debug/cmd/1/array -H "Content-Type: application/json" -d "{\"mode\":\"recon\",\"loop\":false,\"waypoints\":[[0,0,-10],[10,0,-10],[10,10,-10]]}"
   ```
3. 观察 WebSocket 推送的指令执行状态。
4. 模拟无人机依次到达各航点（注入对应位置的遥测数据）：
   ```bash
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[0,0,-10],\"quaternion\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":80,\"gps\":{\"lat\":39.9,\"lon\":116.4,\"alt\":60}}"
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[10,0,-10],\"quaternion\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":79,\"gps\":{\"lat\":39.9,\"lon\":116.401,\"alt\":60}}"
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[10,10,-10],\"quaternion\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":78,\"gps\":{\"lat\":39.901,\"lon\":116.401,\"alt\":60}}"
   ```

**预期结果**：

无人机执行完最后一个航点后，指令队列停止，不再循环回第一个航点。WebSocket 消息显示任务完成状态。

### 测试 10.2：侦察模式（循环）

**测试目的**：验证循环侦察模式下，无人机到达最后一个航点后自动返回第一个航点继续执行。

**操作步骤**：

1. 发送循环侦察模式指令（loop=true）：
   ```bash
   curl -X POST http://localhost:8080/api/debug/cmd/1/array -H "Content-Type: application/json" -d "{\"mode\":\"recon\",\"loop\":true,\"waypoints\":[[0,0,-10],[10,0,-10],[10,10,-10]]}"
   ```
2. 模拟无人机到达所有航点后，观察是否自动回到第一个航点继续执行。
3. 注入第三个航点位置后，继续注入第一个航点位置，观察循环行为。

**预期结果**：

无人机完成最后一个航点后，指令队列自动重置，从第一个航点重新开始执行，形成循环。

### 测试 10.3：巡逻模式 + 目标注入

**测试目的**：验证巡逻模式下，当注入新目标时，无人机中断当前巡逻并飞向新目标。

**操作步骤**：

1. 启动巡逻模式：
   ```bash
   curl -X POST http://localhost:8080/api/debug/cmd/1/array -H "Content-Type: application/json" -d "{\"mode\":\"patrol\",\"loop\":true,\"waypoints\":[[0,0,-10],[20,0,-10],[20,20,-10],[0,20,-10]]}"
   ```
2. 等待 2-3 秒后，注入新目标：
   ```bash
   curl -X POST http://localhost:8080/api/debug/cmd/1/target -H "Content-Type: application/json" -d "{\"target\":[50,50,-15],\"priority\":\"high\"}"
   ```
3. 观察 WebSocket 消息，查看无人机是否中断巡逻并飞向新目标。

**预期结果**：

注入目标后，WebSocket 消息显示无人机中断当前巡逻任务，转向新目标位置飞行。

### 测试 10.4：攻击模式

**测试目的**：验证攻击模式下，无人机飞到最后一个航点后停止（不循环）。

**操作步骤**：

1. 发送攻击模式指令：
   ```bash
   curl -X POST http://localhost:8080/api/debug/cmd/1/array -H "Content-Type: application/json" -d "{\"mode\":\"attack\",\"loop\":false,\"waypoints\":[[0,0,-10],[30,0,-10],[30,30,-20]]}"
   ```
2. 模拟无人机依次到达各航点：
   ```bash
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[30,30,-20],\"quaternion\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":75,\"gps\":{\"lat\":39.902,\"lon\":116.402,\"alt\":70}}"
   ```
3. 观察无人机是否在最后一个航点停止。

**预期结果**：

无人机到达最后一个航点（攻击目标）后停止，不返回也不循环。

### 测试 10.5：多机并发指令

**测试目的**：验证可以同时向多架无人机发送指令，各无人机独立执行自己的指令队列。

**操作步骤**：

1. 确保 ID 为 1 和 2 的无人机均已注册并注入遥测。
2. 批量发送指令：
   ```bash
   curl -X POST http://localhost:8080/api/debug/cmd/batch/array -H "Content-Type: application/json" -d "{\"commands\":[{\"drone_id\":1,\"mode\":\"recon\",\"loop\":false,\"waypoints\":[[0,0,-10],[10,0,-10]]},{\"drone_id\":2,\"mode\":\"patrol\",\"loop\":true,\"waypoints\":[[5,5,-10],[15,5,-10]]}]}"
   ```
3. 分别查询两架无人机的指令队列状态：
   ```bash
   curl http://localhost:8080/api/debug/cmd/1/queue
   curl http://localhost:8080/api/debug/cmd/2/queue
   ```

**预期结果**：

- 无人机 1 执行侦察模式（非循环）。
- 无人机 2 执行巡逻模式（循环）。
- 两架无人机的指令队列相互独立，互不干扰。

### 录屏要点（测试 10）

- 建议将 WebSocket 客户端窗口和 curl 命令窗口并排显示。
- 循环模式测试时，展示至少两个完整循环周期。
- 目标注入测试时，录制注入前后的状态变化，清晰展示中断效果。

---

## 11. 多机并发与避障

### 测试 11.1：基础避障

**测试目的**：验证当两架无人机飞行路径接近时，低优先级无人机会自动获得临时偏移目标以避免碰撞。

**操作步骤**：

1. 确保 ID 为 1 和 2 的无人机均已注册并处于执行状态。
2. 向两架无人机注入接近的位置和速度（模拟即将碰撞的场景）：
   ```bash
   curl -X POST http://localhost:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[0,0,-10],\"quaternion\":[1,0,0,0],\"velocity\":[1,0,0],\"battery\":80,\"gps\":{\"lat\":39.9,\"lon\":116.4,\"alt\":60}}"
   curl -X POST http://localhost:8080/api/debug/drone/2/inject -H "Content-Type: application/json" -d "{\"position\":[3,0,-10],\"quaternion\":[1,0,0,0],\"velocity\":[-1,0,0],\"battery\":80,\"gps\":{\"lat\":39.9,\"lon\":116.401,\"alt\":60}}"
   ```
   （两架无人机位置相距 3 米，速度方向相对，即将碰撞）
3. 观察 WebSocket 推送的消息，查看是否有避障相关事件或目标偏移。

**预期结果**：

- 系统检测到两架无人机即将碰撞。
- 低优先级无人机（ID 较大的，即 ID=2）收到临时偏移目标指令。
- WebSocket 消息中可以看到避障相关的状态变化或事件通知。

### 录屏要点（测试 11）

- 同时展示两架无人机的遥测数据和 WebSocket 消息。
- 清晰展示注入接近位置前后的系统响应变化。

---

## 12. 集成测试

### 测试 12.1：Week 3 集成测试

**测试目的**：运行 Week 3 的完整集成测试脚本，验证所有 Week 3 功能正常工作。

**操作步骤**：

1. 确保后端服务器正在运行。
2. 打开命令提示符，进入后端目录：
   ```bash
   cd /f/UE5DroneControl/BackEnd
   ```
3. 运行 Week 3 集成测试脚本：
   ```bash
   python integration_week3.py
   ```
4. 等待测试完成（可能需要 1-2 分钟）。

**预期结果**：

所有测试项目显示 `PASS`，终端输出类似：
```
[PASS] Drone registration
[PASS] Duplicate prevention
[PASS] Telemetry injection
[PASS] WebSocket push
[PASS] Coordinate conversion
...
All tests passed!
```
没有任何 `FAIL` 或 `ERROR` 项目。

### 测试 12.2：Week 4 集成测试

**测试目的**：运行 Week 4 的完整集成测试脚本，验证所有 Week 4 功能正常工作。

**操作步骤**：

1. 确保后端服务器正在运行。
2. 运行 Week 4 集成测试脚本：
   ```bash
   python integration_week4.py
   ```
3. 等待测试完成（可能需要 2-3 分钟，因为包含超时测试）。

**预期结果**：

所有测试项目显示 `PASS`，终端输出类似：
```
[PASS] Assembly flow
[PASS] Assembly timeout
[PASS] Recon mode (non-loop)
[PASS] Recon mode (loop)
[PASS] Patrol + target injection
[PASS] Attack mode
[PASS] Multi-drone concurrent
[PASS] Basic avoidance
...
All tests passed!
```
没有任何 `FAIL` 或 `ERROR` 项目。

### 录屏要点（测试 12）

- 录制完整的测试运行过程，包括开始和结束。
- 确保最终的 `All tests passed!` 或类似成功消息清晰可见。
- 如果测试失败，不要剪辑掉失败信息，应先修复问题再重新录制。

---

## 13. 录屏建议

### 13.1 推荐屏幕布局

为了让演示视频清晰易懂，建议采用以下屏幕布局：

```
+---------------------------+---------------------------+
|                           |                           |
|   终端窗口（curl 命令）    |   WebSocket 客户端窗口    |
|                           |                           |
|   用于执行 HTTP 请求      |   用于观察实时推送消息    |
|                           |                           |
+---------------------------+---------------------------+
|                                                       |
|   后端服务器日志窗口（可选，展示服务器端处理过程）     |
|                                                       |
+-------------------------------------------------------+
```

**具体操作**：
1. 打开三个命令提示符窗口。
2. 将屏幕分成左右两半（Windows 快捷键：`Win + 左箭头` / `Win + 右箭头`）。
3. 左侧放 curl 命令窗口，右侧放 WebSocket 客户端窗口。
4. 如果需要，可以在底部再开一个窗口显示后端日志。

### 13.2 各测试录屏要点汇总

| 测试类别 | 录屏重点 | 建议时长 |
|---------|---------|---------|
| 单元测试 | 完整测试输出，PASSED 28 行 | 30 秒 |
| 无人机注册 | 命令 + JSON 响应 | 2 分钟 |
| 遥测注入 | curl 命令 + WS 消息流 | 2 分钟 |
| 坐标转换 | 注入值 + WS 中的转换结果 | 1 分钟 |
| 连接状态 | 等待 10 秒 + 事件触发 | 2 分钟 |
| 编队集结 | 完整流程 + 状态变化 | 3 分钟 |
| 侦察/巡逻 | 航点执行 + 循环/停止 | 3 分钟 |
| 集成测试 | 完整脚本运行 + ALL PASS | 3 分钟 |

### 13.3 建议视频结构

**方案 A：按功能分组录制多个短视频**

1. `01_unit_tests.mp4` — 单元测试（约 1 分钟）
2. `02_drone_management.mp4` — 无人机注册与管理（约 3 分钟）
3. `03_telemetry_websocket.mp4` — 遥测与 WebSocket（约 4 分钟）
4. `04_commands_status.mp4` — 指令与状态（约 4 分钟）
5. `05_assembly_modes.mp4` — 编队与飞行模式（约 5 分钟）
6. `06_integration_tests.mp4` — 集成测试（约 5 分钟）

**方案 B：录制一个完整演示视频**

按照本指南的章节顺序，录制一个 15-20 分钟的完整演示视频，涵盖所有 30 个测试用例。

### 13.4 如何标注关键时刻

**使用 OBS 的文字叠加功能**：
1. 在 OBS 的"来源"面板点击 `+` → 选择"文字（GDI+）"。
2. 输入当前测试的名称，例如"测试 3.1：注册无人机"。
3. 每切换测试时更新文字内容。

**使用视频编辑软件后期添加字幕**：
- Windows 内置的"照片"应用支持基本视频编辑和字幕添加。
- 也可以使用免费软件 DaVinci Resolve 进行专业字幕制作。

**录制前的准备**：
1. 关闭不必要的程序和通知，避免干扰画面。
2. 将字体大小调大（命令提示符右键 → 属性 → 字体，设置为 16-18pt）。
3. 使用深色主题的终端，提高可读性。
4. 录制前先演练一遍，确保命令都能正常执行。

### 13.5 录制前检查清单

在开始录制之前，请确认以下事项：

- [ ] 后端服务器已启动并正常运行
- [ ] WebSocket 客户端已连接
- [ ] 所有 curl 命令已在文本编辑器中准备好，可以快速复制粘贴
- [ ] 屏幕分辨率设置为 1920×1080 或更高
- [ ] 录屏软件已开启并测试正常
- [ ] 关闭了所有不必要的通知（微信、QQ 等）
- [ ] 数据库/存储文件已清空（如需从头演示注册流程）

---

## 附录：常见问题排查

### Q1：后端服务器无法启动

**可能原因**：
- 端口 8080 或 9090 已被其他程序占用。

**解决方法**：
```bash
# 查找占用端口的程序
netstat -ano | findstr :8080
# 根据 PID 结束进程
taskkill /PID <PID号> /F
```

### Q2：curl 命令返回"连接被拒绝"

**可能原因**：
- 后端服务器未启动，或启动失败。

**解决方法**：
1. 检查后端服务器终端是否有错误信息。
2. 重新运行 `./DroneBackend.exe`。

### Q3：WebSocket 无法连接

**可能原因**：
- WebSocket 端口（9090）未开放，或防火墙阻止了连接。

**解决方法**：
1. 确认后端服务器日志中显示 `WebSocket server listening on port 9090`。
2. 临时关闭 Windows 防火墙进行测试。

### Q4：Python 脚本报错"ModuleNotFoundError"

**解决方法**：
```bash
pip install requests websocket-client
```

### Q5：集成测试脚本报错

**解决方法**：
1. 确保后端服务器正在运行。
2. 确保数据库中没有残留的测试数据（重启后端服务器可清空内存数据）。
3. 检查 Python 版本是否为 3.7 或更高：`python --version`。

---

*本测试指南涵盖 UE5DroneControl 后端 Week 3 和 Week 4 的全部 30 个测试用例。如有疑问，请联系开发团队。*


---