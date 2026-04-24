"""
Launcher for multiple ue_to_px4_bridge.py instances.
Reads multi_drone_config.yaml and spawns per-drone processes.
"""

import argparse
import os
import sys
import subprocess
from pathlib import Path
from typing import Dict, Any
import yaml


def load_config(config_path: Path) -> Dict[str, Any]:
    if not config_path.exists():
        raise FileNotFoundError(f"配置文件不存在: {config_path}")
    with config_path.open("r", encoding="utf-8") as f:
        return yaml.safe_load(f) or {}


def derive_topic_prefix(ros_topic: str, default_prefix: str) -> str:
    if not ros_topic:
        return default_prefix
    if "/fmu/" in ros_topic:
        return ros_topic.split("/fmu/")[0]
    parts = [p for p in ros_topic.split("/") if p]
    if parts:
        return f"/{parts[0]}"
    return default_prefix


def normalize_drone_config(raw: Dict[str, Any], index: int) -> Dict[str, Any]:
    name = str(raw.get("name") or f"UAV{index + 1}")
    drone_id = int(raw.get("drone_id", index + 1))
    ros_topic = raw.get("ros_topic", f"/px4_{index + 1}/fmu/out/vehicle_odometry")
    topic_prefix = raw.get("topic_prefix", derive_topic_prefix(ros_topic, f"/px4_{index + 1}"))

    ue_receive_port = raw.get("ue_receive_port", raw.get("ue_port"))
    if ue_receive_port is None:
        ue_receive_port = 8888 + index * 2
    ue_send_port = raw.get("ue_send_port", raw.get("ue_tx_port"))
    if ue_send_port is None:
        ue_send_port = int(ue_receive_port) + 1

    return {
        "name": name,
        "drone_id": drone_id,
        "topic_prefix": topic_prefix,
        "udp_port": int(ue_send_port),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Multi UE-to-PX4 launcher (new)")
    parser.add_argument("--config", default="multi_drone_config.yaml", help="配置文件路径")
    parser.add_argument("--max-drones", type=int, default=0, help="限制启动的无人机数量 (0=不限制)")
    parser.add_argument("--dry-run", action="store_true", help="仅打印命令，不启动进程")
    parser.add_argument("--log-level", default="INFO", choices=["DEBUG", "INFO", "WARNING", "ERROR"], help="日志级别")

    args = parser.parse_args()

    config_path = Path(args.config)
    if not config_path.is_absolute():
        config_path = Path(__file__).resolve().parent / config_path

    config = load_config(config_path)
    drones_raw = config.get("drones", [])
    if not drones_raw:
        print("[ERROR] 配置文件中未找到 drones 列表")
        return 1

    if args.max_drones and args.max_drones > 0:
        drones_raw = drones_raw[: args.max_drones]

    drones = [normalize_drone_config(d, i) for i, d in enumerate(drones_raw)]

    script_path = Path(__file__).resolve().parent / "ue_to_px4_bridge.py"
    if not script_path.exists():
        print(f"[ERROR] 未找到脚本: {script_path}")
        return 1

    processes = []
    for drone in drones:
        cmd = [
            sys.executable,
            str(script_path),
            "--drone-id",
            str(drone["drone_id"]),
            "--topic-prefix",
            str(drone["topic_prefix"]),
            "--udp-port",
            str(drone["udp_port"]),
            "--log-level",
            args.log_level,
        ]

        print(f"[{drone['name']}] 启动: {' '.join(cmd)}")
        if not args.dry_run:
            processes.append(subprocess.Popen(cmd))

    if args.dry_run:
        return 0

    try:
        for p in processes:
            p.wait()
    except KeyboardInterrupt:
        print("\n收到中断信号，正在关闭...")
        for p in processes:
            try:
                p.terminate()
            except Exception:
                pass

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
