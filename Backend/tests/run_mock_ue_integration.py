from __future__ import annotations

import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from collections import deque
from pathlib import Path
from typing import Any, Callable


BACKEND_ROOT = Path(__file__).resolve().parents[1]
REPO_ROOT = BACKEND_ROOT.parent
MOCK_UE_ROOT = BACKEND_ROOT / "tests" / "mock_ue"
BACKEND_EXE = BACKEND_ROOT / "build" / "DroneBackend.exe"
VCPKG_BIN = BACKEND_ROOT / "vcpkg_installed" / "x64-windows" / "bin"

sys.path.insert(0, str(MOCK_UE_ROOT))

from mock_ue.app import MockUEApp  # noqa: E402


def pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def http_json(
    method: str,
    url: str,
    payload: dict[str, Any] | None = None,
    timeout: float = 5.0,
) -> Any:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        headers["Content-Type"] = "application/json"

    request = urllib.request.Request(url, data=data, headers=headers, method=method)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        text = response.read().decode("utf-8")
        return json.loads(text) if text.strip() else None


def wait_until(
    description: str,
    predicate: Callable[[], bool],
    timeout: float = 10.0,
    interval: float = 0.05,
) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if predicate():
            return
        time.sleep(interval)
    raise RuntimeError(f"timeout waiting for: {description}")


class UdpPacketCapture:
    def __init__(self, host: str, port: int) -> None:
        self.host = host
        self.port = port
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._sock.bind((host, port))
        self._sock.settimeout(0.2)
        self._stop_event = threading.Event()
        self._thread = threading.Thread(target=self._loop, name="udp-capture", daemon=True)
        self._lock = threading.Lock()
        self._packets: deque[dict[str, Any]] = deque(maxlen=200)

    def start(self) -> None:
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        self._thread.join(timeout=2.0)
        self._sock.close()

    def clear(self) -> None:
        with self._lock:
            self._packets.clear()

    def wait_for(self, predicate: Callable[[dict[str, Any]], bool], timeout: float = 5.0) -> dict[str, Any]:
        deadline = time.time() + timeout
        while time.time() < deadline:
            with self._lock:
                for packet in reversed(self._packets):
                    if predicate(packet):
                        return packet
            time.sleep(0.05)
        raise RuntimeError(f"timeout waiting for UDP packet on {self.host}:{self.port}")

    def _loop(self) -> None:
        while not self._stop_event.is_set():
            try:
                data, addr = self._sock.recvfrom(4096)
            except socket.timeout:
                continue

            if len(data) < 24:
                continue

            timestamp, x, y, z, mode = struct.unpack("<dfffI", data[:24])
            packet = {
                "timestamp": timestamp,
                "x": float(x),
                "y": float(y),
                "z": float(z),
                "mode": int(mode),
                "remote_addr": addr[0],
                "remote_port": addr[1],
            }
            with self._lock:
                self._packets.append(packet)


class BackendProcess:
    def __init__(self, config_path: Path) -> None:
        self.config_path = config_path
        self.proc: subprocess.Popen[str] | None = None
        self._output_lock = threading.Lock()
        self._output: list[str] = []
        self._reader_thread: threading.Thread | None = None

    def start(self) -> None:
        env = os.environ.copy()
        env["PATH"] = f"{VCPKG_BIN};{env['PATH']}"
        self.proc = subprocess.Popen(
            [str(BACKEND_EXE), str(self.config_path)],
            cwd=str(BACKEND_ROOT),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        self._reader_thread = threading.Thread(target=self._read_output, name="backend-log", daemon=True)
        self._reader_thread.start()

    def stop(self) -> None:
        if self.proc is None:
            return

        if self.proc.poll() is None:
            self.proc.terminate()
            try:
                self.proc.wait(timeout=5.0)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5.0)

        if self._reader_thread is not None:
            self._reader_thread.join(timeout=1.0)

    def assert_running(self) -> None:
        if self.proc is None:
            raise RuntimeError("backend process not started")
        if self.proc.poll() is not None:
            raise RuntimeError(f"backend exited early with code {self.proc.returncode}\n{self.output()}")

    def output(self) -> str:
        with self._output_lock:
            return "".join(self._output)

    def _read_output(self) -> None:
        assert self.proc is not None
        assert self.proc.stdout is not None
        for line in self.proc.stdout:
            with self._output_lock:
                self._output.append(line)


