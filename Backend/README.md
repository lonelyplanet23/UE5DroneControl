# BackEnd 运行、测试与联调指南

这份文档只使用 PowerShell 命令格式，覆盖下面几类工作：

1. 如何构建后端
2. 如何启动后端
3. 如何直接用 HTTP / `curl.exe` 测试后端
4. 如何运行 mock UE 模拟前端
5. 如何用 mock UE 做双向交互验证
6. 如何做一键集成测试
7. 如何和真实 UE 前端联调
8. 如何接 Jetson / PX4 真实链路

## 1. 先看推荐用法

在 PowerShell 中，最常用的三条命令是：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\build.bat --test
.\run_backend.bat
```

如果你只想快速确认后端和 mock UE 能不能跑通：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\run_mock_ue_integration.bat
```

## 2. 目录中的关键脚本

优先使用这些现成脚本：

- `build.bat`
  负责自动安装 C++ 依赖、配置 CMake、编译后端、可选运行单元测试。

- `run_backend.bat`
  负责补齐运行时 DLL 所需 `PATH`，并启动 `build\DroneBackend.exe`。

- `run_mock_ue_integration.bat`
  负责自动启动后端并跑一套 mock UE 端到端验证。

- `tests\mock_ue\run.bat`
  启动交互式 mock UE 前端模拟器。

## 3. 首次准备

### 3.1 必备环境

建议本机已有：

- Visual Studio，带 C++ 工具链
- CMake
- Python 3.10+

### 3.2 C++ 依赖

后端依赖由 `vcpkg` manifest 管理，定义文件在：

- [vcpkg.json](/f:/UE5DroneControl/BackEnd/vcpkg.json)

通常不需要手动逐个安装包，直接运行：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\build.bat
```

脚本会自动处理：

- `Boost`
- `yaml-cpp`
- `nlohmann-json`
- `spdlog`
- `gtest`

## 4. 构建后端

### 4.1 推荐构建方式

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\build.bat
```

如果构建后要立刻跑单元测试：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\build.bat --test
```

构建成功后，可执行文件在：

- [build/DroneBackend.exe](/f:/UE5DroneControl/BackEnd/build/DroneBackend.exe)

### 4.2 手动构建方式

如果你不想用脚本，可以手工执行：

```powershell
Set-Location F:\UE5DroneControl\BackEnd

& 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg\vcpkg.exe' install --x-manifest-root=. --triplet x64-windows

cmake -S . -B build -G Ninja `
  -DCMAKE_TOOLCHAIN_FILE='C:\Program Files\Microsoft Visual Studio\18\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake' `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DCMAKE_PREFIX_PATH="$PWD\vcpkg_installed\x64-windows"

cmake --build build --config Release
```

如果你的 Visual Studio / `vcpkg` 路径不同，替换为你机器上的实际路径即可。

## 5. 启动后端

### 5.1 默认启动

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\run_backend.bat
```

默认读取：

- [config.yaml](/f:/UE5DroneControl/BackEnd/config.yaml)

### 5.2 指定配置文件启动

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\run_backend.bat F:\UE5DroneControl\BackEnd\config.yaml
```

### 5.3 直接启动 exe

如果你想手工启动：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
$env:PATH = "$PWD\vcpkg_installed\x64-windows\bin;$env:PATH"
.\build\DroneBackend.exe .\config.yaml
```

### 5.4 检查后端是否启动成功

#### PowerShell 方式

```powershell
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/' -Method Get
```

#### `curl.exe` 方式

```powershell
curl.exe http://127.0.0.1:8080/
```

预期返回类似：

```json
{
  "status": "ok",
  "http_port": 8080,
  "ws_port": 8081,
  "debug": true
}
```

## 6. 配置文件说明

主配置文件：

- [config.yaml](/f:/UE5DroneControl/BackEnd/config.yaml)

常用字段：

- `server.http_port`
  HTTP REST 服务端口，默认 `8080`

- `server.ws_port`
  WebSocket 服务端口，默认 `8081`

- `server.debug`
  是否开启 `/api/debug/*` 调试接口；开发联调建议为 `true`

- `port_map`
  槽位到 UDP 端口的映射

- `jetson.host`
  后端发 UDP 控制包的真实目标主机

### 6.1 一个重要说明

注册接口里的：

- `ip`
- `port`

主要是注册信息和前端显示用途。

真正的 UDP 控制目标由：

- `jetson.host`
- `port_map.<slot>.send_port`

决定。

如果你接真实 Jetson / PX4，优先检查的是 `config.yaml`，不是只看注册接口里传入的 `ip/port`。

## 7. 测试方式总览

