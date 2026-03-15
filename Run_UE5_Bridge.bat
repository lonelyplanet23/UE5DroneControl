@echo off
:: 1. 设置终端编码为 UTF-8，防止中文乱码
chcp 65001 >nul

echo ============================================================
echo [1/4] 正在初始化 Miniforge 环境...
:: 这里指向你电脑上实际的 mamba_hook 路径
call C:\ProgramData\miniforge3\condabin\mamba_hook.bat

echo [2/4] 正在激活 ros_env 虚拟环境...
call mamba activate C:\ProgramData\miniforge3\envs\ros_env

echo [3/4] 正在加载 D:\RedAlert 工作空间变量...
cd /d D:\RedAlert
:: 激活你辛苦编译好的 px4_msgs
call install\setup.bat

echo [4/4] 正在启动 UE5 到 PX4 的桥接程序...
echo ============================================================
cd UE5DroneControl
python ue_to_px4_bridge.py

:: 如果程序崩溃，保留窗口查看报错信息
pause