from __future__ import annotations
import json
import logging
import os
import sys

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware

# resolve config path relative to project root (BackEnd/)
_BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, _BASE)

from app.config import load_config
from app.registry import Registry
from app.ws_manager import WSManager
from app.routers import drones, arrays, debug
from app.models import Waypoint

cfg = load_config(os.path.join(_BASE, "config.yaml"))

logging.basicConfig(
    level=getattr(logging, cfg.log_level.upper(), logging.INFO),
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
logger = logging.getLogger(__name__)

app = FastAPI(title="DroneControl Backend", version="0.1.0")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# shared state
app.state.config = cfg
app.state.registry = Registry(cfg.max_count, cfg.storage_path)
app.state.ws_manager = WSManager()

# routers
app.include_router(drones.router)
app.include_router(arrays.router)
if cfg.debug:
    app.include_router(debug.router)


@app.get("/")
def root():
    return {"service": "DroneControl Backend", "version": "0.1.0", "debug": cfg.debug}


# ---- WebSocket endpoint ----

@app.websocket("/ws")
async def websocket_endpoint(ws: WebSocket):
    ws_mgr: WSManager = app.state.ws_manager
    reg: Registry = app.state.registry
    await ws_mgr.connect(ws)
    try:
        while True:
            raw = await ws.receive_text()
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                await ws_mgr.send_to(ws, {"type": "error", "message": "invalid JSON"})
                continue

            msg_type = msg.get("type")

            if msg_type == "move":
                drone_id = msg.get("drone_id")
                if not drone_id:
                    await ws_mgr.send_to(ws, {"type": "error", "message": "drone_id required"})
                    continue
                try:
                    ned = Waypoint(
                        x=float(msg.get("x", 0)) * 0.01,
                        y=float(msg.get("y", 0)) * 0.01,
                        z=float(msg.get("z", 0)) * -0.01,
                    )
                    reg.push_command(drone_id, ned)
                    logger.info("[WS] move drone=%s NED=(%.2f,%.2f,%.2f)", drone_id, ned.x, ned.y, ned.z)
                    await ws_mgr.send_to(ws, {"type": "ack", "drone_id": drone_id, "cmd": "move"})
                except KeyError as e:
                    await ws_mgr.send_to(ws, {"type": "error", "message": str(e)})

            elif msg_type in ("pause", "resume"):
                paused = msg_type == "pause"
                ids = msg.get("drone_ids") or ([msg["drone_id"]] if msg.get("drone_id") else [])
                errors = []
                for did in ids:
                    try:
                        reg.set_paused(did, paused)
                    except KeyError as e:
                        errors.append(str(e))
                if errors:
                    await ws_mgr.send_to(ws, {"type": "error", "message": "; ".join(errors)})
                else:
                    logger.info("[WS] %s drones=%s", msg_type, ids)
                    await ws_mgr.send_to(ws, {"type": "ack", "cmd": msg_type, "drone_ids": ids})

            else:
                await ws_mgr.send_to(ws, {"type": "error", "message": f"unknown type: {msg_type}"})

    except WebSocketDisconnect:
        pass
    finally:
        await ws_mgr.disconnect(ws)