这里把测试方式明确分成 5 类：

### 7.1 测试 A：后端单元测试

用途：

- 验证纯 C++ 逻辑没有回归
- 不需要启动 UE
- 不需要手工发 HTTP 请求

执行：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\build.bat --test
```

或者手工执行：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
$env:PATH = "$PWD\vcpkg_installed\x64-windows\bin;$env:PATH"
ctest --test-dir .\build -C Release --output-on-failure
```

当前覆盖：

- 坐标转换
- 四元数 / yaw 转换
- GPS anchor 管理
- 指令队列
- 状态机

### 7.2 测试 B：直接用 HTTP / curl 测后端

用途：

- 不启动 UE
- 直接验证 REST 接口和 debug 接口
- 最适合排查“后端是否按协议工作”

先开一个 PowerShell 窗口启动后端：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\run_backend.bat
```

再开第二个 PowerShell 窗口执行下面命令。

#### B1. 健康检查

```powershell
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/' -Method Get
```

或：

```powershell
curl.exe http://127.0.0.1:8080/
```

#### B2. 查看无人机列表

```powershell
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/api/drones' -Method Get
```

或：

```powershell
curl.exe http://127.0.0.1:8080/api/drones
```

#### B3. 注册一架无人机

推荐 PowerShell 原生写法：

```powershell
$body = @{
  name = 'ps-test-1'
  model = 'PX4-SITL'
  slot = 1
  ip = '127.0.0.1'
  port = 8889
  video_url = ''
} | ConvertTo-Json

Invoke-RestMethod `
  -Uri 'http://127.0.0.1:8080/api/drones' `
  -Method Post `
  -ContentType 'application/json' `
  -Body $body
```

`curl.exe` 写法：

```powershell
curl.exe -X POST 'http://127.0.0.1:8080/api/drones' `
  -H "Content-Type: application/json" `
  -d "{\"name\":\"ps-test-1\",\"model\":\"PX4-SITL\",\"slot\":1,\"ip\":\"127.0.0.1\",\"port\":8889,\"video_url\":\"\"}"
```

#### B4. 注入一帧遥测，验证后端是否能处理 `power_on / telemetry / anchor`

```powershell
$body = @{
  position = @(1.0, 2.0, -3.0)
  q = @(0.965925826, 0.0, 0.0, 0.258819045)
  velocity = @(1.0, 0.0, 0.0)
  battery = 85
  gps_lat = 39.9042
  gps_lon = 116.4074
  gps_alt = 50.0
} | ConvertTo-Json

Invoke-RestMethod `
  -Uri 'http://127.0.0.1:8080/api/debug/drone/d1/inject' `
  -Method Post `
  -ContentType 'application/json' `
  -Body $body
```

#### B5. 查询 anchor

```powershell
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/api/drones/d1/anchor' -Method Get
```

#### B6. 直接发 move / pause / resume

发 move：

```powershell
$body = @{
  x = 1000
  y = 2000
  z = 500
} | ConvertTo-Json

Invoke-RestMethod `
  -Uri 'http://127.0.0.1:8080/api/debug/cmd/d1/move' `
  -Method Post `
  -ContentType 'application/json' `
  -Body $body
```

发 pause：

```powershell
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/api/debug/cmd/d1/pause' -Method Post
```

发 resume：

```powershell
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/api/debug/cmd/d1/resume' -Method Post
```

#### B7. 查看后端是否真的收到了命令

```powershell
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/api/debug/drone/d1/queue' -Method Get
```

这个接口现在会返回：

- `queue_size`
- `paused`
- `next_command`

非常适合验证：

- `move` 是否入队
- `pause/resume` 是否生效

### 7.3 测试 C：运行 mock UE 模拟前端

用途：

- 模拟“前端真的通过正式协议和后端交互”
- 不需要打开真实 UE
- 适合排查 HTTP / WebSocket 双向协议是否一致

#### C1. 启动 mock UE

先启动后端：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\run_backend.bat
```

再开第二个 PowerShell 窗口：

```powershell
Set-Location F:\UE5DroneControl\BackEnd\tests\mock_ue
.\run.bat
```

默认连接：

- HTTP: `http://127.0.0.1:8080`
- WebSocket: `ws://127.0.0.1:8081/`

#### C2. mock UE 中常用命令

在 mock UE shell 内输入：

```text
register name=drone-1 model=PX4-SITL slot=1 ip=127.0.0.1 port=8889
list
move d1 1000 2000 500
pause d1
resume d1
array samples/patrol_single_drone.json
events
telemetry
anchors
alerts
```

### 7.4 测试 D：用 mock UE 验证双向交互

这部分最重要。它验证的是：

