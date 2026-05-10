# UE5DroneControl 后端测试指南

> 版本：Week 1-5 后端验收 + Week 5 收尾检查
> 适用人群：非技术人员也可按步骤操作
> 日期：2026-05-10

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

## cmd 与 PowerShell 命令格式说明

本指南中每条命令均提供两种格式：

| 环境 | 打开方式 | curl 用法 | 换行续行符 | JSON 引号 |
|------|---------|-----------|-----------|-----------|
| **cmd**（命令提示符） | Win+R → 输入 `cmd` → 回车 | 直接用 `curl` | `^` | `\"` 转义 |
| **PowerShell** | Win+R → 输入 `powershell` → 回车 | 必须用 `curl.exe` | `` ` `` | 单引号 `'...'` 包裹 |

> PowerShell 中 `curl` 是 `Invoke-WebRequest` 的别名，行为不同，**必须写 `curl.exe`**。

---

## 1. 环境准备

> 在开始任何测试之前，请先完成本节所有步骤。

### 1.1 启动后端服务器

**第一步：编译**

```cmd
:: cmd
cd /d f:\UE5DroneControl\BackEnd
build.bat
```

```powershell
# PowerShell
cd f:\UE5DroneControl\BackEnd
.\build.bat
```

等待终端显示 `Build succeeded` 或 `DroneBackend.exe` 出现在 `build\Release\` 目录中。

**第二步：启动服务**

```cmd
:: cmd（从项目根目录）
f:\UE5DroneControl\BackEnd\build\Release\DroneBackend.exe f:\UE5DroneControl\BackEnd\config.yaml
```

```powershell
# PowerShell（从项目根目录）
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

> 如果端口被占用，请关闭其他占用 8080/8081 端口的程序后重试（排查方法见附录 Q1）。

### 1.2 安装并打开 WebSocket 客户端

**方法 A：使用 wscat（推荐，需要 Node.js）**

```cmd
:: cmd 或 PowerShell 均可
npm install -g wscat
wscat -c ws://127.0.0.1:8081/ws
```

连接成功后终端显示 `Connected (press CTRL+C to quit)`。

**方法 B：使用浏览器在线工具（无需安装）**

1. 打开浏览器，访问：https://www.piesocket.com/websocket-tester
2. 在 "WebSocket URL" 输入框中填入：`ws://127.0.0.1:8081/ws`
3. 点击 "Connect" 按钮。

### 1.3 验证 curl 可用

```cmd
:: cmd
curl --version
```

```powershell
# PowerShell
curl.exe --version
```

看到版本号即表示可用（Windows 10/11 已内置 curl）。

### 1.4 运行 Python 脚本

**安装依赖（仅需执行一次）：**

```cmd
:: cmd 或 PowerShell 均可
pip install requests websocket-client
```

**从项目根目录运行脚本（推荐）：**

```cmd
:: cmd
cd /d f:\UE5DroneControl
python tools\simulate_telemetry.py --register --drones 1 --hz 10
```

```powershell
# PowerShell
cd f:\UE5DroneControl
python tools\simulate_telemetry.py --register --drones 1 --hz 10
```

### 1.5 录屏工具推荐

**方法 A：Windows 内置 Xbox Game Bar（最简单）**

