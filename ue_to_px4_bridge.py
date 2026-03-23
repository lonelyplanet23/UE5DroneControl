#!/usr/bin/env python3
"""
UE5到PX4无人机控制桥接节点
功能：接收UE5仿真无人机的控制指令，转发到PX4真实无人机
作者：基于u1.hpp示例的控制逻辑
"""

import os
import sys
import time
import socket
import struct
import threading
import yaml
import argparse
import logging
from datetime import datetime
from typing import Optional, Dict, Any, Tuple, List

# ROS2相关导入
import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, QoSHistoryPolicy, QoSReliabilityPolicy, QoSDurabilityPolicy

# PX4消息导入 - 假设px4_msgs已完整安装
try:
    from px4_msgs.msg import OffboardControlMode, TrajectorySetpoint, VehicleCommand
    from px4_msgs.msg import VehicleOdometry, VehicleStatus
except ImportError as e:
    print(f"错误: 无法导入PX4消息: {e}")
    print("请确保px4_msgs包已正确安装")
    print("安装方法: cd px4_msgs && pip install -e .")
    sys.exit(1)

# ==================== 配置常量 ====================

# UDP配置
DEFAULT_UDP_PORT = 8889  # 接收UE5数据的端口
DEFAULT_UDP_HOST = "0.0.0.0"  # 监听所有接口

# PX4话题配置
DEFAULT_PX4_TOPIC_PREFIX = "/px4_1"  # 话题前缀，对应无人机ID
DEFAULT_DRONE_ID = 2  # 无人机ID，用于VehicleCommand.target_system

# 控制参数
INIT_COUNT_THRESHOLD = 20  # 进入OFFBOARD前需要发送的空消息次数
CONTROL_RATE_HZ = 100  # 控制频率 (Hz)
OFFBOARD_TIMEOUT_SEC = 0.5  # OFFBOARD模式超时时间

# 坐标转换参数
UE5_TO_NED_SCALE = 0.01  # UE5厘米 → NED米: 除以100
NED_TO_UE5_SCALE = 100.0  # NED米 → UE5厘米: 乘以100

# 安全限制
MAX_VELOCITY = 5.0  # 最大速度 (米/秒)
MAX_ACCELERATION = 2.0  # 最大加速度 (米/秒²)
MIN_ALTITUDE = 1.0  # 最小高度 (米，离地面)
MAX_ALTITUDE = 10.0  # 最大高度 (米)

# ==================== UDP数据解析 ====================

class UEDataParser:
    """UE5 UDP数据解析器"""

    # UE5发送的数据结构 (24字节)
    # struct FDroneSocketData {
    #     double Timestamp;  // 8 bytes
    #     float X;          // 4 bytes (厘米)
    #     float Y;          // 4 bytes (厘米)
    #     float Z;          // 4 bytes (厘米)
    #     int32 Mode;       // 4 bytes (0=悬停, 1=移动)
    # };

    STRUCT_FORMAT = "dfffI"  # double, float, float, float, unsigned int
    DATA_SIZE = struct.calcsize(STRUCT_FORMAT)  # 24字节

    @staticmethod
    def parse_udp_data(data: bytes) -> Optional[Dict[str, Any]]:
        """解析UE5发送的UDP二进制数据

        Args:
            data: UDP数据包 (24字节)

        Returns:
            解析后的字典，包含timestamp, x, y, z, mode
            如果解析失败返回None
        """
        if len(data) != UEDataParser.DATA_SIZE:
            # logging.warning(f"数据包大小错误: 期望{UEDataParser.DATA_SIZE}字节, 实际{len(data)}字节")
            return None

        try:
            # 解析二进制数据
            timestamp, x, y, z, mode = struct.unpack(UEDataParser.STRUCT_FORMAT, data)

            return {
                'timestamp': timestamp,  # UE5时间戳 (秒)
                'x': x,  # X坐标 (厘米)
                'y': y,  # Y坐标 (厘米)
                'z': z,  # Z坐标 (厘米)
                'mode': mode,  # 控制模式 (0=悬停, 1=移动)
                'received_time': time.time()  # 接收时间
            }
        except struct.error as e:
            logging.error(f"数据解析失败: {e}")
            return None

