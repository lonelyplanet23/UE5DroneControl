"""
Multi-drone data bridge (new file).
Reads multi_drone_config.yaml and launches per-drone ROS2 echo + UDP forward.
Does not modify existing scripts.
"""

import os
import sys
import time
import argparse
import platform
import subprocess
from pathlib import Path
from typing import Dict, Any, List, Optional
from datetime import datetime
import threading
import yaml

from drone_data_bridge import WiFiManager, SSHManager, ROS2DataReceiver, logger


class MultiSSHManager(SSHManager):
    """SSH manager with per-drone labels and custom ROS2 topic/output."""

    def open_microxrce_agent_terminal(self, label: str = "终端1") -> bool:
        try:
            import tempfile

            if platform.system() == "Windows":
                with tempfile.NamedTemporaryFile(mode='w', suffix='.bat', delete=False, encoding='utf-8') as f:
                    remote_cmd = "MicroXRCEAgent serial --dev /dev/ttyTHS1 -b 921600"
                    lines = [
                        "@echo off",
                        "chcp 65001 >nul",
                        f"echo [{label}] 等待3秒后执行SSH连接...",
                        "timeout /t 3 /nobreak",
                        f"echo [{label}] 正在连接到 {self.host}...",
                        f"echo [{label}] 执行命令: {remote_cmd}",
                        "echo.",
                        "echo [重要] 如果提示输入密码，请输入: 123456",
                        "echo.",
                        f'ssh -o StrictHostKeyChecking=no {self.user}@{self.host} "{remote_cmd}"',
                        "echo.",
                        f"echo [{label}] 命令已执行",
                        "pause"
                    ]
                    f.write('\n'.join(lines))
                    script_path = f.name

                cmd = f'start cmd /k "{script_path}"'
                subprocess.Popen(cmd, shell=True)
                logger.info(f"[{label}] 已打开新窗口，将自动执行: SSH → MicroXRCE Agent")
                return True

            if platform.system() == "Linux":
                cmd = (
                    "gnome-terminal -- bash -c "
                    f"\"echo '[{label}] 等待3秒后执行SSH连接...'; "
                    f"sleep 3; sshpass -p '{self.password}' ssh -o StrictHostKeyChecking=no "
                    f"{self.user}@{self.host} 'MicroXRCEAgent serial --dev /dev/ttyTHS1 -b 921600'; bash\""
                )
                subprocess.Popen(cmd, shell=True)
                logger.info(f"[{label}] 已打开新窗口，将自动执行: SSH → MicroXRCE Agent")
                return True

            if platform.system() == "Darwin":
                with tempfile.NamedTemporaryFile(mode='w', suffix='.sh', delete=False, encoding='utf-8') as f:
                    lines = [
                        "#!/bin/bash",
                        f'echo "[{label}] 等待3秒后执行SSH连接..."',
                        "sleep 3",
                        f'echo "[{label}] 正在连接到 {self.host}..."',
                        f'echo "{self.password}" | ssh -o StrictHostKeyChecking=no {self.user}@{self.host} '
                        '\'MicroXRCEAgent serial --dev /dev/ttyTHS1 -b 921600\'',
                        f'echo "[{label}] 命令已执行"'
                    ]
                    f.write('\n'.join(lines))
                    script_path = f.name
                os.chmod(script_path, 0o755)

                cmd = f'open -a Terminal {script_path}'
                subprocess.Popen(cmd, shell=True)
                logger.info(f"[{label}] 已打开新窗口，将自动执行: SSH → MicroXRCE Agent")
                return True

        except Exception as e:
            logger.error(f"[ERROR] 打开 {label} 失败: {e}")
            return False

        return False

    def open_ros2_echo_terminal(self, topic: str, output_file: str, label: str = "终端2") -> bool:
        try:
            import tempfile

            output_path = Path(output_file)
            output_path.parent.mkdir(parents=True, exist_ok=True)
            self.ros2_output_file = str(output_path)

            remote_cmd = (
                "source /opt/ros/humble/setup.bash && "
                "source ~/ros2_ws/install/setup.bash && "
                f"ros2 topic echo {topic}"
            )

            if platform.system() == "Windows":
                with tempfile.NamedTemporaryFile(mode='w', suffix='.bat', delete=False, encoding='utf-8') as f:
                    lines = [
                        "@echo off",
                        "chcp 65001 >nul",
                        f"echo [{label}] 等待3秒后执行SSH连接...",
                        "timeout /t 3 /nobreak",
                        f"echo [{label}] 正在连接到 {self.host}...",
                        f"echo [{label}] 执行命令: ros2 topic echo {topic}",
                        f"echo [{label}] 输出将保存到: {output_file}",
                        "echo.",
                        "echo [重要] 如果提示输入密码，请输入: 123456",
                        "echo.",
                        f'ssh -o StrictHostKeyChecking=no {self.user}@{self.host} "{remote_cmd}" 2>&1 > "{output_file}"',
                        "echo.",
                        f"echo [{label}] 命令已执行",
                        "pause"
                    ]
                    f.write('\n'.join(lines))
                    script_path = f.name

                cmd = f'start cmd /k "{script_path}"'
                subprocess.Popen(cmd, shell=True)
                logger.info(f"[{label}] 已打开新窗口，将自动执行: SSH → ros2 topic echo")
                logger.info(f"[{label}] 输出文件: {output_file}")
                return True

            if platform.system() == "Linux":
                cmd = (
                    "gnome-terminal -- bash -c "
                    f"\"echo '[{label}] 等待3秒后执行SSH连接...'; sleep 3; "
                    f"sshpass -p '{self.password}' ssh -o StrictHostKeyChecking=no "
                    f"{self.user}@{self.host} '{remote_cmd}' 2>&1 | tee {output_file}; bash\""
                )
                subprocess.Popen(cmd, shell=True)
                logger.info(f"[{label}] 已打开新窗口，将自动执行: SSH → ros2 topic echo")
                logger.info(f"[{label}] 输出文件: {output_file}")
                return True

            if platform.system() == "Darwin":
                with tempfile.NamedTemporaryFile(mode='w', suffix='.sh', delete=False, encoding='utf-8') as f:
                    lines = [
                        "#!/bin/bash",
                        f'echo "[{label}] 等待3秒后执行SSH连接..."',
                        "sleep 3",
                        f'echo "[{label}] 正在连接到 {self.host}..."',
                        f'echo "[{label}] 执行命令: ros2 topic echo {topic}"',
                        f'echo "{self.password}" | ssh -o StrictHostKeyChecking=no {self.user}@{self.host} '
                        f'\'{remote_cmd}\' 2>&1 | tee {output_file}',
                        f'echo "[{label}] 输出已保存"'
                    ]
                    f.write('\n'.join(lines))
                    script_path = f.name
                os.chmod(script_path, 0o755)

                cmd = f'open -a Terminal {script_path}'
                subprocess.Popen(cmd, shell=True)
                logger.info(f"[{label}] 已打开新窗口，将自动执行: SSH → ros2 topic echo")
                logger.info(f"[{label}] 输出文件: {output_file}")
                return True

        except Exception as e:
            logger.error(f"[ERROR] 打开 {label} 失败: {e}")
            return False

        return False