按下 `Win + G` 打开 Xbox Game Bar，点击"捕获"面板中的红色圆形录制按钮开始录屏。视频保存在 `C:\Users\你的用户名\Videos\Captures\`。

**方法 B：OBS Studio（功能更强大）**

下载安装：https://obsproject.com/，在"来源"面板点击 `+` → 选择"显示器采集"，点击"开始录制"。

---

## 2. 单元测试

### 测试目的

验证后端核心逻辑的 28 个单元测试全部通过，确保基础功能正确。

### 操作步骤

```cmd
:: cmd（从项目根目录）
f:\UE5DroneControl\BackEnd\build\Release\DroneBackend_tests.exe
```

```powershell
# PowerShell（从项目根目录）
.\BackEnd\build\Release\DroneBackend_tests.exe
```

### 预期结果

```
[==========] Running 28 tests from X test suites.
...
[  PASSED  ] 28 tests.
[  FAILED  ] 0 tests.
```

所有 28 个测试显示 `PASSED`，`FAILED` 数量为 0。

### 录屏要点

录制整个终端窗口，确保最终的 `PASSED 28 / FAILED 0` 行清晰可见。

---

## 3. 无人机注册与管理

### 测试 3.1：注册无人机

**测试目的**：验证可以通过 HTTP POST 请求注册新无人机，系统返回唯一 ID。

**前提条件**：后端服务器正在运行。

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/drones -H "Content-Type: application/json" -d "{\"name\":\"UAV1\",\"slot\":1}"
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/drones `
  -H "Content-Type: application/json" `
  -d '{"name":"UAV1","slot":1}'
```

**预期结果**：HTTP 状态码 `201 Created`，响应体：

```json
{"id": 1, "id_str": "d1", "slot": 1, "name": "UAV1"}
```

### 测试 3.2：防止重复注册

**测试目的**：验证系统拒绝重复的无人机名称或槽位，返回 409 错误。

```cmd
:: cmd — 重复名称
curl -X POST http://127.0.0.1:8080/api/drones -H "Content-Type: application/json" -d "{\"name\":\"UAV1\",\"slot\":2}"
:: cmd — 重复槽位
curl -X POST http://127.0.0.1:8080/api/drones -H "Content-Type: application/json" -d "{\"name\":\"UAV2\",\"slot\":1}"
```

```powershell
# PowerShell — 重复名称
curl.exe -X POST http://127.0.0.1:8080/api/drones -H "Content-Type: application/json" -d '{"name":"UAV1","slot":2}'
# PowerShell — 重复槽位
curl.exe -X POST http://127.0.0.1:8080/api/drones -H "Content-Type: application/json" -d '{"name":"UAV2","slot":1}'
```

**预期结果**：两次请求均返回 HTTP `409 Conflict`。

### 测试 3.3：查询无人机列表

```cmd
:: cmd
curl http://127.0.0.1:8080/api/drones
```

```powershell
# PowerShell
curl.exe http://127.0.0.1:8080/api/drones
```

**预期结果**：返回 `200 OK`，JSON 数组包含所有已注册无人机。

### 测试 3.4：更新无人机信息

```cmd
:: cmd
curl -X PUT http://127.0.0.1:8080/api/drones/1 -H "Content-Type: application/json" -d "{\"name\":\"UAV1-A\"}"
curl http://127.0.0.1:8080/api/drones
```

```powershell
# PowerShell
curl.exe -X PUT http://127.0.0.1:8080/api/drones/1 -H "Content-Type: application/json" -d '{"name":"UAV1-A"}'
curl.exe http://127.0.0.1:8080/api/drones
```

**预期结果**：PUT 返回 `200 OK`，随后 GET 显示名称已更新。

### 测试 3.5：删除无人机

```cmd
:: cmd
curl -X DELETE http://127.0.0.1:8080/api/drones/1
curl http://127.0.0.1:8080/api/drones
```

```powershell
# PowerShell
curl.exe -X DELETE http://127.0.0.1:8080/api/drones/1
curl.exe http://127.0.0.1:8080/api/drones
```

**预期结果**：DELETE 返回 `200 OK` 或 `204 No Content`，随后列表中不再包含该无人机。

### 测试 3.6：数据持久化

1. 注册一架无人机（参见测试 3.1）。
2. 按 `Ctrl+C` 关闭后端服务器。
3. 重新启动后端（参见 1.1 节）。
4. 查询列表，确认数据仍存在：

```cmd
:: cmd
curl http://127.0.0.1:8080/api/drones
```

```powershell
# PowerShell
curl.exe http://127.0.0.1:8080/api/drones
```

**预期结果**：重启后注册数据仍然存在，未丢失。

### 录屏要点（测试 3.1-3.6）

将命令提示符窗口最大化，确保 JSON 响应完整可见。持久化测试时录制关闭、重启、再次查询的完整过程。

