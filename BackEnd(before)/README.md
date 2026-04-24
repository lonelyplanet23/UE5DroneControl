# BackEnd 使用与测试说明

> 这不是一份正式规范文档。
> 
> 它的目的只有一个：让你尽快把当前的 C++ 后端和 `mock_ue` 跑起来，并知道怎么做最基本的联调和排查。

---

## 1. 当前这套东西是什么

现在 `BackEnd` 里有两部分：

- `cpp/`
  当前真实后端，C++ 实现
- `mock_ue/`
  Python 写的 UE 前端模拟器，用来按前端文档规定的 HTTP + WebSocket 协议和后端交互

可以把它理解成：

- 后端是真实的
- 前端 UI/Cesium/UE 场景不是真实的
- 但前端通讯层是被 `mock_ue` 模拟出来的

所以现在可以做：

- 注册无人机
- 查询无人机列表
- 发送 `move / pause / resume`
- 下发阵列任务
- 接收后端推送的 `telemetry / event / alert / assembling / assembly_complete / assembly_timeout`

但也要注意：

- `mock_ue` 不模拟 PX4 / ROS2 / 真实无人机
- 如果你想看到后端向前端推送遥测，需要额外给后端喂遥测
- 最简单的喂法是后端 debug 接口
- 更接近真实的是直接发 UDP YAML 遥测

---

## 2. 目录说明

```text
BackEnd/
├── run.bat                  # 启动 C++ 后端（Windows）
├── run.sh                   # 启动 C++ 后端（Linux/Mac）
├── config.yaml              # 后端配置
├── data/drones.json         # 无人机持久化数据
├── logs/backend.log         # 后端日志
├── cpp/                     # C++ 后端源码和可执行文件
├── mock_ue/                 # Python UE 模拟器
├── 开发文档.md               # 上一版后端方案归档文档
└── 使用与测试说明.md          # 本文档
```

---

## 3. 默认端口

默认来自 `config.yaml`：

- HTTP：`8080`
- WebSocket：`8081`
- 无人机 slot 1 遥测接收端口：`8888`
- 无人机 slot 1 控制发送端口：`8889`
- 无人机 slot 2 遥测接收端口：`8890`
- 无人机 slot 2 控制发送端口：`8891`
- 其余依次类推

如果你改了 `config.yaml`，联调命令也要跟着改。

---

## 4. 先启动真实后端

### 4.1 Windows

```powershell
cd /d f:\UE5DroneControl\BackEnd
.\run.bat
```

等价于：

```powershell
cd /d f:\UE5DroneControl\BackEnd
.\cpp\drone_backend.exe config.yaml
```

### 4.2 判断后端是否启动成功

新开一个 PowerShell：

```powershell
Invoke-RestMethod -Uri http://127.0.0.1:8080/
```

正常应返回类似：

```json
{
  "status": "ok",
  "http_port": 8080,
  "ws_port": 8081,
  "debug": true
}
```

如果失败：

- 看 `BackEnd/logs/backend.log`
- 检查 `8080` / `8081` 是否被占用
- 检查是不是已经有旧的 `drone_backend.exe` 进程在跑

---

## 5. 启动 mock_ue

### 5.1 首次安装依赖

```powershell
cd /d f:\UE5DroneControl\BackEnd\mock_ue
python -m pip install -r requirements.txt
```

### 5.2 启动

```powershell
cd /d f:\UE5DroneControl\BackEnd\mock_ue
python -m mock_ue.main shell
```

或者：

```powershell
.\run.bat
```

默认连接：

- HTTP：`http://127.0.0.1:8080`
- WebSocket：`ws://127.0.0.1:8081/`

如果后端不在本机：

```powershell
python -m mock_ue.main --http-base http://192.168.1.100:8080 --ws-url ws://192.168.1.100:8081/ shell
```

---

## 6. 第一次最小跑通

进入 `mock_ue` 后，先执行：

```text
status
list
```

说明：

- `status` 看 HTTP、WS 是否连上
- `list` 看本地同步到的无人机列表

如果刚启动时 `WS connected: False`，有时只是连接线程刚开始，等 1 秒再看一次 `status` 即可。

---

## 7. 注册无人机

在 `mock_ue` 里输入：

