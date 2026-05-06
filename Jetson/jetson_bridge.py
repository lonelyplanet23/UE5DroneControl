#!/usr/bin/env python3
"""
jetson_bridge.py — Jetson 端轻量 UDP↔ROS2 透明桥

职责：
  1. UDP 接收 ← 后端 C++ 发来的 24 字节控制包
     → 发布 ROS2 OffboardControlMode + TrajectorySetpoint + VehicleCommand
  2. ROS2 订阅 odometry/status/battery/GPS 话题
     → 组合 YAML → UDP 发送 → 后端 C++

替代原 ue_to_px4_bridge.py (800+ 行 → 约 200 行)
去掉了：状态机、GPS偏移、坐标转换（全部移到后端）
"""

import struct
import yaml
import socket
import threading
import time
import signal
import sys

import rclpy
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy

from px4_msgs.msg import (
    OffboardControlMode,
    TrajectorySetpoint,
    VehicleCommand,
    VehicleOdometry,
    VehicleStatusV1,
    VehicleLocalPosition,
    VehicleGlobalPosition,
    BatteryStatus,
)


# ============================================================
# 配置（可通过环境变量覆盖）
# ============================================================
BACKEND_HOST = "192.168.30.100"       # 后端 Windows 机器 IP
BACKEND_RECV_PORT = 8888              # 后端遥测接收端口（对应 slot 1）

# 24 字节控制包格式（小端）
CONTROL_FORMAT = "<dfffI"  # double, float, float, float, uint32
CONTROL_SIZE = struct.calcsize(CONTROL_FORMAT)

running = True


def parse_control_packet(data: bytes):
    """解析 24 字节 UDP 控制包 → (timestamp, x, y, z, mode)"""
    if len(data) < CONTROL_SIZE:
        return None
    ts, x, y, z, mode = struct.unpack(CONTROL_FORMAT, data[:CONTROL_SIZE])
    return {"timestamp": ts, "x": x, "y": y, "z": z, "mode": mode}