---

## 4. 遥测数据注入与推送

> **前提条件**：已注册 ID 为 1 的无人机（参见测试 3.1）。

### 测试 4.1：HTTP 遥测注入

**测试目的**：验证可以通过 HTTP 接口向指定无人机注入遥测数据。

```cmd
:: cmd（单行）
curl -X POST http://127.0.0.1:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[1.0,2.0,-3.0],\"q\":[0.965925826,0,0,0.258819045],\"velocity\":[3.0,4.0,0.0],\"battery\":85,\"gps_lat\":39.9042,\"gps_lon\":116.4074,\"gps_alt\":50.0}"
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/1/inject `
  -H "Content-Type: application/json" `
  -d '{"position":[1.0,2.0,-3.0],"q":[0.965925826,0,0,0.258819045],"velocity":[3.0,4.0,0.0],"battery":85,"gps_lat":39.9042,"gps_lon":116.4074,"gps_alt":50.0}'
```

**预期结果**：返回 HTTP `200 OK`。

### 测试 4.2：WebSocket 遥测推送

**测试目的**：确认注入遥测后，WebSocket 客户端可以收到 `power_on` 事件和持续 `telemetry` 消息。

**终端 A：连接 WebSocket**

```cmd
:: cmd 或 PowerShell 均可
wscat -c ws://127.0.0.1:8081/ws
```

**终端 B：再次注入遥测**

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[1.0,2.0,-3.0],\"q\":[0.965925826,0,0,0.258819045],\"velocity\":[3.0,4.0,0.0],\"battery\":85,\"gps_lat\":39.9042,\"gps_lon\":116.4074,\"gps_alt\":50.0}"
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/1/inject `
  -H "Content-Type: application/json" `
  -d '{"position":[1.0,2.0,-3.0],"q":[0.965925826,0,0,0.258819045],"velocity":[3.0,4.0,0.0],"battery":85,"gps_lat":39.9042,"gps_lon":116.4074,"gps_alt":50.0}'
```

**预期结果**：WebSocket 窗口出现类似消息：

```json
{"type":"event","event":"power_on","drone_id":1}
{"type":"telemetry","drone_id":1,"x":100,"y":200,"z":300,"yaw":-30,"speed":5,"battery":85}
```

---

## 5. 坐标与姿态转换

### 测试 5.1：查询转换后的状态

**测试目的**：验证 NED 米制位置、四元数和速度被转换为 UE 厘米位置、左手系 yaw 和合速度。

```cmd
:: cmd
curl http://127.0.0.1:8080/api/debug/drone/1/state
curl http://127.0.0.1:8080/api/drones/1/anchor
```

```powershell
# PowerShell
curl.exe http://127.0.0.1:8080/api/debug/drone/1/state
curl.exe http://127.0.0.1:8080/api/drones/1/anchor
```

**预期结果**：

- `position=[1,2,-3]` 转换为 UE `x=100,y=200,z=300`（单位厘米）。
- `velocity=[3,4,0]` 的 `speed` 为 `5`。
- 示例四元数对应 NED yaw 约 `30°`，UE 推送 yaw 约 `-30°`。
- `/anchor` 返回 `valid=true`，并包含首次遥测的 GPS 坐标。

---

## 6. 心跳与指令队列

### 测试 6.1：发送移动指令并查询队列

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/debug/cmd/1/move -H "Content-Type: application/json" -d "{\"x\":1000,\"y\":2000,\"z\":-500}"
curl http://127.0.0.1:8080/api/debug/drone/1/queue
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/move `
  -H "Content-Type: application/json" `
  -d '{"x":1000,"y":2000,"z":-500}'
curl.exe http://127.0.0.1:8080/api/debug/drone/1/queue
```

**预期结果**：队列中出现 `mode=1` 的移动指令，目标点从 UE `(1000,2000,-500)` cm 转换为 NED `(10,20,5)` m。

