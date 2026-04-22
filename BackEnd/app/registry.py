from __future__ import annotations
import json
import os
import threading
from collections import deque
from datetime import datetime
from typing import Dict, Deque, Optional

from app.models import Drone, DroneStatus, RegisterRequest, UpdateRequest, TelemetryInject, Waypoint


class DroneState:
    def __init__(self, drone: Drone):
        self.drone = drone
        self.queue: Deque[Waypoint] = deque(maxlen=32)
        self.last_telemetry: Optional[TelemetryInject] = None
        self.last_telemetry_at: Optional[datetime] = None
        self.heartbeat_count: int = 0
        self.paused: bool = False
        self.low_battery_alerted: bool = False
        self.lost_alerted: bool = False


class Registry:
    def __init__(self, max_count: int, save_path: str):
        self._lock = threading.RLock()
        self._drones: Dict[str, DroneState] = {}
        self._slots: Dict[int, str] = {}   # slot_number -> drone_id
        self._next_id: int = 1
        self._max_count = max_count
        self._save_path = save_path
        self._load()

    # ---- CRUD ----

    def register(self, req: RegisterRequest) -> Drone:
        with self._lock:
            if len(self._drones) >= self._max_count:
                raise ValueError("maximum drone count reached")
            for ds in self._drones.values():
                if ds.drone.name == req.name:
                    raise ValueError("drone name already exists")
            if req.slot_number in self._slots:
                raise ValueError("slot number already occupied")

            drone_id = f"d{self._next_id}"
            self._next_id += 1
            drone = Drone(
                id=drone_id,
                name=req.name,
                model=req.model,
                ip=req.ip,
                port=req.port,
                video_url=req.video_url,
                slot_number=req.slot_number,
            )
            self._drones[drone_id] = DroneState(drone)
            self._slots[req.slot_number] = drone_id
            self._save()
            return drone

    def list_drones(self) -> list[Drone]:
        with self._lock:
            return [ds.drone for ds in self._drones.values()]

    def get(self, drone_id: str) -> Drone:
        with self._lock:
            ds = self._drones.get(drone_id)
            if ds is None:
                raise KeyError(f"drone {drone_id} not found")
            return ds.drone

    def update(self, drone_id: str, req: UpdateRequest) -> Drone:
        with self._lock:
            ds = self._drones.get(drone_id)
            if ds is None:
                raise KeyError(f"drone {drone_id} not found")
            if req.name is not None:
                ds.drone.name = req.name
            if req.model is not None:
                ds.drone.model = req.model
            if req.ip is not None:
                ds.drone.ip = req.ip
            if req.port is not None:
                ds.drone.port = req.port
            if req.video_url is not None:
                ds.drone.video_url = req.video_url
            self._save()
            return ds.drone

    def delete(self, drone_id: str) -> None:
        with self._lock:
            ds = self._drones.get(drone_id)
            if ds is None:
                raise KeyError(f"drone {drone_id} not found")
            del self._slots[ds.drone.slot_number]
            del self._drones[drone_id]
            self._save()

    def get_state(self, drone_id: str) -> DroneState:
        with self._lock:
            ds = self._drones.get(drone_id)
            if ds is None:
                raise KeyError(f"drone {drone_id} not found")
            return ds

    def inject_telemetry(self, drone_id: str, t: TelemetryInject) -> DroneState:
        with self._lock:
            ds = self._drones.get(drone_id)
            if ds is None:
                raise KeyError(f"drone {drone_id} not found")
            ds.last_telemetry = t
            ds.last_telemetry_at = datetime.utcnow()
            ds.drone.battery = t.battery
            ds.drone.gps_lat = t.gps_lat
            ds.drone.gps_lon = t.gps_lon
            ds.drone.gps_alt = t.gps_alt
            if ds.drone.status in (DroneStatus.offline, DroneStatus.lost):
                ds.drone.status = DroneStatus.online
                ds.lost_alerted = False
            return ds

    def push_command(self, drone_id: str, w: Waypoint) -> None:
        with self._lock:
            ds = self._drones.get(drone_id)
            if ds is None:
                raise KeyError(f"drone {drone_id} not found")
            ds.queue.append(w)

    def set_paused(self, drone_id: str, paused: bool) -> None:
        with self._lock:
            ds = self._drones.get(drone_id)
            if ds is None:
                raise KeyError(f"drone {drone_id} not found")
            ds.paused = paused

    # ---- persistence ----

    def _save(self) -> None:
        try:
            os.makedirs(os.path.dirname(self._save_path) or ".", exist_ok=True)
            data = {
                "next_id": self._next_id,
                "drones": [ds.drone.model_dump() for ds in self._drones.values()],
            }
            with open(self._save_path, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)
        except Exception as e:
            print(f"[registry] save error: {e}")

    def _load(self) -> None:
        if not os.path.exists(self._save_path):
            return
        try:
            with open(self._save_path, "r", encoding="utf-8") as f:
                data = json.load(f)
            self._next_id = data.get("next_id", 1)
            for d in data.get("drones", []):
                drone = Drone(**d)
                self._drones[drone.id] = DroneState(drone)
                self._slots[drone.slot_number] = drone.id
        except Exception as e:
            print(f"[registry] load error: {e}")