def write_temp_config(path: Path, http_port: int, ws_port: int, recv_port: int, send_port: int) -> None:
    path.write_text(
        f"""server:
  http_port: {http_port}
  ws_port: {ws_port}
  debug: true

drone:
  max_count: 6
  heartbeat_hz: 2
  lost_timeout_sec: 10
  arrival_threshold_m: 1.0
  assembly_timeout_sec: 2
  avoidance_radius_m: 3.0
  avoidance_lookahead_sec: 2.0
  low_battery_threshold: 20

port_map:
  1:
    send_port: {send_port}
    recv_port: {recv_port}
    ros_topic_prefix: "/px4_1"

jetson:
  host: "127.0.0.1"

storage:
  path: "{(path.parent / 'drones.json').as_posix()}"

log:
  level: "info"
  file: "{(path.parent / 'backend.log').as_posix()}"
""",
        encoding="utf-8",
    )


def main() -> int:
    if not BACKEND_EXE.exists():
        raise RuntimeError(f"backend executable not found: {BACKEND_EXE}")

    http_port = pick_free_port()
    ws_port = pick_free_port()
    recv_port = pick_free_port()
    send_port = pick_free_port()

    http_base = f"http://127.0.0.1:{http_port}"
    ws_url = f"ws://127.0.0.1:{ws_port}/"

    log_messages: list[str] = []

    def printer(message: str) -> None:
        timestamped = f"[mock_ue] {message}"
        log_messages.append(timestamped)
        print(timestamped)

    with tempfile.TemporaryDirectory(prefix="ue5dronecontrol-") as temp_dir:
        temp_path = Path(temp_dir)
        config_path = temp_path / "config.yaml"
        write_temp_config(config_path, http_port, ws_port, recv_port, send_port)

        udp_capture = UdpPacketCapture("127.0.0.1", send_port)
        backend = BackendProcess(config_path)
        app: MockUEApp | None = None

        try:
            udp_capture.start()
            backend.start()

            wait_until(
                "backend health endpoint",
                lambda: _health_ok(http_base, backend),
                timeout=20.0,
                interval=0.2,
            )

            app = MockUEApp(
                http_base=http_base,
                ws_url=ws_url,
                printer=printer,
                poll_interval=0.5,
                reconnect_delay=0.5,
                verbose_telemetry=False,
            )
            app.start()

            wait_until("mock UE websocket connected", lambda: app.ws.is_connected(), timeout=10.0)

            register_result = app.register_drone(
                {
                    "name": "mock-drone-1",
                    "model": "PX4-SITL",
                    "slot": 1,
                    "ip": "127.0.0.1",
                    "port": send_port,
                    "video_url": "",
                }
            )
            assert register_result["id"] == "d1"

            wait_until("drone d1 visible in mock UE registry", lambda: _has_drone(app, "d1"), timeout=5.0)

            inject_payload = {
                "position": [1.0, 2.0, -3.0],
                "q": [0.965925826, 0.0, 0.0, 0.258819045],
                "velocity": [1.0, 0.0, 0.0],
                "battery": 85,
                "gps_lat": 39.9042,
                "gps_lon": 116.4074,
                "gps_alt": 50.0,
            }
            http_json("POST", f"{http_base}/api/debug/drone/d1/inject", inject_payload)

            wait_until("power_on anchor cached by mock UE", lambda: _has_anchor(app, "d1"), timeout=5.0)
            wait_until("telemetry cached by mock UE", lambda: _has_telemetry(app, "d1"), timeout=5.0)

            anchor_payload = app.get_anchor("d1")
            assert abs(anchor_payload["gps_lat"] - 39.9042) < 1e-6
            assert abs(anchor_payload["gps_lon"] - 116.4074) < 1e-6
            assert abs(anchor_payload["gps_alt"] - 50.0) < 1e-6

            telemetry = _get_telemetry(app, "d1")
            assert telemetry is not None
            assert abs(telemetry["x"] - 100.0) < 1e-3
            assert abs(telemetry["y"] - 200.0) < 1e-3
            assert abs(telemetry["z"] - 300.0) < 1e-3
            assert abs(telemetry["yaw"] + 30.0) < 1e-2

            udp_capture.clear()
            app.move("d1", 1000.0, 2000.0, 500.0)
            move_packet = udp_capture.wait_for(
                lambda packet: packet["mode"] == 1
                and abs(packet["x"] - 10.0) < 0.05
                and abs(packet["y"] - 20.0) < 0.05
                and abs(packet["z"] + 5.0) < 0.05,
                timeout=5.0,
            )
            print(f"[udp] move packet: {move_packet}")

            app.pause(["d1"])
            wait_until(
                "queue paused after pause command",
                lambda: http_json("GET", f"{http_base}/api/debug/drone/d1/queue")["paused"] is True,
                timeout=5.0,
            )

            app.resume(["d1"])
            wait_until(
                "queue resumed after resume command",
                lambda: http_json("GET", f"{http_base}/api/debug/drone/d1/queue")["paused"] is False,
                timeout=5.0,
            )

            http_json(
                "POST",
                f"{http_base}/api/debug/drone/d1/inject",
                {
                    "position": [1.0, 2.0, -3.0],
                    "q": [1.0, 0.0, 0.0, 0.0],
                    "velocity": [0.0, 0.0, 0.0],
                    "battery": 15,
                    "gps_lat": 39.9042,
                    "gps_lon": 116.4074,
                    "gps_alt": 50.0,
                },
            )
            wait_until("low_battery alert cached by mock UE", lambda: _has_alert(app, "d1", "low_battery"), timeout=5.0)

            complete_array = {
                "array_id": "array-complete",
                "mode": "patrol",
                "paths": [
                    {
                        "pathId": 1,
                        "drone_id": "d1",
                        "bClosedLoop": False,
                        "waypoints": [
                            {
                                "location": {"x": 0, "y": 0, "z": 0},
                                "segmentSpeed": 0,
                                "waitTime": 0,
                            }
                        ],
                    }
                ],
            }
            http_json("POST", f"{http_base}/api/arrays", complete_array)
            wait_until("array-complete assembling", lambda: _array_status(app, "array-complete") == "ASSEMBLING", timeout=5.0)

            http_json(
                "POST",
                f"{http_base}/api/debug/drone/d1/inject",
                {
                    "position": [0.0, 0.0, 0.0],
                    "q": [1.0, 0.0, 0.0, 0.0],
                    "velocity": [0.0, 0.0, 0.0],
                    "battery": 50,
                    "gps_lat": 39.9042,
                    "gps_lon": 116.4074,
                    "gps_alt": 50.0,
                },
            )
            wait_until("array-complete finished", lambda: _array_status(app, "array-complete") == "ASSEMBLY_COMPLETE", timeout=5.0)
            http_json("POST", f"{http_base}/api/arrays/array-complete/stop", {})

            timeout_array = {
                "array_id": "array-timeout",
                "mode": "recon",
                "paths": [
                    {
                        "pathId": 1,
                        "drone_id": "d1",
                        "bClosedLoop": False,
                        "waypoints": [
                            {
                                "location": {"x": 1000, "y": 0, "z": 0},
                                "segmentSpeed": 0,
                                "waitTime": 0,
                            }
                        ],
                    }
                ],
            }
            http_json("POST", f"{http_base}/api/arrays", timeout_array)
            wait_until("array-timeout emitted", lambda: _array_status(app, "array-timeout") == "ASSEMBLY_TIMEOUT", timeout=8.0)
            http_json("POST", f"{http_base}/api/arrays/array-timeout/stop", {})

            app.delete_drone("d1")
            wait_until("drone d1 removed from mock UE registry", lambda: not _has_drone(app, "d1"), timeout=5.0)

            print("Integration checks passed:")
            print("- register / list / delete")
            print("- power_on anchor + telemetry push")
            print("- GET /api/drones/{id}/anchor")
            print("- WebSocket move + UDP packet emission")
            print("- WebSocket pause / resume")
            print("- low_battery alert push")
            print("- array_complete and assembly_timeout events")
            return 0
        finally:
            if app is not None:
                app.stop()
            backend.stop()
            udp_capture.stop()