### 测试 6.2：暂停和恢复

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/debug/cmd/1/pause
curl http://127.0.0.1:8080/api/debug/drone/1/state
curl -X POST http://127.0.0.1:8080/api/debug/cmd/1/resume
curl http://127.0.0.1:8080/api/debug/drone/1/state
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/pause
curl.exe http://127.0.0.1:8080/api/debug/drone/1/state
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/resume
curl.exe http://127.0.0.1:8080/api/debug/drone/1/state
```

**预期结果**：暂停后 `queue_paused=true`，恢复后 `queue_paused=false`。

### 测试 6.3：查看心跳

```cmd
:: cmd
curl http://127.0.0.1:8080/api/debug/heartbeat/1
```

```powershell
# PowerShell
curl.exe http://127.0.0.1:8080/api/debug/heartbeat/1
```

**预期结果**：`running=true`，`sent_count` 随时间增长。真实 Jetson 联调时，可用 Wireshark 抓 slot 1 默认控制端口 `8889`，确认 24 字节控制包。

---

## 7. 连接状态与告警

### 测试 7.1：低电量告警

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[0,0,-10],\"q\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":15}"
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/1/inject `
  -H "Content-Type: application/json" `
  -d '{"position":[0,0,-10],"q":[1,0,0,0],"velocity":[0,0,0],"battery":15}'
```

**预期结果**：WebSocket 收到 `{"type":"alert","alert":"low_battery","value":15}`。

### 测试 7.2：失联与重连

1. 停止所有遥测注入，等待 `BackEnd/config.yaml` 中 `drone.lost_timeout_sec` 指定的秒数（默认 10 秒）。
2. 查询状态。

```cmd
:: cmd
curl http://127.0.0.1:8080/api/debug/drone/1/state
```

```powershell
# PowerShell
curl.exe http://127.0.0.1:8080/api/debug/drone/1/state
```

3. 再次注入正常电量遥测。

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[0,0,-10],\"q\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":85,\"gps_lat\":39.91,\"gps_lon\":116.41,\"gps_alt\":50}"
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/1/inject `
  -H "Content-Type: application/json" `
  -d '{"position":[0,0,-10],"q":[1,0,0,0],"velocity":[0,0,0],"battery":85,"gps_lat":39.91,"gps_lon":116.41,"gps_alt":50}'
```

**预期结果**：超时后状态为 `lost`，WebSocket 收到 `lost_connection`；重新注入后收到 `reconnect`。

---

## 8. UDP 遥测通道

### 测试 8.1：使用模拟脚本走 UDP 路径

```cmd
:: cmd
cd /d f:\UE5DroneControl
python tools\simulate_telemetry.py --register --transport udp --drones 1 2 --hz 10
```

```powershell
# PowerShell
cd f:\UE5DroneControl
python tools\simulate_telemetry.py --register --transport udp --drones 1 2 --hz 10
```

**预期结果**：后端通过 `UdpReceiver` 收到 YAML 遥测，WebSocket 继续推送 `telemetry`，`GET /api/drones` 中无人机状态为 `online`。

---

## 9. 编队集结流程

> 前提：ID 为 1 的无人机已注册并在线。若测试 3.5 删除过 ID 1，请重新注册并注入遥测。

### 测试 9.1：双机集结并完成

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/drones -H "Content-Type: application/json" -d "{\"name\":\"UAV2\",\"slot\":2}"
curl -X POST http://127.0.0.1:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[0,0,-5],\"q\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":85,\"gps_lat\":39.9,\"gps_lon\":116.3,\"gps_alt\":50}"
curl -X POST http://127.0.0.1:8080/api/debug/drone/2/inject -H "Content-Type: application/json" -d "{\"position\":[5,0,-5],\"q\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":90,\"gps_lat\":39.9,\"gps_lon\":116.31,\"gps_alt\":50}"
curl -X POST http://127.0.0.1:8080/api/arrays -H "Content-Type: application/json" -d "{\"array_id\":\"a1\",\"mode\":\"recon\",\"paths\":[{\"pathId\":1,\"drone_id\":\"d1\",\"bClosedLoop\":false,\"waypoints\":[{\"location\":{\"x\":1000,\"y\":0,\"z\":-500}},{\"location\":{\"x\":2000,\"y\":1000,\"z\":-500}}]},{\"pathId\":2,\"drone_id\":\"d2\",\"bClosedLoop\":false,\"waypoints\":[{\"location\":{\"x\":-1000,\"y\":0,\"z\":-500}},{\"location\":{\"x\":-2000,\"y\":1000,\"z\":-500}}]}]}"
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/drones -H "Content-Type: application/json" -d '{"name":"UAV2","slot":2}'
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d '{"position":[0,0,-5],"q":[1,0,0,0],"velocity":[0,0,0],"battery":85,"gps_lat":39.9,"gps_lon":116.3,"gps_alt":50}'
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/2/inject -H "Content-Type: application/json" -d '{"position":[5,0,-5],"q":[1,0,0,0],"velocity":[0,0,0],"battery":90,"gps_lat":39.9,"gps_lon":116.31,"gps_alt":50}'
curl.exe -X POST http://127.0.0.1:8080/api/arrays `
  -H "Content-Type: application/json" `
  -d '{"array_id":"a1","mode":"recon","paths":[{"pathId":1,"drone_id":"d1","bClosedLoop":false,"waypoints":[{"location":{"x":1000,"y":0,"z":-500}},{"location":{"x":2000,"y":1000,"z":-500}}]},{"pathId":2,"drone_id":"d2","bClosedLoop":false,"waypoints":[{"location":{"x":-1000,"y":0,"z":-500}},{"location":{"x":-2000,"y":1000,"z":-500}}]}]}'
```