```text
register name=drone-1 model=PX4-SITL slot=1 ip=127.0.0.1 port=8889
register name=drone-2 model=PX4-SITL slot=2 ip=127.0.0.1 port=8891
list
```

如果返回 slot 冲突，通常是：

- `BackEnd/data/drones.json` 里已经有持久化数据
- 或者你之前已经注册过同一个 slot

想从空状态开始，可以停掉后端后把：

```text
BackEnd/data/drones.json
```

改回：

```json
{"next_id":1,"drones":[]}
```

然后重启后端。

---

## 8. 发送实时控制指令

### 8.1 单机 move

```text
move d1 1000 2000 500
```

含义：

- 向 `d1` 发送目标点
- `x/y/z` 是 UE 相对锚点偏移
- 单位是厘米

### 8.2 多机选择后统一发送

```text
select d1 d2
move-selected 1000 2000 500
```

### 8.3 暂停和恢复

```text
pause d1 d2
resume d1 d2
```

如果已经 `select` 了，也可以直接：

```text
pause
resume
```

### 8.4 看本地状态

```text
events
telemetry
anchors
alerts
arrays
```

这些都是 `mock_ue` 本地缓存出来的“模拟 UE 前端状态”。

---

## 9. 下发阵列任务

### 9.1 查看自带样例

在 `mock_ue` 中：

```text
samples
```

会看到：

- `samples/recon_two_drones.json`
- `samples/patrol_single_drone.json`

### 9.2 提交阵列任务

```text
array samples/recon_two_drones.json
arrays
```

### 9.3 停止阵列任务

```text
stop mock-recon-2
```

### 9.4 注意事项

样例文件里默认使用的无人机 ID 是：

- `d1`
- `d2`

如果你实际注册出来的 ID 不是这两个，就需要改样例 JSON 里的 `drone_id`。

样例文件位置：

- `BackEnd/mock_ue/samples/recon_two_drones.json`
- `BackEnd/mock_ue/samples/patrol_single_drone.json`

---

## 10. 为什么我发了 move，但看不到 telemetry

因为：

- `move` 只是前端 -> 后端控制指令
- `telemetry` 是后端 <- 无人机遥测之后，再由后端 -> 前端推送

也就是说，只启动后端和 `mock_ue`，但不喂遥测时：

- 可以测试控制链路
- 不能自动看到真实遥测链路的推送效果

所以你要额外给后端注入遥测。

---

## 11. 最简单的遥测注入方法：debug inject

新开一个 PowerShell，执行：

```powershell
$body = @{
  position = @(1.0, 2.0, -10.0)
  q = @(1.0, 0.0, 0.0, 0.0)
  velocity = @(0.5, 0.3, -0.1)
  battery = 18
  gps_lat = 39.9
  gps_lon = 116.3
  gps_alt = 50.0
} | ConvertTo-Json

Invoke-RestMethod `
  -Method Post `
  -Uri http://127.0.0.1:8080/api/debug/drone/d1/inject `
  -ContentType "application/json" `
  -Body $body
```

如果成功，此时 `mock_ue` 一般会收到：

- `power_on` 或 `reconnect`
- `telemetry`
- 如果电量低于阈值，还会收到 `low_battery`

这时回到 `mock_ue` 看：

```text
events
telemetry
anchors
alerts
```

---

## 12. 更接近真实的遥测方法：直接发 UDP YAML

以后如果不想依赖 debug inject，可以直接往后端监听端口发遥测 YAML。

### 12.1 slot 1 示例

slot 1 默认 `recv_port = 8888`

新开一个 PowerShell：

```powershell
$yaml = @"
timestamp: 1713800000000000
position: [5.0, 6.0, -7.0]
q: [1.0, 0.0, 0.0, 0.0]
velocity: [0.1, 0.2, 0.3]
angular_velocity: [0.0, 0.0, 0.01]
battery: 66
gps_lat: 40.0
gps_lon: 116.5
gps_alt: 55.0
"@

$udp = New-Object System.Net.Sockets.UdpClient
$bytes = [System.Text.Encoding]::UTF8.GetBytes($yaml)
[void]$udp.Send($bytes, $bytes.Length, "127.0.0.1", 8888)
$udp.Close()
```