# ==================== 坐标转换器 ====================

class CoordinateConverter:
    """UE5与NED坐标系转换器"""

    @staticmethod
    def ue5_to_ned(ue_x: float, ue_y: float, ue_z: float) -> Tuple[float, float, float]:
        """UE5坐标系 (厘米) → NED坐标系 (米)

        UE5坐标系:
          X: 前 (Forward)
          Y: 右 (Right)
          Z: 上 (Up)

        NED坐标系:
          X: 北 (North)
          Y: 东 (East)
          Z: 下 (Down)

        转换公式:
          NED_X = UE5_X * 0.01  (假设UE5 X轴指向北)
          NED_Y = UE5_Y * 0.01  (假设UE5 Y轴指向东)
          NED_Z = -UE5_Z * 0.01 (UE5 Z向上，NED Z向下)

        Args:
            ue_x: UE5 X坐标 (厘米)
            ue_y: UE5 Y坐标 (厘米)
            ue_z: UE5 Z坐标 (厘米)

        Returns:
            (ned_x, ned_y, ned_z) NED坐标 (米)
        """
        ned_x = ue_x * UE5_TO_NED_SCALE  # 前 → 北
        ned_y = ue_y * UE5_TO_NED_SCALE  # 右 → 东
        ned_z = -ue_z * UE5_TO_NED_SCALE  # 上 → 下 (取负)

        return ned_x, ned_y, ned_z

    @staticmethod
    def ned_to_ue5(ned_x: float, ned_y: float, ned_z: float) -> Tuple[float, float, float]:
        """NED坐标系 (米) → UE5坐标系 (厘米)

        Args:
            ned_x: NED X坐标 (米, 北)
            ned_y: NED Y坐标 (米, 东)
            ned_z: NED Z坐标 (米, 下)

        Returns:
            (ue_x, ue_y, ue_z) UE5坐标 (厘米)
        """
        ue_x = ned_x * NED_TO_UE5_SCALE  # 北 → 前
        ue_y = ned_y * NED_TO_UE5_SCALE  # 东 → 右
        ue_z = -ned_z * NED_TO_UE5_SCALE  # 下 → 上 (取负)

        return ue_x, ue_y, ue_z

    @staticmethod
    def apply_safety_limits(ned_x: float, ned_y: float, ned_z: float) -> Tuple[float, float, float]:
        """应用安全限制到NED坐标

        Args:
            ned_x: NED X坐标 (米)
            ned_y: NED Y坐标 (米)
            ned_z: NED Z坐标 (米)

        Returns:
            应用限制后的坐标
        """
        # 限制高度
        if ned_z > -MIN_ALTITUDE:  # NED Z向下为负，所以高度是 -ned_z
            ned_z = -MIN_ALTITUDE
        elif ned_z < -MAX_ALTITUDE:
            ned_z = -MAX_ALTITUDE

        return ned_x, ned_y, ned_z

# ==================== 主桥接节点 ====================