**模拟到达首航点并查询集结状态**

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d "{\"position\":[10,0,5],\"q\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":85}"
curl -X POST http://127.0.0.1:8080/api/debug/drone/2/inject -H "Content-Type: application/json" -d "{\"position\":[-10,0,5],\"q\":[1,0,0,0],\"velocity\":[0,0,0],\"battery\":90}"
curl http://127.0.0.1:8080/api/debug/arrays/a1/state
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/1/inject -H "Content-Type: application/json" -d '{"position":[10,0,5],"q":[1,0,0,0],"velocity":[0,0,0],"battery":85}'
curl.exe -X POST http://127.0.0.1:8080/api/debug/drone/2/inject -H "Content-Type: application/json" -d '{"position":[-10,0,5],"q":[1,0,0,0],"velocity":[0,0,0],"battery":90}'
curl.exe http://127.0.0.1:8080/api/debug/arrays/a1/state
```

**预期结果**：WebSocket 依次收到 `assembling`、`assembly_complete`，状态查询中 `exec_running=true`。

### 测试 9.2：停止阵列任务

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/arrays/a1/stop
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/arrays/a1/stop
```

**预期结果**：返回 `{"array_id":"a1","status":"stopped"}`。

---

## 10. 侦察/巡逻/攻击模式

### 测试 10.1：侦察模式

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/debug/cmd/1/array -H "Content-Type: application/json" -d "{\"mode\":\"recon\",\"loop\":true,\"waypoints\":[{\"x\":1000,\"y\":0,\"z\":-500},{\"x\":2000,\"y\":1000,\"z\":-500}]}"
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/array `
  -H "Content-Type: application/json" `
  -d '{"mode":"recon","loop":true,"waypoints":[{"x":1000,"y":0,"z":-500},{"x":2000,"y":1000,"z":-500}]}'
```

**预期结果**：非循环侦察到末航点后悬停；`loop=true` 时到末航点后回到第一个航点。

### 测试 10.2：巡逻模式与目标注入

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/debug/cmd/1/array -H "Content-Type: application/json" -d "{\"mode\":\"patrol\",\"loop\":false,\"waypoints\":[{\"x\":1000,\"y\":0,\"z\":-500},{\"x\":2000,\"y\":0,\"z\":-500}]}"
curl -X POST http://127.0.0.1:8080/api/debug/cmd/1/target -H "Content-Type: application/json" -d "{\"x\":3000,\"y\":3000,\"z\":-500}"
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/array `
  -H "Content-Type: application/json" `
  -d '{"mode":"patrol","loop":false,"waypoints":[{"x":1000,"y":0,"z":-500},{"x":2000,"y":0,"z":-500}]}'
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/target `
  -H "Content-Type: application/json" `
  -d '{"x":3000,"y":3000,"z":-500}'
```

