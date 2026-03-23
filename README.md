# UE5DroneControl

基于 Unreal Engine 5 的无人机控制与数据桥接示例项目，包含：
- UE5 端无人机模型与 UDP 通信
- Python 数据桥接脚本（从 ROS2 / PX4 获取里程计并转发到 UE5）
- Python 模拟器（无真实硬件时测试 UE5 通信）

## 环境要求

### UE 版本
- **Unreal Engine 5.7**

### Visual Studio 2022 必需组件
以下组件来自仓库根目录 `.vsconfig`，建议直接用 VS 导入该配置安装：
- 工作负载：
  - `Native Desktop`（本机 C++ 桌面开发）
  - `Native Game`（游戏开发（C++））
  - `Managed Desktop`（托管桌面开发）
- 单独组件：
  - `Unreal Engine Tools / Debugger / IDE`（`Component.Unreal.*`）
  - MSVC 工具集（x86/x64，含 14.38 与 14.44 版本）
  - Windows 11 SDK 22621
  - .NET 4.6.2 Targeting Pack
  - LLVM/Clang（可选但已在配置中）

### Python 环境
- drone_data_bridge.py 依赖：
  - `PyYAML`
  - `rclpy`
  - `nav-msgs`

## 运行流程

### A. UE5 + 真实无人机（ROS2/PX4）链路
1. 安装 UE5 5.7，并在 VS2022 中安装 `.vsconfig` 所列组件。
2. 右键点击 `UE5DroneControl.uproject`，并选择 `Generate Visual Studio project files`。
3. 点击生成的`UE5DroneControl.sln`打开 UE5 项目，并点击“本地Windows调试器”编译并运行 UE5 项目。
4. 在大纲（outline）中确认 `BP_RealTimeDrone` 存在，检查`remote IP`是否为 `192.168.10.1`，`listen port`是否为 `8888`。(这个无人机只接收来自无人机的数据，发送数据的无人机蓝图是Content/TopDown/Blueprints/BP_TopDownCharacter，此无人机将在游戏运行时自动生成)
5. 点击 UE5 的 `Play`，开始运行，摄像机会自动跟随BP_TopDownCharacter无人机。（当前接收端无人机和发送端无人机没有放在场景中的同一个地方）点击1键可以切换视角到接收端无人机，点击0键可切换回来，点击空格键可在45°俯视和纯俯视视角之间切换。
6. 运行桥接脚本（Windows）：
   - 方式 1：双击 `drone_data_bridge.py`
7. 脚本会自动打开两个终端：
   - 终端1：SSH 连接无人机并运行 `MicroXRCEAgent`
   - 终端2：SSH 连接无人机并运行 `ros2 topic echo /px4_1/fmu/out/vehicle_odometry`
   - 先在终端1中输入密码，再在终端2中输入密码，确认连接成功后即可看到里程计数据在终端2中输出。（需要手动刷新电脑网络列表直至发现无人机网络）
8. 回到 UE5 ，即可看到无人机位置更新。



## 关键配置文件
- `drone_bridge_config.yaml`
  - UE5 端口（默认 `8888`）
  - 无人机 SSH 地址/账号（默认 `192.168.30.101 / jetson1 / 123456`）
  - WiFi SSID/密码（默认 `uav1 / 12345678`）
  - ROS2 话题（默认 `/px4_1/fmu/out/vehicle_odometry`）


