from __future__ import annotations

from collections import deque
from threading import Lock
from typing import Any, Optional

from .models import AlertState, AnchorState, ArrayState, DroneState, TelemetryState, now_iso


class MockUEState:
    def __init__(self) -> None:
        self._lock = Lock()
        self.drones: dict[str, DroneState] = {}
        self.arrays: dict[str, ArrayState] = {}
        self.events: deque[str] = deque(maxlen=100)
        self.selected_ids: list[str] = []

    def remember(self, message: str) -> None:
        with self._lock:
            self.events.appendleft(f"[{now_iso()}] {message}")

    def set_selected(self, drone_ids: list[str]) -> None:
        with self._lock:
            self.selected_ids = drone_ids

    def get_selected(self) -> list[str]:
        with self._lock:
            return list(self.selected_ids)

    def sync_drones(self, payload: dict[str, Any]) -> Optional[str]:
        drones = payload.get("drones", [])
        if not isinstance(drones, list):
            return None

        changes: list[str] = []
        seen: set[str] = set()
        with self._lock:
            for item in drones:
                if not isinstance(item, dict):
                    continue

                drone_id = str(item.get("id", ""))
                if not drone_id:
                    continue
                seen.add(drone_id)

                state = self.drones.get(drone_id)
                if state is None:
                    state = DroneState(drone_id=drone_id)
                    self.drones[drone_id] = state
                    changes.append(f"新增无人机 {drone_id}")

                previous_status = state.status
                previous_battery = state.battery

                state.name = str(item.get("name", state.name))
                state.model = str(item.get("model", state.model))
                state.slot = int(item.get("slot", state.slot or 0))
                state.ip = str(item.get("ip", state.ip))
                state.port = int(item.get("port", state.port or 0))
                state.video_url = str(item.get("video_url", state.video_url))
                state.status = str(item.get("status", state.status))
                state.battery = int(item.get("battery", state.battery))
                state.last_polled_at = now_iso()

                if previous_status and previous_status != state.status:
                    changes.append(f"{drone_id} 状态变更: {previous_status} -> {state.status}")
                if previous_battery != -1 and previous_battery != state.battery:
                    changes.append(f"{drone_id} 电量更新: {previous_battery}% -> {state.battery}%")

            for drone_id in list(self.drones.keys()):
                if drone_id not in seen:
                    del self.drones[drone_id]
                    changes.append(f"无人机已从注册表移除: {drone_id}")

            for message in changes:
                self.events.appendleft(f"[{now_iso()}] {message}")

        if not changes:
            return None
        return "; ".join(changes)

    def handle_ws_message(self, message: dict[str, Any], verbose_telemetry: bool = False) -> Optional[str]:
        message_type = str(message.get("type", ""))
        if not message_type:
            return None

        drone_id = str(message.get("drone_id", ""))
        with self._lock:
            if drone_id and drone_id not in self.drones:
                self.drones[drone_id] = DroneState(drone_id=drone_id)
            drone = self.drones.get(drone_id)

            if message_type == "telemetry" and drone is not None:
                drone.telemetry = TelemetryState(
                    x=float(message.get("x", 0.0)),
                    y=float(message.get("y", 0.0)),
                    z=float(message.get("z", 0.0)),
                    yaw=float(message.get("yaw", 0.0)),
                    pitch=float(message.get("pitch", 0.0)),
                    roll=float(message.get("roll", 0.0)),
                    speed=float(message.get("speed", 0.0)),
                    battery=int(message.get("battery", drone.battery)),
                )
                drone.battery = drone.telemetry.battery
                drone.status = "online"
                if verbose_telemetry:
                    text = (
                        f"遥测 {drone_id}: pos=({drone.telemetry.x:.1f}, {drone.telemetry.y:.1f}, {drone.telemetry.z:.1f}) "
                        f"yaw={drone.telemetry.yaw:.1f} speed={drone.telemetry.speed:.2f} battery={drone.telemetry.battery}%"
                    )
                    self.events.appendleft(f"[{now_iso()}] {text}")
                    return text
                return None

            if message_type == "event" and drone is not None:
                event_name = str(message.get("event", ""))
                if event_name in {"power_on", "reconnect"}:
                    drone.anchor = AnchorState(
                        gps_lat=float(message.get("gps_lat", 0.0)),
                        gps_lon=float(message.get("gps_lon", 0.0)),
                        gps_alt=float(message.get("gps_alt", 0.0)),
                    )
                if event_name == "lost_connection":
                    drone.status = "lost"
                elif event_name in {"power_on", "reconnect"}:
                    drone.status = "online"

                text = f"事件 {drone_id}: {event_name}"
                self.events.appendleft(f"[{now_iso()}] {text}")
                return text

            if message_type == "alert" and drone is not None:
                alert_name = str(message.get("alert", ""))
                value = message.get("value")
                drone.alerts[alert_name] = AlertState(
                    alert=alert_name,
                    value=float(value) if isinstance(value, (int, float)) else None,
                )
                text = f"告警 {drone_id}: {alert_name}"
                if isinstance(value, (int, float)):
                    text += f" ({value})"
                self.events.appendleft(f"[{now_iso()}] {text}")
                return text

            if message_type in {"assembling", "assembly_complete", "assembly_timeout"}:
                array_id = str(message.get("array_id", ""))
                if array_id:
                    status = message_type.upper()
                    self.arrays[array_id] = ArrayState(
                        array_id=array_id,
                        status=status,
                        ready_count=int(message.get("ready_count", self.arrays.get(array_id, ArrayState(array_id, status)).ready_count)),
                        total_count=int(message.get("total_count", self.arrays.get(array_id, ArrayState(array_id, status)).total_count)),
                    )
                    if message_type == "assembling":
                        text = f"集结进度 {array_id}: {self.arrays[array_id].ready_count}/{self.arrays[array_id].total_count}"
                    elif message_type == "assembly_complete":
                        text = f"集结完成 {array_id}"
                    else:
                        text = f"集结超时 {array_id}: {self.arrays[array_id].ready_count}/{self.arrays[array_id].total_count}"
                    self.events.appendleft(f"[{now_iso()}] {text}")
                    return text

        return None

    def format_drones(self) -> str:
        with self._lock:
            if not self.drones:
                return "当前本地注册表为空。"

            lines = [
                "id   slot  name            status      battery  anchor  telemetry",
                "---  ----  --------------  ----------  -------  ------  ---------",
            ]
            for drone in sorted(self.drones.values(), key=lambda item: (item.slot, item.drone_id)):
                lines.append(
                    f"{drone.drone_id:<3}  "
                    f"{drone.slot:<4}  "
                    f"{drone.name[:14]:<14}  "
                    f"{drone.status[:10]:<10}  "
                    f"{str(drone.battery) + '%':<7}  "
                    f"{'Y' if drone.anchor else 'N':<6}  "
                    f"{'Y' if drone.telemetry else 'N':<9}"
                )
            return "\n".join(lines)

    def format_anchors(self) -> str:
        with self._lock:
            lines = []
            for drone in sorted(self.drones.values(), key=lambda item: (item.slot, item.drone_id)):
                if drone.anchor is None:
                    continue
                lines.append(
                    f"{drone.drone_id}: "
                    f"lat={drone.anchor.gps_lat:.6f}, "
                    f"lon={drone.anchor.gps_lon:.6f}, "
                    f"alt={drone.anchor.gps_alt:.2f} "
                    f"(updated {drone.anchor.updated_at})"
                )
            return "\n".join(lines) if lines else "当前没有已缓存的 anchor。"

    def format_telemetry(self, drone_id: Optional[str] = None) -> str:
        with self._lock:
            drones = []
            if drone_id:
                drone = self.drones.get(drone_id)
                if drone is not None:
                    drones = [drone]
            else:
                drones = sorted(self.drones.values(), key=lambda item: (item.slot, item.drone_id))

            lines = []
            for drone in drones:
                if drone.telemetry is None:
                    continue
                lines.append(
                    f"{drone.drone_id}: "
                    f"x={drone.telemetry.x:.1f} y={drone.telemetry.y:.1f} z={drone.telemetry.z:.1f} "
                    f"yaw={drone.telemetry.yaw:.1f} pitch={drone.telemetry.pitch:.1f} roll={drone.telemetry.roll:.1f} "
                    f"speed={drone.telemetry.speed:.2f} battery={drone.telemetry.battery}% "
                    f"(updated {drone.telemetry.updated_at})"
                )
            return "\n".join(lines) if lines else "当前没有遥测缓存。"

    def format_alerts(self) -> str:
        with self._lock:
            lines = []
            for drone in sorted(self.drones.values(), key=lambda item: (item.slot, item.drone_id)):
                alerts = getattr(drone, "alerts", {})
                for alert in alerts.values():
                    value = "" if alert.value is None else f", value={alert.value}"
                    lines.append(f"{drone.drone_id}: {alert.alert}{value} (updated {alert.updated_at})")
            return "\n".join(lines) if lines else "当前没有活跃告警缓存。"

    def format_arrays(self) -> str:
        with self._lock:
            if not self.arrays:
                return "当前没有阵列状态缓存。"
            lines = []
            for array_state in sorted(self.arrays.values(), key=lambda item: item.array_id):
                lines.append(
                    f"{array_state.array_id}: {array_state.status} "
                    f"{array_state.ready_count}/{array_state.total_count} "
                    f"(updated {array_state.updated_at})"
                )
            return "\n".join(lines)

    def format_events(self) -> str:
        with self._lock:
            return "\n".join(self.events) if self.events else "当前没有事件日志。"