**预期结果**：目标注入后，中断当前巡逻航点，飞向目标点。

### 测试 10.3：攻击模式

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/debug/cmd/1/array -H "Content-Type: application/json" -d "{\"mode\":\"attack\",\"loop\":false,\"waypoints\":[{\"x\":500,\"y\":0,\"z\":-500},{\"x\":1000,\"y\":500,\"z\":-500},{\"x\":1500,\"y\":1000,\"z\":-500}]}"
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/1/array `
  -H "Content-Type: application/json" `
  -d '{"mode":"attack","loop":false,"waypoints":[{"x":500,"y":0,"z":-500},{"x":1000,"y":500,"z":-500},{"x":1500,"y":1000,"z":-500}]}'
```

**预期结果**：依次经过中间航点，到最后攻击目标点后悬停，不循环。

---

## 11. 多机并发与避障

### 测试 11.1：批量阵列任务

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/debug/cmd/batch/array -H "Content-Type: application/json" -d "[{\"drone_id\":\"d1\",\"mode\":\"recon\",\"waypoints\":[{\"x\":100,\"y\":0,\"z\":-300}]},{\"drone_id\":\"d2\",\"mode\":\"recon\",\"waypoints\":[{\"x\":-100,\"y\":0,\"z\":-300}]}]"
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/debug/cmd/batch/array `
  -H "Content-Type: application/json" `
  -d '[{"drone_id":"d1","mode":"recon","waypoints":[{"x":100,"y":0,"z":-300}]},{"drone_id":"d2","mode":"recon","waypoints":[{"x":-100,"y":0,"z":-300}]}]'
```

**预期结果**：两架无人机进入同一集结任务；集结完成后，执行引擎为每架机启动独立线程。

### 测试 11.2：基础避障观察

避障需要两机都处于执行阶段，并持续注入相互接近的位置与速度。低优先级机（ID 较大的无人机）会收到临时偏移目标，约 3 秒后恢复原目标。建议用 Wireshark 抓控制端口 `8889/8891`，或查询两机队列：

```cmd
:: cmd
curl http://127.0.0.1:8080/api/debug/drone/1/queue
curl http://127.0.0.1:8080/api/debug/drone/2/queue
```

```powershell
# PowerShell
curl.exe http://127.0.0.1:8080/api/debug/drone/1/queue
curl.exe http://127.0.0.1:8080/api/debug/drone/2/queue
```

### 测试 11.3：阵列预演与碰撞风险

**测试目的**：在正式下发前先检查路径结构、无人机注册情况和碰撞风险。

```cmd
:: cmd
curl -X POST http://127.0.0.1:8080/api/arrays/preview -H "Content-Type: application/json" -d "{\"array_id\":\"preview1\",\"mode\":\"recon\",\"paths\":[{\"pathId\":1,\"drone_id\":\"d1\",\"waypoints\":[{\"location\":{\"x\":100,\"y\":0,\"z\":-300}},{\"location\":{\"x\":300,\"y\":0,\"z\":-300}}]},{\"pathId\":2,\"drone_id\":\"d2\",\"waypoints\":[{\"location\":{\"x\":120,\"y\":0,\"z\":-300}},{\"location\":{\"x\":320,\"y\":0,\"z\":-300}}]}]}"
```

```powershell
# PowerShell
curl.exe -X POST http://127.0.0.1:8080/api/arrays/preview `
  -H "Content-Type: application/json" `
  -d '{"array_id":"preview1","mode":"recon","paths":[{"pathId":1,"drone_id":"d1","waypoints":[{"location":{"x":100,"y":0,"z":-300}},{"location":{"x":300,"y":0,"z":-300}}]},{"pathId":2,"drone_id":"d2","waypoints":[{"location":{"x":120,"y":0,"z":-300}},{"location":{"x":320,"y":0,"z":-300}}]}]}'
```

