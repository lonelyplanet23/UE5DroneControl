from __future__ import annotations

import json
import threading
import time
from pathlib import Path
from typing import Any, Callable, Iterable, Optional

from .http_client import BackendHttpClient
from .state import MockUEState
from .ws_client import WebSocketEventClient


Printer = Callable[[str], None]


class MockUEApp:
    def __init__(
        self,
        http_base: str,
        ws_url: str,
        printer: Printer,
        poll_interval: float = 2.0,
        reconnect_delay: float = 2.0,
        verbose_telemetry: bool = False,
    ) -> None:
        self.http_base = http_base.rstrip("/")
        self.ws_url = ws_url
        self.poll_interval = poll_interval
        self.verbose_telemetry = verbose_telemetry
        self.printer = printer

        self.http = BackendHttpClient(self.http_base)
        self.state = MockUEState()
        self.ws = WebSocketEventClient(
            self.ws_url,
            on_status=self._on_ws_status,
            on_message=self._on_ws_message,
            reconnect_delay=reconnect_delay,
        )

        self._stop_event = threading.Event()
        self._poll_thread: Optional[threading.Thread] = None

    def start(self) -> None:
        self._stop_event.clear()
        self.force_sync(log_result=False)
        self.ws.start()
        self._poll_thread = threading.Thread(target=self._poll_loop, name="mock-ue-poll", daemon=True)
        self._poll_thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        self.ws.stop()
        if self._poll_thread is not None:
            self._poll_thread.join(timeout=2.0)

    def _log(self, message: str) -> None:
        self.printer(message)

    def _on_ws_status(self, message: str) -> None:
        self.state.remember(message)
        self._log(message)

    def _on_ws_message(self, payload: dict[str, Any]) -> None:
        message = self.state.handle_ws_message(payload, verbose_telemetry=self.verbose_telemetry)
        if message:
            self._log(message)

    def _poll_loop(self) -> None:
        while not self._stop_event.is_set():
            try:
                self.force_sync(log_result=False)
            except Exception as error:
                self._log(f"轮询失败: {error}")
            time.sleep(self.poll_interval)

    def force_sync(self, log_result: bool = True) -> None:
        payload = self.http.get_drones()
        change = self.state.sync_drones(payload)
        if log_result:
            if change:
                self._log(f"同步完成: {change}")
            else:
                self._log("同步完成: 无变化")

    def health(self) -> dict[str, Any]:
        return self.http.health()

    def list_drones(self) -> str:
        return self.state.format_drones()

    def list_anchors(self) -> str:
        return self.state.format_anchors()

    def list_telemetry(self, drone_id: Optional[str] = None) -> str:
        return self.state.format_telemetry(drone_id)

    def list_alerts(self) -> str:
        return self.state.format_alerts()

    def list_arrays(self) -> str:
        return self.state.format_arrays()

    def list_events(self) -> str:
        return self.state.format_events()

    def get_selected(self) -> list[str]:
        return self.state.get_selected()

    def set_selected(self, drone_ids: list[str]) -> None:
        self.state.set_selected(drone_ids)
        self._log(f"当前选中: {', '.join(drone_ids) if drone_ids else '(空)'}")

    def clear_selected(self) -> None:
        self.set_selected([])

    def register_drone(self, payload: dict[str, Any]) -> dict[str, Any]:
        result = self.http.register_drone(payload)
        self.force_sync(log_result=False)
        self._log(f"注册成功: {result}")
        return result

    def update_drone(self, drone_id: str, payload: dict[str, Any]) -> dict[str, Any]:
        result = self.http.update_drone(drone_id, payload)
        self.force_sync(log_result=False)
        self._log(f"更新成功: {result}")
        return result

    def delete_drone(self, drone_id: str) -> dict[str, Any]:
        result = self.http.delete_drone(drone_id)
        self.force_sync(log_result=False)
        self._log(f"删除成功: {result}")
        return result

    def get_anchor(self, drone_id: str) -> dict[str, Any]:
        result = self.http.get_anchor(drone_id)
        self._log(json.dumps(result, ensure_ascii=False))
        return result

    def move(self, drone_id: str, x: float, y: float, z: float) -> None:
        self.ws.wait_connected()
        self.ws.send_json({
            "type": "move",
            "drone_id": drone_id,
            "x": x,
            "y": y,
            "z": z,
        })
        self._log(f"已发送 move: {drone_id} -> ({x}, {y}, {z}) cm")

    def move_selected(self, x: float, y: float, z: float) -> None:
        selected = self.get_selected()
        if not selected:
            raise RuntimeError("当前没有选中的无人机")
        for drone_id in selected:
            self.move(drone_id, x, y, z)

    def pause(self, drone_ids: Optional[Iterable[str]] = None) -> None:
        ids = list(drone_ids) if drone_ids else self.get_selected()
        if not ids:
            raise RuntimeError("pause 需要显式 drone_id，或先使用 select")
        self.ws.wait_connected()
        self.ws.send_json({
            "type": "pause",
            "drone_ids": ids,
        })
        self._log(f"已发送 pause: {', '.join(ids)}")

    def resume(self, drone_ids: Optional[Iterable[str]] = None) -> None:
        ids = list(drone_ids) if drone_ids else self.get_selected()
        if not ids:
            raise RuntimeError("resume 需要显式 drone_id，或先使用 select")
        self.ws.wait_connected()
        self.ws.send_json({
            "type": "resume",
            "drone_ids": ids,
        })
        self._log(f"已发送 resume: {', '.join(ids)}")

    def submit_array(self, path: str) -> dict[str, Any]:
        payload = self._load_json(path)
        if not isinstance(payload, dict):
            raise RuntimeError("阵列 JSON 顶层必须是 object")
        result = self.http.submit_array(payload)
        self._log(f"阵列已提交: {result}")
        return result

    def stop_array(self, array_id: str) -> dict[str, Any]:
        result = self.http.stop_array(array_id)
        self._log(f"阵列已停止: {result}")
        return result

    def run_script(self, path: str, executor: Callable[[str], None]) -> None:
        script_path = Path(path)
        lines = script_path.read_text(encoding="utf-8").splitlines()
        for line in lines:
            text = line.strip()
            if not text or text.startswith("#"):
                continue
            self._log(f"[script] {text}")
            executor(text)

    def _load_json(self, path: str) -> Any:
        return json.loads(Path(path).read_text(encoding="utf-8"))
