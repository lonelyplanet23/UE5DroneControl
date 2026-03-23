# UE5DroneControl

基于 Unreal Engine 5 的无人机控制与数据桥接项目。实现 UE5 仿真无人机与真实 PX4 无人机之间的双向数据链路。

---

## 目录

1. [项目架构](#1-项目架构)
2. [文件说明](#2-文件说明)
3. [通信协议](#3-通信协议)
4. [坐标系转换](#4-坐标系转换)
5. [环境要求与安装](#5-环境要求与安装)
6. [运行流程](#6-运行流程)
7. [关键配置](#7-关键配置)
8. [输入控制](#8-输入控制)
9. [调试信息](#9-调试信息)

---

## 1. 项目架构

系统由两条独立的数据链路组成：

```
【链路 A：PX4 → UE5（遥测显示）】

真实无人机 (PX4)
  ↓ UART (ttyTHS1, 921600bps)
MicroXRCE Agent (运行在 Jetson, 192.168.30.104)
  ↓ ROS2 话题: /px4_1/fmu/out/vehicle_odometry
drone_data_bridge.py (运行在 Windows PC)
  ↓ UDP YAML 格式, 端口 8888
ARealTimeDroneReceiver (UE5 C++ Actor)
  → 驱动 BP_RealTimeDrone 无人机模型的位置和姿态


【链路 B：UE5 → PX4（反向控制，可选）】

AUE5DroneControlCharacter (UE5 玩家控制的无人机)
  ↓ UDP 24字节二进制, 端口 8889
ue_to_px4_bridge.py (ROS2 节点，运行在 Linux)
  ↓ ROS2 话题: /px4_1/fmu/in/trajectory_setpoint 等
真实无人机 (PX4 Offboard 模式)
```

UE5 场景中同时存在两架无人机：
- `BP_TopDownCharacter`（玩家控制，游戏开始时自动生成，发送 UDP 数据）
- `BP_RealTimeDrone`（预先放置在场景中，接收 UDP 数据，显示真实无人机状态）

---

## 2. 文件说明

### UE5 C++ 源码（Source/UE5DroneControl/）

| 文件 | 说明 |
|------|------|
| `UE5DroneControlCharacter.h/.cpp` | 玩家控制无人机基类。管理 UDP 发送 Socket、相机弹簧臂、高度控制、点击移动逻辑。定义 `FDroneSocketData` 24字节通信结构体。 |
| `RealTimeDroneReceiver.h/.cpp` | 真实无人机接收端 Actor，继承自 `AUE5DroneControlCharacter`。轮询 UDP Socket（端口 8888），解析 YAML 格式数据，将 NED 坐标转换为 UE5 坐标，平滑插值驱动模型位置和姿态。 |
| `UE5DroneControlPlayerController.h/.cpp` | 玩家控制器。处理鼠标点击移动、数字键 0/1 视角切换。 |
| `UE5DroneControlGameMode.h/.cpp` | 游戏模式基类，无特殊逻辑。 |
| `UE5DroneControl.Build.cs` | UBT 构建配置，声明模块依赖。 |

### Python 脚本

| 文件 | 说明 |
|------|------|
| `drone_data_bridge.py` | **主桥接脚本（链路 A）**。连接 virtualUAV WiFi，SSH 到无人机启动 MicroXRCE Agent 和 ros2 topic echo，从输出文件读取里程计数据，以 YAML 格式通过 UDP 转发到 UE5 端口 8888。 |
| `ue_to_px4_bridge.py` | **反向控制桥接（链路 B）**。ROS2 节点，接收 UE5 发来的 24 字节 UDP 数据（端口 8889），转换坐标后发布 PX4 Offboard 控制话题。 |
| `ue_px4_bridge.py` | 备用/旧版桥接脚本（参考用）。 |

### 配置文件

| 文件 | 说明 |
|------|------|
| `drone_bridge_config.yaml` | `drone_data_bridge.py` 的配置：UE5 地址/端口、无人机 SSH 信息、WiFi 信息、ROS2 话题。 |
| `ue_to_px4_config.yaml` | `ue_to_px4_bridge.py` 的配置：无人机 ID、话题前缀、UDP 端口、安全限制。 |
| `multi_drone_config.yaml` | 多无人机扩展配置（预留）。 |
| `requirements.txt` | Python 依赖（PyYAML 等）。 |

### 启动脚本

| 文件 | 说明 |
|------|------|
| `Run_Drone_Data_Bridge.bat` | 双击启动 `drone_data_bridge.py`（自动激活 drone_env 虚拟环境）。 |
| `Run_UE5_Bridge.bat` | 备用启动脚本。 |

### 其他

| 文件/目录 | 说明 |
|-----------|------|
| `drone_env/` | Python 虚拟环境（Windows，已包含在仓库中）。 |
| `px4_msgs_bak/` | PX4 ROS2 消息定义备份（供 `ue_to_px4_bridge.py` 使用）。 |
| `drone_bridge.log` | 运行日志（滚动，最大 10MB × 5 份）。 |
| `.vsconfig` | Visual Studio 2022 必需组件配置。 |

---

## 3. 通信协议

### 链路 A：PX4 → UE5（YAML over UDP）

`drone_data_bridge.py` 将 ROS2 `VehicleOdometry` 消息序列化为 YAML，通过 UDP 发送到 UE5 端口 8888。

`ARealTimeDroneReceiver` 解析的 YAML 格式（字段顺序固定）：

```yaml
timestamp: 1765353093720576
timestamp_sample: 1765353093720576
pose_frame: 1
position:
- 9.086    # NED X (北, 米)
- 35.671   # NED Y (东, 米)
- 2.786    # NED Z (下, 米，向下为正)
q:
- -0.691   # w
- -0.024   # x
- 0.006    # y
- 0.722    # z
velocity_frame: 1
velocity:
- ...
angular_velocity:
- ...
```

四元数格式：PX4/ROS2 顺序为 `[w, x, y, z]`，UE5 `FQuat` 构造函数为 `(x, y, z, w)`，代码中已做重排：
```cpp
// RealTimeDroneReceiver.cpp:336
OutData.Quaternion = FQuat(QuatData[1], QuatData[2], QuatData[3], QuatData[0]);
```

### 链路 B：UE5 → PX4（24字节二进制 over UDP）

`AUE5DroneControlCharacter` 发送到端口 8889（默认，可在编辑器 Details 面板修改 `RemotePort`）。

数据结构（`#pragma pack(1)`，小端，24字节）：

```cpp
// UE5DroneControlCharacter.h:16
struct FDroneSocketData {
    double Timestamp; // 8字节，Unix 时间戳（秒）
    float X;          // 4字节，位置 X（UE5 厘米）
    float Y;          // 4字节，位置 Y（UE5 厘米）
    float Z;          // 4字节，位置 Z（UE5 厘米）
    int32 Mode;       // 4字节，0=悬停/待机，1=飞行/移动
};
```

Python 解析格式字符串：`struct.unpack('<dfffi', data)`

发送频率：心跳包 10Hz（`SendInterval = 0.1f`），点击移动时额外发送高频包。

---

## 4. 坐标系转换

### NED → UE5（链路 A，接收方向）

`ARealTimeDroneReceiver::NEDToUE5()` 实现：

```
NED 坐标系（米）          UE5 坐标系（厘米）
X = North (北)    →   X = Forward  ×100
Y = East  (东)    →   Y = Right    ×100
Z = Down  (下，正) →   Z = Up      ×(-100)  [取负，方向相反]
```

位置使用相对偏移：第一次收到数据时记录 `ReferencePosition`，后续计算 `RelativeOffset = CurrentNED - ReferencePosition`，再加上 Actor 的 `InitialLocation`（BeginPlay 时记录）。

### 四元数 NED → UE5（姿态转换）

`ARealTimeDroneReceiver::QuatToEuler()` 实现：

```cpp
// 翻转 Z 分量（NED Down → UE5 Up）
FQuat UE5Quat(Q.X, Q.Y, -Q.Z, Q.W);
FRotator EulerAngles = UE5Quat.Rotator();
// 翻转 Yaw（右手系 → 左手系）
EulerAngles.Yaw = -EulerAngles.Yaw;
```

### UE5 → NED（链路 B，发送方向）

`ue_to_px4_bridge.py` 中的转换：

```
UE5 (厘米)    →    NED (米)
X             →    North = X × 0.01
Y             →    East  = Y × 0.01
Z             →    Down  = -Z × 0.01  [取负]
```

---

## 5. 环境要求与安装

### UE5 端

- Unreal Engine 5.7
- Visual Studio 2022（使用仓库根目录 `.vsconfig` 导入组件）
  - 工作负载：Native Desktop、Native Game、Managed Desktop
  - 组件：Unreal Engine Tools、MSVC 14.38/14.44、Windows 11 SDK 22621

### Python 端（Windows）

```bat
:: 创建虚拟环境（仓库已包含 drone_env，可跳过）
python -m venv drone_env

:: 激活
.\drone_env\Scripts\activate

:: 安装依赖（仅需 PyYAML）
pip install -r requirements.txt
```

`drone_data_bridge.py` 会自动检测并切换到 `drone_env`，无需手动激活。

---

## 6. 运行流程

### 链路 A：真实无人机 → UE5 遥测显示

1. 安装 UE5 5.7，并在 VS2022 中安装 `.vsconfig` 所列组件。
2. 右键点击 `UE5DroneControl.uproject`，选择 `Generate Visual Studio project files`。
3. 打开生成的 `UE5DroneControl.sln`，点击"本地 Windows 调试器"编译并运行 UE5 项目。
4. 在 World Outliner 中确认 `BP_RealTimeDrone` 存在，检查其 Details 面板：
   - `Remote IP`：`192.168.10.1`（无人机 IP，仅用于该 Actor 的发送方向，接收数据不依赖此项）
   - `Listen Port`：`8888`
   - `Use Received Rotation`：勾选（使用真实姿态）
   - `Auto Face Target`：取消勾选
   - 注意：`BP_RealTimeDrone` 只接收来自无人机的数据；发送数据的是 `BP_TopDownCharacter`（游戏运行时自动生成，两架无人机初始位置不在同一处）
5. 点击 UE5 的 Play 按钮。摄像机会自动跟随 `BP_TopDownCharacter`。按 `1` 键可切换视角到 `BP_RealTimeDrone`，按 `0` 键切换回来，按空格键在 -60° 斜视和 -90° 纯俯视之间切换。
6. 手动刷新电脑网络列表，直至发现无人机 WiFi 网络（SSID: `virtualUAV`），连接后继续。
7. 启动桥接脚本：
   ```bat
   双击 Run_Drone_Data_Bridge.bat
   :: 或
   python drone_data_bridge.py
   ```
   脚本会自动：
   - 打开终端1：SSH 到 `jetson1@192.168.30.104`，运行 `MicroXRCEAgent serial --dev /dev/ttyTHS1 -b 921600`
   - 打开终端2：SSH 到同一地址，运行 `ros2 topic echo /px4_1/fmu/out/vehicle_odometry`，输出重定向到临时文件
   - 主进程读取临时文件，解析 YAML，通过 UDP 转发到 `127.0.0.1:8888`
8. 先在终端1中输入 SSH 密码（`123456`），再在终端2中输入密码，确认终端2中出现里程计数据输出。
9. 回到 UE5，`BP_RealTimeDrone` 开始跟随真实无人机移动。

### 链路 B：UE5 → 真实无人机反向控制（可选）

需要在 Linux 环境（无人机伴飞计算机）运行：

```bash
python3 ue_to_px4_bridge.py --drone-id 1 --topic-prefix /px4_1 --udp-port 8889
```

同时需要将 UE5 中 `BP_TopDownCharacter` 的 `RemotePort` 改为 `8889`（默认已是 8889）。

---

## 7. 关键配置

### drone_bridge_config.yaml

```yaml
ue5:
  host: "127.0.0.1"   # UE5 地址（本机）
  port: 8888           # RealTimeDroneReceiver 监听端口

drone:
  ssh_host: "192.168.30.104"  # 无人机 Jetson 地址
  ssh_user: "jetson1"
  ssh_password: "123456"

network:
  ssid: "virtualUAV"
  password: "buaa123456"

ros2:
  topic: "/px4_1/fmu/out/vehicle_odometry"
```

### UE5 Details 面板（ARealTimeDroneReceiver）

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `Listen Port` | 8888 | UDP 监听端口 |
| `Smooth Speed` | 10.0 | 位置平滑插值速度（越大越快） |
| `Scale Factor` | 1.0 | 坐标缩放因子 |
| `Auto Face Target` | true | 自动朝向移动方向（有真实姿态时建议关闭） |
| `Use Received Rotation` | true | 使用接收到的四元数姿态 |
| `Max Update Frequency` | 60.0 | 位置更新频率上限（Hz），0 表示不限制 |
| `Rotation Dead Zone` | 0.5 | 旋转死区阈值（度），消除微小抖动 |
| `Auto Detect Port` | false | 自动扫描端口范围（调试用） |

### UE5 Details 面板（AUE5DroneControlCharacter / BP_TopDownCharacter）

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `Remote IP` | "127.0.0.1" | UDP 发送目标 IP |
| `Remote Port` | 8889 | UDP 发送目标端口 |
| `Enable UDP Send` | true | 是否启用 UDP 发送 |
| `Target Height` | 1000.0 | 初始目标高度（厘米） |
| `Lift Speed` | 300.0 | W/S 键升降速度 |

---

## 8. 输入控制

游戏运行时的键盘/鼠标操作：

| 输入 | 功能 |
|------|------|
| 鼠标左键点击地面 | 控制 `BP_TopDownCharacter` 移动到目标点，同时发送 UDP 数据（Mode=1） |
| `W` / `S` | 升高 / 降低无人机高度，发送高频 UDP 数据 |
| `空格键` | 切换当前视角的相机角度（-60° 斜视 ↔ -90° 纯俯视） |
| `0` | 切换视角到 `BP_TopDownCharacter`（玩家控制无人机） |
| `1` | 切换视角到 `BP_RealTimeDrone`（真实无人机接收端） |

---

## 9. 调试信息

### UE5 Output Log 关键日志

```
# Socket 创建成功
✅ UDP Socket Created! Sending to 127.0.0.1:8889

# 接收端监听启动
>>> [RealTimeDrone] 监听启动! Port: 8888 <<<

# 每30个包打印一次姿态（非 Shipping 构建）
>>> [HH:MM:SS.mmm] [四元数] W=..., X=..., Y=..., Z=...
>>> [HH:MM:SS.mmm] [欧拉角] Pitch=...°, Yaw=...°, Roll=...°

# 屏幕左上角实时显示（每10个包刷新）
位置:(X, Y, Z)cm | 偏转角: Pitch=...° Yaw=...° Roll=...°
```

### Python 脚本日志

日志文件：`drone_bridge.log`（滚动，最大 10MB × 5 份）

```
# 每500个包打印一次发送信息
[HH:MM:SS.mmm] [发送#500] 目标: 127.0.0.1:8888 | 大小: ...字节

# 每100个包打印一次位置
[HH:MM:SS.mmm] [数据#100] Position=[X.XXX, Y.YYY, Z.ZZZ]米
```

### 端口分配总览

| 端口 | 方向 | 用途 |
|------|------|------|
| 8888 | PX4 → UE5 | `drone_data_bridge.py` 发送，`ARealTimeDroneReceiver` 接收 |
| 8889 | UE5 → PX4 | `AUE5DroneControlCharacter` 发送，`ue_to_px4_bridge.py` 接收 |