1. 模拟前端 -> 后端
2. 后端 -> 模拟前端

#### D1. 方向一：mock UE -> 后端

目标：验证前端发出的 `move / pause / resume`，后端真的收到了。

步骤：

1. PowerShell 窗口 1 启动后端：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\run_backend.bat
```

2. PowerShell 窗口 2 启动 mock UE：

```powershell
Set-Location F:\UE5DroneControl\BackEnd\tests\mock_ue
.\run.bat
```

3. 在 mock UE shell 里注册：

```text
register name=drone-1 model=PX4-SITL slot=1 ip=127.0.0.1 port=8889
```

4. 在 mock UE shell 里发送命令：

```text
move d1 1000 2000 500
pause d1
resume d1
```

5. PowerShell 窗口 3 查询后端状态：

```powershell
Invoke-RestMethod -Uri 'http://127.0.0.1:8080/api/debug/drone/d1/queue' -Method Get
```

判断标准：

- 发 `move` 后，`queue_size` 应大于 0，`next_command` 应存在
- 发 `pause` 后，`paused` 应为 `true`
- 发 `resume` 后，`paused` 应恢复为 `false`

这就证明“模拟前端 -> 后端”的方向是通的。

#### D2. 方向二：后端 -> mock UE

目标：验证后端推送的 `power_on / telemetry / alert / assembly_*` 能被前端收到。

步骤：

1. 保持后端和 mock UE 已启动
2. 在 PowerShell 窗口 3 向后端注入遥测：

```powershell
$body = @{
  position = @(1.0, 2.0, -3.0)
  q = @(0.965925826, 0.0, 0.0, 0.258819045)
  velocity = @(1.0, 0.0, 0.0)
  battery = 85
  gps_lat = 39.9042
  gps_lon = 116.4074
  gps_alt = 50.0
} | ConvertTo-Json

Invoke-RestMethod `
  -Uri 'http://127.0.0.1:8080/api/debug/drone/d1/inject' `
  -Method Post `
  -ContentType 'application/json' `
  -Body $body
```

3. 回到 mock UE shell，查看：

```text
events
telemetry
anchors
```

判断标准：

- `events` 中应出现 `power_on`
- `anchors` 中应出现 `gps_lat / gps_lon / gps_alt`
- `telemetry` 中应出现位置、yaw、电量

再验证告警：

```powershell
$body = @{
  position = @(1.0, 2.0, -3.0)
  q = @(1.0, 0.0, 0.0, 0.0)
  velocity = @(0.0, 0.0, 0.0)
  battery = 15
  gps_lat = 39.9042
  gps_lon = 116.4074
  gps_alt = 50.0
} | ConvertTo-Json

Invoke-RestMethod `
  -Uri 'http://127.0.0.1:8080/api/debug/drone/d1/inject' `
  -Method Post `
  -ContentType 'application/json' `
  -Body $body
```

然后在 mock UE shell 里查看：

```text
alerts
events
```

判断标准：

- `alerts` 中应出现 `low_battery`

这就证明“后端 -> 模拟前端”的方向是通的。

#### D3. 验证阵列事件

在 mock UE shell 中：

```text
array samples/patrol_single_drone.json
```

再在 PowerShell 中注入合适位置的遥测，或直接运行一键集成测试。

然后在 mock UE shell 中查看：

```text
arrays
events
```

应能看到：

- `assembling`
- `assembly_complete`

或在超时场景下看到：

- `assembly_timeout`

### 7.5 测试 E：一键 mock UE 集成测试

用途：

- 自动完成后端 + mock UE 的 smoke test
- 最适合做回归检查

执行：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\run_mock_ue_integration.bat
```

这套测试会自动验证：

- 注册 / 删除无人机
- `power_on`
- `telemetry`
- `anchor`
- WebSocket `move`
- UDP 24 字节控制包
- `pause / resume`
- `low_battery`
- `assembly_complete`
- `assembly_timeout`

对应脚本：

- [tests/run_mock_ue_integration.py](/f:/UE5DroneControl/BackEnd/tests/run_mock_ue_integration.py)

## 8. 真实 UE 前端联调

当前 UE 网络层已和后端协议对齐，对应文件：

- [Source/UE5DroneControl/DroneOps/Network/DroneNetworkManager.h](/f:/UE5DroneControl/Source/UE5DroneControl/DroneOps/Network/DroneNetworkManager.h)
- [Source/UE5DroneControl/DroneOps/Network/DroneNetworkManager.cpp](/f:/UE5DroneControl/Source/UE5DroneControl/DroneOps/Network/DroneNetworkManager.cpp)

后端地址要求：

