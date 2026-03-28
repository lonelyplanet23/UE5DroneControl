"""
PX4消息定义 (纯Python实现)
对应 px4_msgs 包中的消息类型
"""

from dataclasses import dataclass, field
from typing import List, Dict, Any, Optional, Tuple
import time


# ==================== 里程计和位置消息 ====================

@dataclass
class VehicleOdometry:
    """车辆里程计数据 (对应 px4_msgs/msg/VehicleOdometry.msg)

    位置和姿态的里程计数据，符合ROS REP 147标准

    坐标系说明:
    - position: NED坐标系 (北东下)，单位: 米
    - q: 四元数 [w, x, y, z]，从FRD机身坐标系到参考系的旋转
    """
    timestamp: int = 0
    timestamp_sample: int = 0
    pose_frame: int = 1  # 1 = NED坐标系
    position: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    q: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0, 1.0])
    velocity_frame: int = 1  # 1 = NED坐标系
    velocity: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    angular_velocity: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    position_variance: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    orientation_variance: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    velocity_variance: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    reset_counter: int = 0
    quality: int = 0

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "VehicleOdometry":
        """从字典创建对象"""
        def to_floats(v, length):
            if v is None:
                return [0.0] * length
            try:
                return [float(x) for x in v][:length] + [0.0] * max(0, length - len(v))
            except Exception:
                return [0.0] * length

        return cls(
            timestamp=int(data.get('timestamp', 0) or 0),
            timestamp_sample=int(data.get('timestamp_sample', 0) or 0),
            pose_frame=int(data.get('pose_frame', 1) or 1),
            position=to_floats(data.get('position'), 3),
            q=to_floats(data.get('q'), 4),
            velocity_frame=int(data.get('velocity_frame', 1) or 1),
            velocity=to_floats(data.get('velocity'), 3),
            angular_velocity=to_floats(data.get('angular_velocity'), 3),
            position_variance=to_floats(data.get('position_variance'), 3),
            orientation_variance=to_floats(data.get('orientation_variance'), 3),
            velocity_variance=to_floats(data.get('velocity_variance'), 3),
            reset_counter=int(data.get('reset_counter', 0) or 0),
            quality=int(data.get('quality', 0) or 0)
        )

    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            'timestamp': self.timestamp,
            'timestamp_sample': self.timestamp_sample,
            'pose_frame': self.pose_frame,
            'position': self.position,
            'q': self.q,
            'velocity_frame': self.velocity_frame,
            'velocity': self.velocity,
            'angular_velocity': self.angular_velocity,
            'position_variance': self.position_variance,
            'orientation_variance': self.orientation_variance,
            'velocity_variance': self.velocity_variance,
            'reset_counter': self.reset_counter,
            'quality': self.quality,
        }


