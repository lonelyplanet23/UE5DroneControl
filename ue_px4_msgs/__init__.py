# UE5端PX4消息定义
# 纯Python实现，无需ROS2环境
# 用于UE5与PX4通信的数据结构

from .messages import (
    VehicleOdometry,
    VehicleGlobalPosition,
    VehicleStatus,
    OffboardControlMode,
    TrajectorySetpoint,
    VehicleCommand,
)
from .utils import CoordinateConverter, GPSConverter
from .packets import UEDataPacket, MultiUEDataPacket

__all__ = [
    'VehicleOdometry',
    'VehicleGlobalPosition',
    'VehicleStatus',
    'OffboardControlMode',
    'TrajectorySetpoint',
    'VehicleCommand',
    'CoordinateConverter',
    'GPSConverter',
    'UEDataPacket',
    'MultiUEDataPacket',
]

__version__ = '1.0.0'
