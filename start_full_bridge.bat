@echo off
chcp 65001 >nul
setlocal

:: ============================================================
:: 双端通讯一键启动脚本
:: 分工：bat 负责 WiFi + SSH 终端，Python 只做数据转发
:: 阶段1: PX4 -> UE5  (drone_data_bridge.py --skip-ssh --skip-network)
:: 阶段2: UE5 -> PX4  (ue_to_px4_bridge.py)
:: 配置来源: drone_bridge_config.yaml
:: ============================================================

:: -------- 配置（drone_bridge_config.yaml）--------
set SSH_HOST=192.168.30.101
set SSH_USER=jetson1
set WIFI_SSID=virtualUAV
set WIFI_PASS=buaa12345678
set UE5_HOST=127.0.0.1
set UE5_PORT=8888
set ROS_TOPIC=/px4_1/fmu/out/vehicle_odometry

set SCRIPT_DIR=%~dp0

echo ============================================================
echo  双端通讯一键启动脚本
echo  [阶段1] PX4 -^> UE5  (drone_data_bridge.py)
echo  [阶段2] UE5 -^> PX4  (ue_to_px4_bridge.py)
echo ============================================================
echo.

:: ============================================================
:: 步骤1：连接 WiFi
:: ============================================================
echo.
echo [步骤1/4] 连接 WiFi: %WIFI_SSID%
netsh wlan connect name="%WIFI_SSID%" >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] WiFi 连接命令失败，无法继续
    pause
    exit /b 1
)
echo [OK] WiFi 连接命令已发送，等待3秒...
timeout /t 3 /nobreak >nul

:: ============================================================
:: 步骤2：测试连通性
:: ============================================================
echo.
echo [步骤2/4] 测试 SSH 主机连通性: %SSH_HOST%
ping -n 1 %SSH_HOST% >nul 2>&1
if %errorlevel% neq 0 (
    echo [ERROR] 无法 ping 到 %SSH_HOST%，请检查:
    echo   1. 是否已连接到 %WIFI_SSID% 网络
    echo   2. 无人机是否已开机
    pause
    exit /b 1
)
echo [OK] %SSH_HOST% 可达

:: ============================================================
:: 步骤3：终端1 - MicroXRCE Agent（手动输入）
:: ============================================================
echo.
echo [步骤3/4] 打开终端1 - MicroXRCE Agent

set TERM1=%TEMP%\drone_term1.bat
(
    echo @echo off
    echo chcp 65001 ^>nul
    echo echo [终端1] 请手动执行以下命令：
    echo echo 1^) ssh %SSH_USER%@%SSH_HOST%
    echo echo 2^) MicroXRCEAgent serial --dev /dev/ttyTHS1 -b 921600
    echo echo.
    echo echo 提示：首次连接可能需要确认指纹，并输入密码
) > "%TERM1%"

start "终端1 - MicroXRCE Agent" cmd /k "%TERM1%"
echo [OK] 终端1 已打开，等待3秒...
timeout /t 3 /nobreak >nul
choice /C YN /M "已在终端1成功启动 MicroXRCE Agent?"
if errorlevel 2 (
    echo [ERROR] 终端1 未成功启动，停止后续步骤
    pause
    exit /b 1
)

:: ============================================================
:: 步骤4：终端2 - ROS2 topic echo（手动输入）
:: ============================================================
echo.
echo [步骤4/4] 打开终端2 - ROS2 topic echo

set TERM2=%TEMP%\drone_term2.bat
(
    echo @echo off
    echo chcp 65001 ^>nul
    echo echo [终端2] 请手动执行以下命令：
    echo echo 1^) ssh %SSH_USER%@%SSH_HOST%
    echo echo 2^) source /opt/ros/humble/setup.bash
    echo echo 3^) source ^~/ros2_ws/install/setup.bash
    echo echo 4^) ros2 topic echo %ROS_TOPIC%
    echo echo.
    echo echo 提示：保持该窗口运行，便于观察数据流
) > "%TERM2%"

start "终端2 - ROS2 Topic Echo" cmd /k "%TERM2%"
echo [OK] 终端2 已打开，等待2秒...
timeout /t 2 /nobreak >nul
choice /C YN /M "已在终端2成功开始 ros2 topic echo?"
if errorlevel 2 (
    echo [ERROR] 终端2 未成功启动，停止后续步骤
    pause
    exit /b 1
)

:: ============================================================
:: 阶段1：启动 PX4->UE5 主进程
:: --skip-ssh --skip-network：SSH 终端已由 bat 启动，Python 只做数据转发
:: ============================================================
echo.
echo [阶段1] 启动 PX4-^>UE5 主进程 (drone_data_bridge.py)

set BRIDGE1=%TEMP%\drone_bridge1.bat
(
    echo @echo off
    echo chcp 65001 ^>nul
    echo echo [PX4-^>UE5] 激活虚拟环境...
    echo call "%SCRIPT_DIR%drone_env\Scripts\activate.bat"
    echo echo [PX4-^>UE5] 启动 drone_data_bridge.py...
    echo python "%SCRIPT_DIR%drone_data_bridge.py" --ue-host %UE5_HOST% --ue-port %UE5_PORT% --ssh-host %SSH_HOST% --ssh-user %SSH_USER% --ros-topic %ROS_TOPIC% --skip-ssh --skip-network
    echo echo [PX4-^>UE5] 进程已退出
    echo pause
) > "%BRIDGE1%"

start "PX4->UE5 Bridge" cmd /k "%BRIDGE1%"
echo [OK] PX4-^>UE5 桥接已启动
choice /C YN /M "已确认 PX4->UE5 桥接窗口正常运行?"
if errorlevel 2 (
    echo [ERROR] 阶段1未成功启动，停止后续步骤
    pause
    exit /b 1
)
choice /C YN /M "已确认 ROS2 数据流正常?"
if errorlevel 2 (
    echo [ERROR] 数据流异常，停止后续步骤
    pause
    exit /b 1
)

:: ============================================================
:: 阶段2：UE5->PX4 (ue_to_px4_bridge.py)
:: ============================================================
:STAGE2
echo.
echo ============================================================
echo  [阶段2] 启动 UE5 -^> PX4 桥接 (ue_to_px4_bridge.py)
echo ============================================================

if not exist "%SCRIPT_DIR%Run_UE5_Bridge.bat" (
    echo [ERROR] 未找到 Run_UE5_Bridge.bat: %SCRIPT_DIR%Run_UE5_Bridge.bat
    pause
    exit /b 1
)

start "UE5->PX4 Bridge" cmd /k "%SCRIPT_DIR%Run_UE5_Bridge.bat"
echo [OK] UE5-^>PX4 桥接已启动（Run_UE5_Bridge.bat）

echo.
echo ============================================================
echo  全部启动完成
echo  终端1  : MicroXRCE Agent  (SSH ^> Jetson)
echo  终端2  : ROS2 topic echo  (SSH ^> Jetson)
echo  窗口3  : PX4 -^> UE5      (drone_data_bridge.py)
echo  窗口4  : UE5 -^> PX4      (ue_to_px4_bridge.py)
echo  关闭对应窗口可停止各进程
echo ============================================================
echo.
pause