class MultiROS2DataReceiver(ROS2DataReceiver):
    """ROS2 receiver with custom output file per drone."""

    def __init__(
        self,
        ssh_host: str,
        ssh_user: str,
        ssh_password: str,
        ue_host: str,
        ue_port: int,
        topic: str,
        output_file: str,
    ):
        super().__init__(
            ssh_host=ssh_host,
            ssh_user=ssh_user,
            ssh_password=ssh_password,
            ue_host=ue_host,
            ue_port=ue_port,
            topic=topic,
        )
        self.output_file = output_file

    def start_listening(self) -> bool:
        try:
            output_file = self.output_file
            if not output_file:
                if platform.system() == "Windows":
                    output_file = os.path.join(os.environ.get('TEMP', 'C:\\Temp'), 'ros2_output.txt')
                else:
                    output_file = '/tmp/ros2_output.txt'

            if os.path.exists(output_file):
                try:
                    os.remove(output_file)
                    logger.info(f"[OK] 已删除旧的输出文件: {output_file}")
                except Exception as e:
                    logger.warning(f"[WARNING] 无法删除旧文件: {e}")

            logger.info(f"[OK] 将从文件读取终端输出: {output_file}")
            logger.info(f"[OK] 监听话题: {self.topic}")
            logger.info(f"[等待] 正在等待终端生成输出文件...")

            self.running = True

            def read_output_file():
                yaml_buffer = ""
                separator_count = 0
                line_count = 0
                max_wait = 60
                wait_start = time.time()

                try:
                    while not os.path.exists(output_file) and self.running:
                        if time.time() - wait_start > max_wait:
                            logger.error(f"[ERROR] 等待超时（{max_wait}秒），输出文件未出现")
                            logger.error(f"[ERROR] 请检查终端是否正常运行")
                            return
                        time.sleep(1)

                    if not self.running:
                        logger.info("监听已停止")
                        return

                    logger.info(f"[OK] 输出文件已创建，开始读取数据...")
                    time.sleep(0.1)

                    last_pos = 0
                    while self.running:
                        try:
                            with open(output_file, 'r', encoding='utf-8', errors='replace') as f:
                                f.seek(last_pos)
                                lines = f.readlines()
                                last_pos = f.tell()

                                if not lines:
                                    time.sleep(0.01)
                                    continue

                                for line in lines:
                                    if not self.running:
                                        break

                                    line = line.rstrip('\n')
                                    line_count += 1

                                    if line.strip().startswith('---'):
                                        separator_count += 1
                                        if yaml_buffer.strip() and separator_count > 1:
                                            data = self.parse_yaml_output(yaml_buffer)
                                            if data:
                                                cleaned_data = self.process_odometry_data(data)
                                                if cleaned_data:
                                                    self.send_data_to_ue(cleaned_data)
                                                    if separator_count % 100 == 0:
                                                        pos = cleaned_data.get('position', [0, 0, 0])
                                                        timestamp = datetime.now().strftime('%H:%M:%S.%f')[:-3]
                                                        logger.info(
                                                            f"[{timestamp}] [数据#{separator_count-1}] "
                                                            f"Position=[{pos[0]:.3f}, {pos[1]:.3f}, {pos[2]:.3f}]米"
                                                        )
                                        yaml_buffer = ""
                                        continue

                                    if separator_count > 0:
                                        yaml_buffer += line + "\n"

                        except FileNotFoundError:
                            time.sleep(0.1)
                            continue

                    logger.info(f"输出文件已关闭，共读取 {line_count} 行")

                except Exception as e:
                    logger.error(f"[ERROR] 读取输出文件失败: {e}", exc_info=True)
                finally:
                    logger.info("[OK] 已停止监听")

            self.listener_thread = threading.Thread(target=read_output_file, daemon=True)
            self.listener_thread.start()
            return True

        except Exception as e:
            logger.error(f"[ERROR] 启动监听失败: {e}", exc_info=True)
            return False


