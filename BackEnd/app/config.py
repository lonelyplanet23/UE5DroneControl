from __future__ import annotations
import os
import yaml
from dataclasses import dataclass, field
from typing import Dict


@dataclass
class PortEntry:
    send_port: int
    recv_port: int


@dataclass
class Config:
    http_port: int = 8080
    ws_port: int = 8081          # unused: FastAPI serves WS on same port
    debug: bool = True
    max_count: int = 6
    heartbeat_hz: int = 2
    lost_timeout_sec: int = 10
    arrival_threshold_m: float = 1.0
    assembly_timeout_sec: int = 60
    avoidance_radius_m: float = 3.0
    avoidance_lookahead_sec: float = 2.0
    low_battery_threshold: int = 20
    port_map: Dict[int, PortEntry] = field(default_factory=dict)
    storage_path: str = "./data/drones.json"
    log_level: str = "info"
    log_file: str = ""


def load_config(path: str = "config.yaml") -> Config:
    if not os.path.exists(path):
        return Config()
    with open(path, "r", encoding="utf-8") as f:
        raw = yaml.safe_load(f)

    cfg = Config()
    srv = raw.get("server", {})
    cfg.http_port = srv.get("http_port", cfg.http_port)
    cfg.ws_port = srv.get("ws_port", cfg.ws_port)
    cfg.debug = srv.get("debug", cfg.debug)

    drone = raw.get("drone", {})
    cfg.max_count = drone.get("max_count", cfg.max_count)
    cfg.heartbeat_hz = drone.get("heartbeat_hz", cfg.heartbeat_hz)
    cfg.lost_timeout_sec = drone.get("lost_timeout_sec", cfg.lost_timeout_sec)
    cfg.arrival_threshold_m = drone.get("arrival_threshold_m", cfg.arrival_threshold_m)
    cfg.assembly_timeout_sec = drone.get("assembly_timeout_sec", cfg.assembly_timeout_sec)
    cfg.avoidance_radius_m = drone.get("avoidance_radius_m", cfg.avoidance_radius_m)
    cfg.avoidance_lookahead_sec = drone.get("avoidance_lookahead_sec", cfg.avoidance_lookahead_sec)
    cfg.low_battery_threshold = drone.get("low_battery_threshold", cfg.low_battery_threshold)

    for slot, ports in raw.get("port_map", {}).items():
        cfg.port_map[int(slot)] = PortEntry(
            send_port=ports["send_port"],
            recv_port=ports["recv_port"],
        )

    storage = raw.get("storage", {})
    cfg.storage_path = storage.get("path", cfg.storage_path)

    log = raw.get("log", {})
    cfg.log_level = log.get("level", cfg.log_level)
    cfg.log_file = log.get("file", cfg.log_file)

    return cfg
