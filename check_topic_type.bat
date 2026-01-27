@echo off
echo 正在检查远程话题的消息类型...
echo.
echo 请输入密码: 123456
echo.
ssh jetson1@192.168.10.1 "source /opt/ros/humble/setup.bash && ros2 topic list && echo. && ros2 topic info /px4_1/fmu/out/vehicle_odometry && echo. && ros2 topic type /px4_1/fmu/out/vehicle_odometry"
pause