如果注册的无人机在 slot 1，对应前端缓存就会更新。

---

## 13. 一套建议的联调顺序

如果你只是想确认“这套东西是不是活的”，建议按这个顺序跑：

1. 启动后端
2. 启动 `mock_ue`
3. `status`
4. 注册 1~2 架无人机
5. `list`
6. `move d1 1000 2000 500`
7. 用 debug inject 或 UDP YAML 喂一帧遥测
8. 看 `events`
9. 看 `telemetry`
10. 看 `anchors`
11. 提交一个样例阵列
12. 看 `arrays`

这样就把最关键的链路全走了一遍：

- REST
- WebSocket 上行
- WebSocket 下行
- 后端状态缓存
- 阵列任务入口

---

## 14. mock_ue 常用命令速查

### 14.1 状态和查看

```text
status
list
poll
anchors
anchor d1
telemetry
telemetry d1
alerts
arrays
events
```

### 14.2 无人机管理

```text
register name=drone-1 model=PX4-SITL slot=1 ip=127.0.0.1 port=8889
update d1 name=drone-1A port=9999
delete d1
```

### 14.3 选择集

```text
select d1 d2
selected
clear
```

### 14.4 实时控制

```text
move d1 1000 2000 500
move-selected 1000 2000 500
pause
pause d1 d2
resume
resume d1 d2
```

### 14.5 阵列任务

```text
samples
array samples/recon_two_drones.json
stop mock-recon-2
```

### 14.6 脚本执行

可以把多条命令写到文本文件里，再执行：

```text
script my_commands.txt
```

脚本文件规则：

- 一行一条命令
- 空行忽略
- `#` 开头的行忽略

例如：

```text
# 先注册两架无人机
register name=drone-1 model=PX4-SITL slot=1 ip=127.0.0.1 port=8889
register name=drone-2 model=PX4-SITL slot=2 ip=127.0.0.1 port=8891
select d1 d2
move-selected 1000 2000 500
```

---

## 15. 常见问题

### 15.1 `slot already exists`

原因：

- 这个 slot 已经被注册
- 注册数据是持久化的，重启后端不会自动清空

处理：

- 先 `list`
- 或清空 `BackEnd/data/drones.json`

### 15.2 `WebSocket 未连接`

原因通常是：

- 后端还没启动
- `8081` 没监听
- 地址写错

检查：

```powershell
Invoke-RestMethod -Uri http://127.0.0.1:8080/
```

以及看：

```text
BackEnd/logs/backend.log
```

### 15.3 提交阵列时报 `drone not found`

原因：

- 样例 JSON 里的 `drone_id` 跟当前注册出来的 ID 不一致

处理：

- 用 `list` 看当前 ID
- 改 `mock_ue/samples/*.json`

### 15.4 收不到 telemetry

原因：

- 你只启动了后端和 `mock_ue`
- 但没有给后端喂遥测

处理：

- 用 debug inject
- 或 UDP YAML

### 15.5 后端端口被占用

PowerShell 查端口：

```powershell
Get-NetTCPConnection -LocalPort 8080,8081 -State Listen
```

查占用进程：

```powershell
Get-Process | Where-Object { $_.ProcessName -like 'drone_backend*' }
```

---

## 16. 一句话总结怎么用

如果你只想最快跑起来：

1. 运行 `BackEnd\run.bat`
2. 运行 `BackEnd\mock_ue\run.bat`
3. 在 `mock_ue` 里注册无人机
4. 发送 `move`
5. 另开一个 PowerShell 用 debug inject 喂一帧遥测
6. 回到 `mock_ue` 看 `events / telemetry / anchors / alerts`

这就已经可以完成一轮“Python 模拟前端 UE + C++ 真实后端”的基础联调。

---

## 17. 相关文件

- 后端启动脚本：`BackEnd/run.bat`
- 后端配置：`BackEnd/config.yaml`
- 后端日志：`BackEnd/logs/backend.log`
- 后端持久化数据：`BackEnd/data/drones.json`
- mock_ue 说明：`BackEnd/mock_ue/README.md`
- mock_ue 样例：`BackEnd/mock_ue/samples/`

如果后面联调流程变了，就直接改这份文档，不用纠结格式。
