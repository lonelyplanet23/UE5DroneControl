from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
from typing import Optional


def now_iso() -> str:
    return datetime.now().strftime("%H:%M:%S")


@dataclass
class AnchorState:
    gps_lat: float
    gps_lon: float
    gps_alt: float
    updated_at: str = field(default_factory=now_iso)


@dataclass
class TelemetryState:
    x: float
    y: float
    z: float
    yaw: float
    pitch: float
    roll: float
    speed: float
    battery: int
    updated_at: str = field(default_factory=now_iso)


@dataclass
class AlertState:
    alert: str
    value: Optional[float]
    updated_at: str = field(default_factory=now_iso)


@dataclass
class ArrayState:
    array_id: str
    status: str
    ready_count: int = 0
    total_count: int = 0
    updated_at: str = field(default_factory=now_iso)


@dataclass
class DroneState:
    drone_id: str
    name: str = ""
    model: str = ""
    slot: int = 0
    ip: str = ""
    port: int = 0
    video_url: str = ""
    status: str = "offline"
    battery: int = -1
    anchor: Optional[AnchorState] = None
    telemetry: Optional[TelemetryState] = None
    alerts: dict[str, AlertState] = field(default_factory=dict)
    last_polled_at: str = field(default_factory=now_iso)