@dataclass
class VehicleGlobalPosition:
    """车辆全局位置 (对应 px4_msgs/msg/VehicleGlobalPosition.msg)

    基于WGS84椭球体的融合全局位置估计
    这是由位置估计器发布的（融合了GPS和其他传感器数据）
    """
    timestamp: int = 0
    timestamp_sample: int = 0
    lat: float = 0.0       # 纬度 (度)
    lon: float = 0.0       # 经度 (度)
    alt: float = 0.0       # 海拔高度 AMSL (米)
    alt_ellipsoid: float = 0.0  # 椭球体高度 (米)

    lat_lon_valid: bool = False
    alt_valid: bool = False

    delta_alt: float = 0.0       # 高度重置增量
    delta_terrain: float = 0.0   # 地形重置增量
    lat_lon_reset_counter: int = 0
    alt_reset_counter: int = 0
    terrain_reset_counter: int = 0

    eph: float = 0.0     # 水平位置误差标准差 (米)
    epv: float = 0.0     # 垂直位置误差标准差 (米)

    terrain_alt: float = 0.0       # WGS84地形海拔 (米)
    terrain_alt_valid: bool = False

    dead_reckoning: bool = False   # 是否通过航位推算得到

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "VehicleGlobalPosition":
        """从字典创建对象"""
        return cls(
            timestamp=int(data.get('timestamp', 0) or 0),
            timestamp_sample=int(data.get('timestamp_sample', 0) or 0),
            lat=float(data.get('lat', 0.0) or 0.0),
            lon=float(data.get('lon', 0.0) or 0.0),
            alt=float(data.get('alt', 0.0) or 0.0),
            alt_ellipsoid=float(data.get('alt_ellipsoid', 0.0) or 0.0),
            lat_lon_valid=bool(data.get('lat_lon_valid', False)),
            alt_valid=bool(data.get('alt_valid', False)),
            delta_alt=float(data.get('delta_alt', 0.0) or 0.0),
            delta_terrain=float(data.get('delta_terrain', 0.0) or 0.0),
            lat_lon_reset_counter=int(data.get('lat_lon_reset_counter', 0) or 0),
            alt_reset_counter=int(data.get('alt_reset_counter', 0) or 0),
            terrain_reset_counter=int(data.get('terrain_reset_counter', 0) or 0),
            eph=float(data.get('eph', 0.0) or 0.0),
            epv=float(data.get('epv', 0.0) or 0.0),
            terrain_alt=float(data.get('terrain_alt', 0.0) or 0.0),
            terrain_alt_valid=bool(data.get('terrain_alt_valid', False)),
            dead_reckoning=bool(data.get('dead_reckoning', False))
        )

    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            'timestamp': self.timestamp,
            'timestamp_sample': self.timestamp_sample,
            'lat': self.lat,
            'lon': self.lon,
            'alt': self.alt,
            'alt_ellipsoid': self.alt_ellipsoid,
            'lat_lon_valid': self.lat_lon_valid,
            'alt_valid': self.alt_valid,
            'delta_alt': self.delta_alt,
            'delta_terrain': self.delta_terrain,
            'lat_lon_reset_counter': self.lat_lon_reset_counter,
            'alt_reset_counter': self.alt_reset_counter,
            'terrain_reset_counter': self.terrain_reset_counter,
            'eph': self.eph,
            'epv': self.epv,
            'terrain_alt': self.terrain_alt,
            'terrain_alt_valid': self.terrain_alt_valid,
            'dead_reckoning': self.dead_reckoning,
        }


@dataclass
class VehicleStatus:
    """车辆状态 (对应 px4_msgs/msg/VehicleStatus.msg)"""
    timestamp: int = 0
    arming_state: int = 0  # 0=DISARMED, 1=ARMED, 2=STANDBY, 3=IN_AIR
    nav_state: int = 0     # 导航状态 (例如: 14=OFFBOARD)
    vehicle_type: int = 0
    # ... 其他字段可以根据需要扩展

    # 状态常量
    ARMED_STATE_DISARMED = 0
    ARMED_STATE_ARMED = 1
    ARMED_STATE_STANDBY = 2
    ARMED_STATE_IN_AIR = 3

    NAVIGATION_STATE_OFFBOARD = 14

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "VehicleStatus":
        return cls(
            timestamp=int(data.get('timestamp', 0) or 0),
            arming_state=int(data.get('arming_state', 0) or 0),
            nav_state=int(data.get('nav_state', 0) or 0),
            vehicle_type=int(data.get('vehicle_type', 0) or 0)
        )


# ==================== 控制消息 ====================

@dataclass
class OffboardControlMode:
    """Offboard控制模式消息 (对应 px4_msgs/msg/OffboardControlMode.msg)

    用于告知PX4飞控当前期望的控制模式
    PX4 v1.16要求持续接收此消息（至少2Hz）以维持OFFBOARD模式
    """
    timestamp: int = 0
    position: bool = False      # 位置控制使能
    velocity: bool = False      # 速度控制使能
    acceleration: bool = False  # 加速度控制使能
    attitude: bool = False      # 姿态控制使能
    body_rate: bool = False     # 机体角速率控制使能
    thrust_and_torque: bool = False  # 推力扭矩控制 (PX4 v1.16新增)
    direct_actuator: bool = False   # 直接作动器控制 (PX4 v1.16新增)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "OffboardControlMode":
        return cls(
            timestamp=int(data.get('timestamp', 0) or 0),
            position=bool(data.get('position', False)),
            velocity=bool(data.get('velocity', False)),
            acceleration=bool(data.get('acceleration', False)),
            attitude=bool(data.get('attitude', False)),
            body_rate=bool(data.get('body_rate', False)),
            thrust_and_torque=bool(data.get('thrust_and_torque', False)),
            direct_actuator=bool(data.get('direct_actuator', False))
        )

    def to_dict(self) -> Dict[str, Any]:
        return {
            'timestamp': self.timestamp,
            'position': self.position,
            'velocity': self.velocity,
            'acceleration': self.acceleration,
            'attitude': self.attitude,
            'body_rate': self.body_rate,
            'thrust_and_torque': self.thrust_and_torque,
            'direct_actuator': self.direct_actuator,
        }