def _safe_name(name: str) -> str:
    return "".join(c for c in name if c.isalnum() or c in ("-", "_")) or "UAV"


def _default_output_file(drone_name: str) -> str:
    safe = _safe_name(drone_name)
    if platform.system() == "Windows":
        return os.path.join(os.environ.get('TEMP', 'C:\\Temp'), f'ros2_output_{safe}.txt')
    return f'/tmp/ros2_output_{safe}.txt'


def _normalize_drone_config(raw: Dict[str, Any], index: int) -> Dict[str, Any]:
    name = str(raw.get("name") or f"UAV{index + 1}")
    ue_host = raw.get("ue_host", "127.0.0.1")
    ue_port = raw.get("ue_receive_port", raw.get("ue_port"))
    if ue_port is None:
        ue_port = 8888 + index * 2

    return {
        "name": name,
        "ssh_host": raw.get("ssh_host", ""),
        "ssh_user": raw.get("ssh_user", ""),
        "ssh_password": raw.get("ssh_password", ""),
        "ros_topic": raw.get("ros_topic", f"/px4_{index + 1}/fmu/out/vehicle_odometry"),
        "ue_host": ue_host,
        "ue_port": int(ue_port),
    }


def load_config(config_path: Path) -> Dict[str, Any]:
    if not config_path.exists():
        raise FileNotFoundError(f"配置文件不存在: {config_path}")
    with config_path.open("r", encoding="utf-8") as f:
        data = yaml.safe_load(f) or {}
    return data


