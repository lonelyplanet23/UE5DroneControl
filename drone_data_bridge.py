"""
无人机数据接收与转发脚本 (Drone Data Bridge)
功能：
1. 连接到 UAV1 网络
2. 通过 SSH 连接到无人机
3. 启动 MicroXRCE Agent
4. 订阅 ROS 2 话题
5. 将数据转发到 UE5 引擎
"""

import os
import sys
import time
import json
import socket
import threading
import subprocess
import platform
import argparse
import yaml
from pathlib import Path
from typing import Optional, Dict, Any, Union, List
from datetime import datetime
from px4_msgs.vehicle_odometry import VehicleOdometry
import logging

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    handlers=[
        logging.FileHandler('drone_bridge.log', encoding='utf-8'),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)

# 修复 Windows GBK 编码问题
if platform.system() == "Windows":
    import io
    import sys
    import os
    # 设置控制台为 UTF-8，防止中文乱码
    os.system('chcp 65001 >nul')
    # 重新配置 stdout 以支持 UTF-8
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')

# ==================== 网络检测和连接部分 ====================

class WiFiManager:
    """WiFi 网络管理器"""

    def __init__(self):
        self.system = platform.system()
        self.is_connected = False
        self.target_ssid = "uav1"
        self.target_password = "12345678"

    def scan_networks(self) -> list:
        """扫描可用的 WiFi 网络"""
        try:
            if self.system == "Windows":
                return self._scan_windows()
            elif self.system == "Linux":
                return self._scan_linux()
            elif self.system == "Darwin":  # macOS
                return self._scan_macos()
        except Exception as e:
            logger.error(f"扫描网络失败: {e}")
            return []

    def _scan_windows(self) -> list:
        """Windows 网络扫描"""
        try:
            result = subprocess.run(
                ["netsh", "wlan", "show", "networks", "mode=Bssid"],
                capture_output=True,
                text=True,
                encoding='utf-8',
                errors='replace',
                timeout=5
            )
            networks = []
            for line in result.stdout.split('\n'):
                if 'SSID' in line and ':' in line:
                    ssid = line.split(':', 1)[1].strip()
                    if ssid:
                        networks.append(ssid)
            return networks
        except Exception as e:
            logger.error(f"Windows 网络扫描失败: {e}")
            return []

    def _scan_linux(self) -> list:
        """Linux 网络扫描"""
        try:
            result = subprocess.run(
                ["nmcli", "dev", "wifi", "list"],
                capture_output=True,
                text=True,
                encoding='utf-8',
                errors='replace',
                timeout=5
            )
            networks = []
            for line in result.stdout.split('\n')[1:]:  # 跳过标题行
                parts = line.split()
                if len(parts) > 1:
                    networks.append(parts[1])
            return networks
        except Exception as e:
            logger.error(f"Linux 网络扫描失败: {e}")
            return []

    def _scan_macos(self) -> list:
        """macOS 网络扫描"""
        try:
            result = subprocess.run(
                ["/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport", "-s"],
                capture_output=True,
                text=True,
                encoding='utf-8',
                errors='replace',
                timeout=5
            )
            networks = []
            for line in result.stdout.split('\n')[1:]:
                parts = line.split()
                if len(parts) > 0:
                    networks.append(parts[0])
            return networks
        except Exception as e:
            logger.error(f"macOS 网络扫描失败: {e}")
            return []

    def check_uav1_network(self) -> bool:
        """检查 UAV1 网络是否可用"""
        networks = self.scan_networks()
        return self.target_ssid in networks

    def connect_to_uav1(self, max_attempts: int = 60, check_interval: int = 2) -> bool:
        """
        连接到 UAV1 网络
        
        Args:
            max_attempts: 最大尝试次数
            check_interval: 检查间隔（秒）
        
        Returns:
            是否成功连接
        """
        attempt = 0
        while attempt < max_attempts:
            logger.info(f"[{attempt + 1}/{max_attempts}] 检查 UAV1 网络...")

            if self.check_uav1_network():
                logger.info("[OK] 发现 UAV1 网络，开始连接...")
                if self._do_connect():
                    logger.info("[OK] 成功连接到 UAV1 网络!")
                    self.is_connected = True
                    return True
                else:
                    logger.warning("[ERROR] 连接失败，重试...")

            attempt += 1
            if attempt < max_attempts:
                time.sleep(check_interval)

        logger.error("[ERROR] 无法连接到 UAV1 网络")
        return False

    def _do_connect(self) -> bool:
        """执行实际的网络连接"""
        try:
            if self.system == "Windows":
                # Windows 连接指令
                cmd = f'netsh wlan connect name="{self.target_ssid}"'
                result = subprocess.run(cmd, shell=True, capture_output=True, timeout=10)
                time.sleep(3)  # 等待连接建立
                return self._test_connection()

            elif self.system == "Linux":
                # Linux 连接指令（使用 nmcli）
                cmd = f'nmcli device wifi connect "{self.target_ssid}" password "{self.target_password}"'
                result = subprocess.run(cmd, shell=True, capture_output=True, timeout=10)
                time.sleep(3)
                return self._test_connection()

            elif self.system == "Darwin":
                # macOS 连接指令
                cmd = f'/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport -z && ' \
                      f'/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport -A ' \
                      f'"{self.target_ssid}" "{self.target_password}"'
                result = subprocess.run(cmd, shell=True, capture_output=True, timeout=10)
                time.sleep(3)
                return self._test_connection()

        except Exception as e:
            logger.error(f"连接失败: {e}")
            return False

        return False

    def _test_connection(self) -> bool:
        """测试网络连接"""
        try:
            result = subprocess.run(
                ["ping", "-c" if self.system != "Windows" else "-n", "1", "192.168.10.1"],
                capture_output=True,
                timeout=5
            )
            return result.returncode == 0
        except Exception as e:
            logger.error(f"网络测试失败: {e}")
            return False


