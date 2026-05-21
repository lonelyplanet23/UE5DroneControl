from __future__ import annotations

import argparse
import asyncio
import json
import os
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

import websockets

ROOT = Path(__file__).resolve().parents[1]


def request(method: str, path: str, payload: Any | None = None, timeout: float = 5.0) -> Any:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(f"http://127.0.0.1:8080{path}", data=data, headers=headers, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        text = resp.read().decode("utf-8")
        return json.loads(text) if text.strip() else None


def wait_http_ready(proc: subprocess.Popen[Any], deadline: float = 10.0) -> None:
    until = time.time() + deadline
    while time.time() < until:
        if proc.poll() is not None:
            raise RuntimeError(f"backend exited early with code {proc.returncode}")
        try:
            if request("GET", "/").get("status") == "ok":
                return
        except Exception:
            time.sleep(0.2)
    raise TimeoutError("backend did not become ready")


def close_backend(proc: subprocess.Popen[Any]) -> None:
    if proc.poll() is not None:
        return
    if os.name == "nt":
        proc.terminate()
    else:
        proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def write_test_config(path: Path) -> None:
    tmp = path.parent.as_posix()
    path.write_text(
        f"""
server:
  http_port: 8080
  ws_port: 8081
  debug: true

drone:
  max_count: 6
  heartbeat_hz: 2
  lost_timeout_sec: 3
  arrival_threshold_m: 0.5
  assembly_timeout_sec: 3
  avoidance_radius_m: 3.0
  avoidance_lookahead_sec: 2.0
  low_battery_threshold: 20

port_map:
  1:
    send_port: 18889
    recv_port: 18888
    ros_topic_prefix: "/px4_1"
  2:
    send_port: 18891
    recv_port: 18890
    ros_topic_prefix: "/px4_2"

jetson:
  host: "127.0.0.1"

storage:
  path: "{tmp}/drones.json"

log:
  level: "warn"
  file: "{tmp}/backend.log"
""",
        encoding="utf-8",
    )


async def expect_ws(ws: Any, predicate: Any, description: str, timeout: float = 5.0) -> dict[str, Any]:
    deadline = time.time() + timeout
    seen: list[dict[str, Any]] = []
    while time.time() < deadline:
        try:
            raw = await asyncio.wait_for(ws.recv(), timeout=max(0.05, deadline - time.time()))
        except asyncio.TimeoutError:
            break
        msg = json.loads(raw)
        seen.append(msg)
        if predicate(msg):
            return msg
    raise AssertionError(f"missing WS message: {description}; seen={seen}")


def inject(drone_id: int, ned_n: float, ned_e: float, ned_d: float, battery: int = 85) -> None:
    request(
        "POST",
        f"/api/debug/drone/{drone_id}/inject",
        {
            "position": [ned_n, ned_e, ned_d],
            "q": [1, 0, 0, 0],
            "velocity": [0, 0, 0],
            "battery": battery,
            "gps_lat": 39.9,
            "gps_lon": 116.3,
            "gps_alt": 50.0,
        },
    )


def inject_at_target(drone_id: int, ue_x: float, ue_y: float, ue_z: float) -> None:
    inject(drone_id, ue_x / 100.0, ue_y / 100.0, -ue_z / 100.0)


async def run_checks() -> None:
    async with websockets.connect("ws://127.0.0.1:8081/ws") as ws:
        request("POST", "/api/drones", {"name": "w5-d1", "slot": 1, "port": 18889})
        request("POST", "/api/drones", {"name": "w5-d2", "slot": 2, "port": 18891})

        inject(1, 0, 0, -5)
        inject(2, 0, 0, -5)
        await expect_ws(ws, lambda m: m.get("type") == "event" and m.get("event") == "power_on", "power_on")

        preview = request(
            "POST",
            "/api/arrays/preview",
            {
                "array_id": "w5-preview",
                "mode": "scout",
                "paths": [
                    {
                        "pathId": 1,
                        "drone_id": "d1",
                        "waypoints": [
                            {"location": {"x": 100, "y": 0, "z": -500}},
                            {"location": {"x": 300, "y": 0, "z": -500}},
                        ],
                    },
                    {
                        "pathId": 2,
                        "drone_id": "d2",
                        "waypoints": [
                            {"location": {"x": 120, "y": 0, "z": -500}},
                            {"location": {"x": 320, "y": 0, "z": -500}},
                        ],
                    },
                ],
            },
        )
        assert preview["valid"] is False
        assert preview["collision_risks"], preview

        clean_preview = request(
            "POST",
            "/api/arrays/preview",
            {
                "array_id": "w5-clean",
                "mode": "scout",
                "paths": [
                    {
                        "pathId": 1,
                        "drone_id": "d1",
                        "waypoints": [
                            {"location": {"x": 100, "y": 0, "z": -500}},
                            {"location": {"x": 200, "y": 0, "z": -500}},
                        ],
                    },
                    {
                        "pathId": 2,
                        "drone_id": "d2",
                        "waypoints": [
                            {"location": {"x": 500, "y": 0, "z": -500}},
                            {"location": {"x": 700, "y": 0, "z": -500}},
                        ],
                    },
                ],
            },
        )
        assert clean_preview["valid"] is True
        assert clean_preview["collision_risks"] == []

        created = request(
            "POST",
            "/api/arrays",
            {
                "array_id": "w5-run",
                "mode": "scout",
                "paths": [
                    {
                        "pathId": 1,
                        "drone_id": "d1",
                        "waypoints": [{"location": {"x": 100, "y": 0, "z": -500}}],
                    },
                    {
                        "pathId": 2,
                        "drone_id": "d2",
                        "waypoints": [{"location": {"x": 500, "y": 0, "z": -500}}],
                    },
                ],
            },
        )
        assert created["status"] == "assembling"
        state = request("GET", "/api/debug/arrays/w5-run/state")
        assert state["assembly_state"] in {"assembling", "executing"}
        metrics = request("GET", "/api/debug/metrics")
        assert metrics["registered_drones"] == 2

        inject_at_target(1, 100, 0, -500)
        inject_at_target(2, 500, 0, -500)
        time.sleep(0.5)
        avoidance = request("GET", "/api/debug/avoidance")
        assert "events_total" in avoidance

        request("POST", "/api/arrays/w5-run/stop")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=str(ROOT / "BackEnd" / "build" / "Release" / "DroneBackend.exe"))
    args = parser.parse_args()

    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp = Path(tmp_dir)
        config_path = tmp / "config.yaml"
        write_test_config(config_path)
        proc = subprocess.Popen([args.exe, str(config_path)], cwd=str(ROOT))
        try:
            wait_http_ready(proc)
            asyncio.run(run_checks())
            print("ALL PASS")
            return 0
        finally:
            close_backend(proc)


if __name__ == "__main__":
    raise SystemExit(main())
