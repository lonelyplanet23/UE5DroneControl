#!/bin/bash
# 无人机数据接收与转发脚本 - Linux/macOS 快速启动脚本

# 设置颜色输出
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}======================================== ${NC}"
echo -e "${BLUE}无人机数据接收与转发脚本 ${NC}"
echo -e "${BLUE}======================================== ${NC}"
echo ""

# 检查虚拟环境
if [ ! -d "drone_env" ]; then
    echo "创建虚拟环境..."
    python3 -m venv drone_env
fi

# 激活虚拟环境
source drone_env/bin/activate

# 安装依赖
echo "安装依赖库..."
pip install -q -r requirements.txt

# 添加 ROS 2 环境变量
if [ -f "/opt/ros/humble/setup.bash" ]; then
    source /opt/ros/humble/setup.bash
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}启动无人机数据接收与转发脚本${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""

# 启动脚本
python drone_data_bridge.py "$@"