# ==================== SSH 连接部分 ====================

class SSHManager:
    """SSH 连接管理器"""

    def __init__(self, host: str = "192.168.10.1", user: str = "jetson1", password: str = "123456"):
        self.host = host
        self.user = user
        self.password = password
        self.ssh_process = None

    def test_connection(self) -> bool:
        """测试 SSH 连接"""
        try:
            result = subprocess.run(
                ["ping", "-c" if platform.system() != "Windows" else "-n", "1", self.host],
                capture_output=True,
                timeout=5
            )
            return result.returncode == 0
        except Exception as e:
            logger.error(f"无法 ping 到 {self.host}: {e}")
            return False

    def open_microxrce_agent_terminal(self) -> bool:
        """
        打开新的 CMD/Terminal 窗口运行 MicroXRCE Agent
        自动执行：等待 → SSH 连接 → MicroXRCEAgent 命令
        """
        try:
            import tempfile
            
            if platform.system() == "Windows":
                # Windows: 创建批处理脚本
                with tempfile.NamedTemporaryFile(mode='w', suffix='.bat', delete=False, encoding='utf-8') as f:
                    # 构建远程命令
                    remote_cmd = "MicroXRCEAgent serial --dev /dev/ttyTHS1 -b 921600"

                    lines = [
                        "@echo off",
                        "chcp 65001 >nul",
                        "echo [终端1] 等待3秒后执行SSH连接...",
                        "timeout /t 3 /nobreak",
                        f"echo [终端1] 正在连接到 {self.host}...",
                        "echo [终端1] 执行命令: MicroXRCEAgent serial --dev /dev/ttyTHS1 -b 921600",
                        "echo.",
                        "echo [重要] 如果提示输入密码，请输入: 123456",
                        "echo.",
                        f'ssh -o StrictHostKeyChecking=no {self.user}@{self.host} "{remote_cmd}"',
                        "echo.",
                        "echo [终端1] 命令已执行",
                        "pause"
                    ]
                    f.write('\n'.join(lines))
                    script_path = f.name
                
                cmd = f'start cmd /k "{script_path}"'
                subprocess.Popen(cmd, shell=True)
                logger.info("[终端1] 已打开新窗口，将自动执行: SSH → MicroXRCE Agent")
                return True

            elif platform.system() == "Linux":
                # Linux: gnome-terminal
                cmd = f'gnome-terminal -- bash -c "echo \'[终端1] 等待3秒后执行SSH连接...\'; sleep 3; sshpass -p \'{self.password}\' ssh -o StrictHostKeyChecking=no {self.user}@{self.host} \'MicroXRCEAgent serial --dev /dev/ttyTHS1 -b 921600\'; bash"'
                subprocess.Popen(cmd, shell=True)
                logger.info("[终端1] 已打开新窗口，将自动执行: SSH → MicroXRCE Agent")
                return True

            elif platform.system() == "Darwin":
                # macOS: Terminal
                with tempfile.NamedTemporaryFile(mode='w', suffix='.sh', delete=False, encoding='utf-8') as f:
                    lines = [
                        "#!/bin/bash",
                        'echo "[终端1] 等待3秒后执行SSH连接..."',
                        "sleep 3",
                        f'echo "[终端1] 正在连接到 {self.host}..."',
                        f'echo "{self.password}" | ssh -o StrictHostKeyChecking=no {self.user}@{self.host} \'MicroXRCEAgent serial --dev /dev/ttyTHS1 -b 921600\'',
                        'echo "[终端1] 命令已执行"'
                    ]
                    f.write('\n'.join(lines))
                    script_path = f.name
                os.chmod(script_path, 0o755)
                
                cmd = f'open -a Terminal {script_path}'
                subprocess.Popen(cmd, shell=True)
                logger.info("[终端1] 已打开新窗口，将自动执行: SSH → MicroXRCE Agent")
                return True

        except Exception as e:
            logger.error(f"[ERROR] 打开终端1 失败: {e}")
            return False

        return False

    def open_ros2_echo_terminal(self) -> bool:
        """
        打开新的 CMD/Terminal 窗口运行 ROS 2 echo
        自动执行：等待 → SSH 连接 → ros2 topic echo → 输出到文件
        主进程从文件读取输出
        """
        try:
            import tempfile
            
            # 获取输出文件路径
            if platform.system() == "Windows":
                output_file = os.path.join(os.environ.get('TEMP', 'C:\\Temp'), 'ros2_output.txt')
            else:
                output_file = '/tmp/ros2_output.txt'
            
            # 存储输出文件路径供主进程使用
            self.ros2_output_file = output_file
            
            if platform.system() == "Windows":
                # Windows: 创建批处理脚本
                with tempfile.NamedTemporaryFile(mode='w', suffix='.bat', delete=False, encoding='utf-8') as f:
                    # 构建远程命令
                    remote_cmd = f"source /opt/ros/humble/setup.bash && source ~/ros2_ws/install/setup.bash && ros2 topic echo /px4_1/fmu/out/vehicle_odometry"

                    lines = [
                        "@echo off",
                        "chcp 65001 >nul",
                        "echo [终端2] 等待3秒后执行SSH连接...",
                        "timeout /t 3 /nobreak",
                        f"echo [终端2] 正在连接到 {self.host}...",
                        "echo [终端2] 执行命令: ros2 topic echo /px4_1/fmu/out/vehicle_odometry",
                        f"echo [终端2] 输出将保存到: {output_file}",
                        "echo.",
                        "echo [重要] 如果提示输入密码，请输入: 123456",
                        "echo.",
                        f'ssh -o StrictHostKeyChecking=no {self.user}@{self.host} "{remote_cmd}" 2>&1 > "{output_file}"',
                        "echo.",
                        "echo [终端2] 命令已执行",
                        "pause"
                    ]
                    f.write('\n'.join(lines))
                    script_path = f.name
                
                cmd = f'start cmd /k "{script_path}"'
                subprocess.Popen(cmd, shell=True)
                logger.info("[终端2] 已打开新窗口，将自动执行: SSH → ros2 topic echo")
                logger.info(f"[终端2] 输出文件: {output_file}")
                return True

            elif platform.system() == "Linux":
                # Linux: gnome-terminal
                cmd = f'gnome-terminal -- bash -c "echo \'[终端2] 等待3秒后执行SSH连接...\'; sleep 3; sshpass -p \'{self.password}\' ssh -o StrictHostKeyChecking=no {self.user}@{self.host} \'source /opt/ros/humble/setup.bash && source ~/ros2_ws/install/setup.bash && ros2 topic echo /px4_1/fmu/out/vehicle_odometry\' 2>&1 | tee {output_file}; bash"'
                subprocess.Popen(cmd, shell=True)
                logger.info("[终端2] 已打开新窗口，将自动执行: SSH → ros2 topic echo")
                logger.info(f"[终端2] 输出文件: {output_file}")
                return True

            elif platform.system() == "Darwin":
                # macOS: Terminal
                with tempfile.NamedTemporaryFile(mode='w', suffix='.sh', delete=False, encoding='utf-8') as f:
                    lines = [
                        "#!/bin/bash",
                        'echo "[终端2] 等待3秒后执行SSH连接..."',
                        "sleep 3",
                        f'echo "[终端2] 正在连接到 {self.host}..."',
                        'echo "[终端2] 执行命令: ros2 topic echo /px4_1/fmu/out/vehicle_odometry"',
                        f'echo "{self.password}" | ssh -o StrictHostKeyChecking=no {self.user}@{self.host} \'source /opt/ros/humble/setup.bash && source ~/ros2_ws/install/setup.bash && ros2 topic echo /px4_1/fmu/out/vehicle_odometry\' 2>&1 | tee {output_file}',
                        'echo "[终端2] 输出已保存"'
                    ]
                    f.write('\n'.join(lines))
                    script_path = f.name
                os.chmod(script_path, 0o755)
                
                cmd = f'open -a Terminal {script_path}'
                subprocess.Popen(cmd, shell=True)
                logger.info("[终端2] 已打开新窗口，将自动执行: SSH → ros2 topic echo")
                logger.info(f"[终端2] 输出文件: {output_file}")
                return True

        except Exception as e:
            logger.error(f"[ERROR] 打开终端2 失败: {e}")
            return False

        return False


