# UE5DroneControl

**基于 Unreal Engine 5 的无人机控制与数据桥接系统**

实现 UE5 仿真环境与真实 PX4 无人机之间的**双向通信**，支持单机和多机（最多3架）控制。

---

## 目录

1. [核心特性](#核心特性)
2. [系统架构](#系统架构)
3. [硬件配置规范](#硬件配置规范)
4. [通信协议](#通信协议)
5. [坐标系转换](#坐标系转换)
6. [文件结构说明](#文件结构说明)
7. [环境要求与安装](#环境要求与安装)
8. [运行流程](#运行流程)
9. [配置详解](#配置详解)
10. [故障排除](#故障排除)
11. [API参考](#api参考)
12. [开发者指南](#开发者指南)

---

## 核心特性

### ✅ 已完成功能

- **双向数据链路**: PX4 → UE5 (遥测显示) + UE5 → PX4 (控制指令)
- **OFFBOARD模式控制**: 纯Python实现，100Hz控制频率，兼容PX4 v1.16
- **GPS全局坐标统一**: 所有飞机参考同一地图中心 (lat0, lon0)，无需各自启动点
- **状态机控制逻辑**: 自动管理 上锁→解锁→OFFBOARD 流程
- **多机支持**: 同时控制3架无人机，通过位掩码选择目标
- **纯Python消息定义**: `ue_px4_msgs` 包，UE5端无需ROS2环境
- **实时遥测显示**: UE5实时显示无人机位置、姿态

---

## 系统架构

### 2.1 单机模式架构

```
【PX4 → UE5 遥测链路】
真实无人机 (PX4)
  ↓ UART (ttyTHS1, 921600bps)
MicroXRCE Agent (Jetson 192.168.30.xxx)
  ↓ ROS2 话题
  • /px4_1/fmu/out/vehicle_odometry (位置姿态)
  • /px4_1/fmu/out/vehicle_global_position (GPS)
drone_data_bridge.py (Windows PC)
  ↓ SSH + ROS2 echo → 文件读取 → UDP YAML
  (端口: 数据转发到 8888)
UE5: BP_RealTimeDroneReceiver (C++ Actor)
  → 驱动 BP_RealTimeDrone 模型


【UE5 → PX4 控制链路】
UE5: BP_TopDownCharacter (玩家控制)
  ↓ UDP 24字节二进制 (端口 8889)
ue_to_px4_bridge.py (Linux/Jetson)
  ↓ ROS2 话题
  • /px4_1/fmu/in/offboard_control_mode
  • /px4_1/fmu/in/trajectory_setpoint
  • /px4_1/fmu/in/vehicle_command
真实无人机 (PX4, OFFBOARD模式)
```

### 2.2 多机模式架构

```
UE5 仿真环境 (发送选择掩码 + 目标位置)
  ↓ UDP 32字节 (端口 8899)
multi_ue_controller.py (主控制器，运行在Windows)
  ↓ 解析位掩码，分别转发到各桥接端口
  • 8889 → px4_1
  • 8891 → px4_2
  • 8893 → px4_3
ue_to_px4_bridge.py ×3 (3个实例，分别对应3架飞机)
  ↓ ROS2 话题 (各自的话题前缀)
PX4 真实无人机 ×3 (px4_1, px4_2, px4_3)
```

**关键设计**:
- UE5端：支持多机选择（位掩码）和目标位置广播
- 主控制器：单点接收，灵活分发，维护各机选择状态
- 单机桥接：独立进程，互不干扰，每机专属线程
- 坐标系：统一GPS地图中心，确保多机在同一参考系

---

## 硬件配置规范

### 3.1 无人机命名与ID

| 无人机 | ROS2话题前缀       | MAVLink System ID | SSH默认用户 | SSH默认密码 |
|--------|-------------------|-------------------|-------------|-------------|
| px4_1  | `/px4_1`          | `2`               | `jetson1`   | `123456`    |
| px4_2  | `/px4_2`          | `3`               | `jetson2`   | `123456`    |
| px4_3  | `/px4_3`          | `4`               | `jetson3`   | `123456`    |

**重要**: MAVLink System ID ≠ 话题编号！`px4_1` 对应 ID `2`，以此类推。

### 3.2 网络配置

- **WiFi SSID**: `virtualUAV`
- **WiFi 密码**: `buaa123456`
- **局域网网段**: `192.168.30.xxx`

### 3.3 静态IP地址

| 无人机 | Jetson IP      | 推荐连接PC IP |
|--------|----------------|---------------|
| px4_1  | `192.168.30.104` | `192.168.30.100` (建议) |
| px4_2  | `192.168.30.102` | `192.168.30.100` |
| px4_3  | `192.168.30.103` | `192.168.30.100` |

**注意**: 原配置中的 `192.168.30.101` 已废弃，px4_1 实际 IP 为 `192.168.30.104`。

### 3.4 UDP端口分配

| 方向         | 单机端口    | 多机端口  | 说明                         |
|--------------|-------------|-----------|------------------------------|
| PX4 → UE5    | 8888, 8890, 8892 | 8888, 8890, 8892 | 每架飞机独占端口，传输YAML   |
| UE5 → PX4    | 8889, 8891, 8893 | 8899 (统一) | 多机模式：UE5→主控用8899，主控→单机桥接用各专属端口 |

---

## 通信协议

### 4.1 PX4 → UE5 (遥测数据)

**格式**: YAML over UDP

```yaml
timestamp: 1765353093720576
timestamp_sample: 1765353093720576
pose_frame: 1
position:
- 9.086    # NED X (北, 米)
- 35.671   # NED Y (东, 米)
- 2.786    # NED Z (下, 米)
q:
- -0.691   # w
- -0.024   # x
- -0.006   # y
- 0.722    # z
velocity_frame: 1
velocity:
- 0.0
- 0.0
- 0.0
angular_velocity:
- 0.0
- 0.0
- 0.0
position_variance:
- 0.0
- 0.0
- 0.0
orientation_variance:
- 0.0
- 0.0
- 0.0
velocity_variance:
- 0.0
- 0.0
- 0.0
reset_counter: 0
quality: 0
```

**字段说明**:
- `timestamp`: 自系统启动以来的时间（微秒）
- `position`: NED坐标系 (北-东-下)，单位米
- `q`: 四元数 [w, x, y, z]，从FRD车身坐标系到参考系
- 更多字段见 `px4_msgs/msg/VehicleOdometry.msg`

### 4.2 UE5 → PX4 (控制指令)

**格式**: 24字节二进制 (小端序)

```c
struct FDroneSocketData {
    double Timestamp;   // 8字节, Unix时间戳 (秒)
    float X;           // 4字节, UE5 X坐标 (厘米)
    float Y;           // 4字节, UE5 Y坐标 (厘米)
    float Z;           // 4字节, UE5 Z坐标 (厘米)
    int32 Mode;        // 4字节, 0=悬停, 1=移动
};
```

**坐标假设**: UE5坐标系中 X=前, Y=右, Z=上 → NED坐标系转换公式:
```
NED_X = X × 0.01   (前 → 北)
NED_Y = Y × 0.01   (右 → 东)
NED_Z = -Z × 0.01  (上 → 下)
```

**端口配置**:
- 单机模式: `8889` (px4_1), `8891` (px4_2), `8893` (px4_3)
- 多机模式: UE5 → 主控使用 `8899` (包含选择掩码)

### 4.3 多机控制协议 (32字节扩展)

```c
struct MultiDroneData {
    double timestamp;    // 8字节
    float x;            // 4字节 (UE5 X, cm)
    float y;            // 4字节
    float z;            // 4字节
    uint32 mode;        // 4字节
    uint32 drone_mask;  // 4字节 - 位掩码选择无人机
    uint32 sequence;    // 4字节 - 序列号
};
```

**位掩码编码**:
- 位0 (0x01): 选择 px4_1 (ID=1)
- 位1 (0x02): 选择 px4_2 (ID=2)
- 位2 (0x04): 选择 px4_3 (ID=3)

**向后兼容**: 24字节单机数据包也支持，自动解释为 `drone_mask = 0x07` (全选)。

---

## 坐标系转换

### 5.1 NED 与 UE5 坐标系

| 轴向 | NED (PX4) | UE5 (Unreal) | 转换公式 |
|------|-----------|--------------|----------|
| X    | 北 (North) | 前 (Forward) | UE5_X = NED_X × 100 |
| Y    | 东 (East)  | 右 (Right)   | UE5_Y = NED_Y × 100 |
| Z    | 下 (Down)  | 上 (Up)      | UE5_Z = NED_Z × (-100) |

**关键**: NED Z向下为正，UE5 Z向上为正，因此需要取负。

### 5.2 GPS 到局部坐标转换 (全局统一)

为了确保所有飞机参考同一坐标系（地图中心），我们引入GPS全局位置：

```python
# 用户在初始化时提供地图中心GPS坐标
map_center = (lat0, lon0, alt0)  # 例如: (39.90872, 116.39748, 50.0)

# 实时GPS → ENU (东-北-上) 局部坐标
(e, n, u) = gps_to_enu(lat, lon, alt, lat0, lon0, alt0)

# ENU → NED
(ned_n, ned_e, ned_d) = enu_to_ned(e, n, u)  # ned_d = -u

# 应用于odometry位置
corrected_position = raw_odom_position + [ned_n, ned_e, ned_d]
```

**数学原理** (简化Haversine):
```
纬度每度 ≈ 111319米 (在赤道)
经度每度 ≈ 111319 × cos(lat0) 米

n = (lat - lat0) × 111319
e = (lon - lon0) × 111319 × cos(lat0_rad)
u = alt - alt0
```

---

## 文件结构说明

```
UE5DroneControl/
├── Source/                          # UE5 C++源码
│   ├── UE5DroneControl/
│   │   ├── UE5DroneControlCharacter.{h,cpp}  # 玩家控制无人机 (发送UDP)
│   │   ├── RealTimeDroneReceiver.{h,cpp}     # 真实无人机接收端 (接收YAML)
│   │   └── ...
│   ├── Variant_Strategy/            # 游戏变体（非核心）
│   └── Variant_TwinStick/           # 游戏变体（非核心）
│
├── python/                          # Python脚本 (建议移动到此目录)
│   ├── ue_to_px4_bridge.py          # 单机控制桥接（主节点）
│   ├── multi_ue_controller.py       # 多机主控制器
│   ├── drone_data_bridge.py         # 数据接收桥接 (PX4→UE5)
│   ├── ue_px4_msgs/                 # UE5用纯Python消息包
│   │   ├── __init__.py
│   │   ├── messages.py              # 消息类定义
│   │   ├── utils.py                 # GPS和坐标转换
│   │   └── packets.py               # UDP数据包解析
│   ├── px4_msgs/                    # ROS2消息包 (系统依赖)
│   ├── px4_ros_com/                 # ROS2工具包 (含offboard示例)
│   ├── Config/                      # 配置文件
│   │   ├── drone_bridge_config.yaml
│   │   ├── ue_to_px4_config.yaml
│   │   ├── multi_drone_config.yaml
│   │   └── multi_controller_config.yaml
│   └── requirements.txt
│
├── Content/                         # UE5资源
├── Config/                          # UE5配置文件
├── README.md                        # 本文档
├── change.md                        # 变更记录 (详细)
├── todo.md                          # 待办事项清单
├── .vsconfig                        # Visual Studio 配置
├── UE5DroneControl.uproject        # UE5项目文件
└── (其他UE5生成文件)
```

---

## 环境要求与安装

### 7.1 UE5 端 (Windows)

- **Unreal Engine 5.7+**
- **Visual Studio 2022**: 使用 `.vsconfig` 导入组件
  - 工作负载: Native Desktop, Native Game, Managed Desktop
  - 组件: Unreal Engine Tools, MSVC 14.38/14.44, Windows SDK 11 22621
- **Python 3.10+** (可选，仅用于编辑C++代码中嵌入的Python脚本，如使用CLI工具)

**编译步骤**:
1. 双击 `UE5DroneControl.uproject`
2. 选择 VS2022 生成项目文件
3. 打开 `UE5DroneControl.sln`
4. 右键项目 → Build

### 7.2 Python 端 (Windows/Linux)

Python 依赖 (运行 `drone_data_bridge.py`):

```bash
# 进入项目目录
cd /path/to/UE5DroneControl

# 创建虚拟环境 (推荐)
python -m venv drone_env

# 激活虚拟环境
# Windows:
drone_env\Scripts\activate
# Linux/macOS:
source drone_env/bin/activate

# 安装依赖
pip install -r requirements.txt

# 可选: 安装ue_px4_msgs包 (用于Python端类型定义)
pip install -e ./ue_px4_msgs

# 安装ROS2 px4_msgs (必须在ROS2环境中)
# source /opt/ros/humble/setup.bash
# cd px4_msgs && pip install -e .
```

**requirements.txt**:
```
PyYAML>=6.0
```

---

## 运行流程

### 8.1 单机模式完整流程

#### 步骤1: 连接硬件网络
1. 在Windows上连接无人机WiFi: SSID=`virtualUAV`, 密码=`buaa123456`
2. 验证网络连通: `ping 192.168.30.104`
3. 准备SSH连接 (密码: `123456`)

#### 步骤2: 启动UE5
1. 打开 `UE5DroneControl.uproject`
2. 编译C++代码 (首次)
3. 点击 ▶ Play 运行游戏
4. 场景中应存在 `BP_RealTimeDrone` Actor

#### 步骤3: 启动数据桥接 (PX4 → UE5)
```bat
:: 方法A: 双击启动脚本
Run_Drone_Data_Bridge.bat

:: 方法B: 命令行
python drone_data_bridge.py --ue-port 8888 --ssh-host 192.168.30.104
```
脚本会自动:
- 打开终端1, 运行 `MicroXRCEAgent serial --dev /dev/ttyTHS1 -b 921600`
- 打开终端2, 运行 `ros2 topic echo /px4_1/fmu/out/vehicle_odometry`
- 主进程从文件读取YAML数据 → 转发UDP到本机8888端口

#### 步骤4: 启动控制桥接 (UE5 → PX4)
```bash
# 在Linux/Jetson终端中运行
cd /path/to/UE5DroneControl
source /opt/ros/humble/setup.bash
source ~/ros2_ws/install/setup.bash  # 假设已编译px4_msgs

python ue_to_px4_bridge.py \
  --drone-id 2 \
  --topic-prefix /px4_1 \
  --udp-port 8889 \
  --map-center-lat 39.90872 \
  --map-center-lon 116.39748
```
**参数说明**:
- `--drone-id`: MAVLink system ID (对应px4_1=2)
- `--topic-prefix`: ROS2话题前缀
- `--map-center-lat/lon`: 地图中心GPS坐标 (用于全局坐标统一)

#### 步骤5: 交互控制
1. UE5中，使用鼠标左键点击地面，控制 `BP_TopDownCharacter` 移动到目标点
2. 控制桥接自动:
   - 检测飞机状态 (上锁 → 发送ARM命令)
   - 进入OFFBOARD模式 (20次空setpoint + VEHICLE_CMD_DO_SET_MODE)
   - 持续发送轨迹设定点 (100Hz)
3. 真实无人机飞向目标位置
4. UE5中 `BP_RealTimeDrone` 实时显示真实无人机位置

**切换视角**:
- `0` 键 → 回到玩家控制视角 (BP_TopDownCharacter)
- `1` 键 → 切换到真实无人机视角 (BP_RealTimeDrone)
- `空格` → 切换相机角度 (斜视/俯视)

---

### 8.2 多机模式完整流程

#### 前置: 配置各无人机
编辑 `multi_drone_config.yaml`，确认各机IP地址正确:
```yaml
drones:
  - name: "UAV1"
    mavlink_system_id: 2
    ssh_host: "192.168.30.104"
  - name: "UAV2"
    mavlink_system_id: 3
    ssh_host: "192.168.30.102"
  - name: "UAV3"
    mavlink_system_id: 4
    ssh_host: "192.168.30.103"
```

#### 步骤1: 启动所有数据桥接 (每个无人机1个)

打开3个终端，分别运行:
```bash
# 终端1: px4_1 数据接收
python drone_data_bridge.py --ue-port 8888 --ssh-host 192.168.30.104

# 终端2: px4_2 数据接收 (修改配置或使用参数)
python drone_data_bridge.py --ue-port 8890 --ssh-host 192.168.30.102 --ros-topic /px4_2/fmu/out/vehicle_odometry

# 终端3: px4_3 数据接收
python drone_data_bridge.py --ue-port 8892 --ssh-host 192.168.30.103 --ros-topic /px4_3/fmu/out/vehicle_odometry
```

#### 步骤2: 启动所有控制桥接 (每个无人机1个)

在3个不同的Linux/Jetson终端中:
```bash
# 终端A: px4_1 控制
python ue_to_px4_bridge.py --drone-id 2 --topic-prefix /px4_1

# 终端B: px4_2 控制
python ue_to_px4_bridge.py --drone-id 3 --topic-prefix /px4_2

# 终端C: px4_3 控制
python ue_to_px4_bridge.py --drone-id 4 --topic-prefix /px4_3
```

#### 步骤3: 启动多机主控制器
```bash
python multi_ue_controller.py --config multi_controller_config.yaml
```
控制器将:
- 监听UDP端口 `8899`
- 接收UE5的多机指令（含选择掩码）
- 分别转发到各单机桥接端口 (8889, 8891, 8893)

#### 步骤4: UE5多机控制逻辑

需要在UE5 Blueprint或C++中实现:
1. 按键1/2/3选择不同的无人机 (设置drone_mask)
2. 鼠标点击发送目标位置
3. 构造32字节数据包，包含 `timestamp, x, y, z, mode, drone_mask, sequence`
4. 发送到 `127.0.0.1:8899`

**示例BluePrint逻辑**:
```
On Key 1 Pressed → SetCurrentMask(0x01)
On Key 2 Pressed → SetCurrentMask(0x02)
On Key 3 Pressed → SetCurrentMask(0x04)
On Mouse Click → SendMultiDroneData(x,y,z, mode=1, mask=CurrentMask)
```

---

## 配置详解

### 9.1 drone_bridge_config.yaml (数据桥接)
```yaml
ue5:
  host: "127.0.0.1"
  port: 8894              # 新增: 用于GPS转发 (可选)
  odometry_port: 8888     # 旧端口保持兼容

drone:
  ssh_host: "192.168.30.104"
  ssh_user: "jetson1"
  ssh_password: "123456"

network:
  ssid: "virtualUAV"
  password: "buaa123456"

ros2:
  odometry_topic: "/px4_1/fmu/out/vehicle_odometry"
  global_position_topic: "/px4_1/fmu/out/vehicle_global_position"

enable_global_position: true
```

### 9.2 ue_to_px4_config.yaml (控制桥接)
```yaml
drone:
  id: 2                        # ⚠️ 必须是2,3,4之一，不是1,2,3
  topic_prefix: "/px4_1"
  system_id: 2                 # 与id一致

control:
  rate_hz: 100
  init_count: 20

safety:
  max_altitude: 50.0
  min_altitude: 1.0
```

### 9.3 multi_controller_config.yaml (多机主控)
```yaml
drones:
  - name: "UAV1"
    drone_id: 1
    mavlink_system_id: 2
    control_port: 8889        # 转发到单机桥接1
  - name: "UAV2"
    drone_id: 2
    mavlink_system_id: 3
    control_port: 8891
  - name: "UAV3"
    drone_id: 3
    mavlink_system_id: 4
    control_port: 8893

controller:
  listen_port: 8899           # UE5统一发送到此端口
  safety_limits:
    max_altitude: 100.0
```

---

## 故障排除

### 常见问题

#### Q1:无人机无法进入OFFBOARD模式
**检查**:
- 是否持续收到 `OffboardControlMode` (100Hz) ?
- `VehicleCommand` 是否正确 (target_system匹配)?
- PX4固件版本是否v1.16+ (需要额外字段)?

**调试**:
```bash
# 查看PX4状态
ros2 topic echo /px4_1/fmu/out/vehicle_status

# 查看Offboard消息流
ros2 topic hz /px4_1/fmu/in/offboard_control_mode
ros2 topic hz /px4_1/fmu/in/trajectory_setpoint
```

#### Q2:坐标偏移过大
**原因**: GPS偏移未正确计算或未应用到odometry。

**检查**:
- `ue_to_px4_bridge.py` 日志中是否输出GPS坐标和偏移量
- 地图中心 `lat0/lon0` 是否输入正确
- `VehicleGlobalPosition` 话题是否有效 (`lat_lon_valid=True`)

#### Q3:多机控制时某些飞机无响应
**检查**:
- 对应飞机的桥接进程是否启动
- `ue_to_px4_bridge.py` 中的 `--drone-id` 是否正确
- 主控制器日志中 `drone_mask` 是否正确解析
- 端到端网络是否可达 (ping各飞机IP)

#### Q4:UE5收不到YAML数据
**检查**:
- `drone_data_bridge.py` 是否正常运行，UDP socket是否绑定
- UE5中 `BP_RealTimeDrone` 的 `Listen Port` 是否正确
- Wireshark抓包验证UDP流

### 日志位置

| 组件 | 日志文件 | 输出位置 |
|------|----------|----------|
| drone_data_bridge.py | `drone_bridge.log` | 文件滚动 (10MB×5) |
| ue_to_px4_bridge.py | ROS2日志 + 控制台 | 配置指定 |
| multi_ue_controller.py | `multi_controller.log` | 文件 |
| UE5 | Output Log | 编辑器面板 |

---

## API参考

### ue_px4_msgs (Python)

#### GPSConverter
```python
from ue_px4_msgs import GPSConverter

e, n, u = GPSConverter.gps_to_enu(lat, lon, alt, lat0, lon0, alt0)
ned_n, ned_e, ned_d = GPSConverter.enu_to_ned(e, n, u)
```

#### UEDataPacket.parse(data: bytes)
解析UE5发送的24字节UDP数据包。

### ROS2消息类型 (Python API)
```python
from px4_msgs.msg import (
    OffboardControlMode,
    TrajectorySetpoint,
    VehicleCommand,
    VehicleOdometry,
    VehicleStatus,
    VehicleGlobalPosition
)
```

**关键字段**:
- `VehicleStatus.arming_state`: 1=DISARMED, 2=ARMED
- `VehicleStatus.nav_state`: 14=OFFBOARD
- `VehicleCommand.VEHICLE_CMD_DO_SET_MODE`: 176
- `VehicleCommand.VEHICLE_CMD_COMPONENT_ARM_DISARM`: 400
- `VehicleCommand.PX4_CUSTOM_MAIN_MODE_OFFBOARD`: 6.0

---

## 开发者指南

### 代码审查要点

1. **坐标系一致性**: NED与UE5坐标轴映射是否正确
2. **控制频率**: offboard消息必须>=100Hz，否则PX4会退出
3. **状态机**: arming → offboard 顺序不能乱
4. **GPS偏移**: `corrected_position = raw + offset`，注意符号
5. **多机选择**: 位掩码bit0=UAV1, bit1=UAV2, bit2=UAV3

### 测试建议

1. 先单机后多机
2. 先用仿真测试坐标转换
3. 地面测试解锁和OFFBOARD状态确认
4. 低高度测试 (1-2米) 确保安全

### 扩展建议

- 添加更严格的安全围栏 (地理围栏)
- 使用 pyproj 库替代简化版GPS转换
- 实现轨迹插值和平滑 (避免setpoint跳变)
- 添加消息序列号和ACK保证可靠性

---

## 许可证

本项目遵循MIT许可证 (或项目原有许可证)。

---

## 参考资源

- [PX4 Offboard控制示例](px4_ros_com/src/examples/offboard_py/offboard_control.py)
- [PX4官方文档](https://docs.px4.io/main/en/flight_modes/offboard.html)
- [ROS2消息定义](px4_msgs/msg/)
- 项目变更记录: [change.md](change.md)

---

**更新**: 2026-03-26

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

---

## 10. 多机控制系统扩展

### 10.1 架构概述

项目已扩展支持多无人机控制，通过扩展端口分配和新增主控制器实现多机管理。

**多机通信架构：**

```
UE5 仿真环境
    ↓ UDP 32字节数据包 (端口8899，包含选择掩码)
多机主控制器 (multi_ue_controller.py)
    ↓ 内部转发到各无人机端口
单机控制桥接 (ue_to_px4_bridge.py)
    ↓ ROS2 话题
PX4 真实无人机 (px4_1, px4_2, px4_3)
```

### 10.2 端口分配方案

| 无人机 | PX4→UE5（接收） | UE5→PX4（发送） | 多机控制端口 |
|--------|----------------|----------------|-------------|
| UAV1   | 8888           | 8889           | 8899（统一） |
| UAV2   | 8890           | 8891           | 8899（统一） |
| UAV3   | 8892           | 8893           | 8899（统一） |
| UAV-N  | 8888 + 2*(N-1) | 8889 + 2*(N-1) | 8899（统一） |

### 10.3 核心文件说明

#### multi_ue_controller.py（多机主控制器）

**作用**：监听UDP端口8899，接收UE5的多机指令，解析选择掩码，转发到各无人机的单机桥接进程。

**核心类**：
- `DroneStatus`：单个无人机状态管理
- `MultiUEController`：多无人机主控制器

**关键功能**：
- 解析32字节多机数据包（包含mode、drone_mask、sequence）
- 根据位掩码选择无人机（位0=px4_1，位1=px4_2，位2=px4_3）
- 坐标转换（UE5厘米→NED米）
- 安全限制检查
- 转发到选中的无人机端口

#### multi_drone_config.yaml（多机配置）

```yaml
drones:
  - name: "UAV1"
    drone_id: 1
    topic_prefix: "/px4_1"
    ue_receive_port: 8888
    control_port: 8889

  - name: "UAV2"
    drone_id: 2
    topic_prefix: "/px4_2"
    ue_receive_port: 8890
    control_port: 8891

  - name: "UAV3"
    drone_id: 3
    topic_prefix: "/px4_3"
    ue_receive_port: 8892
    control_port: 8893

controller:
  listen_port: 8899
  safety_limits:
    max_altitude: 100.0
    max_horizontal_distance: 500.0
```

### 10.4 数据协议

#### 多机数据包格式（32字节）

```
struct MultiDroneData {
    double timestamp;      // 8字节，时间戳
    float x;              // 4字节，UE5 X坐标(厘米)
    float y;              // 4字节，UE5 Y坐标(厘米)
    float z;              // 4字节，UE5 Z坐标(厘米)
    uint32 mode;          // 4字节，控制模式(0=悬停, 1=移动)
    uint32 drone_mask;    // 4字节，无人机选择位掩码
    uint32 sequence;      // 4字节，序列号
};
```

**位掩码编码**：
- 位0 (0x01): 选择无人机ID 1 (px4_1)
- 位1 (0x02): 选择无人机ID 2 (px4_2)
- 位2 (0x04): 选择无人机ID 3 (px4_3)

#### 向后兼容：单机数据包格式（24字节）

```
struct SingleDroneData {
    double timestamp;      // 8字节
    float x;              // 4字节
    float y;              // 4字节
    float z;              // 4字节
    uint32 mode;          // 4字节
};
// 缺省选择所有无人机
```

### 10.5 运行多机系统

#### 步骤1：启动各无人机数据桥接

为每架无人机单独启动数据接收桥接：

```bash
# 终端1：UAV1数据桥接
python drone_data_bridge.py

# 终端2：UAV2数据桥接（需要修改配置或复制脚本）
python drone_data_bridge.py --config multi_drone_config.yaml --drone-index 1

# 终端3：UAV3数据桥接（如需要）
python drone_data_bridge.py --config multi_drone_config.yaml --drone-index 2
```

#### 步骤2：启动多机主控制器

```bash
python multi_ue_controller.py
```

#### 步骤3：启动各无人机控制桥接

在无人机伴飞计算机上运行（或通过SSH）：

```bash
# 终端1：UAV1控制桥接
python ue_to_px4_bridge.py --drone-id 1 --topic-prefix /px4_1 --udp-port 8889

# 终端2：UAV2控制桥接
python ue_to_px4_bridge.py --drone-id 2 --topic-prefix /px4_2 --udp-port 8891

# 终端3：UAV3控制桥接（如需要）
python ue_to_px4_bridge.py --drone-id 3 --topic-prefix /px4_3 --udp-port 8893
```

#### 步骤4：在UE5中配置

在UE5编辑器中：
1. 放置多个 `BP_RealTimeDrone` 实例，分别设置不同的 `ListenPort`（8888, 8890, 8892）
2. 放置多个 `BP_TopDownCharacter` 实例（可选，如使用多角色控制）
3. 配置多机控制逻辑（可通过蓝图或C++实现）
4. 运行游戏，多机主控制器将自动处理指令分发

### 10.6 交互控制

在多机模式下，UE5需要向端口8899发送包含选择掩码的数据包：

**操作示例（UE5蓝图/C++）**：
- 按键1 → 设置drone_mask = 0x01，控制UAV1
- 按键2 → 设置drone_mask = 0x02，控制UAV2
- 按键3 → 设置drone_mask = 0x04，控制UAV3
- 鼠标左键点击 → 发送目标位置，同时应用当前选择的无人机掩码

### 10.7 注意事项

1. **向后兼容**：多机控制器同时支持24字节单机数据包（自动选择所有无人机）
2. **线程安全**：各无人机桥接进程独立运行，互不干扰
3. **安全限制**：多机控制器内置安全限制，防止无人机超出范围
4. **序列号追踪**：可用于调试和丢包检测
5. **单机模式不变**：原有8888/8889端口的单机系统不受影响

---

## 11. 故障排除

### 常见问题

1. **无法连接无人机WiFi**
   - 检查WiFi密码是否正确
   - 确认无人机电源已开启
   - 尝试手动连接SSID "virtualUAV"

2. **UE5收不到数据**
   - 检查`ARealTimeDroneReceiver`的`Listen Port`设置
   - 确认`drone_data_bridge.py`正常运行
   - 使用Wireshark或netstat检查UDP端口

3. **无法进入OFFBOARD模式**
   - PX4 v1.16要求持续接收Offboard消息（至少2Hz）
   - 确认`ue_to_px4_bridge.py`发送频率为100Hz
   - 检查ROS2话题是否正确

4. **坐标偏移**
   - 确认坐标系转换公式
   - 检查`ARealTimeDroneReceiver`的`Scale Factor`
   - 验证`ReferencePosition`计算

### 调试工具

- **UE5 Output Log**：查看Actor日志输出
- **Python日志**：`drone_bridge.log`（滚动，最大10MB×5份）
- **网络抓包**：Wireshark过滤UDP端口
- **ROS2工具**：`ros2 topic list`、`ros2 topic echo`
