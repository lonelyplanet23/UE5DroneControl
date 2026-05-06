"""
Mock backend server for UE5DroneControl frontend testing.
Provides:
  - HTTP REST: GET/POST/PUT/DELETE /api/drones  (port 8080)
  - WebSocket: ws://127.0.0.1:8081/ws  (telemetry push at 10Hz, receives control commands)

Usage:
  pip install flask websockets
  python mock_server.py
"""

import asyncio
import json
import math
import threading
import time
from flask import Flask, jsonify, request

# ---- Fake drone data ----
_next_id = 3
DRONES = [
    {"id": 1, "name": "UAV1", "mavlink_system_id": 2, "bit_index": 0,
     "ue_receive_port": 8888, "topic_prefix": "/px4_1"},
    {"id": 2, "name": "UAV2", "mavlink_system_id": 3, "bit_index": 1,
     "ue_receive_port": 8890, "topic_prefix": "/px4_2"},
]

# ---- HTTP server (Flask, port 8080) ----
app = Flask(__name__)

@app.route("/api/drones", methods=["GET"])
def get_drones():
    print("[HTTP] GET /api/drones")
    return jsonify(DRONES)

@app.route("/api/drones", methods=["POST"])
def create_drone():
    global _next_id
    data = request.get_json(force=True) or {}
    drone = {
        "id": _next_id,
        "name": data.get("name", f"UAV{_next_id}"),
        "mavlink_system_id": data.get("mavlink_system_id", _next_id + 1),
        "bit_index": data.get("bit_index", _next_id - 1),
        "ue_receive_port": data.get("ue_receive_port", 8888 + (_next_id - 1) * 2),
        "topic_prefix": data.get("topic_prefix", f"/px4_{_next_id}"),
    }
    DRONES.append(drone)
    _next_id += 1
    print(f"[HTTP] POST /api/drones -> created id={drone['id']} name={drone['name']}")
    return jsonify(drone), 201

@app.route("/api/drones/<int:drone_id>", methods=["PUT"])
def update_drone(drone_id):
    data = request.get_json(force=True) or {}
    for drone in DRONES:
        if drone["id"] == drone_id:
            drone.update({k: v for k, v in data.items() if k != "id"})
            print(f"[HTTP] PUT /api/drones/{drone_id} -> updated: {data}")
            return jsonify(drone)
    print(f"[HTTP] PUT /api/drones/{drone_id} -> not found")
    return jsonify({"error": "not found"}), 404

@app.route("/api/drones/<int:drone_id>", methods=["DELETE"])
def delete_drone(drone_id):
    for i, drone in enumerate(DRONES):
        if drone["id"] == drone_id:
            DRONES.pop(i)
            print(f"[HTTP] DELETE /api/drones/{drone_id} -> deleted")
            return jsonify({"ok": True})
    print(f"[HTTP] DELETE /api/drones/{drone_id} -> not found")
    return jsonify({"error": "not found"}), 404

@app.route("/api/drones/<int:drone_id>/anchor", methods=["GET"])
def get_anchor(drone_id):
    print(f"[HTTP] GET /api/drones/{drone_id}/anchor")
    return jsonify({"drone_id": drone_id, "lat": 39.9, "lon": 116.3, "alt": 50.0})

@app.route("/api/arrays", methods=["POST"])
def create_array():
    data = request.get_json(force=True) or {}
    paths = data.get("paths", [])
    print(f"[HTTP] POST /api/arrays -> received {len(paths)} path(s)")
    for p in paths:
        print(f"  drone_id={p.get('drone_id')}  pathId={p.get('pathId')}  "
              f"waypoints={len(p.get('waypoints', []))}  closed={p.get('bClosedLoop')}")
    array_id = "a1"
    asyncio.run_coroutine_threadsafe(_push_assembly_sequence(array_id, len(paths)), _ws_loop)
    return jsonify({"array_id": array_id, "status": "accepted"})

# ---- Test injection endpoints ----

@app.route("/test/event", methods=["POST"])
def test_event():
    """Inject a WS event message. Body: {"drone_id":1,"event":"power_on","gps_lat":39.9,"gps_lon":116.3,"gps_alt":50}"""
    data = request.get_json(force=True, silent=True) or {}
    msg = json.dumps({
        "type": "event",
        "drone_id": data.get("drone_id", 1),
        "event": data.get("event", "power_on"),
        "gps_lat": data.get("gps_lat", 39.9),
        "gps_lon": data.get("gps_lon", 116.3),
        "gps_alt": data.get("gps_alt", 50.0),
    })
    asyncio.run_coroutine_threadsafe(_broadcast(msg), _ws_loop)
    print(f"[HTTP] POST /test/event -> {msg}")
    return jsonify({"ok": True})

@app.route("/test/alert", methods=["POST"])
def test_alert():
    """Inject a WS alert message. Body: {"drone_id":1,"alert":"low_battery","value":15}"""
    data = request.get_json(force=True, silent=True) or {}
    msg = json.dumps({
        "type": "alert",
        "drone_id": data.get("drone_id", 1),
        "alert": data.get("alert", "low_battery"),
        "value": data.get("value", 15),
    })
    asyncio.run_coroutine_threadsafe(_broadcast(msg), _ws_loop)
    print(f"[HTTP] POST /test/alert -> {msg}")
    return jsonify({"ok": True})