def main() -> bool:
    parser = argparse.ArgumentParser(description="Multi-drone data bridge (new)")
    parser.add_argument("--config", default="multi_drone_config.yaml", help="配置文件路径")
    parser.add_argument("--skip-network", action="store_true", help="跳过网络连接步骤")
    parser.add_argument("--skip-ssh", action="store_true", help="跳过 SSH 连接步骤")
    parser.add_argument("--max-drones", type=int, default=0, help="限制启动的无人机数量 (0=不限制)")

    args = parser.parse_args()

    config_path = Path(args.config)
    if not config_path.is_absolute():
        config_path = Path(__file__).resolve().parent / config_path

    logger.info("=" * 60)
    logger.info("多机数据桥接脚本启动")
    logger.info(f"配置文件: {config_path}")
    logger.info("=" * 60)

    config = load_config(config_path)
    drones_raw = config.get("drones", [])
    network_cfg = config.get("network", {})

    if not drones_raw:
        logger.error("[ERROR] 配置文件中未找到 drones 列表")
        return False

    if args.max_drones and args.max_drones > 0:
        drones_raw = drones_raw[: args.max_drones]

    drones = [_normalize_drone_config(d, i) for i, d in enumerate(drones_raw)]

    # Step 1: WiFi (optional)
    skip_network = args.skip_network or bool(network_cfg.get("skip_network_check", False))
    if not skip_network:
        logger.info("\n" + "=" * 60)
        logger.info("[步骤 1/4] 连接 virtualUAV 网络...")
        logger.info("=" * 60)
        wifi = WiFiManager()
        if not wifi.connect_to_uav1():
            logger.error("[WARNING] 无法连接到 virtualUAV 网络，但继续尝试...")
            time.sleep(2)
    else:
        logger.info("\n" + "=" * 60)
        logger.info("[步骤 1/4] 跳过网络连接")
        logger.info("=" * 60)

    receivers: List[MultiROS2DataReceiver] = []

    # Step 2-3: SSH terminals (optional)
    skip_ssh = args.skip_ssh or bool(network_cfg.get("skip_ssh_check", False))
    for idx, drone in enumerate(drones):
        name = drone["name"]
        ssh_host = drone["ssh_host"]
        ssh_user = drone["ssh_user"]
        ssh_password = drone["ssh_password"]
        topic = drone["ros_topic"]
        ue_host = drone["ue_host"]
        ue_port = drone["ue_port"]
        output_file = _default_output_file(name)

        logger.info("\n" + "=" * 60)
        logger.info(f"[无人机 {idx + 1}] {name}")
        logger.info("=" * 60)

        if not skip_ssh:
            if not ssh_host or not ssh_user:
                logger.warning(f"[WARNING] {name} 缺少 SSH 配置，跳过 SSH 终端启动")
            else:
                ssh = MultiSSHManager(ssh_host, ssh_user, ssh_password)
                if ssh.test_connection():
                    logger.info(f"[OK] SSH 连接可用: {ssh_user}@{ssh_host}")

                    logger.info("[步骤 2/4] 打开终端 - MicroXRCE Agent")
                    ssh.open_microxrce_agent_terminal(label=f"{name}-终端1")
                    time.sleep(1)

                    logger.info("[步骤 3/4] 打开终端 - ROS 2 echo")
                    ssh.open_ros2_echo_terminal(topic=topic, output_file=output_file, label=f"{name}-终端2")
                    time.sleep(1)
                else:
                    logger.error(f"[ERROR] 无法连接到 SSH: {ssh_host}，请检查网络")
        else:
            logger.info("[步骤 2-3/4] 跳过 SSH 连接和终端启动")

        # Step 4: receiver
        receiver = MultiROS2DataReceiver(
            ssh_host=ssh_host,
            ssh_user=ssh_user,
            ssh_password=ssh_password,
            ue_host=ue_host,
            ue_port=ue_port,
            topic=topic,
            output_file=output_file,
        )

        if receiver.setup_udp_socket():
            receiver.start_listening()
            receivers.append(receiver)
        else:
            logger.error(f"[ERROR] {name} UDP Socket 设置失败")

        logger.info(f"[OK] {name} -> UE5 {ue_host}:{ue_port}")

    logger.info("\n" + "=" * 60)
    logger.info("[OK] 多机桥接已启动，按 Ctrl+C 退出")
    logger.info("=" * 60)

    try:
        while any(r.running for r in receivers):
            time.sleep(0.5)
    except KeyboardInterrupt:
        logger.info("\n收到中断信号，正在关闭...")
        for r in receivers:
            r.stop_listening()
        logger.info("[OK] 脚本已退出")

    return True


if __name__ == "__main__":
    sys.exit(0 if main() else 1)
