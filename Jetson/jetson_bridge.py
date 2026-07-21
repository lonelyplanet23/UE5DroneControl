#!/usr/bin/env python3
"""
jetson_bridge.py — Jetson 端轻量 UDP↔ROS2 透明桥 (PX4 v1.17)

职责：
  1. UDP 接收 ← 后端 C++ 发来的 JSON 控制数据报
     → 同一 command_id 在时间窗内达到确认阈值后才更新 setpoint
     → 缓存最新 setpoint，由独立 50Hz 定时器持续发布
       ROS2 OffboardControlMode + TrajectorySetpoint
     → 切 Offboard 模式 / 解锁（仅在需要时发一次 VehicleCommand）
  2. ROS2 订阅 odometry/status/battery/GPS 话题
     → 组合 YAML → UDP 发送 → 后端 C++（10Hz）

PX4 v1.17 话题变更：
  - vehicle_status        → vehicle_status_v1   (VehicleStatusV1)
  - VehicleGlobalPosition 字段: latitude_deg/longitude_deg/altitude_amsl
                               → lat/lon/alt，valid → lat_lon_valid
"""

import json
import yaml
import socket
import threading
import time
import signal
import sys
import math
import os

import rclpy
import rclpy.parameter
from rclpy.node import Node
from rclpy.qos import QoSProfile, ReliabilityPolicy, HistoryPolicy, DurabilityPolicy

from px4_msgs.msg import (
    OffboardControlMode,
    TrajectorySetpoint,
    VehicleCommand,
    VehicleOdometry,
    VehicleStatus,
    VehicleLocalPosition,
    VehicleGlobalPosition,
    BatteryStatus,
)

# Some deployed PX4/px4_msgs combinations do not provide VehicleCommandAck.
# It is only used for diagnostics, so it must not prevent the control bridge
# from starting on those vehicles.
try:
    from px4_msgs.msg import VehicleCommandAck
except ImportError:
    VehicleCommandAck = None


# ============================================================
# 配置（可通过环境变量覆盖）
# ============================================================
# 当前外场局域网默认值。仍建议用 BACKEND_HOST 显式覆盖，避免换网后依赖默认值。
BACKEND_HOST = os.environ.get("BACKEND_HOST", "192.168.10.30").strip()
CONTROL_BIND_HOST = os.environ.get("CONTROL_BIND_HOST", "0.0.0.0").strip()

# 默认沿用当前单机实机环境的无前缀 PX4 话题。多机命名空间环境可设置：
#   ROS_TOPIC_PREFIX=/px4_1
ROS_TOPIC_PREFIX = os.environ.get("ROS_TOPIC_PREFIX", "").strip().rstrip("/")

# 周期诊断日志间隔。外场建议保留默认 5 秒。
DIAGNOSTIC_INTERVAL_SEC = float(os.environ.get("DIAGNOSTIC_INTERVAL_SEC", "5"))

# UDP 应用层可靠性策略：默认后端发 5 次，Jetson 在 2.5 秒内收到
# 3 个 repeat_index 不同且负载一致的数据报后才应用目标点。
COMMAND_CONFIRM_COUNT = int(os.environ.get("COMMAND_CONFIRM_COUNT", "3"))
COMMAND_CONFIRM_WINDOW_SEC = float(
    os.environ.get("COMMAND_CONFIRM_WINDOW_SEC", "2.5")
)
MAX_CONTROL_PACKET_BYTES = int(os.environ.get("MAX_CONTROL_PACKET_BYTES", "4096"))
MAX_ABS_TARGET_M = float(os.environ.get("MAX_ABS_TARGET_M", "5000"))
MAX_TELEMETRY_SAMPLE_AGE_SEC = float(
    os.environ.get("MAX_TELEMETRY_SAMPLE_AGE_SEC", "1.0")
)
MAX_GEOGRAPHIC_SAMPLE_SKEW_SEC = float(
    os.environ.get("MAX_GEOGRAPHIC_SAMPLE_SKEW_SEC", "0.25")
)
CONTROL_PROTOCOL = "ue5_drone_control"
CONTROL_PROTOCOL_VERSION = 1

# Offboard 心跳频率（Hz）——必须 > 2Hz，50Hz 留足余量
OFFBOARD_HZ = 50
OFFBOARD_INTERVAL = 1.0 / OFFBOARD_HZ

# 遥测发送频率（Hz）
TELEMETRY_HZ = 10

running = True