**预期结果**：返回 JSON 中 `valid=false`，`collision_risks` 非空，`threshold_m` 对应 `config.yaml` 中的避障半径。

### 测试 11.4：调试指标与避障状态

```cmd
:: cmd
curl http://127.0.0.1:8080/api/debug/metrics
curl http://127.0.0.1:8080/api/debug/avoidance
```

```powershell
# PowerShell
curl.exe http://127.0.0.1:8080/api/debug/metrics
curl.exe http://127.0.0.1:8080/api/debug/avoidance
```

**预期结果**：`/api/debug/metrics` 显示注册数、在线数、集结状态和避障统计；`/api/debug/avoidance` 显示最近一次碰撞风险或恢复事件。

---

## 12. 集成测试

> 集成脚本会自己启动和关闭后端。运行前请先关闭手动启动的 `DroneBackend.exe`，避免占用 8080/8081。

### 测试 12.1：Week 1-3 后端集成测试

```cmd
:: cmd
cd /d f:\UE5DroneControl
python tools\integration_week3.py
```

```powershell
# PowerShell
cd f:\UE5DroneControl
python tools\integration_week3.py
```

**预期结果**：终端输出 `week1-3 backend integration: PASS`。

### 测试 12.2：Week 4 后端集成测试

```cmd
:: cmd
cd /d f:\UE5DroneControl
python tools\integration_week4.py
```

```powershell
# PowerShell
cd f:\UE5DroneControl
python tools\integration_week4.py
```

**预期结果**：终端输出 `week4 backend integration: ALL PASS`。

### 测试 12.3：Week 5 后端收尾测试

```cmd
:: cmd
cd /d f:\UE5DroneControl
python tools\integration_week5.py
```

```powershell
# PowerShell
cd f:\UE5DroneControl
python tools\integration_week5.py
```

**预期结果**：终端输出 `ALL PASS`。

**覆盖内容**：注册、`/api/arrays/preview` 预演、正式集结、`/api/debug/metrics`、`/api/debug/avoidance`、停机收尾。

> 注意：Week 3/4/5 集成脚本都会启动同一套默认端口的后端实例，**请顺序执行，不要并行运行**，否则会互相抢占 8080/8081 和临时文件。

---

## 13. 录屏建议

推荐录制三个窗口：后端日志窗口、curl 命令窗口、WebSocket 客户端窗口。集成测试可以单独录制一个窗口，重点展示脚本开始命令和最终 `PASS / ALL PASS` 行。

录制前检查：

- 后端已启动，或集成脚本运行前已关闭手动后端。
- WebSocket 客户端已连接到 `ws://127.0.0.1:8081/ws`。
- 终端字体调大到 16pt 以上。
- 如果演示注册流程，先确认没有残留同名/同槽位无人机。

---

## 附录：常见问题排查

### Q1：端口 8080/8081 被占用

```cmd
:: cmd
netstat -ano | findstr :8080
netstat -ano | findstr :8081
taskkill /PID <PID号> /F
```

```powershell
# PowerShell
Get-NetTCPConnection -LocalPort 8080,8081 | Select-Object LocalPort,OwningProcess
Stop-Process -Id <PID号> -Force
```

### Q2：PowerShell 中 curl 行为异常

请使用 `curl.exe`，不要只写 `curl`。PowerShell 中 `curl` 是 `Invoke-WebRequest` 的别名。

### Q3：Python 脚本缺少依赖

```cmd
:: cmd
pip install websockets requests websocket-client
```

```powershell
# PowerShell
pip install websockets requests websocket-client
```

### Q4：注册接口返回 409

说明名称或 slot 已存在。换一个 `name/slot`，或先删除已有无人机：

```cmd
:: cmd
curl -X DELETE http://127.0.0.1:8080/api/drones/1
```

```powershell
# PowerShell
curl.exe -X DELETE http://127.0.0.1:8080/api/drones/1
```
