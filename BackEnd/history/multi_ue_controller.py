#!/usr/bin/env python3
"""
多无人机主控制器
功能：接收UE5的多机指令，解析选择掩码，转发到各单机桥接进程

架构：
  UE5 仿真环境
    ↓ UDP 32字节数据包 (端口8899，包含选择掩码)
  多机主控制器 (multi_ue_controller.py)
    ↓ 内部转发到各无人机端口 (8889, 8891, 8893)
  单机控制桥接 (ue_to_px4_bridge.py)
    ↓ ROS2 话题
  PX4 真实无人机 (px4_1, px4_2, px4_3)
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
from typing import Optional, Dict, Any, List, Tuple
from datetime import datetime

# 导入本地消息包（用于数据包解析）
try:
    from ue_px4_msgs import MultiUEDataPacket, UEDataPacket
except ImportError:
    print("[ERROR] 未找到ue_px4_msgs包，请确保该包在Python路径中")
    sys.exit(1)

# ==================== 配置 ====================

# 默认多机配置（可通过配置覆盖）
DEFAULT_LISTEN_PORT = 8899
DEFAULT_BUFFER_SIZE = 1024

# 默认无人机配置
DEFAULT_DRONES = [
    {
        'name': 'UAV1',
        'drone_id': 1,
        'topic_prefix': '/px4_1',
        'control_port': 8889,  # 发送到单机桥接的端口
    },
    {
        'name': 'UAV2',
        'drone_id': 2,
        'topic_prefix': '/px4_2',
        'control_port': 8891,
    },
    {
        'name': 'UAV3',
        'drone_id': 3,
        'topic_prefix': '/px4_3',
        'control_port': 8893,
    }
]

# 安全限制
SAFETY_LIMITS = {
    'max_altitude': 100.0,       # 最大高度 (米)
    'max_horizontal_distance': 500.0,  # 最大水平距离 (米)
    'min_altitude': 1.0,         # 最小高度 (米)
}

# 控制频率限制
FORWARD_RATE_LIMIT = 100  # Hz

# ==================== 无人机状态管理 ====================

class DroneStatus:
    """单个无人机状态管理"""

    def __init__(self, config: Dict[str, Any]):
        self.name = config['name']
        self.drone_id = config['drone_id']
        self.topic_prefix = config['topic_prefix']
        self.control_port = config['control_port']

        # 选择状态 (由位掩码控制)
        self.selected = False

        # UDP转发套接字 (懒加载)
        self.forward_socket = None
        self.forward_host = '127.0.0.1'  # 假设单机桥接在本机运行

        # 统计信息
        self.packets_forwarded = 0
        self.last_forward_time = 0

    def send_command(self, x: float, y: float, z: float, mode: int) -> bool:
        """
        发送命令到单机桥接

        Args:
            x, y, z: NED坐标 (米)
            mode: 控制模式 (0=悬停, 1=移动)

        Returns:
            发送是否成功
        """
        try:
            if self.forward_socket is None:
                self.forward_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

            # 打包为24字节单机数据包
            data = struct.pack('<dfffI', time.time(), x, y, z, mode)

            self.forward_socket.sendto(data, (self.forward_host, self.control_port))
            self.packets_forwarded += 1
            self.last_forward_time = time.time()
            return True
        except Exception as e:
            logging.error(f"[{self.name}] 发送命令失败: {e}")
            return False

    def get_stats(self) -> Dict[str, Any]:
        """获取统计信息"""
        return {
            'name': self.name,
            'drone_id': self.drone_id,
            'selected': self.selected,
            'control_port': self.control_port,
            'packets_forwarded': self.packets_forwarded,
        }


class MultiUEController:
    """多无人机主控制器"""

    def __init__(self, config: Dict[str, Any]):
        """
        初始化多机控制器

        Args:
            config: 配置字典
        """
        self.config = config

        # 无人机列表
        self.drones: List[DroneStatus] = []
        for drone_cfg in config.get('drones', DEFAULT_DRONES):
            self.drones.append(DroneStatus(drone_cfg))

        # 控制器配置
        self.listen_port = config.get('controller', {}).get('listen_port', DEFAULT_LISTEN_PORT)
        self.safety_limits = config.get('safety', SAFETY_LIMITS)

        # UDP接收套接字
        self.udp_socket = None
        self.running = False

        # 统计
        self.total_packets_received = 0
        self.packets_parsed = 0
        self.packets_forwarded = 0
        self.start_time = time.time()

        # 接收线程
        self.receiver_thread = None

        # 日志节流
        self._last_stats_log = 0

    def setup_udp_socket(self) -> bool:
        """设置UDP接收套接字"""
        try:
            self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.udp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.udp_socket.bind(('0.0.0.0', self.listen_port))
            self.udp_socket.settimeout(0.1)  # 100ms超时
            logging.info(f"多机控制器监听启动: 0.0.0.0:{self.listen_port}")
            return True
        except Exception as e:
            logging.error(f"UDP套接字设置失败: {e}")
            return False

    def parse_multi_drone_data(self, data: bytes) -> Optional[Dict[str, Any]]:
        """
        解析多无人机数据包

        支持两种格式:
        - 32字节完整格式 (包含drone_mask)
        - 24字节单机格式 (兼容，默认选择所有无人机)
        """
        # 尝试32字节格式
        if len(data) >= 32:
            try:
                timestamp, x, y, z, mode, drone_mask, sequence = struct.unpack('<dfffIII', data[:32])
                return {
                    'timestamp': timestamp,
                    'x': x,
                    'y': y,
                    'z': z,
                    'mode': int(mode),
                    'drone_mask': int(drone_mask),
                    'sequence': int(sequence),
                    'format': 'multi'
                }
            except struct.error:
                pass

        # 回退到24字节单机格式
        if len(data) >= 24:
            try:
                timestamp, x, y, z, mode = struct.unpack('<dfffI', data[:24])
                return {
                    'timestamp': timestamp,
                    'x': x,
                    'y': y,
                    'z': z,
                    'mode': int(mode),
                    'drone_mask': 0x07,  # 默认选择所有3架
                    'sequence': 0,
                    'format': 'single'
                }
            except struct.error:
                pass

        return None

    def update_selection_from_mask(self, drone_mask: int):
        """
        根据位掩码更新无人机选择状态

        位掩码编码:
        位0 (0x01): 选择无人机ID 1 (px4_1)
        位1 (0x02): 选择无人机ID 2 (px4_2)
        位2 (0x04): 选择无人机ID 3 (px4_3)
        """
        for drone in self.drones:
            bit_position = drone.drone_id - 1
            drone.selected = (drone_mask >> bit_position) & 0x01 == 1

    def apply_safety_limits(self, x: float, y: float, z: float) -> Tuple[float, float, float]:
        """应用安全限制"""
        # TODO: 实现水平距离限制
        max_alt = self.safety_limits.get('max_altitude', 100.0)
        min_alt = self.safety_limits.get('min_altitude', 1.0)

        # NED Z向下为正，所以高度是 -z
        alt = -z
        if alt < min_alt:
            z = -min_alt
        elif alt > max_alt:
            z = -max_alt

        return x, y, z

    def process_and_forward(self, parsed: Dict[str, Any]):
        """
        处理并转发数据包到选中的无人机

        Args:
            parsed: 解析后的数据包字典
        """
        self.packets_parsed += 1

        # 更新选择状态
        self.update_selection_from_mask(parsed['drone_mask'])

        # 坐标转换: UE5厘米 -> NED米
        x_cm, y_cm, z_cm = parsed['x'], parsed['y'], parsed['z']
        ned_x = x_cm * 0.01   # 前 -> 北
        ned_y = y_cm * 0.01   # 右 -> 东
        ned_z = -z_cm * 0.01  # 上 -> 下

        # 应用安全限制
        ned_x, ned_y, ned_z = self.apply_safety_limits(ned_x, ned_y, ned_z)

        # 广播给选中的无人机
        selected_count = 0
        for drone in self.drones:
            if drone.selected:
                success = drone.send_command(ned_x, ned_y, ned_z, parsed['mode'])
                if success:
                    self.packets_forwarded += 1
                    selected_count += 1

        # 日志（降低频率）
        if not hasattr(self, '_log_counter'):
            self._log_counter = 0
        self._log_counter += 1

        if self._log_counter % 100 == 0:
            selected_names = [d.name for d in self.drones if d.selected]
            logging.info(
                f"收到控制指令: pos=({x_cm:.0f},{y_cm:.0f},{z_cm:.0f})cm, "
                f"mode={parsed['mode']}, mask=0x{parsed['drone_mask']:02X}, "
                f"选中={selected_names}"
            )

    def receiver_loop(self):
        """接收循环"""
        logging.info("多机控制器接收线程启动")

        while self.running:
            try:
                data, addr = self.udp_socket.recvfrom(DEFAULT_BUFFER_SIZE)
                self.total_packets_received += 1

                # 解析数据包
                parsed = self.parse_multi_drone_data(data)
                if parsed is None:
                    logging.warning(f"无法解析数据包 (长度={len(data)})")
                    continue

                # 处理和转发
                self.process_and_forward(parsed)

            except socket.timeout:
                continue
            except Exception as e:
                logging.error(f"接收循环错误: {e}")

        logging.info("接收线程结束")

    def print_stats(self):
        """定期打印统计信息"""
        current_time = time.time()
        runtime = current_time - self.start_time

        stats_msgs = []
        stats_msgs.append(f"运行时间: {runtime/60:.1f}分钟")
        stats_msgs.append(f"接收包数: {self.total_packets_received}")
        stats_msgs.append(f"解析包数: {self.packets_parsed}")
        stats_msgs.append(f"转发包数: {self.packets_forwarded}")

        # 各无人机状态
        drone_stats = []
        for drone in self.drones:
            drone_stats.append(f"{drone.name}({drone.drone_id}): {'✓' if drone.selected else '✗'} {drone.packets_forwarded}包")
        stats_msgs.append(" | ".join(drone_stats))

        logging.info(" | ".join(stats_msgs))

    def start(self):
        """启动控制器"""
        if not self.setup_udp_socket():
            return False

        self.running = True
        self.receiver_thread = threading.Thread(target=self.receiver_loop, daemon=True)
        self.receiver_thread.start()

        logging.info(f"多机控制器已启动，监听端口: {self.listen_port}")
        logging.info(f"无人机配置: {[d.name for d in self.drones]}")
        return True

    def stop(self):
        """停止控制器"""
        self.running = False
        if self.receiver_thread and self.receiver_thread.is_alive():
            self.receiver_thread.join(timeout=2.0)
        if self.udp_socket:
            self.udp_socket.close()
        logging.info("多机控制器已停止")


# ==================== 命令行接口 ====================

def main():
    """主函数"""
    parser = argparse.ArgumentParser(description="多无人机主控制器")
    parser.add_argument('--config', type=str, default='multi_drone_config.yaml',
                       help='配置文件路径 (YAML格式)')
    parser.add_argument('--port', type=int, default=DEFAULT_LISTEN_PORT,
                       help=f'监听端口 (默认: {DEFAULT_LISTEN_PORT})')
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

    # 加载配置文件
    config = {}
    if os.path.exists(args.config):
        try:
            with open(args.config, 'r', encoding='utf-8') as f:
                config = yaml.safe_load(f)
            logging.info(f"[INFO] 已加载配置文件: {args.config}")
        except Exception as e:
            logging.error(f"[ERROR] 加载配置文件失败: {e}")
            return False
    else:
        logging.warning(f"[WARNING] 配置文件不存在: {args.config}, 使用默认配置")
        config = {}

    # 命令行参数覆盖配置
    config['controller'] = config.get('controller', {})
    config['controller']['listen_port'] = args.port

    # 创建控制器
    controller = MultiUEController(config)

    # 启动
    if not controller.start():
        logging.error("控制器启动失败")
        return False

    logging.info("=" * 60)
    logging.info("多机控制器运行中...")
    logging.info("按 Ctrl+C 停止")
    logging.info("=" * 60)

    # 定期打印统计
    try:
        while True:
            time.sleep(10)
            controller.print_stats()
    except KeyboardInterrupt:
        logging.info("收到中断信号")
    finally:
        controller.stop()

    return True


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
