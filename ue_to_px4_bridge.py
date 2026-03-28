#!/usr/bin/env python3
"""
UE5到PX4无人机控制桥接节点
功能：接收UE5仿真无人机控制指令，转发到PX4真实无人机

改进内容 (基于todo.md要求):
1. 基于状态机的控制逻辑：明确上锁→解锁→OFFBOARD流程
2. GPS全局坐标统一：订阅vehicle_global_position，计算相对于地图中心的偏移
3. 适配多架飞机 (target_system = 2,3,4)
4. PX4 v1.16兼容：持续发布offboard control mode和trajectory setpoint
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

# PX4消息导入 - 使用系统安装的px4_msgs包
try:
    from px4_msgs.msg import OffboardControlMode, TrajectorySetpoint, VehicleCommand
    from px4_msgs.msg import VehicleOdometry, VehicleStatus, VehicleGlobalPosition
except ImportError as e:
    print(f"错误: 无法导入PX4消息: {e}")
    print("请确保px4_msgs包已正确安装")
    print("安装方法: cd px4_msgs && pip install -e .")
    sys.exit(1)

# 可选：使用本地ue_px4_msgs包（用于类型定义和工具函数）
try:
    from ue_px4_msgs import GPSConverter, CoordinateConverter
except ImportError:
    # 如果未安装，使用内联定义
    class GPSConverter:
        @staticmethod
        def gps_to_enu(lat, lon, alt, lat0, lon0, alt0):
            """简化的GPS->ENU转换"""
            import math
            dlat = lat - lat0
            dlon = lon - lon0
            dalt = alt - alt0

            lat0_rad = math.radians(lat0)
            meters_per_deg_lat = 111319.0
            meters_per_deg_lon = 111319.0 * math.cos(lat0_rad)

            n = dlat * meters_per_deg_lat
            e = dlon * meters_per_deg_lon
            u = dalt
            return e, n, u

        @staticmethod
        def enu_to_ned(e, n, u):
            """ENU->NED转换"""
            return n, e, -u

    CoordinateConverter = None
    print("[WARNING] ue_px4_msgs未安装，使用内置GPS转换")

# ==================== 配置常量 ====================

# UDP配置
DEFAULT_UDP_PORT = 8889  # 接收UE5数据的端口（单机模式）
DEFAULT_UDP_HOST = "0.0.0.0"  # 监听所有接口

# PX4话题配置
DEFAULT_PX4_TOPIC_PREFIX = "/px4_1"  # 话题前缀，对应无人机ID
DEFAULT_DRONE_ID = 2  # 无人机MAVLink ID，用于VehicleCommand.target_system
# 注意：PX4无人机ID: px4_1=2, px4_2=3, px4_3=4 (不是1,2,3)

# 控制参数
INIT_COUNT_THRESHOLD = 20  # 进入OFFBOARD前需要发送的空消息次数
CONTROL_RATE_HZ = 100  # 控制频率 (Hz)
OFFBOARD_TIMEOUT_SEC = 0.5  # OFFBOARD模式超时时间
ARM_RETRY_INTERVAL = 2.0  # 解锁重试间隔 (秒)

# 坐标转换参数
UE5_TO_NED_SCALE = 0.01  # UE5厘米 → NED米: 除以100
NED_TO_UE5_SCALE = 100.0  # NED米 → UE5厘米: 乘以100

# 安全限制
MAX_VELOCITY = 5.0  # 最大速度 (米/秒)
MAX_ACCELERATION = 2.0  # 最大加速度 (米/秒²)
MIN_ALTITUDE = 1.0  # 最小高度 (米，离地面)
MAX_ALTITUDE = 50.0  # 最大高度 (米，适当放宽)

# ==================== GPS 全局坐标管理 ====================

class GPSOffsetManager:
    """GPS偏移管理器：计算并维护相对于地图中心的坐标偏移"""

    def __init__(self, map_center_lat: float = None, map_center_lon: float = None, map_center_alt: float = 0.0):
        """
        初始化GPS偏移管理器

        Args:
            map_center_lat: 地图中心纬度 (度)
            map_center_lon: 地图中心经度 (度)
            map_center_alt: 地图中心海拔 (米)
        """
        self.map_center_lat = map_center_lat
        self.map_center_lon = map_center_lon
        self.map_center_alt = map_center_alt
        self.offset_ned = [0.0, 0.0, 0.0]  # (N, E, D) 偏移量 (米)
        self.gps_received = False
        self.current_gps: Optional[VehicleGlobalPosition] = None

    def set_map_center(self, lat: float, lon: float, alt: float = 0.0):
        """设置地图中心GPS坐标"""
        self.map_center_lat = lat
        self.map_center_lon = lon
        self.map_center_alt = alt
        print(f"[GPSOffset] 地图中心已设置: lat={lat:.6f}, lon={lon:.6f}, alt={alt:.1f}m")

    def update_gps(self, gps_msg: VehicleGlobalPosition):
        """接收到新的GPS数据，更新偏移量"""
        if not gps_msg.lat_lon_valid:
            return

        self.current_gps = gps_msg
        self.gps_received = True

        if self.map_center_lat is None or self.map_center_lon is None:
            # 尚未设置地图中心，暂不计算偏移
            return

        # 计算ENU相对坐标
        e, n, u = GPSConverter.gps_to_enu(
            gps_msg.lat, gps_msg.lon, gps_msg.alt,
            self.map_center_lat, self.map_center_lon, self.map_center_alt
        )

        # 转换为NED
        ned_n, ned_e, ned_d = GPSConverter.enu_to_ned(e, n, u)

        # 更新偏移量 (注意：odometry的原点是飞机启动点，我们需要反过来应用)
        # 目标是： corrected_odom = raw_odom + offset
        self.offset_ned = [ned_n, ned_e, ned_d]

        # 调试日志（降低频率）
        if not hasattr(self, '_last_log_time'):
            self._last_log_time = 0
        if time.time() - self._last_log_time > 10.0:
            print(f"[GPSOffset] GPS: lat={gps_msg.lat:.6f}, lon={gps_msg.lon:.6f}, alt={gps_msg.alt:.1f}m")
            print(f"[GPSOffset] 偏移量 NED: N={ned_n:.2f}, E={ned_e:.2f}, D={ned_d:.2f}m")
            self._last_log_time = time.time()

    def apply_to_position(self, raw_position: List[float]) -> List[float]:
        """
        将GPS偏移应用到原始odometry位置

        PX4的vehicle_odometry位置是以飞机启动点为原点的
        我们需要将其转换为以地图中心为参考的全局坐标

        raw_position = [x_raw, y_raw, z_raw] (NED, 米)
        offset = [n_offset, e_offset, d_offset] (NED, 米)

        corrected_position = raw_position + offset
        """
        if not self.gps_received or self.map_center_lat is None:
            return raw_position

        return [
            raw_position[0] + self.offset_ned[0],
            raw_position[1] + self.offset_ned[1],
            raw_position[2] + self.offset_ned[2]
        ]

    def get_current_global_position(self) -> Optional[List[float]]:
        """获取当前全局位置（经过GPS偏移校正）"""
        if not self.gps_received:
            return None

        # 直接返回基于GPS的全局位置
        if self.current_gps and self.map_center_lat is not None:
            e, n, u = GPSConverter.gps_to_enu(
                self.current_gps.lat, self.current_gps.lon, self.current_gps.alt,
                self.map_center_lat, self.map_center_lon, self.map_center_alt
            )
            ned_n, ned_e, ned_d = GPSConverter.enu_to_ned(e, n, u)
            return [ned_n, ned_e, ned_d]

        return None


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

    STRUCT_FORMAT = "<dfffI"  # 小端序: double, float, float, float, uint32
    DATA_SIZE = struct.calcsize(STRUCT_FORMAT)  # 24字节

    @staticmethod
    def parse_udp_data(data: bytes) -> Optional[Dict[str, Any]]:
        """解析UE5发送的UDP二进制数据"""
        if len(data) != UEDataParser.DATA_SIZE:
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


# ==================== 主桥接节点 ====================

class UEToPX4Bridge(Node):
    """UE5到PX4无人机控制桥接节点"""

    def __init__(self,
                 drone_id: int = DEFAULT_DRONE_ID,
                 topic_prefix: str = DEFAULT_PX4_TOPIC_PREFIX,
                 udp_port: int = DEFAULT_UDP_PORT,
                 map_center_lat: float = None,
                 map_center_lon: float = None,
                 map_center_alt: float = 0.0):
        """
        初始化桥接节点

        Args:
            drone_id: 无人机ID (MAVLink system ID: 2,3,4)
            topic_prefix: ROS2话题前缀 (如"/px4_1")
            udp_port: 接收UE5数据的UDP端口
            map_center_lat: 地图中心纬度
            map_center_lon: 地图中心经度
            map_center_alt: 地图中心海拔
        """
        super().__init__('ue_to_px4_bridge')

        self.drone_id = drone_id
        self.topic_prefix = topic_prefix
        self.udp_port = udp_port

        # 初始化GPS偏移管理器
        self.gps_manager = GPSOffsetManager(map_center_lat, map_center_lon, map_center_alt)

        # ============ 状态变量 ============
        self.is_initialized = False
        self.is_offboard = False
        self.is_armed = False  # 解锁状态
        self.init_counter = 0
        self.last_ue_data = None
        self.last_ue_time = 0
        self.current_target = None  # 当前目标位置 (NED坐标系)
        self.offboard_command_sent = False
        self.arm_command_sent = False

        # 位置状态（来自odometry，已应用GPS偏移）
        self.current_position = [0.0, 0.0, 0.0]
        self.position_valid = False

        # 心跳监测
        self.last_odometry_time = 0.0
        self.odometry_count = 0
        self.odometry_frequency = 0.0
        self.last_heartbeat_print = 0.0
        self.control_loop_count = 0

        # UDP接收日志节流
        self._last_udp_log_time = 0.0
        self._udp_log_interval = 1.0

        # 设置QoS配置
        qos_profile = QoSProfile(
            history=QoSHistoryPolicy.KEEP_LAST,
            depth=10,
            reliability=QoSReliabilityPolicy.BEST_EFFORT,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL
        )

        # ============ ROS2发布者 ============
        self.offboard_pub = self.create_publisher(
            OffboardControlMode,
            f"{self.topic_prefix}/fmu/in/offboard_control_mode",
            qos_profile
        )
        self.trajectory_pub = self.create_publisher(
            TrajectorySetpoint,
            f"{self.topic_prefix}/fmu/in/trajectory_setpoint",
            qos_profile
        )
        self.vehicle_cmd_pub = self.create_publisher(
            VehicleCommand,
            f"{self.topic_prefix}/fmu/in/vehicle_command",
            qos_profile
        )

        # ============ ROS2订阅者 ============
        # 订阅odometry (用于位置反馈)
        self.odometry_sub = self.create_subscription(
            VehicleOdometry,
            f"{self.topic_prefix}/fmu/out/vehicle_odometry",
            self.odometry_callback,
            qos_profile
        )

        # 订阅vehicle_status (检测解锁和OFFBOARD状态)
        self.status_sub = self.create_subscription(
            VehicleStatus,
            f"{self.topic_prefix}/fmu/out/vehicle_status",
            self.status_callback,
            qos_profile
        )

        # 订阅vehicle_global_position (GPS全局位置)
        self.global_position_sub = self.create_subscription(
            VehicleGlobalPosition,
            f"{self.topic_prefix}/fmu/out/vehicle_global_position",
            self.global_position_callback,
            qos_profile
        )

        # ============ UDP套接字 ============
        self.udp_socket = None
        self.setup_udp_socket()

        # ============ 定时器 ============
        self.control_timer = self.create_timer(1.0 / CONTROL_RATE_HZ, self.control_loop)
        self.status_timer = self.create_timer(1.0, self.print_status)

        # ============ UDP接收线程 ============
        self.udp_thread = threading.Thread(target=self.udp_receiver_thread, daemon=True)
        self.udp_running = True
        self.udp_thread.start()

        self.get_logger().info(f"UE5到PX4桥接节点初始化完成")
        self.get_logger().info(f"无人机ID: {self.drone_id}")
        self.get_logger().info(f"话题前缀: {self.topic_prefix}")
        self.get_logger().info(f"UDP监听端口: {self.udp_port}")
        self.get_logger().info(f"控制频率: {CONTROL_RATE_HZ}Hz")
        if map_center_lat is not None:
            self.get_logger().info(f"地图中心: lat={map_center_lat:.6f}, lon={map_center_lon:.6f}")

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
                data, addr = self.udp_socket.recvfrom(1024)
                now = time.time()
                if now - self._last_udp_log_time >= self._udp_log_interval:
                    self._last_udp_log_time = now
                    self.get_logger().info(f"收到UDP包: {len(data)} bytes from {addr[0]}:{addr[1]}")

                # 解析数据
                parsed_data = UEDataParser.parse_udp_data(data)
                if parsed_data:
                    self.last_ue_data = parsed_data
                    self.last_ue_time = time.time()

                    # 坐标转换: UE5厘米 -> NED米
                    ned_x, ned_y, ned_z = CoordinateConverter.ue5_to_ned(
                        parsed_data['x'], parsed_data['y'], parsed_data['z']
                    ) if CoordinateConverter else (
                        parsed_data['x'] * UE5_TO_NED_SCALE,
                        parsed_data['y'] * UE5_TO_NED_SCALE,
                        -parsed_data['z'] * UE5_TO_NED_SCALE
                    )

                    # 应用安全限制
                    ned_x, ned_y, ned_z = self.apply_safety_limits(ned_x, ned_y, ned_z)

                    self.current_target = {
                        'x': ned_x,
                        'y': ned_y,
                        'z': ned_z,
                        'mode': parsed_data['mode']
                    }

                    # 调试日志
                    if not hasattr(self, '_udp_counter'):
                        self._udp_counter = 0
                    self._udp_counter += 1

                    if self._udp_counter % 50 == 0:
                        self.get_logger().info(
                            f"收到UE5数据: 模式={parsed_data['mode']}, "
                            f"UE5=({parsed_data['x']:.1f},{parsed_data['y']:.1f},{parsed_data['z']:.1f})cm, "
                            f"NED=({ned_x:.2f},{ned_y:.2f},{ned_z:.2f})m"
                        )

            except socket.timeout:
                continue
            except Exception as e:
                self.get_logger().error(f"UDP接收错误: {e}")
                time.sleep(0.1)

        self.get_logger().info("UDP接收线程结束")

    def global_position_callback(self, msg: VehicleGlobalPosition):
        """GPS全局位置回调"""
        self.gps_manager.update_gps(msg)

    def odometry_callback(self, msg: VehicleOdometry):
        """无人机odometry回调 - 处理位置和速度"""
        current_time = time.time()

        # 获取原始odometry位置
        raw_position = [float(x) for x in msg.position[:3]]

        # 应用GPS偏移（如果有）
        corrected_position = self.gps_manager.apply_to_position(raw_position)

        # 保存修正后的位置
        self.current_position = corrected_position
        self.current_velocity = [float(x) for x in msg.velocity[:3]]
        self.position_valid = True

        self.last_odometry_time = current_time
        self.odometry_count += 1

        # 计算频率
        if not hasattr(self, '_last_freq_calc_time'):
            self._last_freq_calc_time = current_time
            self._odom_count_since_last = 0

        self._odom_count_since_last = getattr(self, '_odom_count_since_last', 0) + 1

        if current_time - self._last_freq_calc_time >= 1.0:
            self.odometry_frequency = self._odom_count_since_last / (current_time - self._last_freq_calc_time)
            self._last_freq_calc_time = current_time
            self._odom_count_since_last = 0

        # 调试日志
        if not hasattr(self, '_odom_counter'):
            self._odom_counter = 0
        self._odom_counter += 1

        if self._odom_counter % 100 == 0:
            self.get_logger().debug(
                f"无人机位置: [{self.current_position[0]:.2f}, "
                f"{self.current_position[1]:.2f}, {self.current_position[2]:.2f}]m, "
                f"频率: {self.odometry_frequency:.1f}Hz"
            )

    def status_callback(self, msg: VehicleStatus):
        """无人机状态回调 - 检测解锁和OFFBOARD状态"""
        was_armed = self.is_armed
        # 使用正确的常量: ARMING_STATE_ARMED = 2
        # 注意：不同PX4版本可能有差异，这里使用常见的值
        # 如果ROS2包中有定义VehicleStatus.ARMING_STATE_ARMED，应使用该常量
        try:
            ARMING_STATE_ARMED = VehicleStatus.ARMING_STATE_ARMED
        except AttributeError:
            ARMING_STATE_ARMED = 2  # 回退值

        try:
            NAVIGATION_STATE_OFFBOARD = VehicleStatus.NAVIGATION_STATE_OFFBOARD
        except AttributeError:
            NAVIGATION_STATE_OFFBOARD = 14

        self.is_armed = (msg.arming_state == ARMING_STATE_ARMED)
        in_offboard = (msg.nav_state == NAVIGATION_STATE_OFFBOARD)

        if in_offboard and not self.is_offboard:
            self.is_offboard = True
            self.get_logger().info("飞控已确认OFFBOARD模式")

        if not in_offboard and self.is_offboard:
            self.is_offboard = False
            self.offboard_command_sent = False
            self.init_counter = 0
            self.get_logger().warn("飞控已退出OFFBOARD，准备重新进入")

        if self.is_armed and not was_armed:
            self.get_logger().info("无人机已解锁")
        elif not self.is_armed and was_armed:
            self.get_logger().warn("无人机已上锁")

    def publish_vehicle_command(self, command: int, param1: float = 0.0, param2: float = 0.0):
        """发布车辆命令"""
        msg = VehicleCommand()
        msg.timestamp = int(self.get_clock().now().nanoseconds / 1000)
        msg.command = command
        msg.param1 = param1
        msg.param2 = param2
        msg.target_system = self.drone_id  # 使用配置的drone_id
        msg.target_component = 1
        msg.source_system = 1
        msg.source_component = 1
        msg.confirmation = 0
        msg.from_external = True

        self.vehicle_cmd_pub.publish(msg)

        # 命令名称映射（使用常量或回退到数字）
        cmd_names = {
            getattr(VehicleCommand, 'VEHICLE_CMD_DO_SET_MODE', 176): "VEHICLE_CMD_DO_SET_MODE",
            getattr(VehicleCommand, 'VEHICLE_CMD_COMPONENT_ARM_DISARM', 400): "VEHICLE_CMD_COMPONENT_ARM_DISARM",
        }
        cmd_name = cmd_names.get(command, f"CMD_{command}")
        self.get_logger().info(f"发送车辆命令: {cmd_name}, param1={param1}, param2={param2}")

    def enter_offboard_mode(self) -> bool:
        """
        进入OFFBOARD模式

        流程:
        1. 发送INIT_COUNT_THRESHOLD次空TrajectorySetpoint
        2. 发送OFFBOARD切换命令 (VEHICLE_CMD_DO_SET_MODE, param1=1.0, param2=6.0)
        3. 等待飞控确认进入OFFBOARD

        Returns:
            是否成功进入OFFBOARD模式
        """
        # 使用命令常量 (优先使用ROS2消息类中定义的值)
        VEHICLE_CMD_DO_SET_MODE = getattr(VehicleCommand, 'VEHICLE_CMD_DO_SET_MODE', 176)
        PX4_CUSTOM_MAIN_MODE_OFFBOARD = getattr(VehicleCommand, 'PX4_CUSTOM_MAIN_MODE_OFFBOARD', 6.0)

        # 步骤1: 发送空setpoint (预填充)
        if self.init_counter < INIT_COUNT_THRESHOLD:
            self.publish_empty_setpoint()
            self.init_counter += 1

            if self.init_counter >= INIT_COUNT_THRESHOLD:
                if not self.offboard_command_sent:
                    self.publish_vehicle_command(VEHICLE_CMD_DO_SET_MODE, 1.0, PX4_CUSTOM_MAIN_MODE_OFFBOARD)
                    self.offboard_command_sent = True
                    self.get_logger().info("已发送OFFBOARD切换命令，等待飞控确认...")

            return False

        # 持续重试OFFBOARD命令直到确认
        if not self.is_offboard:
            if not hasattr(self, '_last_offboard_retry'):
                self._last_offboard_retry = 0
            if time.time() - self._last_offboard_retry > 1.0:
                self.publish_vehicle_command(VEHICLE_CMD_DO_SET_MODE, 1.0, PX4_CUSTOM_MAIN_MODE_OFFBOARD)
                self._last_offboard_retry = time.time()
                self.get_logger().info("重试OFFBOARD切换命令...")
            return False

        return True

    def publish_empty_setpoint(self):
        """发布空的轨迹设定点 (用于初始化OFFBOARD模式)"""
        msg = TrajectorySetpoint()
        msg.timestamp = int(self.get_clock().now().nanoseconds / 1000)

        nan = float('nan')
        msg.position = [nan, nan, nan]
        msg.velocity = [0.0, 0.0, 0.0]
        msg.acceleration = [nan, nan, nan]
        msg.yaw = nan
        msg.yawspeed = nan

        self.trajectory_pub.publish(msg)

    def publish_offboard_control_mode(self, position: bool = True, velocity: bool = False):
        """发布OFFBOARD控制模式消息

        PX4 v1.16要求持续接收此消息以维持OFFBOARD模式
        """
        msg = OffboardControlMode()
        msg.timestamp = int(self.get_clock().now().nanoseconds / 1000)
        msg.position = position
        msg.velocity = velocity
        msg.acceleration = False
        msg.attitude = False
        msg.body_rate = False
        msg.thrust_and_torque = False  # PX4 v1.16新增字段
        msg.direct_actuator = False    # PX4 v1.16新增字段

        self.offboard_pub.publish(msg)

    def publish_position_setpoint(self, x: float, y: float, z: float, yaw: float = 0.0):
        """发布位置设定点

        Args:
            x, y, z: NED坐标 (米)
            yaw: 偏航角 (弧度, 0=北)
        """
        msg = TrajectorySetpoint()
        msg.timestamp = int(self.get_clock().now().nanoseconds / 1000)
        msg.position = [float(x), float(y), float(z)]

        nan = float('nan')
        msg.velocity = [nan, nan, nan]
        msg.acceleration = [nan, nan, nan]
        msg.yaw = float(yaw)
        msg.yawspeed = nan

        self.trajectory_pub.publish(msg)

    def apply_safety_limits(self, ned_x: float, ned_y: float, ned_z: float) -> Tuple[float, float, float]:
        """应用安全限制到NED坐标"""
        # 限制高度（NED Z向下为正，所以高度是 -ned_z）
        if ned_z > -MIN_ALTITUDE:
            ned_z = -MIN_ALTITUDE
        elif ned_z < -MAX_ALTITUDE:
            ned_z = -MAX_ALTITUDE

        return ned_x, ned_y, ned_z

    def control_loop(self):
        """主控制循环 (100Hz) - 持续发送OFFBOARD控制消息"""
        current_time = time.time()

        self.control_loop_count += 1

        # 检查UDP数据超时
        if self.last_ue_time > 0 and (current_time - self.last_ue_time) > OFFBOARD_TIMEOUT_SEC:
            if self.is_offboard:
                self.current_target = None

        # 检查odometry超时
        odometry_age = current_time - self.last_odometry_time if self.last_odometry_time > 0 else float('inf')
        if odometry_age > 5.0:
            pass  # 可添加处理逻辑

        # ============ OFFBOARD模式维持 ============
        if self.is_offboard:
            # 检查解锁状态，未解锁则尝试解锁（每2秒一次）
            if not self.is_armed:
                if not hasattr(self, '_last_arm_retry'):
                    self._last_arm_retry = 0
                if time.time() - self._last_arm_retry > ARM_RETRY_INTERVAL:
                    self.publish_vehicle_command(400, 1.0, 0.0)  # ARM
                    self._last_arm_retry = time.time()
                    self.get_logger().info("尝试解锁...")

            # 关键：必须持续发送OffboardControlMode
            self.publish_offboard_control_mode(position=True)

            # 根据目标位置发布TrajectorySetpoint
            if self.current_target is None or self.current_target.get('mode', 1) == 0:
                # 悬停模式：保持当前位置
                if self.position_valid:
                    self.publish_position_setpoint(
                        self.current_position[0],
                        self.current_position[1],
                        self.current_position[2],
                        0.0
                    )
                else:
                    # 使用安全悬停位置
                    safe_altitude = -2.0
                    self.publish_position_setpoint(0.0, 0.0, safe_altitude, 0.0)
            else:
                # 移动模式：飞向目标位置
                target = self.current_target
                self.publish_position_setpoint(
                    target['x'],
                    target['y'],
                    target['z'],
                    0.0
                )

            # 打印心跳
            if self.control_loop_count % 500 == 0:
                self.get_logger().info(
                    f"[心跳] 控制循环: {self.control_loop_count}, "
                    f"频率: {1.0/(current_time - self.last_control_loop_time) if 'last_control_loop_time' in locals() else 0:.1f}Hz"
                )
        else:
            # 初始化阶段：进入OFFBOARD
            if not self.is_initialized:
                if self.enter_offboard_mode():
                    self.is_initialized = True
            else:
                # 持续尝试进入OFFBOARD
                self.enter_offboard_mode()

        self.last_control_loop_time = current_time

    def print_status(self):
        """打印状态信息 (1Hz)"""
        current_time = time.time()
        status_msgs = []

        status_msgs.append(f"控制循环: {self.control_loop_count}")

        odometry_age = current_time - self.last_odometry_time if self.last_odometry_time > 0 else float('inf')
        if odometry_age < 2.0:
            status_msgs.append(f"Odometry: {self.odometry_frequency:.1f}Hz")
        else:
            status_msgs.append(f"Odometry: 超时({odometry_age:.1f}s)")

        if self.last_ue_time > 0:
            udp_age = current_time - self.last_ue_time
            if udp_age < OFFBOARD_TIMEOUT_SEC:
                status_msgs.append(f"UDP: {udp_age:.1f}s前")
            else:
                status_msgs.append(f"UDP: 超时({udp_age:.1f}s)")
        else:
            status_msgs.append("UDP: 无数据")

        status_msgs.append(f"OFFBOARD: {'是' if self.is_offboard else '否'}")
        if not self.is_offboard and self.is_initialized:
            status_msgs.append(f"初始化: {self.init_counter}/{INIT_COUNT_THRESHOLD}")

        if self.current_target:
            mode_str = "悬停" if self.current_target.get('mode', 1) == 0 else "移动"
            status_msgs.append(f"目标: {mode_str}")

        if self.position_valid:
            height = -self.current_position[2]
            status_msgs.append(f"高度: {height:.1f}m")

        if odometry_age > 5.0:
            status_msgs.append("⚠️ Odometry异常")
        if self.last_ue_time > 0 and current_time - self.last_ue_time > 10.0:
            status_msgs.append("⚠️ UDP长时间无数据")

        self.get_logger().info(" | ".join(status_msgs))

        # 每10秒打印详细报告
        if not hasattr(self, '_last_detailed_heartbeat'):
            self._last_detailed_heartbeat = current_time

        if current_time - self._last_detailed_heartbeat >= 10.0:
            self._last_detailed_heartbeat = current_time
            self.get_logger().info("=" * 60)
            self.get_logger().info("详细心跳报告 (每10秒):")
            self.get_logger().info(f"  • 控制循环总数: {self.control_loop_count}")
            self.get_logger().info(f"  • Odometry频率: {self.odometry_frequency:.1f}Hz")
            self.get_logger().info(f"  • 最后Odometry: {odometry_age:.1f}秒前")
            self.get_logger().info(f"  • OFFBOARD模式: {self.is_offboard}")
            self.get_logger().info(f"  • 位置有效: {self.position_valid}")
            if self.position_valid:
                self.get_logger().info(f"  • 当前位置: [{self.current_position[0]:.2f}, {self.current_position[1]:.2f}, {self.current_position[2]:.2f}]m")
                if self.gps_manager.current_gps:
                    self.get_logger().info(f"  • 当前GPS: lat={self.gps_manager.current_gps.lat:.6f}, lon={self.gps_manager.current_gps.lon:.6f}")
            self.get_logger().info("=" * 60)

    def shutdown(self):
        """关闭节点"""
        self.get_logger().info("正在关闭节点...")
        self.udp_running = False
        if self.udp_thread.is_alive():
            self.udp_thread.join(timeout=2.0)
        if self.udp_socket:
            self.udp_socket.close()
        self.get_logger().info("节点关闭完成")


# ==================== 命令行接口 ====================

def main():
    """主函数"""
    parser = argparse.ArgumentParser(description="UE5到PX4无人机控制桥接")
    parser.add_argument('--drone-id', type=int, default=DEFAULT_DRONE_ID,
                       help=f'无人机MAVLink ID (默认: {DEFAULT_DRONE_ID})')
    parser.add_argument('--topic-prefix', type=str, default=DEFAULT_PX4_TOPIC_PREFIX,
                       help=f'ROS2话题前缀 (默认: {DEFAULT_PX4_TOPIC_PREFIX})')
    parser.add_argument('--udp-port', type=int, default=DEFAULT_UDP_PORT,
                       help=f'接收UE5数据的UDP端口 (默认: {DEFAULT_UDP_PORT})')
    parser.add_argument('--map-center-lat', type=float, default=None,
                       help='地图中心纬度 (度)')
    parser.add_argument('--map-center-lon', type=float, default=None,
                       help='地图中心经度 (度)')
    parser.add_argument('--map-center-alt', type=float, default=0.0,
                       help='地图中心海拔 (米)')
    parser.add_argument('--config', type=str, default=None,
                       help='配置文件路径 (YAML格式)')
    parser.add_argument('--log-level', type=str, default='INFO',
                       choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'],
                       help='日志级别 (默认: INFO)')

    args, unknown = parser.parse_known_args()

    # 加载配置文件（如果提供）
    config = {}
    if args.config:
        try:
            with open(args.config, 'r', encoding='utf-8') as f:
                config = yaml.safe_load(f)
            print(f"[INFO] 已加载配置文件: {args.config}")
        except Exception as e:
            print(f"[WARNING] 加载配置文件失败: {e}")

    # 命令行参数覆盖配置文件
    map_center_lat = args.map_center_lat or config.get('map_center', {}).get('lat')
    map_center_lon = args.map_center_lon or config.get('map_center', {}).get('lon')
    map_center_alt = args.map_center_alt or config.get('map_center', {}).get('alt', 0.0)

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
        udp_port=args.udp_port,
        map_center_lat=map_center_lat,
        map_center_lon=map_center_lon,
        map_center_alt=map_center_alt
    )

    bridge.get_logger().info("=" * 60)
    bridge.get_logger().info("UE5到PX4桥接节点启动")
    bridge.get_logger().info(f"无人机ID: {args.drone_id}")
    bridge.get_logger().info(f"话题前缀: {args.topic_prefix}")
    bridge.get_logger().info(f"UDP端口: {args.udp_port}")
    bridge.get_logger().info(f"日志级别: {args.log_level}")
    if map_center_lat is not None:
        bridge.get_logger().info(f"地图中心: lat={map_center_lat:.6f}, lon={map_center_lon:.6f}")
    bridge.get_logger().info("=" * 60)
    bridge.get_logger().info("等待UE5数据... (按Ctrl+C退出)")

    try:
        rclpy.spin(bridge)
    except KeyboardInterrupt:
        bridge.get_logger().info("收到中断信号")
    except Exception as e:
        bridge.get_logger().error(f"运行错误: {e}")
    finally:
        bridge.shutdown()
        bridge.destroy_node()
        rclpy.shutdown()

    bridge.get_logger().info("程序退出")


if __name__ == "__main__":
    main()
