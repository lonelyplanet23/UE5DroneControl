# Mock UE 测试客户端

这个项目是一个独立的 Python UE 模拟器，用来并行联调当前 C++ 后端。

它只使用前端文档要求的正式接口：

- HTTP REST：`/api/drones`、`/api/arrays`、`/api/drones/{id}/anchor`
- WebSocket：`ws://{backend_ip}:8081/`
- UE -> 后端：`move` / `pause` / `resume`
- 后端 -> UE：`telemetry` / `event` / `alert` / `assembling` / `assembly_complete` / `assembly_timeout`

也就是说，它模拟的是“前端通讯层 + 本地注册表 + 锚点/告警状态缓存”，而不是调用后端调试接口冒充后端内部模块。

## 目录结构

```text
mock_ue/
├── mock_ue/
│   ├── __init__.py
│   ├── __main__.py
│   ├── app.py
│   ├── http_client.py
│   ├── main.py
│   ├── models.py
│   ├── state.py
│   └── ws_client.py
├── samples/
│   ├── patrol_single_drone.json
│   └── recon_two_drones.json
├── requirements.txt
├── run.bat
└── run.sh
```

## 安装

```bash
cd BackEnd/mock_ue
pip install -r requirements.txt
```

## 启动

```bash
python -m mock_ue.main shell
```

或直接：

```bash
run.bat
```

默认会：

- 连接 `ws://127.0.0.1:8081/`
- 每 2 秒轮询一次 `GET /api/drones`
- 在本地维护 UE 侧注册表、锚点、遥测缓存、告警状态和阵列状态

## 常用命令

### 连接与状态

```text
status
list
telemetry
anchors
alerts
arrays
events
poll
```

### 无人机管理

```text
register name=drone-1 model=PX4-SITL slot=1 ip=127.0.0.1 port=8889
update d1 name=drone-1A port=9999
delete d1
anchor d1
```

### 选择集

```text
select d1 d2
selected
clear
```

### 实时控制

```text
move d1 1000 2000 500
move-selected 1000 2000 500
pause d1 d2
resume d1 d2
pause
resume
```

说明：

- `move` 和 `move-selected` 的 `x/y/z` 是相对锚点的 UE 偏移，单位厘米
- `pause` / `resume` 不带参数时，默认作用于当前 `select` 的无人机

### 阵列任务

```text
array samples/recon_two_drones.json
array samples/patrol_single_drone.json
stop a1
```

### 批量脚本

```text
script my_commands.txt
```

脚本文件中一行一个 shell 命令，空行和 `#` 开头会被忽略。

## 与前端文档的对应关系

- `HTTP Client 封装`：`http_client.py`
- `WebSocket Client 封装`：`ws_client.py`
- `本地注册表与后端同步`：`state.py`
- `无人机列表轮询`：`app.py` 内部轮询线程
- `power_on / reconnect 锚定流程`：`state.py` 会缓存 GPS anchor
- `告警弹窗 / 集结弹窗`：这里用事件日志代替 UI 弹窗

## 限制

- 不做 Cesium GPS -> UE 世界坐标转换，这里只缓存 anchor 数据，便于验证协议正确性
- 不模拟 UE 影子机/镜像机渲染，只维护其必要的通讯态和本地状态
- 不调用后端调试接口；如果要驱动后端产生遥测，请继续使用后端已有的测试入口或真实 UDP 遥测源