@dataclass
class TrajectorySetpoint:
    """轨迹设定点 (对应 px4_msgs/msg/TrajectorySetpoint.msg)

    用于发送位置、速度、加速度和偏航角的设定点
    这是OFFBOARD模式下的主要控制接口
    """
    timestamp: int = 0
    position: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])  # NED, 米
    velocity: List[float] = field(default_factory=lambda: [float('nan'), float('nan'), float('nan')])
    acceleration: List[float] = field(default_factory=lambda: [float('nan'), float('nan'), float('nan')])
    yaw: float = float('nan')          # 偏航角 (弧度, 0=北)
    yawspeed: float = float('nan')     # 偏航角速度 (弧度/秒)

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "TrajectorySetpoint":
        def to_floats(v, length):
            if v is None:
                return [float('nan')] * length
            try:
                return [float(x) for x in v][:length] + [float('nan')] * max(0, length - len(v))
            except Exception:
                return [float('nan')] * length

        return cls(
            timestamp=int(data.get('timestamp', 0) or 0),
            position=to_floats(data.get('position'), 3),
            velocity=to_floats(data.get('velocity'), 3),
            acceleration=to_floats(data.get('acceleration'), 3),
            yaw=float(data.get('yaw', float('nan')) or float('nan')),
            yawspeed=float(data.get('yawspeed', float('nan')) or float('nan'))
        )

    def to_dict(self) -> Dict[str, Any]:
        return {
            'timestamp': self.timestamp,
            'position': self.position,
            'velocity': self.velocity,
            'acceleration': self.acceleration,
            'yaw': self.yaw,
            'yawspeed': self.yawspeed,
        }


@dataclass
class VehicleCommand:
    """车辆命令 (对应 px4_msgs/msg/VehicleCommand.msg)

    用于发送高 level 命令，如:
    - VEHICLE_CMD_DO_SET_MODE (176): 切换飞行模式
    - VEHICLE_CMD_COMPONENT_ARM_DISARM (400): 解锁/上锁
    - VEHICLE_CMD_NAV_LAND (85): 降落
    等
    """
    timestamp: int = 0
    command: int = 0
    param1: float = 0.0
    param2: float = 0.0
    param3: float = 0.0
    param4: float = 0.0
    param5: float = 0.0
    param6: float = 0.0
    param7: float = 0.0
    target_system: int = 0      # 目标系统ID (MAVLink system ID)
    target_component: int = 0   # 目标组件ID
    source_system: int = 0      # 源系统ID
    source_component: int = 0   # 源组件ID
    confirmation: int = 0       # 确认次数 (0=第一次)
    from_external: bool = False # 是否来自外部（如地面站）

    # 命令常量
    VEHICLE_CMD_DO_SET_MODE = 176
    VEHICLE_CMD_COMPONENT_ARM_DISARM = 400
    VEHICLE_CMD_NAV_LAND = 85

    # 模式参数
    PX4_CUSTOM_MAIN_MODE_OFFBOARD = 6.0

    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> "VehicleCommand":
        return cls(
            timestamp=int(data.get('timestamp', 0) or 0),
            command=int(data.get('command', 0) or 0),
            param1=float(data.get('param1', 0.0) or 0.0),
            param2=float(data.get('param2', 0.0) or 0.0),
            param3=float(data.get('param3', 0.0) or 0.0),
            param4=float(data.get('param4', 0.0) or 0.0),
            param5=float(data.get('param5', 0.0) or 0.0),
            param6=float(data.get('param6', 0.0) or 0.0),
            param7=float(data.get('param7', 0.0) or 0.0),
            target_system=int(data.get('target_system', 0) or 0),
            target_component=int(data.get('target_component', 1) or 1),
            source_system=int(data.get('source_system', 1) or 1),
            source_component=int(data.get('source_component', 1) or 1),
            confirmation=int(data.get('confirmation', 0) or 0),
            from_external=bool(data.get('from_external', True))
        )

    def to_dict(self) -> Dict[str, Any]:
        return {
            'timestamp': self.timestamp,
            'command': self.command,
            'param1': self.param1,
            'param2': self.param2,
            'param3': self.param3,
            'param4': self.param4,
            'param5': self.param5,
            'param6': self.param6,
            'param7': self.param7,
            'target_system': self.target_system,
            'target_component': self.target_component,
            'source_system': self.source_system,
            'source_component': self.source_component,
            'confirmation': self.confirmation,
            'from_external': self.from_external,
        }