class UEToPX4Bridge(Node):
    """UE5到PX4无人机控制桥接节点"""

    def __init__(self,
                 drone_id: int = DEFAULT_DRONE_ID,
                 topic_prefix: str = DEFAULT_PX4_TOPIC_PREFIX,
                 udp_port: int = DEFAULT_UDP_PORT):
        """
        初始化桥接节点

        Args:
            drone_id: 无人机ID (1,2,3...)
            topic_prefix: ROS2话题前缀 (如"/px4_1")
            udp_port: 接收UE5数据的UDP端口
        """
        super().__init__('ue_to_px4_bridge')

        self.drone_id = drone_id
        self.topic_prefix = topic_prefix
        self.udp_port = udp_port

        # 状态变量
        self.is_initialized = False
        self.is_offboard = False
        self.is_armed = False  # 解锁状态
        self.init_counter = 0
        self.last_ue_data = None
        self.last_ue_time = 0
        self.current_target = None  # 当前目标位置 (NED坐标系)，鼠标点击后持续生效直到下一次点击
        self.offboard_command_sent = False  # OFFBOARD命令是否已发送
        self.arm_command_sent = False  # 解锁命令是否已发送
        # UDP接收日志节流
        self._last_udp_log_time = 0.0
        self._udp_log_interval = 1.0  # 秒

        # 设置QoS配置 (与u1.hpp保持一致)
        qos_profile = QoSProfile(
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=10,
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL
        )

        # ============ ROS2发布者初始化 ============
        # 1. OffboardControlMode发布者
        self.offboard_pub = self.create_publisher(
            OffboardControlMode,
            f"{self.topic_prefix}/fmu/in/offboard_control_mode",
            qos_profile
        )

        # 2. TrajectorySetpoint发布者
        self.trajectory_pub = self.create_publisher(
            TrajectorySetpoint,
            f"{self.topic_prefix}/fmu/in/trajectory_setpoint",
            qos_profile
        )

        # 3. VehicleCommand发布者
        self.vehicle_cmd_pub = self.create_publisher(
            VehicleCommand,
            f"{self.topic_prefix}/fmu/in/vehicle_command",
            qos_profile
        )
    
        # ============ ROS2订阅者初始化 ============
        # 订阅无人机状态 (用于获取当前位置)
        self.odometry_sub = self.create_subscription(
            VehicleOdometry,
            f"{self.topic_prefix}/fmu/out/vehicle_odometry",
            self.odometry_callback,
            qos_profile
        )

        # 订阅无人机状态 (用于检测解锁和OFFBOARD状态)
        self.status_sub = self.create_subscription(
            VehicleStatus,
            f"{self.topic_prefix}/fmu/out/vehicle_status",
            self.status_callback,
            qos_profile
        )

        # 状态变量
        self.current_position = [0.0, 0.0, 0.0]  # 当前位置 (NED)
        self.current_velocity = [0.0, 0.0, 0.0]  # 当前速度 (NED)
        self.position_valid = False

        # 心跳监测变量
        self.last_odometry_time = 0.0  # 上次收到odometry数据的时间
        self.odometry_count = 0  # odometry消息计数
        self.odometry_frequency = 0.0  # 计算出的频率 (Hz)
        self.last_heartbeat_print = 0.0  # 上次打印心跳的时间
        self.control_loop_count = 0  # 控制循环计数
        self.last_control_loop_time = time.time()  # 上次控制循环时间

        # ============ UDP套接字初始化 ============
        self.udp_socket = None
        self.setup_udp_socket()

        # ============ 定时器初始化 ============
        # 控制循环定时器 (100Hz)
        self.control_timer = self.create_timer(1.0 / CONTROL_RATE_HZ, self.control_loop)

        # 状态打印定时器 (1Hz)
        self.status_timer = self.create_timer(1.0, self.print_status)

        # UDP接收线程
        self.udp_thread = threading.Thread(target=self.udp_receiver_thread, daemon=True)
        self.udp_running = True
        self.udp_thread.start()

        self.get_logger().info(f"UE5到PX4桥接节点初始化完成")
        self.get_logger().info(f"无人机ID: {self.drone_id}")
        self.get_logger().info(f"话题前缀: {self.topic_prefix}")
        self.get_logger().info(f"UDP监听端口: {self.udp_port}")
        self.get_logger().info(f"控制频率: {CONTROL_RATE_HZ}Hz")

    def setup_udp_socket(self) -> bool:
        """设置UDP接收套接字"""
        try:
            self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.udp_socket.bind((DEFAULT_UDP_HOST, self.udp_port))
            self.udp_socket.settimeout(0.01)  # 10ms超时，避免阻塞
            self.get_logger().info(f"UDP套接字绑定成功: {DEFAULT_UDP_HOST}:{self.udp_port}")
            return True
        except Exception as e:
            self.get_logger().error(f"UDP套接字设置失败: {e}")
            return False

    def udp_receiver_thread(self):
        """UDP数据接收线程"""
        self.get_logger().info("UDP接收线程启动")

        while self.udp_running and rclpy.ok():
            try:
                # 接收UDP数据
                data, addr = self.udp_socket.recvfrom(1024)  # 缓冲区1024字节
                now = time.time()
                if now - self._last_udp_log_time >= self._udp_log_interval:
                    self._last_udp_log_time = now
                    self.get_logger().info(f"收到UDP包: {len(data)} bytes from {addr[0]}:{addr[1]}")

                # 解析数据
                parsed_data = UEDataParser.parse_udp_data(data)
                if parsed_data:
                    self.last_ue_data = parsed_data
                    self.last_ue_time = time.time()

                    # 坐标转换: UE5厘米 → NED米
                    ned_x, ned_y, ned_z = CoordinateConverter.ue5_to_ned(
                        parsed_data['x'], parsed_data['y'], parsed_data['z']
                    )

                    # 应用安全限制
                    ned_x, ned_y, ned_z = CoordinateConverter.apply_safety_limits(
                        ned_x, ned_y, ned_z
                    )

                    self.current_target = {
                        'x': ned_x,
                        'y': ned_y,
                        'z': ned_z,
                        'mode': parsed_data['mode']
                    }

                    # 调试日志 (降低频率)
                    if hasattr(self, '_udp_counter'):
                        self._udp_counter += 1
                    else:
                        self._udp_counter = 0

                    if self._udp_counter % 50 == 0:  # 每50个包打印一次
                        self.get_logger().info(
                            f"收到UE5数据: 模式={parsed_data['mode']}, "
                            f"UE5=({parsed_data['x']:.1f},{parsed_data['y']:.1f},{parsed_data['z']:.1f})cm, "
                            f"NED=({ned_x:.2f},{ned_y:.2f},{ned_z:.2f})m"
                        )

            except socket.timeout:
                # 超时是正常的，继续循环
                continue
            except Exception as e:
                self.get_logger().error(f"UDP接收错误: {e}")
                time.sleep(0.1)

        self.get_logger().info("UDP接收线程结束")

    def odometry_callback(self, msg: VehicleOdometry):
        """无人机状态回调函数"""
        current_time = time.time()

        # 保存当前位置和速度
        self.current_position = [float(x) for x in msg.position[:3]]
        self.current_velocity = [float(x) for x in msg.velocity[:3]]
        self.position_valid = True

        # 心跳监测: 更新odometry接收统计
        self.last_odometry_time = current_time
        self.odometry_count += 1

        # 每秒计算一次频率
        if not hasattr(self, '_last_freq_calc_time'):
            self._last_freq_calc_time = current_time
            self._odom_count_since_last = 0

        self._odom_count_since_last = getattr(self, '_odom_count_since_last', 0) + 1

        if current_time - self._last_freq_calc_time >= 1.0:
            self.odometry_frequency = self._odom_count_since_last / (current_time - self._last_freq_calc_time)
            self._last_freq_calc_time = current_time
            self._odom_count_since_last = 0

        # 调试日志 (降低频率)
        if hasattr(self, '_odom_counter'):
            self._odom_counter += 1
        else:
            self._odom_counter = 0

        if self._odom_counter % 100 == 0:  # 每100个消息打印一次
            self.get_logger().debug(
                f"无人机位置: [{self.current_position[0]:.2f}, "
                f"{self.current_position[1]:.2f}, {self.current_position[2]:.2f}]m, "
                f"频率: {self.odometry_frequency:.1f}Hz"
            )

    def status_callback(self, msg: VehicleStatus):
        """无人机状态回调 - 检测解锁和OFFBOARD状态"""
        was_armed = self.is_armed
        self.is_armed = (msg.arming_state == 2)  # 2=ARMED
        in_offboard = (msg.nav_state == 14)  # 14=OFFBOARD

        # 飞控确认OFFBOARD模式
        if in_offboard and not self.is_offboard:
            self.is_offboard = True
            self.get_logger().info("飞控已确认OFFBOARD模式")

        # 飞控退出OFFBOARD，重置以便重新进入
        if not in_offboard and self.is_offboard:
            self.is_offboard = False
            self.offboard_command_sent = False
            self.init_counter = 0
            self.get_logger().warn("飞控已退出OFFBOARD，准备重新进入")

        # 解锁状态变化
        if self.is_armed and not was_armed:
            self.get_logger().info("无人机已解锁")
        elif not self.is_armed and was_armed:
            self.get_logger().warn("无人机已上锁")

    def publish_vehicle_command(self, command: int, param1: float = 0.0, param2: float = 0.0):
        """发布车辆命令 (仿照u1.hpp)

        Args:
            command: 命令类型
            param1: 参数1
            param2: 参数2
        """
        msg = VehicleCommand()
        msg.timestamp = int(self.get_clock().now().nanoseconds / 1000)  # 微秒
        msg.command = command
        msg.param1 = param1
        msg.param2 = param2
        msg.target_system = self.drone_id
        msg.target_component = 1
        msg.source_system = 1
        msg.source_component = 1
        msg.confirmation = 0
        msg.from_external = True

        self.vehicle_cmd_pub.publish(msg)

        # 记录日志
        cmd_names = {
            176: "VEHICLE_CMD_DO_SET_MODE",
            400: "VEHICLE_CMD_COMPONENT_ARM_DISARM"
        }
        cmd_name = cmd_names.get(command, f"CMD_{command}")
        self.get_logger().info(f"发送车辆命令: {cmd_name}, param1={param1}, param2={param2}")

    def enter_offboard_mode(self):
        """进入OFFBOARD模式 (仿照u1.hpp流程)
        
        流程：
        1. 先发送20次空的TrajectorySetpoint（预设点）
        2. 发送OFFBOARD模式切换命令（无论是否解锁都发）
        3. 飞控通过status_callback确认进入OFFBOARD
        
        即使失败也会持续尝试，不会停止发送。
        """
        # 步骤1: 先发送足够的空setpoint
        if self.init_counter < INIT_COUNT_THRESHOLD:
            self.publish_empty_setpoint()
            self.init_counter += 1

            if self.init_counter >= INIT_COUNT_THRESHOLD:
                # 步骤2: 发送OFFBOARD模式命令
                if not self.offboard_command_sent:
                    self.publish_vehicle_command(176, 1.0, 6.0)
                    self.offboard_command_sent = True
                    self.get_logger().info("已发送OFFBOARD切换命令，等待飞控确认...")

            return False

        # init_counter已满但还没确认OFFBOARD，继续发命令
        if not self.is_offboard:
            # 每秒重试一次OFFBOARD命令
            if not hasattr(self, '_last_offboard_retry'):
                self._last_offboard_retry = 0
            if time.time() - self._last_offboard_retry > 1.0:
                self.publish_vehicle_command(176, 1.0, 6.0)
                self._last_offboard_retry = time.time()
                self.get_logger().info("重试OFFBOARD切换命令...")
            return False

        return True

    def publish_empty_setpoint(self):
        """发布空的轨迹设定点 (用于初始化OFFBOARD模式)"""
        msg = TrajectorySetpoint()
        msg.timestamp = int(self.get_clock().now().nanoseconds / 1000)

        # 设置NaN值，表示不使用该字段
        nan = float('nan')
        msg.position = [nan, nan, nan]
        msg.velocity = [0.0, 0.0, 0.0]
        msg.acceleration = [nan, nan, nan]
        msg.yaw = nan
        msg.yawspeed = nan

        self.trajectory_pub.publish(msg)

    def publish_offboard_control_mode(self, position: bool = True, velocity: bool = False):
        """发布OFFBOARD控制模式消息"""
        msg = OffboardControlMode()
        msg.timestamp = int(self.get_clock().now().nanoseconds / 1000)
        msg.position = position
        msg.velocity = velocity
        msg.acceleration = False
        msg.attitude = False
        msg.body_rate = False

        self.offboard_pub.publish(msg)

    def publish_position_setpoint(self, x: float, y: float, z: float, yaw: float = 0.0):
        """发布位置设定点

        Args:
            x: NED X坐标 (米, 北)
            y: NED Y坐标 (米, 东)
            z: NED Z坐标 (米, 下)
            yaw: 偏航角 (弧度, 0=北)
        """
        msg = TrajectorySetpoint()
        msg.timestamp = int(self.get_clock().now().nanoseconds / 1000)
        msg.position = [float(x), float(y), float(z)]

        # 其他字段设为NaN
        nan = float('nan')
        msg.velocity = [nan, nan, nan]
        msg.acceleration = [nan, nan, nan]
        msg.yaw = float(yaw)
        msg.yawspeed = nan

        self.trajectory_pub.publish(msg)

        # 调试日志 (降低频率)
        if hasattr(self, '_pos_counter'):
            self._pos_counter += 1
        else:
            self._pos_counter = 0

        if self._pos_counter % 50 == 0:  # 每50个包打印一次
            self.get_logger().debug(
                f"发布位置设定点: [{x:.2f}, {y:.2f}, {z:.2f}]m, yaw={yaw:.2f}rad"
            )

    def control_loop(self):
        """主控制循环 (100Hz) - 持续发送OFFBOARD控制消息防止断开"""
        current_time = time.time()

        # 更新控制循环统计 (用于心跳监测)
        self.control_loop_count += 1
        control_interval = current_time - self.last_control_loop_time
        self.last_control_loop_time = current_time

        # 检查UDP数据是否超时
        if self.last_ue_time > 0 and (current_time - self.last_ue_time) > OFFBOARD_TIMEOUT_SEC:
            # UDP数据超时，进入悬停模式
            if self.is_offboard:
                # self.get_logger().warn(f"UDP数据超时({current_time - self.last_ue_time:.1f}s)，切换到悬停模式")
                pass
                self.current_target = None

        # 检查odometry数据是否超时 (心跳监测)
        odometry_age = current_time - self.last_odometry_time if self.last_odometry_time > 0 else float('inf')
        if odometry_age > 1.0:  # 1秒没有收到odometry数据
            # self.get_logger().warn(f"Odometry数据超时: {odometry_age:.1f}s未收到")
            pass
            # 这里可以添加更复杂的处理，比如尝试重新初始化

        # ============ OFFBOARD模式维持 ============
        # 关键: 必须持续发送OffboardControlMode，否则PX4会自动退出OFFBOARD
        # 即使未解锁也要持续发送，保持连接

        if self.is_offboard:
            # 检查解锁状态，未解锁则尝试解锁（每2秒一次）
            if not self.is_armed:
                if not hasattr(self, '_last_arm_retry'):
                    self._last_arm_retry = 0
                if time.time() - self._last_arm_retry > 2.0:
                    self.publish_vehicle_command(400, 1.0, 0.0)  # ARM
                    self._last_arm_retry = time.time()
                    self.get_logger().info("尝试解锁...")
            # 无论是否有UDP指令，都必须发布OffboardControlMode
            self.publish_offboard_control_mode(position=True)

            # 检查是否有有效的目标位置
            if self.current_target is None or self.current_target.get('mode', 1) == 0:
                # 悬停模式: 保持当前位置或安全位置
                if self.position_valid:
                    # 使用当前位置悬停
                    self.publish_position_setpoint(
                        self.current_position[0],
                        self.current_position[1],
                        self.current_position[2],
                        0.0
                    )
                else:
                    # 没有有效位置，使用安全悬停位置 (例如: 0,0,高度-2米)
                    safe_altitude = -2.0  # NED坐标系，-2米高度
                    self.get_logger().debug(f"使用安全悬停位置: [0, 0, {safe_altitude}]m")
                    self.publish_position_setpoint(0.0, 0.0, safe_altitude, 0.0)
            else:
                # 移动模式: 飞向目标位置
                target = self.current_target
                self.publish_position_setpoint(
                    target['x'],
                    target['y'],
                    target['z'],
                    0.0  # 保持当前偏航角
                )

            # 每500次控制循环打印一次心跳 (约5秒一次)
            if self.control_loop_count % 500 == 0:
                self.get_logger().info(
                    f"[心跳] 控制循环: {self.control_loop_count}, "
                    f"频率: {1.0/control_interval if control_interval > 0 else 0:.1f}Hz, "
                    f"Odometry: {self.odometry_frequency:.1f}Hz, "
                    f"UDP超时: {current_time - self.last_ue_time if self.last_ue_time > 0 else -1:.1f}s"
                )

        # ============ 初始化阶段 ============
        elif not self.is_initialized:
            # 初始化阶段: 进入OFFBOARD模式
            if self.enter_offboard_mode():
                self.is_initialized = True

        # ============ 不在OFFBOARD模式但已初始化 ============
        else:
            # 持续尝试进入OFFBOARD模式（即使解锁失败也继续发）
            self.enter_offboard_mode()

    def print_status(self):
        """打印状态信息 (1Hz) - 包含心跳监测"""
        current_time = time.time()
        status_msgs = []

        # ============ 心跳监测信息 ============
        # 控制循环统计
        status_msgs.append(f"控制循环: {self.control_loop_count}")

        # Odometry心跳
        odometry_age = current_time - self.last_odometry_time if self.last_odometry_time > 0 else float('inf')
        if odometry_age < 2.0:  # 2秒内有数据
            status_msgs.append(f"Odometry: {self.odometry_frequency:.1f}Hz")
        else:
            status_msgs.append(f"Odometry: 超时({odometry_age:.1f}s)")

        # UDP心跳
        if self.last_ue_time > 0:
            udp_age = current_time - self.last_ue_time
            if udp_age < OFFBOARD_TIMEOUT_SEC:
                status_msgs.append(f"UDP: {udp_age:.1f}s前")
            else:
                status_msgs.append(f"UDP: 超时({udp_age:.1f}s)")
        else:
            status_msgs.append("UDP: 无数据")

        # ============ 控制状态 ============
        status_msgs.append(f"OFFBOARD: {'是' if self.is_offboard else '否'}")
        if not self.is_offboard and self.is_initialized:
            status_msgs.append(f"初始化: {self.init_counter}/{INIT_COUNT_THRESHOLD}")

        # ============ 目标状态 ============
        if self.current_target:
            mode_str = "悬停" if self.current_target.get('mode', 1) == 0 else "移动"
            status_msgs.append(f"目标: {mode_str}")
            # 显示目标位置 (简化显示)
            status_msgs.append(f"位置: [{self.current_target['x']:.1f},{self.current_target['y']:.1f},{self.current_target['z']:.1f}]m")
        else:
            status_msgs.append("目标: 悬停")

        # ============ 位置状态 ============
        if self.position_valid:
            height = -self.current_position[2]  # NED Z向下为负，高度是-z
            status_msgs.append(f"高度: {height:.1f}m")
        else:
            status_msgs.append("位置: 无效")

        # ============ 系统状态 ============
        # 检查关键topic是否活跃
        if odometry_age > 5.0:
            status_msgs.append("⚠️ Odometry异常")
        if self.last_ue_time > 0 and current_time - self.last_ue_time > 10.0:
            status_msgs.append("⚠️ UDP长时间无数据")

        # 打印状态
        self.get_logger().info(" | ".join(status_msgs))

        # 每10秒打印一次详细心跳
        if not hasattr(self, '_last_detailed_heartbeat'):
            self._last_detailed_heartbeat = current_time

        if current_time - self._last_detailed_heartbeat >= 10.0:
            self._last_detailed_heartbeat = current_time
            self.get_logger().info("=" * 60)
            self.get_logger().info("详细心跳报告 (每10秒):")
            self.get_logger().info(f"  • 控制循环总数: {self.control_loop_count}")
            self.get_logger().info(f"  • Odometry频率: {self.odometry_frequency:.1f}Hz")
            self.get_logger().info(f"  • 最后Odometry: {odometry_age:.1f}秒前")
            self.get_logger().info(f"  • 最后UDP数据: {current_time - self.last_ue_time if self.last_ue_time > 0 else -1:.1f}秒前")
            self.get_logger().info(f"  • OFFBOARD模式: {self.is_offboard}")
            self.get_logger().info(f"  • 位置有效: {self.position_valid}")
            if self.position_valid:
                self.get_logger().info(f"  • 当前位置: [{self.current_position[0]:.2f}, {self.current_position[1]:.2f}, {self.current_position[2]:.2f}]m")
            self.get_logger().info("=" * 60)

    def shutdown(self):
        """关闭节点"""
        self.get_logger().info("正在关闭节点...")

        # 停止UDP线程
        self.udp_running = False
        if self.udp_thread.is_alive():
            self.udp_thread.join(timeout=2.0)

        # 关闭UDP套接字
        if self.udp_socket:
            self.udp_socket.close()

        self.get_logger().info("节点关闭完成")

