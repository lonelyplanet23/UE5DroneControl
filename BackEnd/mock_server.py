"""
Mock backend server for UE5DroneControl frontend testing.
Provides:
  - HTTP REST: GET /api/drones  (port 8080)
  - WebSocket: ws://127.0.0.1:8081/ws  (telemetry push at 10Hz)

Usage:
  pip install flask websockets
  python mock_server.py
"""

import asyncio
import json
import math
import threading
import time
from flask import Flask, jsonify

# ---- Fake drone data ----
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

def run_http():
    app.run(host="127.0.0.1", port=8080, use_reloader=False)

# ---- WebSocket server (websockets, port 8081) ----
ws_clients = set()

async def ws_handler(websocket):
    print("[WS] Client connected")
    ws_clients.add(websocket)
    try:
        async for msg in websocket:
            print(f"[WS] Received: {msg}")
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
                "data": {
                    "x": round(radius * math.cos(t * 0.3 + did), 2),
                    "y": round(radius * math.sin(t * 0.3 + did), 2),
                    "z": round(500.0 + 100.0 * math.sin(t * 0.5), 2),
                    "pitch": 0.0,
                    "yaw": round(math.degrees(t * 0.3 + did) % 360, 1),
                    "roll": 0.0,
                }
            })
            dead = set()
            for ws in ws_clients:
                try:
                    await ws.send(msg)
                except Exception:
                    dead.add(ws)
            ws_clients -= dead

async def run_ws():
    import websockets
    async with websockets.serve(ws_handler, "127.0.0.1", 8081):
        print("[WS] WebSocket server on ws://127.0.0.1:8081/ws")
        await telemetry_push()

if __name__ == "__main__":
    threading.Thread(target=run_http, daemon=True).start()
    print("[HTTP] REST server on http://127.0.0.1:8080")
    asyncio.run(run_ws())
