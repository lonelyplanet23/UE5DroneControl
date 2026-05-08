"""
simulate_telemetry.py — 持续向后端注入模拟遥测数据，替代真实 Jetson/PX4。

用法：
  python tools/simulate_telemetry.py
  python tools/simulate_telemetry.py --drones 1 2 --register
  python tools/simulate_telemetry.py --transport udp --drones 1 2
  python tools/simulate_telemetry.py --ue-target 1000 0 -500
  python tools/simulate_telemetry.py --move-circle --battery 15
"""

from __future__ import annotations

import argparse
import math
import socket
import sys
import time
import threading
import urllib.request
import urllib.error
import json
from typing import Any


def http_post(base: str, path: str, payload: Any, timeout: float = 3.0) -> Any:
    data = json.dumps(payload).encode("utf-8")
    req = urllib.request.Request(
        f"{base}{path}",
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        print(f"[WARN] POST {path} -> HTTP {e.code}: {body[:120]}", file=sys.stderr)
        return None
    except Exception as e:
        print(f"[WARN] POST {path} -> {e}", file=sys.stderr)
        return None


def http_get(base: str, path: str, timeout: float = 3.0) -> Any:
    req = urllib.request.Request(
        f"{base}{path}",
        headers={"Accept": "application/json"},
        method="GET",
    )
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except Exception as e:
        print(f"[WARN] GET {path} -> {e}", file=sys.stderr)
        return None


def ensure_registered(base: str, drone_ids: list[int]) -> None:
    """Register missing drones using matching slot numbers."""
    existing = http_get(base, "/api/drones")
    if existing is None:
        print("[WARN] 无法读取无人机列表，跳过自动注册", file=sys.stderr)
        return

    registered_ids = set()
    registered_slots = set()
    if isinstance(existing, list):
        for item in existing:
            if not isinstance(item, dict):
                continue
            try:
                registered_ids.add(int(item.get("id", 0)))
                registered_slots.add(int(item.get("slot", 0)))
            except Exception:
                pass

    for drone_id in drone_ids:
        if drone_id in registered_ids or drone_id in registered_slots:
            continue
        payload = {
            "name": f"sim-d{drone_id}",
            "model": "PX4-SIM",
            "slot": drone_id,
        }
        result = http_post(base, "/api/drones", payload)
        if result is not None:
            print(f"[d{drone_id}] 已自动注册 slot={drone_id}: {result}")


def ue_to_ned(ue_x: float, ue_y: float, ue_z: float) -> tuple[float, float, float]:
    return ue_x * 0.01, ue_y * 0.01, -ue_z * 0.01


def make_telemetry(
    drone_id: int,
    t: float,
    *,
    move_circle: bool = False,
    battery: int = 85,
    gps_lat: float = 39.9042,
    gps_lon: float = 116.4074,
    gps_alt: float = 50.0,
    fixed_position: tuple[float, float, float] | None = None,
) -> dict[str, Any]:
    """生成一帧模拟遥测数据。

    坐标系：NED（米），position[2] 为负表示在地面以上。
    """
    # 缓慢圆周运动（可选）
    radius = 5.0 * drone_id if move_circle else 0.0
    omega = 0.1  # rad/s
    if fixed_position is not None:
        ned_n, ned_e, ned_d = fixed_position
    else:
        ned_n = radius * math.cos(omega * t + drone_id)
        ned_e = radius * math.sin(omega * t + drone_id)
        ned_d = -10.0  # 10m 高度

    vn = -radius * omega * math.sin(omega * t + drone_id) if move_circle else 0.0
    ve =  radius * omega * math.cos(omega * t + drone_id) if move_circle else 0.0
    vd = 0.0

    # 偏航角随时间变化（NED 右手系，弧度）
    yaw_ned = omega * t + drone_id
    # 四元数（绕 Z 轴旋转 yaw_ned）
    qw = math.cos(yaw_ned / 2)
    qz = math.sin(yaw_ned / 2)

    return {
        "position": [ned_n, ned_e, ned_d],
        "q": [qw, 0.0, 0.0, qz],          # [w, x, y, z]
        "velocity": [vn, ve, vd],
        "battery": battery,
        "gps_lat": gps_lat + ned_n * 9e-6,  # 粗略 GPS 偏移
        "gps_lon": gps_lon + ned_e * 1.1e-5,
        "gps_alt": gps_alt - ned_d,
        "arming_state": 2,   # ARMED
        "nav_state": 14,     # OFFBOARD
    }


def telemetry_to_yaml(tel: dict[str, Any]) -> str:
    """Emit the subset of YAML accepted by BackEnd/communication/udp_receiver.cpp."""
    timestamp = int(time.time() * 1_000_000)
    position = tel["position"]
    quat = tel["q"]
    velocity = tel["velocity"]
    return (
        f"timestamp: {timestamp}\n"
        f"position: [{position[0]}, {position[1]}, {position[2]}]\n"
        f"q: [{quat[0]}, {quat[1]}, {quat[2]}, {quat[3]}]\n"
        f"velocity: [{velocity[0]}, {velocity[1]}, {velocity[2]}]\n"
        "angular_velocity: [0.0, 0.0, 0.0]\n"
        f"battery: {tel['battery']}\n"
        f"gps_lat: {tel['gps_lat']}\n"
        f"gps_lon: {tel['gps_lon']}\n"
        f"gps_alt: {tel['gps_alt']}\n"
        "gps_fix: true\n"
        "local_position: [0.0, 0.0, 0.0]\n"
        f"arming_state: {tel['arming_state']}\n"
        f"nav_state: {tel['nav_state']}\n"
    )


def udp_recv_port(drone_id: int, base_port: int) -> int:
    return base_port + (drone_id - 1) * 2


def inject_loop(
    drone_id: int,
    base: str,
    udp_host: str,
    hz: float,
    transport: str,
    udp_base_port: int,
    move_circle: bool,
    battery: int,
    fixed_position: tuple[float, float, float] | None,
    stop_event: threading.Event,
) -> None:
    interval = 1.0 / hz
    t = 0.0
    path = f"/api/debug/drone/{drone_id}/inject"
    count = 0
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) if transport == "udp" else None
    udp_addr = (udp_host, udp_recv_port(drone_id, udp_base_port))

    print(
        f"[d{drone_id}] 开始注入遥测，transport={transport}，频率={hz}Hz，"
        f"move_circle={move_circle}，battery={battery}%"
    )

    while not stop_event.is_set():
        tel = make_telemetry(
            drone_id,
            t,
            move_circle=move_circle,
            battery=battery,
            fixed_position=fixed_position,
        )
        if transport == "http":
            result = http_post(base, path, tel)
            if result is None and count == 0:
                print(f"[d{drone_id}] 首次注入失败，请确认无人机已注册且后端正在运行", file=sys.stderr)
        else:
            assert sock is not None
            sock.sendto(telemetry_to_yaml(tel).encode("utf-8"), udp_addr)
        count += 1
        if count % (int(hz) * 5) == 0:  # 每 5 秒打印一次
            pos = tel["position"]
            print(f"[d{drone_id}] t={t:.1f}s  NED=({pos[0]:.2f},{pos[1]:.2f},{pos[2]:.2f})  battery={battery}%")
        t += interval
        stop_event.wait(interval)

    print(f"[d{drone_id}] 注入停止（共 {count} 帧）")
    if sock is not None:
        sock.close()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="向后端持续注入模拟遥测数据（替代真实 Jetson/PX4）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--drones", nargs="+", type=int, default=[1],
        metavar="ID",
        help="要模拟的无人机 ID 列表（默认: 1）",
    )
    parser.add_argument(
        "--hz", type=float, default=10.0,
        help="注入频率，Hz（默认: 10）",
    )
    parser.add_argument(
        "--transport", choices=("http", "udp"), default="http",
        help="注入方式：http 走 /api/debug/drone/{id}/inject；udp 直接发 YAML 到后端遥测端口（默认: http）",
    )
    parser.add_argument(
        "--move", "--move-circle", dest="move_circle", action="store_true",
        help="让无人机缓慢做圆周运动",
    )
    parser.add_argument(
        "--battery", type=int, default=85,
        help="电量百分比（默认: 85；设为 15 可触发低电量告警）",
    )
    parser.add_argument(
        "--position", nargs=3, type=float, metavar=("N", "E", "D"),
        help="固定 NED 位置（米），例如 --position 10 0 5",
    )
    parser.add_argument(
        "--ue-target", nargs=3, type=float, metavar=("X", "Y", "Z"),
        help="固定 UE 偏移目标（厘米），脚本自动转换成 NED 注入，例如 --ue-target 1000 0 -500",
    )
    parser.add_argument(
        "--register", action="store_true",
        help="启动前自动注册缺失无人机（slot 与 ID 相同，仅 HTTP 模式/辅助准备时使用）",
    )
    parser.add_argument(
        "--base", default="http://127.0.0.1:8080",
        help="后端 HTTP 地址（默认: http://127.0.0.1:8080）",
    )
    parser.add_argument(
        "--udp-host", default="127.0.0.1",
        help="UDP YAML 目标主机（默认: 127.0.0.1；端口按 slot 使用 8888/8890/...）",
    )
    parser.add_argument(
        "--udp-base-port", type=int, default=8888,
        help="slot 1 的 UDP YAML 目标端口（默认: 8888；slot 2 自动为 base+2）",
    )
    args = parser.parse_args()

    if args.hz <= 0:
        parser.error("--hz must be > 0")
    if args.position and args.ue_target:
        parser.error("--position and --ue-target cannot be used together")

    fixed_position: tuple[float, float, float] | None = None
    if args.position:
        fixed_position = tuple(args.position)  # type: ignore[assignment]
    elif args.ue_target:
        fixed_position = ue_to_ned(*args.ue_target)

    if args.register:
        ensure_registered(args.base, args.drones)

    stop_event = threading.Event()
    threads = []
    for drone_id in args.drones:
        t = threading.Thread(
            target=inject_loop,
            args=(
                drone_id,
                args.base,
                args.udp_host,
                args.hz,
                args.transport,
                args.udp_base_port,
                args.move_circle,
                args.battery,
                fixed_position,
                stop_event,
            ),
            daemon=True,
        )
        t.start()
        threads.append(t)

    print(f"模拟遥测已启动，按 Ctrl+C 停止。后端: {args.base}")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\n正在停止...")
        stop_event.set()
        for t in threads:
            t.join(timeout=2.0)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