# ==================== 命令行接口 ====================

def main():
    """主函数"""
    parser = argparse.ArgumentParser(description="UE5到PX4无人机控制桥接")
    parser.add_argument('--drone-id', type=int, default=DEFAULT_DRONE_ID,
                       help=f'无人机ID (默认: {DEFAULT_DRONE_ID})')
    parser.add_argument('--topic-prefix', type=str, default=DEFAULT_PX4_TOPIC_PREFIX,
                       help=f'ROS2话题前缀 (默认: {DEFAULT_PX4_TOPIC_PREFIX})')
    parser.add_argument('--udp-port', type=int, default=DEFAULT_UDP_PORT,
                       help=f'接收UE5数据的UDP端口 (默认: {DEFAULT_UDP_PORT})')
    parser.add_argument('--log-level', type=str, default='INFO',
                       choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'],
                       help='日志级别 (默认: INFO)')

    args = parser.parse_args()

    # 配置日志
    logging.basicConfig(
        level=getattr(logging, args.log_level),
        format='%(asctime)s [%(levelname)s] %(message)s',
        datefmt='%H:%M:%S'
    )

    # 初始化ROS2
    rclpy.init(args=sys.argv)

    # 创建节点
    bridge = UEToPX4Bridge(
        drone_id=args.drone_id,
        topic_prefix=args.topic_prefix,
        udp_port=args.udp_port
    )

    bridge.get_logger().info("=" * 60)
    bridge.get_logger().info("UE5到PX4桥接节点启动")
    bridge.get_logger().info(f"无人机ID: {args.drone_id}")
    bridge.get_logger().info(f"话题前缀: {args.topic_prefix}")
    bridge.get_logger().info(f"UDP端口: {args.udp_port}")
    bridge.get_logger().info(f"日志级别: {args.log_level}")
    bridge.get_logger().info("=" * 60)
    bridge.get_logger().info("等待UE5数据... (按Ctrl+C退出)")

    try:
        # 运行节点
        rclpy.spin(bridge)
    except KeyboardInterrupt:
        bridge.get_logger().info("收到中断信号")
    except Exception as e:
        bridge.get_logger().error(f"运行错误: {e}")
    finally:
        # 清理
        bridge.shutdown()
        bridge.destroy_node()
        rclpy.shutdown()

    bridge.get_logger().info("程序退出")

if __name__ == "__main__":
    main()