# ==================== ROS 2 数据接收部分 ====================

class ROS2DataReceiver:
    """ROS 2 数据接收器 - 从终端2的输出文件读取 ros2 topic echo 的结果"""

    def __init__(self, ssh_host: str = "192.168.10.1", ssh_user: str = "jetson1", 
                 ssh_password: str = "123456", ue_host: str = "127.0.0.1", 
                 ue_port: int = 8888, topic: str = "/px4_1/fmu/out/vehicle_odometry"):
        self.ssh_host = ssh_host
        self.ssh_user = ssh_user
        self.ssh_password = ssh_password
        self.ue_host = ue_host
        self.ue_port = ue_port
        self.topic = topic
        self.running = False
        self.udp_socket = None
        self.process = None
        self.listener_thread = None
        self.ros2_output_file = None

    def setup_udp_socket(self) -> bool:
        """设置 UDP Socket"""
        try:
            self.udp_socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            logger.info(f"[OK] UDP Socket 创建成功，准备发送到 {self.ue_host}:{self.ue_port}")
            return True
        except Exception as e:
            logger.error(f"[ERROR] UDP Socket 创建失败: {e}")
            return False

    def send_data_to_ue(self, data: Dict[str, Any]) -> bool:
        """将数据以 YAML 格式发送到 UE5"""
        try:
            # 【关键修复】不使用sort_keys，保持字段顺序（position在前）
            yaml_data = yaml.dump(data, default_flow_style=False, sort_keys=False)
            yaml_bytes = yaml_data.encode('utf-8')

            # 【诊断】每100个包打印一次实际发送的YAML内容
            if not hasattr(self, '_send_counter'):
                self._send_counter = 0
            self._send_counter += 1

            if self._send_counter % 100 == 0:
                timestamp = datetime.now().strftime('%H:%M:%S.%f')[:-3]
                preview = yaml_data[:400] if len(yaml_data) > 400 else yaml_data
                logger.info(f"[{timestamp}] [发送#{self._send_counter}] 目标: {self.ue_host}:{self.ue_port}")
                logger.info(f"[YAML内容]\n{preview}")
                logger.info(f"[数据大小] {len(yaml_bytes)} 字节")

            self.udp_socket.sendto(yaml_bytes, (self.ue_host, self.ue_port))
            return True
        except Exception as e:
            logger.error(f"[ERROR] 数据发送失败: {e}")
            return False

    def parse_yaml_output(self, yaml_text: str) -> Dict[str, Any]:
        """解析 ros2 topic echo 的 YAML 输出"""
        try:
            data = yaml.safe_load(yaml_text)
            if data:
                return data
        except Exception as e:
            logger.debug(f"YAML 解析失败: {e}")
        return {}

    def process_odometry_data(self, data: Union[Dict[str, Any], VehicleOdometry]) -> Dict[str, Any]:
        """处理 Odometry 数据，提取关键信息。
        支持原始 dict（ros2 echo 输出解析）或手动定义的 VehicleOdometry 实例。
        """
        try:
            # 如果已经是 VehicleOdometry 实例，直接使用
            if isinstance(data, VehicleOdometry):
                vo: VehicleOdometry = data
                timestamp_value = int(vo.timestamp or 0)
                position_list = [float(x) for x in (vo.position or [0.0, 0.0, 0.0])][:3]
                q_list = [float(x) for x in (vo.q or [0.0, 0.0, 0.0, 1.0])][:4]
                velocity_list = [float(x) for x in (vo.velocity or [0.0, 0.0, 0.0])][:3]
                angular_velocity_list = [float(x) for x in (vo.angular_velocity or [0.0, 0.0, 0.0])][:3]
                position_variance = [float(x) for x in (vo.position_variance or [0.0, 0.0, 0.0])][:3]
                velocity_variance = [float(x) for x in (vo.velocity_variance or [0.0, 0.0, 0.0])][:3]
                reset_counter = int(vo.reset_counter or 0)
                quality = int(vo.quality or 0)

            else:
                # 【修正】处理 PX4 VehicleOdometry 的扁平 YAML 格式
                # 实际数据格式示例：
                # {
                #   'timestamp': 1765353093720576,
                #   'position': [9.086, 35.671, 2.786],
                #   'q': [-0.691, -0.024, 0.006, 0.722],
                #   ...
                # }

                # 1. 提取时间戳（直接从最外层）
                timestamp_value = int(data.get('timestamp', 0))

                # 2. 提取位置（直接处理列表）
                raw_position = data.get('position', [0.0, 0.0, 0.0])
                position_list = [float(x) for x in raw_position] if isinstance(raw_position, list) else [0.0, 0.0, 0.0]

                # 3. 提取四元数（直接处理列表）
                raw_q = data.get('q', [0.0, 0.0, 0.0, 1.0])
                q_list = [float(x) for x in raw_q] if isinstance(raw_q, list) else [0.0, 0.0, 0.0, 1.0]

                # 4. 提取速度（直接处理列表）
                raw_velocity = data.get('velocity', [0.0, 0.0, 0.0])
                velocity_list = [float(x) for x in raw_velocity] if isinstance(raw_velocity, list) else [0.0, 0.0, 0.0]

                # 5. 提取角速度
                raw_angular_velocity = data.get('angular_velocity', [0.0, 0.0, 0.0])
                angular_velocity_list = [float(x) for x in raw_angular_velocity] if isinstance(raw_angular_velocity, list) else [0.0, 0.0, 0.0]

                # 6. 提取方差数据
                raw_position_variance = data.get('position_variance', [0.0, 0.0, 0.0])
                position_variance = [float(x) for x in raw_position_variance] if isinstance(raw_position_variance, list) else [0.0, 0.0, 0.0]

                raw_velocity_variance = data.get('velocity_variance', [0.0, 0.0, 0.0])
                velocity_variance = [float(x) for x in raw_velocity_variance] if isinstance(raw_velocity_variance, list) else [0.0, 0.0, 0.0]

                reset_counter = int(data.get('reset_counter', 0))
                quality = int(data.get('quality', 0))

            # 组织为 YAML 格式的字典
            cleaned_data = {
                'timestamp': int(timestamp_value),
                'timestamp_sample': int(timestamp_value),
                'pose_frame': 1,
                'position': [float(x) for x in position_list],
                'q': [float(x) for x in q_list],
                'velocity_frame': 1,
                'velocity': [float(x) for x in velocity_list],
                'angular_velocity': [float(x) for x in angular_velocity_list],
                'position_variance': [float(x) for x in position_variance],
                'velocity_variance': [float(x) for x in velocity_variance],
                'reset_counter': int(reset_counter),
                'quality': int(quality)
            }

            # 【调试日志】打印实际解析的position值
            logger.debug(f"[数据处理] Position: [{position_list[0]:.6f}, {position_list[1]:.6f}, {position_list[2]:.6f}]")

            return cleaned_data

        except Exception as e:
            logger.error(f"[ERROR] 数据处理失败: {e}")
            import traceback
            logger.error(traceback.format_exc())
            return {}

    def start_listening(self) -> bool:
        """从终端2的输出文件读取 ros2 topic echo 的数据"""
        try:
            # 获取输出文件路径
            if platform.system() == "Windows":
                output_file = os.path.join(os.environ.get('TEMP', 'C:\\Temp'), 'ros2_output.txt')
            else:
                output_file = '/tmp/ros2_output.txt'

            # 【修复】删除旧的输出文件，确保读取新数据
            if os.path.exists(output_file):
                try:
                    os.remove(output_file)
                    logger.info(f"[OK] 已删除旧的输出文件: {output_file}")
                except Exception as e:
                    logger.warning(f"[WARNING] 无法删除旧文件: {e}")

            logger.info(f"[OK] 将从文件读取终端2的输出: {output_file}")
            logger.info(f"[OK] 监听话题: {self.topic}")
            logger.info(f"[等待] 正在等待终端2生成输出文件...")

            self.running = True
            
            # 在线程中读取文件
            def read_output_file():
                yaml_buffer = ""
                separator_count = 0
                line_count = 0
                max_wait = 60
                wait_start = time.time()
                
                try:
                    # 等待输出文件出现
                    while not os.path.exists(output_file) and self.running:
                        if time.time() - wait_start > max_wait:
                            logger.error(f"[ERROR] 等待超时（{max_wait}秒），输出文件未出现")
                            logger.error(f"[ERROR] 请检查终端2是否正常运行")
                            return
                        time.sleep(1)
                    
                    if not self.running:
                        logger.info("监听已停止")
                        return
                    
                    logger.info(f"[OK] 输出文件已创建，开始读取数据...")
                    time.sleep(1)
                    
                    # 读取文件内容
                    last_pos = 0
                    while self.running:
                        try:
                            with open(output_file, 'r', encoding='utf-8', errors='replace') as f:
                                f.seek(last_pos)
                                lines = f.readlines()
                                last_pos = f.tell()
                                
                                if not lines:
                                    time.sleep(0.5)
                                    continue
                                
                                for line in lines:
                                    if not self.running:
                                        break

                                    line = line.rstrip('\n')
                                    line_count += 1

                                    # 【诊断】恢复前5行的详细日志输出
                                    if line_count <= 5:
                                        logger.info(f"[行{line_count}] {line}")

                                    # 检查分隔符（ROS 2 使用 --- 分隔消息）
                                    if line.strip().startswith('---'):
                                        separator_count += 1
                                        # logger.debug(f"发现分隔符 #{separator_count}")

                                        if yaml_buffer.strip() and separator_count > 1:
                                            data = self.parse_yaml_output(yaml_buffer)
                                            if data:
                                                cleaned_data = self.process_odometry_data(data)
                                                if cleaned_data:
                                                    self.send_data_to_ue(cleaned_data)
                                                    # 【诊断】提高日志频率，每10条打印一次position数据（带时间戳）
                                                    if separator_count % 10 == 0:
                                                        pos = cleaned_data.get('position', [0, 0, 0])
                                                        timestamp = datetime.now().strftime('%H:%M:%S.%f')[:-3]  # 精确到毫秒
                                                        logger.info(f"[{timestamp}] [数据#{separator_count-1}] Position=[{pos[0]:.6f}, {pos[1]:.6f}, {pos[2]:.6f}]米")
                                        yaml_buffer = ""
                                        continue
                                    
                                    # 积累 YAML 内容
                                    if separator_count > 0:
                                        yaml_buffer += line + "\n"
                        
                        except FileNotFoundError:
                            time.sleep(0.5)
                            continue
                    
                    logger.info(f"输出文件已关闭，共读取 {line_count} 行")
                
                except Exception as e:
                    logger.error(f"[ERROR] 读取输出文件失败: {e}", exc_info=True)
                finally:
                    logger.info("[OK] 已停止监听")
            
            # 启动读取线程
            self.listener_thread = threading.Thread(target=read_output_file, daemon=True)
            self.listener_thread.start()
            
            return True
        
        except Exception as e:
            logger.error(f"[ERROR] 启动监听失败: {e}", exc_info=True)
            return False

    def stop_listening(self):
        """停止监听"""
        self.running = False
        if self.process:
            try:
                self.process.terminate()
                self.process.wait(timeout=2)
            except:
                self.process.kill()
        if self.udp_socket:
            self.udp_socket.close()
        logger.info("[OK] 已停止监听")


