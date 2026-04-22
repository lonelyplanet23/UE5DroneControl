from __future__ import annotations
import logging
from datetime import datetime
from fastapi import APIRouter, HTTPException, Request
from app.models import TelemetryInject, Waypoint, DebugArrayCmd, BatchArrayEntry
from typing import List

logger = logging.getLogger(__name__)
router = APIRouter(prefix="/api/debug", tags=["debug"])


def _reg(request: Request):
    return request.app.state.registry


def _ws(request: Request):
    return request.app.state.ws_manager


# ---- state query ----

@router.get("/drone/{drone_id}/state")
def drone_state(drone_id: str, request: Request):
    try:
        ds = _reg(request).get_state(drone_id)
    except KeyError as e:
        raise HTTPException(404, str(e))
    return {
        "drone": ds.drone,
        "queue_length": len(ds.queue),
        "paused": ds.paused,
        "last_telemetry": ds.last_telemetry,
        "last_telemetry_at": ds.last_telemetry_at.isoformat() if ds.last_telemetry_at else None,
        "heartbeat_count": ds.heartbeat_count,
    }


@router.get("/drone/{drone_id}/queue")
def drone_queue(drone_id: str, request: Request):
    try:
        ds = _reg(request).get_state(drone_id)
    except KeyError as e:
        raise HTTPException(404, str(e))
    return list(ds.queue)


@router.get("/heartbeat/{drone_id}")
def heartbeat_stats(drone_id: str, request: Request):
    try:
        ds = _reg(request).get_state(drone_id)
    except KeyError as e:
        raise HTTPException(404, str(e))
    return {
        "drone_id": drone_id,
        "heartbeat_count": ds.heartbeat_count,
        "last_telemetry_at": ds.last_telemetry_at.isoformat() if ds.last_telemetry_at else None,
    }


# ---- telemetry injection ----

@router.post("/drone/{drone_id}/inject")
async def inject_telemetry(drone_id: str, body: TelemetryInject, request: Request):
    reg = _reg(request)
    ws = _ws(request)
    try:
        ds = reg.inject_telemetry(drone_id, body)
    except KeyError as e:
        raise HTTPException(404, str(e))

    # NED -> UE offset (cm)
    ux = body.position[0] * 100
    uy = body.position[1] * 100
    uz = body.position[2] * -100

    await ws.broadcast({
        "type": "telemetry",
        "drone_id": drone_id,
        "x": ux, "y": uy, "z": uz,
        "yaw": 0.0, "pitch": 0.0, "roll": 0.0,
        "speed": 0.0,
        "battery": body.battery,
    })

    # low battery alert (with dedup)
    cfg = request.app.state.config
    if body.battery < cfg.low_battery_threshold and not ds.low_battery_alerted:
        ds.low_battery_alerted = True
        await ws.broadcast({"type": "alert", "drone_id": drone_id, "alert": "low_battery", "value": body.battery})
    elif body.battery >= cfg.low_battery_threshold:
        ds.low_battery_alerted = False

    # power_on event when GPS is provided
    if body.gps_lat != 0.0 or body.gps_lon != 0.0:
        await ws.broadcast({
            "type": "event", "drone_id": drone_id, "event": "power_on",
            "gps_lat": body.gps_lat, "gps_lon": body.gps_lon, "gps_alt": body.gps_alt,
        })

    return {"status": "injected"}


# ---- command simulation ----

@router.post("/cmd/{drone_id}/move")
def cmd_move(drone_id: str, body: Waypoint, request: Request):
    reg = _reg(request)
    ned = Waypoint(x=body.x * 0.01, y=body.y * 0.01, z=body.z * -0.01)
    try:
        reg.push_command(drone_id, ned)
    except KeyError as e:
        raise HTTPException(404, str(e))
    logger.info("[STUB] move cmd drone=%s NED=(%.2f,%.2f,%.2f)", drone_id, ned.x, ned.y, ned.z)
    return {"status": "queued", "ned": ned}


@router.post("/cmd/{drone_id}/pause")
def cmd_pause(drone_id: str, request: Request):
    try:
        _reg(request).set_paused(drone_id, True)
    except KeyError as e:
        raise HTTPException(404, str(e))
    logger.info("[STUB] pause drone=%s", drone_id)
    return {"status": "paused"}


@router.post("/cmd/{drone_id}/resume")
def cmd_resume(drone_id: str, request: Request):
    try:
        _reg(request).set_paused(drone_id, False)
    except KeyError as e:
        raise HTTPException(404, str(e))
    logger.info("[STUB] resume drone=%s", drone_id)
    return {"status": "resumed"}


@router.post("/cmd/{drone_id}/array")
def cmd_array(drone_id: str, body: DebugArrayCmd, request: Request):
    try:
        _reg(request).get(drone_id)
    except KeyError as e:
        raise HTTPException(404, str(e))
    logger.info("[STUB] array cmd drone=%s mode=%s waypoints=%d", drone_id, body.mode, len(body.waypoints))
    return {"status": "accepted", "drone_id": drone_id, "mode": body.mode}


@router.post("/cmd/{drone_id}/target")
def cmd_target(drone_id: str, body: Waypoint, request: Request):
    try:
        _reg(request).get(drone_id)
    except KeyError as e:
        raise HTTPException(404, str(e))
    logger.info("[STUB] target event drone=%s UE=(%.0f,%.0f,%.0f)", drone_id, body.x, body.y, body.z)
    return {"status": "target_injected", "drone_id": drone_id}


@router.post("/cmd/batch/array")
def cmd_batch_array(body: List[BatchArrayEntry], request: Request):
    reg = _reg(request)
    results = []
    for entry in body:
        try:
            reg.get(entry.drone_id)
            results.append({"drone_id": entry.drone_id, "status": "accepted", "mode": entry.mode})
        except KeyError:
            results.append({"drone_id": entry.drone_id, "status": "not_found"})
    logger.info("[STUB] batch array: %d drones", len(body))
    return results


@router.get("/arrays/{array_id}/state")
def array_state(array_id: str):
    # stub: always ASSEMBLING
    return {"array_id": array_id, "status": "ASSEMBLING"}
