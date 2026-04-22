from __future__ import annotations
from enum import Enum
from typing import List, Optional
from pydantic import BaseModel


class DroneStatus(str, Enum):
    offline = "offline"
    connecting = "connecting"
    online = "online"
    lost = "lost"


class Drone(BaseModel):
    id: str
    name: str
    model: str
    ip: str
    port: int
    video_url: str = ""
    slot_number: int          # 1~6
    status: DroneStatus = DroneStatus.offline
    battery: int = 0
    gps_lat: float = 0.0
    gps_lon: float = 0.0
    gps_alt: float = 0.0


class RegisterRequest(BaseModel):
    name: str
    model: str
    ip: str
    port: int
    video_url: str = ""
    slot_number: int


class UpdateRequest(BaseModel):
    name: Optional[str] = None
    model: Optional[str] = None
    ip: Optional[str] = None
    port: Optional[int] = None
    video_url: Optional[str] = None


class Waypoint(BaseModel):
    x: float
    y: float
    z: float


class TelemetryInject(BaseModel):
    position: List[float]     # NED metres [N, E, D]
    q: List[float]            # [w, x, y, z]
    velocity: List[float]
    battery: int
    gps_lat: float = 0.0
    gps_lon: float = 0.0
    gps_alt: float = 0.0


class SlotEntry(BaseModel):
    drone_id: str
    waypoints: List[Waypoint]


class ArrayTask(BaseModel):
    mode: str                 # recon | patrol | attack
    loop: bool = False
    slots: List[SlotEntry]


class DebugArrayCmd(BaseModel):
    mode: str
    loop: bool = False
    waypoints: List[Waypoint]


class BatchArrayEntry(BaseModel):
    drone_id: str
    mode: str
    waypoints: List[Waypoint]
