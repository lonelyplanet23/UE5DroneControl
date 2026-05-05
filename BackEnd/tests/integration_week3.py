from __future__ import annotations

import argparse
import asyncio
import json
import math
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

    req = urllib.request.Request(
        f"http://127.0.0.1:8080{path}",
        data=data,
        headers=headers,
        method=method,
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        text = resp.read().decode("utf-8")
        return json.loads(text) if text.strip() else None


def expect_http_error(status: int, method: str, path: str, payload: Any | None = None) -> None:
    try:
        request(method, path, payload)
    except urllib.error.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        assert exc.code == status, f"{method} {path}: expected {status}, got {exc.code}: {body}"
        return
    raise AssertionError(f"{method} {path}: expected HTTP {status}")


def wait_http_ready(proc: subprocess.Popen[Any], deadline: float = 10.0) -> None:
    until = time.time() + deadline
    last_error = ""
    while time.time() < until:
        if proc.poll() is not None:
            raise RuntimeError(f"backend exited early with code {proc.returncode}")
        try:
            health = request("GET", "/")
            if health.get("status") == "ok":
                return
        except Exception as exc:  # noqa: BLE001 - readiness loop
            last_error = str(exc)
            time.sleep(0.2)
    raise TimeoutError(f"backend did not become ready: {last_error}")


def wait_until(predicate: Any, description: str, timeout: float = 5.0, interval: float = 0.1) -> Any:
    until = time.time() + timeout
    last_value = None
    while time.time() < until:
        last_value = predicate()
        if last_value:
            return last_value
        time.sleep(interval)
    raise AssertionError(f"condition not met: {description}; last={last_value}")


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
    path.write_text(
        """
server:
  http_port: 8080
  ws_port: 8081
  debug: true

drone:
  max_count: 6
  heartbeat_hz: 2
  lost_timeout_sec: 2
  arrival_threshold_m: 1.0
  assembly_timeout_sec: 2
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
  path: "__TMP__/drones.json"

log:
  level: "warn"
  file: "__TMP__/backend.log"
""".replace("__TMP__", str(path.parent).replace("\\", "/")),
        encoding="utf-8",
    )


async def expect_ws(ws: Any, predicate: Any, description: str, timeout: float = 5.0) -> dict[str, Any]:
    deadline = time.time() + timeout
    seen: list[dict[str, Any]] = []
    while time.time() < deadline:
        try:
            raw = await asyncio.wait_for(ws.recv(), timeout=deadline - time.time())
        except asyncio.TimeoutError:
            break
        msg = json.loads(raw)
        seen.append(msg)
        if predicate(msg):
            return msg
    raise AssertionError(f"missing WS message: {description}; seen={seen}")


async def run_checks() -> None:
    async with websockets.connect("ws://127.0.0.1:8081/ws") as ws:
        assert request("GET", "/api/drones") == []

        reg = request(
            "POST",
            "/api/drones",
            {"name": "week3-d1", "model": "PX4", "slot": 1, "ip": "127.0.0.1", "port": 18889},
        )
        assert reg["id"] == 1 and reg["id_str"] == "d1"

        expect_http_error(
            409,
            "POST",
            "/api/drones",
            {"name": "week3-dup", "model": "PX4", "slot": 1, "ip": "127.0.0.1", "port": 18889},
        )

        drones = request("GET", "/api/drones")
        assert isinstance(drones, list) and len(drones) == 1
        assert drones[0]["id"] == 1
        assert drones[0]["id_str"] == "d1"
        assert drones[0]["ue_receive_port"] == 18888
        assert drones[0]["topic_prefix"] == "/px4_1"

        tel = {
            "position": [1.0, 2.0, -3.0],
            "q": [0.965925826, 0.0, 0.0, 0.258819045],
            "velocity": [3.0, 4.0, 0.0],
            "battery": 87,
            "gps_lat": 39.9042,
            "gps_lon": 116.4074,
            "gps_alt": 45.0,
            "arming_state": 2,
            "nav_state": 14,
        }
        request("POST", "/api/debug/drone/1/inject", tel)
        power_on = await expect_ws(
            ws,
            lambda m: m.get("type") == "event" and m.get("event") == "power_on" and m.get("drone_id") == 1,
            "power_on event",
        )
        assert math.isclose(power_on["gps_lat"], 39.9042)

        telemetry = await expect_ws(
            ws,
            lambda m: m.get("type") == "telemetry" and m.get("drone_id") == 1,
            "telemetry",
        )
        assert telemetry["drone_id_str"] == "d1"
        assert math.isclose(telemetry["x"], 100.0)
        assert math.isclose(telemetry["y"], 200.0)
        assert math.isclose(telemetry["z"], 300.0)
        assert math.isclose(telemetry["yaw"], -30.0, abs_tol=0.02)
        assert math.isclose(telemetry["speed"], 5.0)
        assert telemetry["armed"] is True and telemetry["offboard"] is True

        anchor = request("GET", "/api/drones/1/anchor")
        assert anchor["valid"] is True
        assert math.isclose(anchor["gps_lon"], 116.4074)

        await ws.send(json.dumps({"type": "move", "request_id": "move-1", "drone_id": "1", "x": 1000, "y": 2000, "z": 500}))
        await expect_ws(
            ws,
            lambda m: m.get("type") == "command_ack" and m.get("request_id") == "move-1",
            "move ack",
        )
        queue = wait_until(
            lambda: (lambda q: q if q["commands"] else None)(request("GET", "/api/debug/drone/1/queue")),
            "move command queued",
        )
        cmd = queue["commands"][-1]
        assert cmd["mode"] == 1
        assert math.isclose(cmd["x"], 10.0)
        assert math.isclose(cmd["y"], 20.0)
        assert math.isclose(cmd["z"], -5.0)

        await ws.send(json.dumps({"type": "pause", "request_id": "pause-1", "drone_ids": ["1"]}))
        await expect_ws(
            ws,
            lambda m: m.get("type") == "command_ack" and m.get("request_id") == "pause-1",
            "pause ack",
        )
        wait_until(
            lambda: request("GET", "/api/debug/drone/1/state")["queue_paused"] is True,
            "pause command applied",
        )

        await ws.send(json.dumps({"type": "resume", "request_id": "resume-1", "drone_ids": ["1"]}))
        await expect_ws(
            ws,
            lambda m: m.get("type") == "command_ack" and m.get("request_id") == "resume-1",
            "resume ack",
        )
        wait_until(
            lambda: request("GET", "/api/debug/drone/1/state")["queue_paused"] is False,
            "resume command applied",
        )

        await ws.send(json.dumps({"type": "move", "drone_id": "99", "x": 0, "y": 0, "z": 0}))
        await expect_ws(
            ws,
            lambda m: m.get("type") == "error" and m.get("code") == 404,
            "invalid move error",
        )

        low_bat = dict(tel)
        low_bat["battery"] = 15
        request("POST", "/api/debug/drone/1/inject", low_bat)
        await expect_ws(
            ws,
            lambda m: m.get("type") == "alert" and m.get("alert") == "low_battery" and m.get("value") == 15,
            "low battery alert",
        )
        await expect_ws(ws, lambda m: m.get("type") == "telemetry", "low battery telemetry")

        time.sleep(2.4)
        request("GET", "/api/drones")
        await expect_ws(
            ws,
            lambda m: m.get("type") == "event" and m.get("event") == "lost_connection",
            "lost connection",
        )
        await expect_ws(
            ws,
            lambda m: m.get("type") == "alert" and m.get("alert") == "lost_connection",
            "lost alert",
        )
        assert request("GET", "/api/debug/drone/1/state")["status"] == "lost"

        reconnect = dict(tel)
        reconnect["gps_lat"] = 30.0
        reconnect["gps_lon"] = 120.0
        reconnect["gps_alt"] = 50.0
        request("POST", "/api/debug/drone/1/inject", reconnect)
        await expect_ws(
            ws,
            lambda m: m.get("type") == "event" and m.get("event") == "reconnect" and math.isclose(m.get("gps_lon", 0), 120.0),
            "reconnect event",
        )
        await expect_ws(ws, lambda m: m.get("type") == "telemetry", "reconnect telemetry")
        assert math.isclose(request("GET", "/api/drones/1/anchor")["gps_lon"], 120.0)

        array_payload = {
            "array_id": "a-week3",
            "mode": "recon",
            "paths": [
                {
                    "pathId": 1,
                    "drone_id": "1",
                    "bClosedLoop": False,
                    "waypoints": [
                        {"location": {"x": 100, "y": 200, "z": -300}, "segmentSpeed": 3.0, "waitTime": 0.0}
                    ],
                }
            ],
        }
        created = request("POST", "/api/arrays", array_payload)
        assert created["status"] == "assembling"
        await expect_ws(
            ws,
            lambda m: m.get("type") == "assembling" and m.get("array_id") == "a-week3",
            "assembling progress",
        )
        request("POST", "/api/debug/drone/1/inject", {**tel, "position": [1.0, 2.0, 3.0]})
        await expect_ws(
            ws,
            lambda m: m.get("type") == "assembly_complete" and m.get("array_id") == "a-week3",
            "assembly complete",
        )
        request("POST", "/api/arrays/a-week3/stop")

        batch = request(
            "POST",
            "/api/debug/cmd/batch/array",
            [{"drone_id": "1", "mode": "recon", "waypoints": [{"x": 9999, "y": 0, "z": 0}]}],
        )
        assert batch["status"] == "assembling"
        await expect_ws(ws, lambda m: m.get("type") == "assembling", "batch assembling")
        await expect_ws(
            ws,
            lambda m: m.get("type") == "assembly_timeout" and m.get("array_id") == "debug_batch",
            "assembly timeout",
            timeout=5.0,
        )
        request("POST", "/api/arrays/debug_batch/stop")

        delete_result = request("DELETE", "/api/drones/1")
        assert delete_result["deleted"] is True
        assert request("GET", "/api/drones") == []


def main() -> int:
    parser = argparse.ArgumentParser(description="Week 1-3 backend integration test")
    parser.add_argument("--exe", default=str(ROOT / "build" / "Release" / "DroneBackend.exe"))
    parser.add_argument("--keep-backend", action="store_true")
    args = parser.parse_args()

    exe = Path(args.exe)
    if not exe.exists():
        raise FileNotFoundError(f"backend executable not found: {exe}")

    with tempfile.TemporaryDirectory(prefix="ue5drone-week3-") as tmp:
        tmp_path = Path(tmp)
        config = tmp_path / "config.yaml"
        write_test_config(config)

        proc = subprocess.Popen(
            [str(exe), str(config)],
            cwd=str(ROOT),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            wait_http_ready(proc)
            asyncio.run(run_checks())
            print("week1-3 backend integration: PASS")
        finally:
            if not args.keep_backend:
                close_backend(proc)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
