@echo off
chcp 65001 >nul
echo ======================================
echo 验证 PX4 话题（需要先启动 MicroXRCE Agent）
echo ======================================
echo.
echo 请确保：
echo   1. 已连接到 UAV1 网络
echo   2. MicroXRCE Agent 正在运行
echo   3. 无人机已通电并连接
echo.
echo 请输入密码: 123456
echo.

echo [检查话题列表]
ssh -o StrictHostKeyChecking=no jetson1@192.168.10.1 "bash -l -c 'source /opt/ros/humble/setup.bash && source ~/ros2_ws/install/setup.bash && ros2 topic list'"
echo.

echo [检查 PX4 相关话题]
ssh -o StrictHostKeyChecking=no jetson1@192.168.10.1 "bash -l -c 'source /opt/ros/humble/setup.bash && source ~/ros2_ws/install/setup.bash && ros2 topic list | grep px4'"
echo.

echo [查看 vehicle_odometry 话题信息]
ssh -o StrictHostKeyChecking=no jetson1@192.168.10.1 "bash -l -c 'source /opt/ros/humble/setup.bash && source ~/ros2_ws/install/setup.bash && ros2 topic info /px4_1/fmu/out/vehicle_odometry'"
echo.

echo [查看话题类型]
ssh -o StrictHostKeyChecking=no jetson1@192.168.10.1 "bash -l -c 'source /opt/ros/humble/setup.bash && source ~/ros2_ws/install/setup.bash && ros2 topic type /px4_1/fmu/out/vehicle_odometry'"
echo.

pause
