@echo off
chcp 65001 >nul

:: --- 路径配置 ---
set CONDA_ROOT=C:\ProgramData\miniforge3
set ENV_ROOT=%CONDA_ROOT%\envs\ros_env
set WORKSPACE=D:\RedAlert

echo ============================================================
echo [1/3] 激活隔离环境...
set PATH=%CONDA_ROOT%;%CONDA_ROOT%\Scripts;%CONDA_ROOT%\condabin;%SystemRoot%\system32;%SystemRoot%
call "%CONDA_ROOT%\condabin\mamba_hook.bat"
call mamba activate "%ENV_ROOT%"

echo [2/3] 注入 ROS2 与自定义消息 DLL 路径...
:: 核心修复：必须把 install 下的 bin 文件夹加入 PATH，Python 才能加载自定义消息的 C 扩展
set PATH=%WORKSPACE%\install\px4_msgs\bin;%ENV_ROOT%\Library\bin;%ENV_ROOT%\Scripts;%PATH%

:: 注入 Python 搜索路径
set PYTHONPATH=%WORKSPACE%\install\px4_msgs\lib\site-packages;%PYTHONPATH%

echo [3/3] 环境锁定完成，启动桥接程序...
echo ============================================================

cd /d "%WORKSPACE%\UE5DroneControl"
:: 使用环境内的 python 运行
"%ENV_ROOT%\python.exe" ue_to_px4_bridge.py

pause