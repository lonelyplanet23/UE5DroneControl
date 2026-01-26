@echo off
REM 无人机数据接收与转发脚本 - Windows 快速启动脚本

REM 检查虚拟环境
if not exist "drone_env" (
    echo 创建虚拟环境...
    python -m venv drone_env
)

REM 激活虚拟环境
call drone_env\Scripts\activate.bat

REM 安装依赖
echo 安装依赖库...
pip install -q -r requirements.txt

REM 启动脚本
echo.
echo ========================================
echo 启动无人机数据接收与转发脚本
echo ========================================
echo.
python drone_data_bridge.py %*

REM 保持窗口打开
pause
