# ✈️ UE5 无人机通信接口与 Python 模拟器指南

本文档包含两部分内容：

1. **通信协议规范**：供飞控/硬件端 (PX4, Companion Computer) 集成开发使用。
2. **Python 模拟器使用说明**：提供了一个独立的 Python 脚本 (`main2.py`)，用于在没有真实硬件时验证网络链路。

## 1. 通信协议规范 (Protocol Specification)

为了驱动 UE5 客户端中的无人机模型，硬件端需要通过 **UDP 协议** 发送符合以下定义的二进制数据包。

### A. 网络配置 (Network Config)

- **传输协议**: UDP (User Datagram Protocol)
- **目标 IP**: 运行 UE5 程序的电脑局域网 IP (例如 `192.168.1.X`，如果在 `main2.py` 中已写死为 `10.192.200.83`，请根据实际情况修改)
  - *注意：如果是单机自测，使用 `127.0.0.1`；如果是跨设备，请务必填写 UE5 电脑的真实 IP。*
- **目标端口 (Target Port)**: `9999` (UE5 默认监听端口，请与 UE5 端确认)
- **字节序 (Endianness)**: Little-Endian (小端模式)
  - *注：常见的* x86/x64 和 ARM *架构（树莓派, Jetson）默认均为小端，通常无需转换。*

### B. 数据结构 (Data Structure)

发送端 **必须** 严格遵守以下 Python `struct` 格式定义。

**数据包总大小: 24 字节**

```
# struct 格式字符串: '<dfffi'
# < : 小端模式 (Little-Endian)
# d : double (8 bytes) - 时间戳
# f : float (4 bytes)  - X 坐标
# f : float (4 bytes)  - Y 坐标
# f : float (4 bytes)  - Z 坐标
# i : int (4 bytes)    - 模式位
```

对应的 C/C++ 结构体如下 (需使用 `#pragma pack(1)` 强制 1 字节对齐):

```
#pragma pack(push, 1)
struct FDroneSocketData {
    double Timestamp; // [8 bytes] 发送时间戳 (秒)
    float X;          // [4 bytes] 位置 X (单位: cm)
    float Y;          // [4 bytes] 位置 Y (单位: cm)
    float Z;          // [4 bytes] 位置 Z (高度, 单位: cm)
    int32_t Mode;     // [4 bytes] 模式位 (建议: 1=飞行, 0=待机)
};
#pragma pack(pop)
```

### C. 坐标系转换 (Coordinate System)

UE5 使用 **左手坐标系** (X前, Y右, Z上)。硬件端通常使用 NED 或 ENU 坐标系，需在发送前进行转换。

**假设硬件端使用 ENU (东北天) 且单位为米 (m)：**

| **字段** | **映射关系**              | **说明**                    |
| -------- | ------------------------- | --------------------------- |
| **X**    | `Position.North * 100.0f` | 北 -> 前 (UE X)，单位转厘米 |
| **Y**    | `Position.East * 100.0f`  | 东 -> 右 (UE Y)，单位转厘米 |
| **Z**    | `Position.Up * 100.0f`    | 天 -> 上 (UE Z)，单位转厘米 |

## 2. Python 模拟器使用说明 (Simulator Usage)

为了方便在没有真实飞控的情况下调试网络，我们提供了一个 Python 模拟器 `main2.py`。

### A. 环境准备

- **源代码文件**: `main2.py`
- **Python 版本**: Python 3.6+
- **依赖库**: 仅使用标准库 (`socket`, `struct`, `time`, `math`, `threading`, `sys`, `os`)，无需额外安装 pip 包。

### B. 配置修改

打开 `main2.py`，根据实际环境修改以下配置：

```
# ================= 端口配置 =================
# 发送目标 (UE5 监听端口)
UE5_IP = "10.192.200.83" # <--- 修改为 UE5 电脑的 IP
UE5_PORT = 9999          # <--- 修改为 UE5 监听的端口 (如 8888 或 9999)

# 本机接收端口 (用于接收 UE5 发回的数据)
PYTHON_LISTEN_PORT = 8888
```

### C. 运行与测试

1. **启动 UE5**: 打开 Unreal 项目，点击 **Play**，确保飞机已在场景中且监听端口已开启。

2. 运行模拟器:

   在终端或命令行中运行：

   ```
   python main2.py
   ```

3. 交互指令:

   模拟器启动后，会显示 指令 > 提示符。你可以输入以下指令：

   - `x y z`: 设置目标坐标并模拟飞行 (例如: `500` 0` 200`，飞向 X=500, Y=0, Z=200)。
   - `status`: 查看当前模拟器的状态 (当前位置、目标位置、UE5 反馈)。
   - `logs`: 开启/关闭实时日志刷屏 (开启后会实时打印接收到的 UE5 数据)。
   - `q`: 退出程序。

   **示例**:

   ```
   指令 > 500 0 200
   ✅ 指令已更新: 目标设为 (500.0, 0.0, 200.0)
   ```

## 3. 常见问题排查

- **UE5 没反应 (无法控制)**:
  - 检查 `main2.py` 中的 `UE5_IP` 是否正确填写了 UE5 电脑的局域网 IP。
  - 检查 Windows 防火墙是否允许 UE5 接收 UDP 数据包 (端口 9999/8888)。
  - 确认 UE5 中的接收端 Actor 是否已放入场景并启动监听。
- **端口被占用** (Bind **Error)**:
  - 如果 `main2.py` 报错 `❌` 端口 8888` 被占用！`，请检查是否有其他 Python 脚本或程序占用了该端口。关闭其他程序后重试。
- **没有 UE5 反馈**:
  - 这可能是单向通信通了 (Python -> UE5)，但反向不通 (UE5 -> Python)。
  - 检查 UE5 发送逻辑中的目标 IP 和端口是否指向