@app.route("/test/assembly", methods=["POST"])
def test_assembly():
    """Trigger full assembly sequence (0->1->2->complete). Body optional."""
    data = request.get_json(force=True, silent=True) or {}
    array_id = data.get("array_id", "a1")
    total = data.get("total_count", 2)
    asyncio.run_coroutine_threadsafe(_push_assembly_sequence(array_id, total), _ws_loop)
    print(f"[HTTP] POST /test/assembly -> array_id={array_id} total={total}")
    return jsonify({"ok": True, "array_id": array_id})

@app.route("/test/assembly_timeout", methods=["POST"])
def test_assembly_timeout():
    """Trigger assembly timeout sequence. Body optional."""
    data = request.get_json(force=True, silent=True) or {}
    array_id = data.get("array_id", "a1")
    total = data.get("total_count", 2)
    asyncio.run_coroutine_threadsafe(_push_timeout_sequence(array_id, total), _ws_loop)
    print(f"[HTTP] POST /test/assembly_timeout -> array_id={array_id} total={total}")
    return jsonify({"ok": True, "array_id": array_id})

def run_http():
    app.run(host="127.0.0.1", port=8080, use_reloader=False)

# ---- WebSocket server (websockets, port 8081) ----
ws_clients = set()
_ws_loop: asyncio.AbstractEventLoop | None = None

async def _broadcast(msg: str):
    dead = set()
    for ws in ws_clients:
        try:
            await ws.send(msg)
        except Exception:
            dead.add(ws)
    ws_clients.difference_update(dead)

async def _push_assembly_sequence(array_id: str, total: int):
    """Simulate assembling 0->total->complete over ~8 seconds."""
    await asyncio.sleep(1.0)
    await _broadcast(json.dumps({"type": "assembling", "array_id": array_id,
                                  "ready_count": 0, "total_count": total}))
    for i in range(1, total + 1):
        await asyncio.sleep(3.0)
        await _broadcast(json.dumps({"type": "assembling", "array_id": array_id,
                                      "ready_count": i, "total_count": total}))
    await asyncio.sleep(1.0)
    await _broadcast(json.dumps({"type": "assembly_complete", "array_id": array_id}))
    print(f"[WS] Assembly sequence complete: {array_id}")

async def _push_timeout_sequence(array_id: str, total: int):
    """Simulate assembly that stalls and times out."""
    await _broadcast(json.dumps({"type": "assembling", "array_id": array_id,
                                  "ready_count": 0, "total_count": total}))
    await asyncio.sleep(3.0)
    await _broadcast(json.dumps({"type": "assembling", "array_id": array_id,
                                  "ready_count": 1, "total_count": total}))
    await asyncio.sleep(5.0)
    await _broadcast(json.dumps({"type": "assembly_timeout", "array_id": array_id,
                                  "ready_count": 1, "total_count": total}))
    print(f"[WS] Assembly timeout: {array_id}")

async def ws_handler(websocket):
    print("[WS] Client connected")
    ws_clients.add(websocket)
    try:
        async for msg in websocket:
            try:
                data = json.loads(msg)
                msg_type = data.get("type", "unknown")
                if msg_type == "move":
                    print(f"[WS] MOVE  drone_id={data.get('drone_id')}  "
                          f"x={data.get('x'):.1f}  y={data.get('y'):.1f}  z={data.get('z'):.1f}")
                elif msg_type in ("pause", "resume"):
                    print(f"[WS] {msg_type.upper()}  drone_ids={data.get('drone_ids')}")
                else:
                    print(f"[WS] Received ({msg_type}): {msg}")
            except json.JSONDecodeError:
                print(f"[WS] Received (raw): {msg}")
    except Exception:
        pass
    finally:
        ws_clients.discard(websocket)
        print("[WS] Client disconnected")

async def telemetry_push():
    global ws_clients
    t = 0.0
    while True:
        await asyncio.sleep(0.1)
        t += 0.1
        if not ws_clients:
            continue
        for drone in DRONES:
            did = drone["id"]
            radius = 500.0 * did
            msg = json.dumps({
                "type": "telemetry",
                "drone_id": did,
                "x": round(radius * math.cos(t * 0.3 + did), 2),
                "y": round(radius * math.sin(t * 0.3 + did), 2),
                "z": round(500.0 + 100.0 * math.sin(t * 0.5), 2),
                "pitch": 0.0,
                "yaw": round(math.degrees(t * 0.3 + did) % 360, 1),
                "roll": 0.0,
                "speed": round(radius * 0.3, 1),
                "battery": 85,
            })
            await _broadcast(msg)

async def run_ws():
    global _ws_loop
    _ws_loop = asyncio.get_running_loop()
    import websockets
    async with websockets.serve(ws_handler, "127.0.0.1", 8081):
        print("[WS] WebSocket server on ws://127.0.0.1:8081/ws")
        await telemetry_push()

if __name__ == "__main__":
    print("[HTTP] Starting REST server on http://127.0.0.1:8080")
    threading.Thread(target=run_http, daemon=True).start()
    time.sleep(0.5)  # Give Flask time to start
    print("[WS] Starting WebSocket server on ws://127.0.0.1:8081/ws")
    asyncio.run(run_ws())