def _health_ok(http_base: str, backend: BackendProcess) -> bool:
    backend.assert_running()
    try:
        payload = http_json("GET", f"{http_base}/")
    except Exception:
        return False
    return isinstance(payload, dict) and payload.get("status") == "ok"


def _has_drone(app: MockUEApp, drone_id: str) -> bool:
    with app.state._lock:
        return drone_id in app.state.drones


def _has_anchor(app: MockUEApp, drone_id: str) -> bool:
    with app.state._lock:
        drone = app.state.drones.get(drone_id)
        return drone is not None and drone.anchor is not None


def _has_telemetry(app: MockUEApp, drone_id: str) -> bool:
    with app.state._lock:
        drone = app.state.drones.get(drone_id)
        return drone is not None and drone.telemetry is not None


def _get_telemetry(app: MockUEApp, drone_id: str) -> dict[str, Any] | None:
    with app.state._lock:
        drone = app.state.drones.get(drone_id)
        if drone is None or drone.telemetry is None:
            return None
        return {
            "x": drone.telemetry.x,
            "y": drone.telemetry.y,
            "z": drone.telemetry.z,
            "yaw": drone.telemetry.yaw,
            "pitch": drone.telemetry.pitch,
            "roll": drone.telemetry.roll,
            "speed": drone.telemetry.speed,
            "battery": drone.telemetry.battery,
        }


def _has_alert(app: MockUEApp, drone_id: str, alert_name: str) -> bool:
    with app.state._lock:
        drone = app.state.drones.get(drone_id)
        return drone is not None and alert_name in drone.alerts


def _array_status(app: MockUEApp, array_id: str) -> str | None:
    with app.state._lock:
        array_state = app.state.arrays.get(array_id)
        return None if array_state is None else array_state.status


if __name__ == "__main__":
    raise SystemExit(main())