# ============================================================
# ROS2 桥接节点
# ============================================================
class JetsonBridge(Node):
    def __init__(self, slot: int = 1):
        topic_prefix = f"/px4_{slot}"
        super().__init__(f"jetson_bridge_{slot}")

        # 遥测接收端口（后端发送此端口）
        self.control_port = 8889 + (slot - 1) * 2
        # 遥测发送端口（后端监听此端口）
        self.telemetry_port = 8888 + (slot - 1) * 2

        # QoS
        best_effort = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )

        # --- 发布者（接收 UDP → 发 ROS2） ---
        self.offboard_pub = self.create_publisher(
            OffboardControlMode, f"{topic_prefix}/fmu/in/offboard_control_mode", best_effort)
        self.trajectory_pub = self.create_publisher(
            TrajectorySetpoint, f"{topic_prefix}/fmu/in/trajectory_setpoint", best_effort)
        self.command_pub = self.create_publisher(
            VehicleCommand, f"{topic_prefix}/fmu/in/vehicle_command", best_effort)

        # --- 订阅者（收 ROS2 → 发 UDP） ---
        self.odometry = None
        self.status_v1 = None
        self.local_pos = None
        self.global_pos = None
        self.battery = None

        self.create_subscription(VehicleOdometry,
            f"{topic_prefix}/fmu/out/vehicle_odometry", self._on_odometry, best_effort)
        self.create_subscription(VehicleStatusV1,
            f"{topic_prefix}/fmu/out/vehicle_status_v1", self._on_status_v1, best_effort)
        self.create_subscription(VehicleLocalPosition,
            f"{topic_prefix}/fmu/out/vehicle_local_position", self._on_local_pos, best_effort)
        self.create_subscription(VehicleGlobalPosition,
            f"{topic_prefix}/fmu/out/vehicle_global_position", self._on_global_pos, best_effort)
        self.create_subscription(BatteryStatus,
            f"{topic_prefix}/fmu/out/battery_status", self._on_battery, best_effort)

        # 遥测发送定时器（10Hz）
        self.telemetry_timer = self.create_timer(0.1, self._send_telemetry)

        # UDP socket（控制接收 + 遥测发送）
        self.ctrl_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.ctrl_sock.bind(("0.0.0.0", self.control_port))
        self.ctrl_sock.settimeout(0.1)

        self.tel_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.backend_addr = (BACKEND_HOST, self.telemetry_port)

        self.get_logger().info(
            f"Bridge started: UDP ctrl:{self.control_port} → ROS2 {topic_prefix}")
        self.get_logger().info(
            f"  ROS2 {topic_prefix} → UDP tel:{self.telemetry_port} → {BACKEND_HOST}")

    def _on_odometry(self, msg):
        self.odometry = msg

    def _on_status_v1(self, msg):
        self.status_v1 = msg

    def _on_local_pos(self, msg):
        self.local_pos = msg

    def _on_global_pos(self, msg):
        self.global_pos = msg

    def _on_battery(self, msg):
        self.battery = msg

    def _send_telemetry(self):
        """组合所有 ROS2 数据 → YAML → UDP 发送到后端"""
        data = {}
        data["timestamp"] = self.get_clock().now().nanoseconds // 1000  # 微秒

        if self.odometry:
            data["position"] = list(self.odometry.position)
            data["q"] = list(self.odometry.q)
            data["velocity"] = list(self.odometry.velocity)
            data["angular_velocity"] = list(self.odometry.angular_velocity)

        if self.status_v1:
            data["arming_state"] = self.status_v1.arming_state
            data["nav_state"] = self.status_v1.nav_state

        if self.local_pos:
            data["local_position"] = [
                self.local_pos.x, self.local_pos.y, self.local_pos.z]

        if self.global_pos:
            data["gps_lat"] = self.global_pos.latitude_deg
            data["gps_lon"] = self.global_pos.longitude_deg
            data["gps_alt"] = self.global_pos.altitude_amsl
            data["gps_fix"] = (
                self.global_pos.valid and
                abs(self.global_pos.latitude_deg) > 1.0
            )

        if self.battery:
            data["battery"] = int(
                self.battery.remaining * 100) if self.battery.remaining >= 0 else -1

        yaml_bytes = yaml.dump(data, sort_keys=False, default_flow_style=None).encode("utf-8")
        try:
            self.tel_sock.sendto(yaml_bytes, self.backend_addr)
        except Exception as e:
            self.get_logger().warn(f"UDP send failed: {e}")

    def process_control(self):
        """接收 UDP 控制包 → 发布 ROS2"""
        try:
            data, addr = self.ctrl_sock.recvfrom(1024)
        except socket.timeout:
            return
        except Exception:
            return

        parsed = parse_control_packet(data)
        if not parsed:
            return

        # 1. 发布 OffboardControlMode（位置控制模式）
        ocm = OffboardControlMode()
        ocm.timestamp = int(time.time() * 1e6)
        ocm.position = True
        ocm.velocity = False
        ocm.acceleration = False
        ocm.attitude = False
        ocm.body_rate = False
        ocm.thrust_and_torque = False
        ocm.direct_actuator = False
        self.offboard_pub.publish(ocm)

        # 2. 发布 TrajectorySetpoint
        tsp = TrajectorySetpoint()
        tsp.timestamp = int(time.time() * 1e6)
        if parsed["mode"] == 1:
            # 移动模式
            tsp.position = [parsed["x"], parsed["y"], parsed["z"]]
            tsp.yaw = float("nan")
        else:
            # 悬停模式（维持最后位置）
            tsp.position = [parsed["x"], parsed["y"], parsed["z"]]
            tsp.yaw = float("nan")
        tsp.velocity = [float("nan")] * 3
        tsp.acceleration = [float("nan")] * 3
        self.trajectory_pub.publish(tsp)

        # 3. 如果是移动指令，发送 VehicleCommand
        if parsed["mode"] == 1:
            cmd = VehicleCommand()
            cmd.timestamp = int(time.time() * 1e6)
            cmd.command = VehicleCommand.VEHICLE_CMD_DO_SET_MODE
            cmd.param1 = 1.0
            cmd.param2 = float(VehicleCommand.PX4_CUSTOM_MAIN_MODE_OFFBOARD)
            cmd.target_system = self._get_system_id()
            cmd.target_component = 1
            cmd.source_system = 255
            cmd.source_component = 0
            cmd.from_external = True
            self.command_pub.publish(cmd)

    def _get_system_id(self) -> int:
        """从话题前缀推导 MAVLink system ID（px4_1→2, px4_2→3, ...）"""
        return 2  # 默认 px4_1

    def cleanup(self):
        self.ctrl_sock.close()
        self.tel_sock.close()


def main():
    global running

    rclpy.init()

    slot = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    bridge = JetsonBridge(slot)

    spin_thread = threading.Thread(target=rclpy.spin, args=(bridge,), daemon=True)
    spin_thread.start()

    def shutdown(sig, frame):
        global running
        running = False
        bridge.cleanup()
        rclpy.shutdown()

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    bridge.get_logger().info("Control loop started (≥2Hz relay)...")

    while running and rclpy.ok():
        bridge.process_control()
        time.sleep(0.01)  # ~100Hz 控制接收轮询

    bridge.cleanup()


if __name__ == "__main__":
    main()
