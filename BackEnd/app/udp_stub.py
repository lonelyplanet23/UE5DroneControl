"""
UDP communication stub.

Real implementation will:
- Send 24-byte little-endian control packets to PX4 drones
- Receive YAML telemetry packets from PX4 drones

Packet format (24 bytes, little-endian):
  [0:8]   double  timestamp (seconds)
  [8:12]  float   x (NED North, metres)
  [12:16] float   y (NED East, metres)
  [16:20] float   z (NED Down, metres)
  [20:24] int32   mode (0=hover/heartbeat, 1=move)
"""
from __future__ import annotations
import logging
import socket
import struct
import time

logger = logging.getLogger(__name__)

CONTROL_FMT = "<dfffi"   # 8+4+4+4+4 = 24 bytes


def send_control(ip: str, port: int, x: float, y: float, z: float, mode: int) -> None:
    """Send a 24-byte control packet to a drone."""
    ts = time.time()
    payload = struct.pack(CONTROL_FMT, ts, x, y, z, mode)
    assert len(payload) == 24
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.sendto(payload, (ip, port))
        logger.debug("[UDP] sent 24B -> %s:%d mode=%d x=%.2f y=%.2f z=%.2f", ip, port, mode, x, y, z)
    except OSError as e:
        logger.warning("[UDP] send error %s:%d: %s", ip, port, e)


def send_heartbeat(ip: str, port: int) -> None:
    """Send a mode=0 hover heartbeat packet."""
    logger.debug("[UDP][HB] heartbeat -> %s:%d", ip, port)
    send_control(ip, port, 0.0, 0.0, 0.0, 0)