# ============================================================
# 控制包解析
# ============================================================
def parse_control_packet(data: bytes):
    """严格解析并验证后端 JSON 控制协议；失败时抛出 ValueError。"""
    if not data:
        raise ValueError("empty UDP datagram")
    if len(data) > MAX_CONTROL_PACKET_BYTES:
        raise ValueError(
            f"packet too large: {len(data)}B > {MAX_CONTROL_PACKET_BYTES}B"
        )
    try:
        message = json.loads(data.decode("utf-8"))
    except UnicodeDecodeError as exc:
        raise ValueError(f"payload is not UTF-8: {exc}") from exc
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON at char {exc.pos}: {exc.msg}") from exc

    if not isinstance(message, dict):
        raise ValueError("JSON root must be an object")
    if message.get("protocol") != CONTROL_PROTOCOL:
        raise ValueError(f"unexpected protocol={message.get('protocol')!r}")
    if message.get("version") != CONTROL_PROTOCOL_VERSION:
        raise ValueError(f"unsupported version={message.get('version')!r}")
    if message.get("type") != "control":
        raise ValueError(f"unexpected type={message.get('type')!r}")

    session_id = message.get("session_id")
    command_id = message.get("command_id")
    mode = message.get("mode")
    if not isinstance(session_id, str) or not session_id:
        raise ValueError("session_id must be a non-empty string")
    if not isinstance(command_id, str) or not command_id:
        raise ValueError("command_id must be a non-empty string")
    if mode not in ("move", "hold"):
        raise ValueError(f"mode must be move/hold, got {mode!r}")

    try:
        sequence = int(message["sequence"])
        drone_id = int(message["drone_id"])
        slot = int(message["slot"])
        issued_at = float(message["issued_at_unix_s"])
        sent_at = float(message["sent_at_unix_s"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"invalid command metadata: {exc}") from exc
    if sequence <= 0 or drone_id <= 0 or slot <= 0:
        raise ValueError(
            f"sequence/drone_id/slot must be positive: {sequence}/{drone_id}/{slot}"
        )

    target = message.get("target")
    if not isinstance(target, dict):
        raise ValueError("target must be an object")
    if target.get("frame") != "NED":
        raise ValueError(f"target.frame must be NED, got {target.get('frame')!r}")
    if target.get("reference") != "power_on_origin":
        raise ValueError(
            "target.reference must be power_on_origin, got "
            f"{target.get('reference')!r}"
        )
    if target.get("unit") != "m":
        raise ValueError(f"target.unit must be m, got {target.get('unit')!r}")
    try:
        north = float(target["north"])
        east = float(target["east"])
        down = float(target["down"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"invalid NED target: {exc}") from exc

    delivery = message.get("delivery")
    if not isinstance(delivery, dict):
        raise ValueError("delivery must be an object")
    try:
        repeat_index = int(delivery["repeat_index"])
        repeat_total = int(delivery["repeat_total"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError(f"invalid delivery metadata: {exc}") from exc
    if repeat_index <= 0 or repeat_total < 0:
        raise ValueError(
            f"invalid repeat_index/repeat_total={repeat_index}/{repeat_total}"
        )
    if repeat_total > 0 and repeat_index > repeat_total:
        raise ValueError(
            f"repeat_index {repeat_index} exceeds repeat_total {repeat_total}"
        )

    numeric_values = (issued_at, sent_at, north, east, down)
    if not all(math.isfinite(value) for value in numeric_values):
        raise ValueError("timestamps and NED coordinates must be finite")
    if max(abs(north), abs(east), abs(down)) > MAX_ABS_TARGET_M:
        raise ValueError(
            f"NED target exceeds MAX_ABS_TARGET_M={MAX_ABS_TARGET_M}: "
            f"({north},{east},{down})"
        )

    return {
        "session_id": session_id,
        "command_id": command_id,
        "sequence": sequence,
        "drone_id": drone_id,
        "slot": slot,
        "mode": mode,
        "issued_at": issued_at,
        "sent_at": sent_at,
        "x": north,
        "y": east,
        "z": down,
        "repeat_index": repeat_index,
        "repeat_total": repeat_total,
    }


# ============================================================
# ROS2 桥接节点
# ============================================================
def valid_vehicle_local_ned(local_position):
    """Return a finite PX4 VehicleLocalPosition NED tuple, or ``None``."""
    if local_position is None:
        return None

    try:
        local_ned = [
            float(local_position.x),
            float(local_position.y),
            float(local_position.z),
        ]
    except (AttributeError, TypeError, ValueError, OverflowError):
        return None

    if not bool(getattr(local_position, "xy_valid", False)):
        return None
    if not bool(getattr(local_position, "z_valid", False)):
        return None
    if not all(math.isfinite(value) for value in local_ned):
        return None
    return local_ned


def ros_sample_is_fresh(last_sample_times, name, now_monotonic, max_age_sec):
    """Whether a named ROS sample is recent enough to drive control telemetry."""
    if not math.isfinite(now_monotonic) or not math.isfinite(max_age_sec):
        return False
    if max_age_sec <= 0.0:
        return False
    last_sample = last_sample_times.get(name)
    if last_sample is None or not math.isfinite(last_sample):
        return False
    age = now_monotonic - last_sample
    return 0.0 <= age <= max_age_sec


class JetsonBridge(Node):
    def __init__(self, slot: int = 1):
        if slot < 1 or slot > 6:
            raise ValueError(f"slot must be in 1..6, got {slot}")
        if COMMAND_CONFIRM_COUNT < 1:
            raise ValueError("COMMAND_CONFIRM_COUNT must be >= 1")
        if COMMAND_CONFIRM_WINDOW_SEC <= 0:
            raise ValueError("COMMAND_CONFIRM_WINDOW_SEC must be > 0")
        if not math.isfinite(MAX_TELEMETRY_SAMPLE_AGE_SEC) or MAX_TELEMETRY_SAMPLE_AGE_SEC <= 0:
            raise ValueError("MAX_TELEMETRY_SAMPLE_AGE_SEC must be finite and > 0")
        if not math.isfinite(MAX_GEOGRAPHIC_SAMPLE_SKEW_SEC) or MAX_GEOGRAPHIC_SAMPLE_SKEW_SEC <= 0:
            raise ValueError("MAX_GEOGRAPHIC_SAMPLE_SKEW_SEC must be finite and > 0")

        self.slot = slot
        self._topic_prefix = ROS_TOPIC_PREFIX
        if self._topic_prefix and not self._topic_prefix.startswith("/"):
            self._topic_prefix = "/" + self._topic_prefix

        # Keep the legacy bridge behaviour by default: slot 1 targets SYSID 1.
        # The former working script used ``target_system = slot``.  A different
        # PX4 SYSID must be explicitly provided by MAVLINK_SYSTEM_ID, rather
        # than silently changing the target to slot + 1.
        self._mavlink_system_id = int(
            os.environ.get("MAVLINK_SYSTEM_ID", str(slot))
        )
        if not 1 <= self._mavlink_system_id <= 255:
            raise ValueError(
                f"MAVLINK_SYSTEM_ID must be in 1..255, got {self._mavlink_system_id}"
            )

        super().__init__(f"jetson_bridge_{slot}")

        # UDP 端口（与接口规范对齐）
        # 控制接收：后端发到此端口
        self._ctrl_port = int(os.environ.get(
            "CONTROL_PORT", str(8889 + (slot - 1) * 2)
        ))  # slot1=8889, slot2=8891, ...
        # 遥测发送：后端监听此端口
        self._tel_port = int(os.environ.get(
            "TELEMETRY_PORT", str(8888 + (slot - 1) * 2)
        ))  # slot1=8888, slot2=8890, ...
        if not 1 <= self._ctrl_port <= 65535 or not 1 <= self._tel_port <= 65535:
            raise ValueError(
                f"invalid UDP ports: control={self._ctrl_port}, telemetry={self._tel_port}"
            )

        # -------- QoS --------
        sensor_qos = QoSProfile(
            reliability=ReliabilityPolicy.BEST_EFFORT,
            durability=DurabilityPolicy.VOLATILE,
            history=HistoryPolicy.KEEP_LAST,
            depth=10,
        )

        # -------- 发布者 --------
        self._offboard_pub = self.create_publisher(
            OffboardControlMode,
            f"{self._topic_prefix}/fmu/in/offboard_control_mode",
            sensor_qos,
        )
        self._traj_pub = self.create_publisher(
            TrajectorySetpoint,
            f"{self._topic_prefix}/fmu/in/trajectory_setpoint",
            sensor_qos,
        )
        self._cmd_pub = self.create_publisher(
            VehicleCommand,
            f"{self._topic_prefix}/fmu/in/vehicle_command",
            sensor_qos,
        )

        # -------- 订阅者 --------
        self._odometry: VehicleOdometry | None = None
        self._status: VehicleStatus | None = None
        self._local_pos: VehicleLocalPosition | None = None
        self._global_pos: VehicleGlobalPosition | None = None
        self._battery: BatteryStatus | None = None

        self.create_subscription(
            VehicleOdometry,
            f"{self._topic_prefix}/fmu/out/vehicle_odometry",
            self._on_odometry, sensor_qos,
        )
        # PX4 v1.17: vehicle_status_v1
        self.create_subscription(
            VehicleStatus,
            f"{self._topic_prefix}/fmu/out/vehicle_status_v1",
            self._on_status, sensor_qos,
        )
        self.create_subscription(
            VehicleLocalPosition,
            f"{self._topic_prefix}/fmu/out/vehicle_local_position",
            self._on_local_pos, sensor_qos,
        )
        self.create_subscription(
            VehicleGlobalPosition,
            f"{self._topic_prefix}/fmu/out/vehicle_global_position",
            self._on_global_pos, sensor_qos,
        )
        self.create_subscription(
            BatteryStatus,
            f"{self._topic_prefix}/fmu/out/battery_status",
            self._on_battery, sensor_qos,
        )
        if VehicleCommandAck is not None:
            self.create_subscription(
                VehicleCommandAck,
                f"{self._topic_prefix}/fmu/out/vehicle_command_ack",
                self._on_command_ack, sensor_qos,
            )
        else:
            self.get_logger().warning(
                "[ROS-CHECK] VehicleCommandAck is unavailable in this px4_msgs "
                "installation; command-ack diagnostics are disabled."
            )

        # -------- 链路诊断状态 --------
        self._started_monotonic = time.monotonic()
        self._udp_rx_total = 0
        self._udp_rx_valid = 0
        self._udp_rx_invalid = 0
        self._udp_rx_hold = 0
        self._udp_rx_move = 0
        self._udp_rx_duplicate = 0
        self._udp_rx_stale = 0
        self._udp_recv_errors = 0
        self._last_ctrl_monotonic = None
        self._last_ctrl_sender = None
        self._last_backend_timestamp = None
        self._last_clock_warning_monotonic = 0.0
        self._last_no_control_warning_monotonic = 0.0
        self._last_ros_link_warning_monotonic = 0.0
        self._telemetry_sent = 0
        self._telemetry_send_errors = 0
        self._telemetry_last_bytes = 0
        self._offboard_publish_count = 0
        self._vehicle_command_count = 0
        self._command_ack_count = 0
        # None 表示尚未收到 PX4 状态；之后只在解锁/上锁状态切换时输出一次。
        self._px4_armed = None
        self._ros_rx_counts = {
            "odometry": 0,
            "status": 0,
            "local_position": 0,
            "global_position": 0,
            "battery": 0,
            "command_ack": 0,
        }
        self._ros_last_monotonic = {}
        self._last_vehicle_state = None
        self._active_backend_session = None
        self._retired_backend_sessions = set()
        self._highest_applied_sequence = 0
        self._pending_commands = {}
        self._applied_command_ids = {}
        self._commands_applied = 0
        self._last_applied_command = None

        # -------- 最新 setpoint 缓存 --------
        # 在 PX4 给出首个有效 VehicleLocalPosition 前不得猜测本地原点。
        self._last_setpoint = None
        self._setpoint_lock = threading.Lock()

        # 预热计数 + 手动触发标志
        # 无遥控器流程：bridge 发心跳预热后，等待用户键盘确认再 ARM + 切模式
        self._warmup_count = 0
        self._warmup_needed = OFFBOARD_HZ  # 等待 1 秒（50 帧）
        self._arm_triggered = False        # 由主线程键盘输入置 True
        self._arm_sent_count = 0
        self._offboard_sent_count = 0

        # -------- 定时器 --------
        # 50Hz Offboard 心跳 + setpoint
        self._offboard_timer = self.create_timer(
            OFFBOARD_INTERVAL, self._offboard_loop
        )
        # 10Hz 遥测发送
        self._tel_timer = self.create_timer(
            1.0 / TELEMETRY_HZ, self._send_telemetry
        )
        self._diag_timer = self.create_timer(
            max(1.0, DIAGNOSTIC_INTERVAL_SEC), self._log_diagnostics
        )

        # -------- UDP sockets --------
        self._ctrl_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self._ctrl_sock.bind((CONTROL_BIND_HOST, self._ctrl_port))
        except OSError as exc:
            self.get_logger().fatal(
                f"[UDP-RX] bind failed on {CONTROL_BIND_HOST}:{self._ctrl_port}: "
                f"{type(exc).__name__}: {exc}. Check whether another bridge is running."
            )
            self._ctrl_sock.close()
            raise
        self._ctrl_sock.settimeout(0.05)  # 50ms 超时，不阻塞控制线程

        self._tel_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._backend_addr = (BACKEND_HOST, self._tel_port)
        self._route_local_ip = self._detect_route_local_ip()

        self.get_logger().info(
            f"[slot {slot}] Bridge ready | "
            f"ctrl UDP {CONTROL_BIND_HOST}:{self._ctrl_port} | "
            f"tel UDP → {BACKEND_HOST}:{self._tel_port} | "
            f"ROS2 prefix: {self._topic_prefix or '<none>'} | "
            f"MAVLink system_id: {self._mavlink_system_id}"
        )
        self.get_logger().info(
            f"[NETWORK-CHECK] Jetson route IP to backend is "
            f"{self._route_local_ip or '<unknown>'}. Backend config.yaml must use "
            f"jetson.host={self._route_local_ip or '<this Jetson IP>'} and "
            f"slot {slot} send_port={self._ctrl_port}."
        )
        self.get_logger().info(
            f"[ROS-CHECK] IN topics: "
            f"{self._offboard_pub.topic_name}, {self._traj_pub.topic_name}, "
            f"{self._cmd_pub.topic_name}"
        )
        self.get_logger().info(
            f"[PROTOCOL] JSON {CONTROL_PROTOCOL} v{CONTROL_PROTOCOL_VERSION}; "
            f"confirm={COMMAND_CONFIRM_COUNT} unique packets within "
            f"{COMMAND_CONFIRM_WINDOW_SEC:.2f}s; target=NED meters relative to "
            f"power_on_origin; max_abs_target={MAX_ABS_TARGET_M:.1f}m"
        )
        self.get_logger().info(
            f"[slot {slot}] Offboard heartbeat: {OFFBOARD_HZ}Hz | "
            f"Warmup: {self._warmup_needed} frames (~1s)"
        )

    # ------------------------------------------------------------------
    # ROS2 订阅回调
    # ------------------------------------------------------------------
    def _on_odometry(self, msg: VehicleOdometry):
        self._mark_ros_rx("odometry")
        self._odometry = msg

    def _on_status(self, msg: VehicleStatus):
        self._mark_ros_rx("status")
        self._status = msg
        state = (int(msg.arming_state), int(msg.nav_state))

        # VehicleStatus.ARMING_STATE_ARMED 在不同 px4_msgs 版本中均为 2；
        # 使用 getattr 保持对旧消息包的兼容性。
        armed_state = int(getattr(VehicleStatus, "ARMING_STATE_ARMED", 2))
        is_armed = state[0] == armed_state
        if self._px4_armed is None:
            self._px4_armed = is_armed
            if is_armed:
                self.get_logger().info(
                    f"[PX4-ARM] Drone is ARMED / 已解锁 (arming_state={state[0]})."
                )
        elif is_armed != self._px4_armed:
            self._px4_armed = is_armed
            if is_armed:
                self.get_logger().info(
                    f"[PX4-ARM] Drone is ARMED / 已解锁 (arming_state={state[0]})."
                )
            else:
                self.get_logger().warning(
                    f"[PX4-ARM] Drone is DISARMED / 已上锁 (arming_state={state[0]})."
                )

        if state != self._last_vehicle_state:
            self.get_logger().info(
                f"[PX4-STATE] arming_state={state[0]}, nav_state={state[1]} "
                f"(expected after trigger: armed=2, offboard=14)"
            )
            self._last_vehicle_state = state

    def _on_local_pos(self, msg: VehicleLocalPosition):
        self._mark_ros_rx("local_position")
        self._local_pos = msg
        if valid_vehicle_local_ned(msg) is None:
            with self._setpoint_lock:
                cleared_stale_setpoint = self._last_setpoint is not None
                self._last_setpoint = None
            self._warmup_count = 0
            if cleared_stale_setpoint:
                self.get_logger().warning(
                    "[SAFE-HOLD] PX4 local position became invalid; "
                    "cleared the old-frame setpoint"
                )
            return
        self._ensure_safe_hold_initialized(msg)

    def _ensure_safe_hold_initialized(self, local_position) -> bool:
        """Initialize the first setpoint from a valid PX4 local NED sample."""
        local_ned = valid_vehicle_local_ned(local_position)
        if local_ned is None:
            return False

        initialized_now = False
        with self._setpoint_lock:
            if self._last_setpoint is None:
                self._last_setpoint = {
                    "x": local_ned[0],
                    "y": local_ned[1],
                    "z": local_ned[2],
                    "mode": "hold",
                    "sequence": 0,
                }
                initialized_now = True

        if initialized_now:
            self.get_logger().info(
                f"[SAFE-HOLD] initialized from first valid VehicleLocalPosition: "
                f"NED=({local_ned[0]:.3f},{local_ned[1]:.3f},{local_ned[2]:.3f})"
            )
        return True

    def _on_global_pos(self, msg: VehicleGlobalPosition):
        self._mark_ros_rx("global_position")
        self._global_pos = msg

    def _on_battery(self, msg: BatteryStatus):
        self._mark_ros_rx("battery")
        self._battery = msg

    def _on_command_ack(self, msg):
        self._mark_ros_rx("command_ack")
        self._command_ack_count += 1
        result_names = {
            0: "ACCEPTED",
            1: "TEMPORARILY_REJECTED",
            2: "DENIED",
            3: "UNSUPPORTED",
            4: "FAILED",
            5: "IN_PROGRESS",
            6: "CANCELLED",
        }
        command_names = {
            int(VehicleCommand.VEHICLE_CMD_COMPONENT_ARM_DISARM): "ARM_DISARM",
            int(VehicleCommand.VEHICLE_CMD_DO_SET_MODE): "DO_SET_MODE",
        }
        command = int(msg.command)
        result = int(msg.result)
        self.get_logger().info(
            f"[PX4-ACK] #{self._command_ack_count} "
            f"command={command}({command_names.get(command, 'OTHER')}) "
            f"result={result}({result_names.get(result, 'UNKNOWN')}) "
            f"result_param1={int(msg.result_param1)} "
            f"result_param2={int(msg.result_param2)} "
            f"target_system={int(msg.target_system)} "
            f"target_component={int(msg.target_component)} "
            f"from_external={bool(msg.from_external)}"
        )

    def _mark_ros_rx(self, name: str):
        self._ros_rx_counts[name] += 1
        self._ros_last_monotonic[name] = time.monotonic()
        if self._ros_rx_counts[name] == 1:
            self.get_logger().info(f"[ROS-RX] first {name} message received")

    # ------------------------------------------------------------------
    # 50Hz Offboard 心跳循环（核心）
    # ------------------------------------------------------------------
    def _offboard_loop(self):
        now_us = int(self.get_clock().now().nanoseconds / 1000)

        # Never publish a guessed origin setpoint or advance toward
        # ARM/OFFBOARD before the PX4 local estimator is valid.
        with self._setpoint_lock:
            sp = dict(self._last_setpoint) if self._last_setpoint is not None else None
        if sp is None:
            return

        # 1. 持续发布 OffboardControlMode（位置控制）
        ocm = OffboardControlMode()
        ocm.timestamp = now_us
        ocm.position = True
        ocm.velocity = False
        ocm.acceleration = False
        ocm.attitude = False
        ocm.body_rate = False
        ocm.thrust_and_torque = False
        ocm.direct_actuator = False
        self._offboard_pub.publish(ocm)

        # 2. 持续发布 TrajectorySetpoint（最新缓存值）
        tsp = TrajectorySetpoint()
        tsp.timestamp = now_us
        tsp.position = [sp["x"], sp["y"], sp["z"]]
        tsp.velocity = [float("nan")] * 3
        tsp.acceleration = [float("nan")] * 3
        tsp.yaw = float("nan")
        tsp.yawspeed = float("nan")
        self._traj_pub.publish(tsp)
        self._offboard_publish_count += 1

        # 3. 预热计数
        if self._warmup_count < self._warmup_needed:
            self._warmup_count += 1
            if self._warmup_count == self._warmup_needed:
                self.get_logger().info(
                    f"[slot {self.slot}] Heartbeat ready. "
                    f"Terminal: press ENTER to ARM + OFFBOARD, or Ctrl+C to abort."
                )
            return

        # 4. 等待用户键盘确认，未确认不操作
        if not self._arm_triggered:
            return

        # 5. ARM，连发 5 次
        if self._arm_sent_count < 5:
            self._send_vehicle_command(
                VehicleCommand.VEHICLE_CMD_COMPONENT_ARM_DISARM,
                param1=1.0,
            )
            self._arm_sent_count += 1
            if self._arm_sent_count == 1:
                self.get_logger().info(f"[slot {self.slot}] ARM sending (x5)...")
            return

        # 6. 切 OFFBOARD，连发 5 次
        if self._offboard_sent_count < 5:
            self._send_vehicle_command(
                VehicleCommand.VEHICLE_CMD_DO_SET_MODE,
                param1=1.0,
                param2=6.0,  # PX4_CUSTOM_MAIN_MODE_OFFBOARD
            )
            self._offboard_sent_count += 1
            if self._offboard_sent_count == 1:
                self.get_logger().info(f"[slot {self.slot}] OFFBOARD mode sending (x5)...")
            elif self._offboard_sent_count == 5:
                self.get_logger().info(f"[slot {self.slot}] Done. Check QGC: ARMED + OFFBOARD")

    # ------------------------------------------------------------------
    # 10Hz 遥测发送
    # ------------------------------------------------------------------
    def _send_telemetry(self):
        data = {}
        data["timestamp"] = self.get_clock().now().nanoseconds // 1000  # μs
        now_monotonic = time.monotonic()

        if self._odometry:
            o = self._odometry
            data["position"] = [float(v) for v in o.position]
            data["q"] = [float(v) for v in o.q]
            data["velocity"] = [float(v) for v in o.velocity]
            data["angular_velocity"] = [float(v) for v in o.angular_velocity]

        if self._status:
            data["arming_state"] = int(self._status.arming_state)
            data["nav_state"] = int(self._status.nav_state)

        local_position_fresh = ros_sample_is_fresh(
            self._ros_last_monotonic,
            "local_position",
            now_monotonic,
            MAX_TELEMETRY_SAMPLE_AGE_SEC,
        )
        if self._local_pos and local_position_fresh:
            lp = self._local_pos
            data["local_position"] = [float(lp.x), float(lp.y), float(lp.z)]
            data["local_velocity"] = [float(lp.vx), float(lp.vy), float(lp.vz)]
            # Use the exact frame consumed by TrajectorySetpoint. VehicleOdometry
            # may advertise a different pose frame (for example FRD).
            local_ned = valid_vehicle_local_ned(lp)
            data["local_position_valid"] = local_ned is not None
            if local_ned is not None:
                data["position"] = local_ned
        else:
            data["local_position_valid"] = False

        global_position_fresh = ros_sample_is_fresh(
            self._ros_last_monotonic,
            "global_position",
            now_monotonic,
            MAX_TELEMETRY_SAMPLE_AGE_SEC,
        )
        if self._global_pos and global_position_fresh:
            gp = self._global_pos
            # PX4 v1.17: lat / lon / alt / lat_lon_valid
            gps_lat = float(gp.lat)
            gps_lon = float(gp.lon)
            gps_alt = float(gp.alt)
            data["gps_lat"] = gps_lat
            data["gps_lon"] = gps_lon
            data["gps_alt"] = gps_alt
            geographic_sample_skew = abs(
                self._ros_last_monotonic.get("global_position", float("-inf"))
                - self._ros_last_monotonic.get("local_position", float("inf"))
            )
            data["gps_fix"] = (
                bool(gp.lat_lon_valid)
                and bool(getattr(gp, "alt_valid", True))
                and local_position_fresh
                and geographic_sample_skew <= MAX_GEOGRAPHIC_SAMPLE_SKEW_SEC
                and all(math.isfinite(value) for value in (gps_lat, gps_lon, gps_alt))
                and -90.0 <= gps_lat <= 90.0
                and -180.0 <= gps_lon <= 180.0
            )
        else:
            data["gps_fix"] = False

        if self._battery:
            data["battery"] = (
                int(self._battery.remaining * 100)
                if self._battery.remaining >= 0
                else -1
            )

        # 应用层 ACK：只有达到确认阈值且 setpoint 已写入缓存后才回传。
        # 后端据此能区分“UDP 已发出”和“Jetson 已确认执行”。
        if self._last_applied_command:
            data["control_ack"] = dict(self._last_applied_command)

        try:
            payload = yaml.dump(
                data, sort_keys=False, default_flow_style=None
            ).encode("utf-8")
            sent = self._tel_sock.sendto(payload, self._backend_addr)
            self._telemetry_sent += 1
            self._telemetry_last_bytes = sent
            if self._telemetry_sent == 1:
                self.get_logger().info(
                    f"[UDP-TX] first telemetry sent: {sent}B → "
                    f"{self._backend_addr[0]}:{self._backend_addr[1]}, "
                    f"fields={list(data.keys())}"
                )
        except Exception as e:
            self._telemetry_send_errors += 1
            self.get_logger().error(
                f"[UDP-TX] telemetry send failed #{self._telemetry_send_errors} "
                f"to {self._backend_addr[0]}:{self._backend_addr[1]}: "
                f"{type(e).__name__}: {e}"
            )

    # ------------------------------------------------------------------
    # UDP 控制包接收（主线程轮询调用）
    # ------------------------------------------------------------------
    def process_control(self):
        try:
            data, addr = self._ctrl_sock.recvfrom(MAX_CONTROL_PACKET_BYTES + 1)
        except socket.timeout:
            return
        except OSError as exc:
            if not running:
                return
            self._udp_recv_errors += 1
            self.get_logger().error(
                f"[UDP-RX] recvfrom failed #{self._udp_recv_errors}: "
                f"{type(exc).__name__}: {exc}"
            )
            return

        self._udp_rx_total += 1
        try:
            parsed = parse_control_packet(data)
        except ValueError as exc:
            self._udp_rx_invalid += 1
            self.get_logger().warning(
                f"[UDP-RX] rejected packet #{self._udp_rx_total} from "
                f"{addr[0]}:{addr[1]}: {exc}; size={len(data)}B; "
                f"preview={data[:160]!r}"
            )
            return

        if parsed["slot"] != self.slot:
            self._udp_rx_invalid += 1
            self.get_logger().warning(
                f"[UDP-RX] rejected command_id={parsed['command_id']}: "
                f"JSON slot={parsed['slot']} but this bridge slot={self.slot}. "
                f"Check backend port_map; packet came from {addr[0]}:{addr[1]}."
            )
            return

        if not self._accept_backend_session(parsed):
            return

        now_monotonic = time.monotonic()
        if self._last_ctrl_sender is not None and addr != self._last_ctrl_sender:
            self.get_logger().warning(
                f"[UDP-RX] control sender changed: "
                f"{self._last_ctrl_sender[0]}:{self._last_ctrl_sender[1]} → "
                f"{addr[0]}:{addr[1]}"
            )
        self._last_ctrl_sender = addr

        # UDP sendto() 在错误目标 IP 时通常也会返回成功，单靠
        # telemetry_sent 不能证明后端收到了包。已通过协议校验的控制包
        # 来自真实后端时，以其源 IP 自愈遥测回传目标，端口仍固定为本 slot 的
        # telemetry port（slot 1 为 8888）。这样后端重启或切换局域网后，
        # 手动执行 fresh 即可让 Jetson 重新发现后端。
        sender_backend_addr = (addr[0], self._tel_port)
        if sender_backend_addr != self._backend_addr:
            previous_backend_addr = self._backend_addr
            self._backend_addr = sender_backend_addr
            self._route_local_ip = self._detect_route_local_ip()
            self.get_logger().warning(
                f"[UDP-TX] telemetry target corrected from "
                f"{previous_backend_addr[0]}:{previous_backend_addr[1]} to "
                f"{self._backend_addr[0]}:{self._backend_addr[1]} based on "
                f"validated control sender"
            )
        self._last_ctrl_monotonic = now_monotonic
        self._last_backend_timestamp = parsed["sent_at"]
        self._udp_rx_valid += 1
        if parsed["mode"] == "hold":
            self._udp_rx_hold += 1
        else:
            self._udp_rx_move += 1

        clock_delta = time.time() - parsed["sent_at"]
        if (
            abs(clock_delta) > 10.0
            and now_monotonic - self._last_clock_warning_monotonic >= 30.0
        ):
            self.get_logger().warning(
                f"[CLOCK] backend timestamp differs from Jetson by "
                f"{clock_delta:+.3f}s. Check NTP/time sync; packet is still accepted."
            )
            self._last_clock_warning_monotonic = now_monotonic

        # 每个 move 重发包都打印；持续 hold 降采样，避免长时间运行刷屏。
        should_log = (
            parsed["mode"] == "move"
            or self._udp_rx_valid == 1
            or self._udp_rx_hold % 25 == 0
        )
        if should_log:
            self.get_logger().info(
                f"[UDP-RX] valid #{self._udp_rx_valid} id={parsed['command_id']} "
                f"seq={parsed['sequence']} repeat={parsed['repeat_index']}/"
                f"{parsed['repeat_total'] or 'continuous'} from {addr[0]}:{addr[1]} "
                f"mode={parsed['mode']} "
                f"NED=({parsed['x']:.3f},{parsed['y']:.3f},{parsed['z']:.3f}) "
                f"reference=power_on_origin unit=m clock_delta={clock_delta:+.3f}s"
            )

        self._stage_control_command(parsed, addr, now_monotonic)

    def _accept_backend_session(self, parsed: dict) -> bool:
        """识别后端重启会话，并阻止旧会话的迟到包重新生效。"""
        session_id = parsed["session_id"]
        if self._active_backend_session is None:
            self._active_backend_session = session_id
            self.get_logger().info(
                f"[SESSION] backend session established: {session_id}"
            )
            return True
        if session_id == self._active_backend_session:
            return True
        if session_id in self._retired_backend_sessions:
            self._udp_rx_stale += 1
            self.get_logger().warning(
                f"[SESSION] rejected packet from retired backend session "
                f"{session_id}; active={self._active_backend_session}"
            )
            return False

        old_session = self._active_backend_session
        self._retired_backend_sessions.add(old_session)
        self._active_backend_session = session_id
        self._highest_applied_sequence = 0
        self._pending_commands.clear()
        self._applied_command_ids.clear()
        self.get_logger().warning(
            f"[SESSION] backend session changed {old_session} → {session_id}; "
            f"pending commands cleared and sequence ordering restarted"
        )
        return True

    @staticmethod
    def _control_fingerprint(parsed: dict):
        """同一 command_id 的所有重发包必须具有完全一致的执行语义。"""
        return (
            parsed["session_id"], parsed["command_id"], parsed["sequence"],
            parsed["drone_id"], parsed["slot"], parsed["mode"],
            parsed["x"], parsed["y"], parsed["z"],
        )

    def _stage_control_command(self, parsed: dict, addr, now_monotonic: float):
        command_id = parsed["command_id"]
        sequence = parsed["sequence"]

        if command_id in self._applied_command_ids:
            self._udp_rx_duplicate += 1
            return
        if sequence <= self._highest_applied_sequence:
            self._udp_rx_stale += 1
            self.get_logger().warning(
                f"[COMMAND-STALE] rejected id={command_id} sequence={sequence}; "
                f"highest_applied={self._highest_applied_sequence}"
            )
            return

        pending = self._pending_commands.get(command_id)
        fingerprint = self._control_fingerprint(parsed)
        if pending and pending["fingerprint"] != fingerprint:
            self._udp_rx_invalid += 1
            del self._pending_commands[command_id]
            self.get_logger().error(
                f"[COMMAND-CONFLICT] same command_id={command_id} carried "
                f"different payloads; entire confirmation group discarded"
            )
            return

        if pending and now_monotonic - pending["first_seen"] > COMMAND_CONFIRM_WINDOW_SEC:
            self.get_logger().warning(
                f"[COMMAND-WINDOW] id={command_id} did not reach "
                f"{COMMAND_CONFIRM_COUNT} packets within "
                f"{COMMAND_CONFIRM_WINDOW_SEC:.2f}s; restarting window"
            )
            pending = None

        if pending is None:
            pending = {
                "first_seen": now_monotonic,
                "last_seen": now_monotonic,
                "repeat_indices": set(),
                "fingerprint": fingerprint,
                "packet": parsed,
                "sender": addr,
            }
            self._pending_commands[command_id] = pending

        repeat_index = parsed["repeat_index"]
        if repeat_index in pending["repeat_indices"]:
            self._udp_rx_duplicate += 1
            return

        pending["repeat_indices"].add(repeat_index)
        pending["last_seen"] = now_monotonic
        confirmed = len(pending["repeat_indices"])
        self.get_logger().info(
            f"[COMMAND-PENDING] id={command_id} sequence={sequence} "
            f"unique={confirmed}/{COMMAND_CONFIRM_COUNT} "
            f"indices={sorted(pending['repeat_indices'])} "
            f"age={now_monotonic - pending['first_seen']:.3f}s"
        )

        if parsed["repeat_total"] and parsed["repeat_total"] < COMMAND_CONFIRM_COUNT:
            self.get_logger().warning(
                f"[COMMAND-POLICY] backend repeat_total={parsed['repeat_total']} "
                f"is lower than Jetson threshold={COMMAND_CONFIRM_COUNT}"
            )

        if confirmed >= COMMAND_CONFIRM_COUNT:
            self._apply_control_command(parsed, confirmed, now_monotonic)

    def _apply_control_command(
        self, parsed: dict, confirmed_packets: int, now_monotonic: float
    ):
        sequence = parsed["sequence"]
        if sequence <= self._highest_applied_sequence:
            self._udp_rx_stale += 1
            return

        # JSON 中已经是 PX4 所需的 NED 米坐标，原点为本次上电位置。
        # Jetson 只透传到 TrajectorySetpoint，不做 UE/NED 二次转换。
        with self._setpoint_lock:
            if self._last_setpoint is None:
                self.get_logger().warning(
                    f"[COMMAND-SAFETY] deferred id={parsed['command_id']} "
                    f"sequence={sequence}: waiting for first valid "
                    f"VehicleLocalPosition"
                )
                return False
            self._last_setpoint = {
                "x": parsed["x"],
                "y": parsed["y"],
                "z": parsed["z"],
                "mode": parsed["mode"],
                "sequence": sequence,
            }

        self._highest_applied_sequence = sequence
        self._commands_applied += 1
        self._applied_command_ids[parsed["command_id"]] = now_monotonic
        self._last_applied_command = {
            "session_id": parsed["session_id"],
            "command_id": parsed["command_id"],
            "sequence": sequence,
            "mode": parsed["mode"],
            "confirmed_packets": confirmed_packets,
            "applied_at_unix_s": time.time(),
        }

        for command_id, pending in list(self._pending_commands.items()):
            if pending["packet"]["sequence"] <= sequence:
                del self._pending_commands[command_id]
        self._prune_applied_commands(now_monotonic)

        self.get_logger().info(
            f"[COMMAND-EXECUTE] #{self._commands_applied} "
            f"id={parsed['command_id']} sequence={sequence} mode={parsed['mode']} "
            f"confirmed={confirmed_packets} NED(m, power_on_origin)="
            f"({parsed['x']:.3f},{parsed['y']:.3f},{parsed['z']:.3f})"
        )
        return True

    def _prune_applied_commands(self, now_monotonic: float):
        for command_id, applied_at in list(self._applied_command_ids.items()):
            if now_monotonic - applied_at > 300.0:
                del self._applied_command_ids[command_id]
        while len(self._applied_command_ids) > 256:
            oldest = next(iter(self._applied_command_ids))
            del self._applied_command_ids[oldest]

    # ------------------------------------------------------------------
    # 发送 VehicleCommand 辅助函数
    # ------------------------------------------------------------------
    def _send_vehicle_command(
        self,
        command: int,
        param1: float = 0.0,
        param2: float = 0.0,
        param3: float = 0.0,
        param4: float = 0.0,
        param5: float = 0.0,
        param6: float = 0.0,
        param7: float = 0.0,
    ):
        cmd = VehicleCommand()
        cmd.timestamp = int(self.get_clock().now().nanoseconds / 1000)
        cmd.command = command
        cmd.param1 = param1
        cmd.param2 = param2
        cmd.param3 = param3
        cmd.param4 = param4
        cmd.param5 = param5
        cmd.param6 = param6
        cmd.param7 = param7
        cmd.target_system = self._get_system_id()
        cmd.target_component = 1
        cmd.source_system = 255
        cmd.source_component = 0
        cmd.from_external = True
        self._cmd_pub.publish(cmd)
        self._vehicle_command_count += 1
        self.get_logger().info(
            f"[ROS-TX] VehicleCommand #{self._vehicle_command_count}: "
            f"command={command}, target_system={cmd.target_system}, "
            f"target_component={cmd.target_component}, "
            f"param1={param1}, param2={param2}"
        )

    def _get_system_id(self) -> int:
        """Return the configured PX4 MAVLink system ID for this bridge."""
        return self._mavlink_system_id

    def _detect_route_local_ip(self):
        """返回访问后端时内核选择的 Jetson 本地 IP，不发送任何数据。"""
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            probe.connect((BACKEND_HOST, self._tel_port))
            return probe.getsockname()[0]
        except OSError as exc:
            self.get_logger().warning(
                f"[NETWORK-CHECK] cannot determine route to {BACKEND_HOST}: "
                f"{type(exc).__name__}: {exc}"
            )
            return None
        finally:
            probe.close()

    def _log_diagnostics(self):
        now = time.monotonic()
        for command_id, pending in list(self._pending_commands.items()):
            if now - pending["last_seen"] > COMMAND_CONFIRM_WINDOW_SEC * 2:
                del self._pending_commands[command_id]
        uptime = now - self._started_monotonic
        ctrl_age = (
            f"{now - self._last_ctrl_monotonic:.1f}s"
            if self._last_ctrl_monotonic is not None else "never"
        )
        sender = (
            f"{self._last_ctrl_sender[0]}:{self._last_ctrl_sender[1]}"
            if self._last_ctrl_sender else "none"
        )
        with self._setpoint_lock:
            sp = (
                dict(self._last_setpoint)
                if self._last_setpoint is not None
                else None
            )
        sp_text = (
            f"({sp['x']:.2f},{sp['y']:.2f},{sp['z']:.2f},"
            f"{sp['mode']},seq={sp['sequence']})"
            if sp is not None
            else "waiting_for_valid_local_position"
        )

        status_text = "none"
        if self._status is not None:
            status_text = (
                f"armed={int(self._status.arming_state)},"
                f"nav={int(self._status.nav_state)}"
            )

        pub_links = (
            self._offboard_pub.get_subscription_count(),
            self._traj_pub.get_subscription_count(),
            self._cmd_pub.get_subscription_count(),
        )
        seen_topics = ",".join(
            name for name, count in self._ros_rx_counts.items() if count > 0
        ) or "none"

        self.get_logger().info(
            f"[DIAG] up={uptime:.0f}s | UDP control total/valid/invalid="
            f"{self._udp_rx_total}/{self._udp_rx_valid}/{self._udp_rx_invalid} "
            f"hold/move={self._udp_rx_hold}/{self._udp_rx_move} "
            f"duplicate/stale={self._udp_rx_duplicate}/{self._udp_rx_stale} "
            f"last_age={ctrl_age} sender={sender} | "
            f"confirmed applied/pending={self._commands_applied}/"
            f"{len(self._pending_commands)} highest_seq={self._highest_applied_sequence} | "
            f"setpoint={sp_text} | "
            f"ROS pub_subscribers ocm/traj/cmd={pub_links[0]}/{pub_links[1]}/{pub_links[2]} "
            f"published={self._offboard_publish_count} PX4={status_text} "
            f"ACK={self._command_ack_count} ROS_RX={seen_topics} | "
            f"telemetry target={self._backend_addr[0]}:{self._backend_addr[1]} "
            f"sent/errors/last_bytes="
            f"{self._telemetry_sent}/{self._telemetry_send_errors}/{self._telemetry_last_bytes}"
        )

        # 遥测能发而控制一直收不到，正是本次外场出现的单向链路症状。
        if (
            uptime >= 10.0
            and self._telemetry_sent > 0
            and self._udp_rx_valid == 0
            and now - self._last_no_control_warning_monotonic >= 30.0
        ):
            self.get_logger().warning(
                f"[ONE-WAY-LINK] telemetry is being sent to backend, but no valid "
                f"control packet has arrived on {CONTROL_BIND_HOST}:{self._ctrl_port}. "
                f"Verify backend config jetson.host={self._route_local_ip or '<Jetson IP>'}, "
                f"send_port={self._ctrl_port}, backend heartbeat sent_count, "
                f"and Jetson firewall/route."
            )
            self._last_no_control_warning_monotonic = now

        if (
            self._offboard_publish_count > OFFBOARD_HZ * 2
            and min(pub_links) == 0
            and now - self._last_ros_link_warning_monotonic >= 30.0
        ):
            self.get_logger().warning(
                f"[ROS-LINK] at least one PX4 input publisher has 0 subscribers "
                f"(ocm/traj/cmd={pub_links}). Check MicroXRCEAgent and "
                f"ROS_TOPIC_PREFIX={self._topic_prefix or '<none>'}."
            )
            self._last_ros_link_warning_monotonic = now

    # ------------------------------------------------------------------
    # 清理
    # ------------------------------------------------------------------
    def cleanup(self):
        try:
            self._ctrl_sock.close()
        except OSError:
            pass
        try:
            self._tel_sock.close()
        except OSError:
            pass
        self.get_logger().info(f"[slot {self.slot}] Bridge shutdown")


# ============================================================
# 入口
# ============================================================
def main(args=None):
    global running

    rclpy.init(args=args)

    # 支持两种传参方式：
    #   ros2 run jetson_bridge jetson_bridge --ros-args -p slot:=2
    #   python3 jetson_bridge.py 2
    _tmp_node = rclpy.create_node("_slot_reader")
    _tmp_node.declare_parameter("slot", 1)
    slot = _tmp_node.get_parameter("slot").get_parameter_value().integer_value
    _tmp_node.destroy_node()

    # 兼容直接 python3 jetson_bridge.py 2 的用法
    if slot == 1 and len(sys.argv) > 1:
        try:
            slot = int(sys.argv[1])
        except ValueError:
            pass

    bridge = JetsonBridge(slot)

    # ROS2 spin 在独立线程，主线程做 UDP 控制包轮询
    spin_thread = threading.Thread(
        target=rclpy.spin, args=(bridge,), daemon=True
    )
    spin_thread.start()

    def _shutdown(sig, frame):
        global running
        running = False
        # 退出时发送 DISARM 指令，防止电机继续转
        bridge.get_logger().info("Shutting down: sending DISARM...")
        bridge._send_vehicle_command(
            VehicleCommand.VEHICLE_CMD_COMPONENT_ARM_DISARM,
            param1=0.0,
        )

    signal.signal(signal.SIGINT, _shutdown)
    signal.signal(signal.SIGTERM, _shutdown)

    bridge.get_logger().info("UDP control receive loop started...")
    bridge.get_logger().info("Waiting for heartbeat warmup (~1s)...")

    # 独立线程等待键盘输入，避免阻塞主循环
    def _wait_for_arm():
        import os
        # 等预热完成
        while running and bridge._warmup_count < bridge._warmup_needed:
            time.sleep(0.1)
        if not running:
            return
        # 环境变量 ARM_NOW=1 跳过确认，直接解锁
        if os.environ.get("ARM_NOW") == "1":
            print("\n>>> ARM_NOW=1 detected, skipping confirmation.")
        else:
            sys.stdout.write("\n>>> Heartbeat ready. Press ENTER to ARM + OFFBOARD (Ctrl+C to abort): ")
            sys.stdout.flush()
            sys.stdin.readline()
        print(">>> ARM + OFFBOARD triggered. Motor should spin soon.")
        print(">>> Press Ctrl+C to DISARM immediately.\n")
        bridge._arm_triggered = True

    arm_thread = threading.Thread(target=_wait_for_arm, daemon=True)
    arm_thread.start()

    while running and rclpy.ok():
        bridge.process_control()

    bridge.cleanup()
    rclpy.shutdown()


if __name__ == "__main__":
    main()