# ==================== 主函数 ====================

def main():
    """主程序"""
    parser = argparse.ArgumentParser(description="无人机数据接收与转发脚本")
    parser.add_argument('--ue-host', default='127.0.0.1', help='UE5 主机地址')
    parser.add_argument('--ue-port', type=int, default=8888, help='UE5 监听端口')
    parser.add_argument('--ssh-host', default='192.168.10.1', help='无人机 SSH 地址')
    parser.add_argument('--ssh-user', default='jetson1', help='SSH 用户名')
    parser.add_argument('--ssh-password', default='123456', help='SSH 密码')
    parser.add_argument('--ros-topic', default='/px4_1/fmu/out/vehicle_odometry', help='ROS 2 话题')
    parser.add_argument('--skip-network', action='store_true', help='跳过网络连接步骤')
    parser.add_argument('--skip-ssh', action='store_true', help='跳过 SSH 连接步骤')

    args = parser.parse_args()

    logger.info("=" * 60)
    logger.info("无人机数据接收与转发脚本启动")
    logger.info("=" * 60)

    receiver = None

    try:
        # ============ 步骤 1: 连接 WiFi ============
        if not args.skip_network:
            logger.info("\n" + "="*60)
            logger.info("[步骤 1/5] 连接 UAV1 网络...")
            logger.info("="*60)
            wifi = WiFiManager()
            if not wifi.connect_to_uav1():
                logger.error("[WARNING] 无法连接到 UAV1 网络，但继续尝试...")
                time.sleep(2)
        else:
            logger.info("\n" + "="*60)
            logger.info("[步骤 1/5] 跳过网络连接")
            logger.info("="*60)

        # ============ 步骤 2-5: SSH 连接和终端启动 ============
        if not args.skip_ssh:
            logger.info("\n" + "="*60)
            logger.info("[步骤 2/5] 测试 SSH 连接...")
            logger.info("="*60)
            ssh = SSHManager(args.ssh_host, args.ssh_user, args.ssh_password)

            if ssh.test_connection():
                logger.info("[OK] SSH 连接可用，可以访问 jetson1@192.168.10.1")

                # ============ 步骤 3: 打开终端1 运行 MicroXRCE Agent ============
                logger.info("\n" + "="*60)
                logger.info("[步骤 3/5] 打开终端1 - 执行 SSH 连接 → MicroXRCE Agent")
                logger.info("="*60)
                logger.info("命令: ssh jetson1@192.168.10.1")
                logger.info("      → MicroXRCEAgent serial --dev /dev/ttyTHS1 -b 921600")
                ssh.open_microxrce_agent_terminal()
                logger.info("[OK] 终端1 已打开 (请保持运行)")
                time.sleep(3)

                # ============ 步骤 4: 打开终端2 运行 ROS 2 echo ============
                logger.info("\n" + "="*60)
                logger.info("[步骤 4/5] 打开终端2 - 执行 SSH 连接 → ROS 2 echo")
                logger.info("="*60)
                logger.info("命令: ssh jetson1@192.168.10.1")
                logger.info("      → ros2 topic echo /px4_1/fmu/out/vehicle_odometry")
                ssh.open_ros2_echo_terminal()
                logger.info("[OK] 终端2 已打开 (将显示 Odometry 数据)")
                time.sleep(2)

            else:
                logger.error("[ERROR] 无法连接到 SSH，请检查网络")
                logger.error("请确保:")
                logger.error("  1. 已连接到 UAV1 网络")
                logger.error("  2. 无人机地址正确: 192.168.10.1")
                logger.error("  3. SSH 服务已启动")
                return False
        else:
            logger.info("\n" + "="*60)
            logger.info("[步骤 2-4/5] 跳过 SSH 连接和终端启动")
            logger.info("="*60)

        # ============ 步骤 5: 主进程接收数据并转发到 UE5 ============
        logger.info("\n" + "="*60)
        logger.info("[步骤 5/5] 初始化主进程 - 接收数据并转发到 UE5...")
        logger.info("="*60)
        receiver = ROS2DataReceiver(
            ssh_host=args.ssh_host,
            ssh_user=args.ssh_user,
            ssh_password=args.ssh_password,
            ue_host=args.ue_host,
            ue_port=args.ue_port,
            topic=args.ros_topic
        )

        if not receiver.setup_udp_socket():
            logger.error("[ERROR] UDP Socket 设置失败")
            return False

        # 启动监听 - 从终端2的输出文件读取数据
        if not receiver.start_listening():
            logger.warning("[ERROR] 无法启动 ROS 2 监听")

        logger.info("\n" + "="*60)
        logger.info("[OK] 所有步骤完成！脚本运行中...")
        logger.info("="*60)
        logger.info("当前配置:")
        logger.info(f"  • 终端1: MicroXRCE Agent (SSH jetson1@{args.ssh_host})")
        logger.info(f"  • 终端2: ros2 topic echo (SSH jetson1@{args.ssh_host})")
        logger.info(f"  • 主进程: 从文件读取话题 {args.ros_topic}")
        logger.info(f"  • 数据转发: {args.ue_host}:{args.ue_port}")
        logger.info("="*60)
        logger.info("按 Ctrl+C 退出\n")

        # 保持程序运行，等待监听线程
        while receiver.running:
            time.sleep(0.5)
        
        logger.info("监听线程已结束")
        time.sleep(1)

    except KeyboardInterrupt:
        logger.info("\n收到中断信号，正在关闭...")
        if receiver is not None:
            receiver.stop_listening()
        logger.info("[OK] 脚本已退出")

    except Exception as e:
        logger.error(f"\n[ERROR] 发生错误: {e}")
        import traceback
        traceback.print_exc()
        if receiver is not None:
            receiver.stop_listening()
        return False

    return True


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