- HTTP: `http://<backend-ip>:8080`
- WebSocket: `ws://<backend-ip>:8081/`

### 8.1 真实 UE 联调步骤

1. 启动后端：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\run_backend.bat
```

2. 在 UE 中确认：

- `BackendBaseUrl = http://<backend-ip>:8080`
- `WebSocketUrl = ws://<backend-ip>:8081/`

3. 启动 UE Play

4. 让 UE 通过正式接口注册无人机
   如果当前 UE UI 还没全部接好，也可以先用 PowerShell 手工注册

5. 给后端提供遥测，有两种方式：

- 开发调试方式：
  用 `/api/debug/drone/{id}/inject`

- 真实链路方式：
  让 Jetson / PX4 桥接程序往 `recv_port` 发 YAML 遥测

6. 在 UE 中验证：

- 能轮询到 `/api/drones`
- 能收到 `power_on`
- 能收到 `telemetry`
- 能收到 `alert`
- 点击地图后发出 `move`
- `pause / resume` 正常

### 8.2 推荐的“纯前后端联调”方式

如果你这次只想确认 UE <-> BackEnd 协议，不想拉真实飞机，推荐：

1. 启动后端
2. 启动 UE
3. 用正式 HTTP 注册无人机
4. 用 `inject` 调试接口喂遥测
5. 在 UE 中看列表、锚点、姿态、移动、告警、阵列事件

这套方式最稳定，也最适合排查协议和显示问题。

## 9. Jetson / PX4 真实链路测试

真实链路的数据流是：

```text
UE  --HTTP/WS-->  BackEnd  --UDP-->  Jetson/PX4
Jetson/PX4 --YAML UDP--> BackEnd --WS push--> UE
```

### 9.1 需要确认的配置

先检查：

- [config.yaml](/f:/UE5DroneControl/BackEnd/config.yaml)

重点字段：

- `jetson.host`
- `port_map.<slot>.send_port`
- `port_map.<slot>.recv_port`

### 9.2 Jetson 侧桥接脚本

仓库中已有：

- [jetson_bridge.py](/f:/UE5DroneControl/BackEnd/jetson_bridge.py)

它负责：

- 接收后端 24 字节 UDP 控制包
- 转发到 ROS2 / PX4
- 订阅 ROS2 / PX4 遥测
- 打包成 YAML UDP 回给后端

### 9.3 真实链路联调顺序

推荐顺序：

1. 配好 `config.yaml`
2. 启动后端
3. 启动 Jetson / PX4 桥接程序
4. 注册无人机，确保 `slot` 与端口映射一致
5. 启动 UE
6. 验证是否出现：
   - `power_on`
   - 锚点
   - 持续 `telemetry`
   - UE 发出的 `move` 被桥接收到

## 10. 常见问题

### 10.1 构建时找不到 Boost / yaml-cpp / spdlog

直接重跑：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\build.bat
```

### 10.2 运行时提示缺少 DLL

不要直接双击 `build\DroneBackend.exe`，请优先用：

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\run_backend.bat
```

### 10.3 UE 能看到无人机列表，但没有 `power_on / telemetry`

先排查：

1. WebSocket 地址是否正确
2. `8081` 是否被防火墙拦截
3. 是否真的有遥测进入后端

最快判断方法：

```powershell
$body = @{
  position = @(0, 0, 0)
  q = @(1, 0, 0, 0)
  velocity = @(0, 0, 0)
  battery = 90
  gps_lat = 39.9
  gps_lon = 116.3
  gps_alt = 50.0
} | ConvertTo-Json

Invoke-RestMethod `
  -Uri 'http://127.0.0.1:8080/api/debug/drone/d1/inject' `
  -Method Post `
  -ContentType 'application/json' `
  -Body $body
```

如果 UE 立刻有反应，说明 UE 协议链路没问题，问题在真实遥测输入链路。

### 10.4 收到 `power_on` 但没有 anchor

请确认遥测中包含：

- `gps_lat`
- `gps_lon`
- `gps_alt`

后端现在会先记录 anchor，再推送 `power_on / reconnect`。

## 11. 推荐工作流

### 11.1 改完后端逻辑后

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\build.bat --test
```

### 11.2 做协议回归

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\run_mock_ue_integration.bat
```

### 11.3 做手动联调

```powershell
Set-Location F:\UE5DroneControl\BackEnd
.\run_backend.bat
```

然后：

- 启动真实 UE，或
- 启动 `tests\mock_ue\run.bat`

---

如果后面你还想要，我可以继续把这份文档再拆成两份：

- `README-QuickStart.md`
- `README-Testing.md`

这样新同学上手会更快，测试同学查命令也更方便。
