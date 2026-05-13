# BackEnd Week 1-3 Acceptance Notes

This document records the backend scope that must be complete by week 3, the
implemented behavior in `BackEnd`, and the commands used to verify it. It is
based on the root project documents:

- `后端开发文档.md`
- `接口与通讯数据规范.md`
- `架构设计.md`
- `坐标系转换说明.md`
- `UE5DroneControl需求文档.md`

Only files under `BackEnd` are modified by this backend pass.

## Scope Through Week 3

### Week 1: Backend Foundation

Status: complete for backend development and local testing.

- CMake project builds `DroneBackend` and `DroneBackend_tests`.
- `config.yaml` defines HTTP/WS ports, drone limits, timeout settings, fixed
  slot-to-UDP port mappings, Jetson target, storage path, and log path.
- Core data types cover telemetry, GPS anchors, status snapshots, connection
  events, and the 24-byte control packet.
- Local debug/test surfaces are enabled through `server.debug: true`.

### Week 2: Core Communication Link

Status: complete for backend and mock frontend integration.

- HTTP REST server listens on `0.0.0.0:8080`.
- WebSocket server listens on `0.0.0.0:8081` and accepts `/` or `/ws`.
- UDP receiver listens on configured `recv_port` values and parses YAML
  telemetry into `TelemetryData`.
- UDP sender emits packed 24-byte little-endian control packets:
  `double timestamp`, `float x`, `float y`, `float z`, `uint32 mode`.
- NED to UE offset conversion is implemented as:
  `UE_X=N*100`, `UE_Y=E*100`, `UE_Z=-D*100`.
- UE offset to NED conversion is implemented as:
  `N=X*0.01`, `E=Y*0.01`, `D=-Z*0.01`.
- Quaternion yaw conversion follows the protocol requirement:
  `Yaw_UE = -Yaw_NED`.
- GPS anchors are set from the first valid GPS telemetry and returned by
  `GET /api/drones/{id}/anchor`.

### Week 3: Realtime Control and State Management

Status: complete for required chain-level acceptance.

- Drone registration enforces max count, unique names, unique slots, and fixed
  port-map slots.
- `GET /api/drones` returns the real UE-compatible top-level JSON array.
- Each drone record includes numeric `id`, `id_str`, `slot`,
  `ue_receive_port`, `topic_prefix`, `bit_index`, and `mavlink_system_id`.
- WebSocket `move` accepts numeric or string `drone_id`, converts UE cm to NED m,
  and queues a Mode=1 command.
- WebSocket `pause` / `resume` accepts either `drone_id` or `drone_ids` arrays.
- State machine handles `offline -> online`, `online -> lost`, and
  `lost -> online`.
- WebSocket event push includes `power_on`, `lost_connection`, and `reconnect`.
- `power_on` and `reconnect` events include `gps_lat`, `gps_lon`, `gps_alt`.
- Low battery and lost connection alerts are pushed over WebSocket.
- Command queues support pause/resume, bounded FIFO behavior, and debug
  snapshots.
- Heartbeat threads start for online drones and send >=2Hz hover packets when
  no movement command is queued.
- Debug telemetry injection can drive telemetry push, anchor creation, state
  transitions, alerts, and assembly arrival checks.
- Assembly progress emits `assembling`, `assembly_complete`, and
  `assembly_timeout` messages.

## Real UE Compatibility Notes

The real UE networking code under `Source/UE5DroneControl/DroneOps/Network`
expects these exact shapes:

- `GET /api/drones`: top-level array, not `{ "drones": [...] }`.
- Drone id fields: numeric `id` for UE, plus `id_str` for tools.
- WS telemetry: numeric `drone_id`, flat fields `x`, `y`, `z`, `yaw`, `pitch`,
  `roll`, `speed`, `battery`.
- WS event: numeric `drone_id`, `event`, and GPS fields for `power_on` /
  `reconnect`.
- WS alert: field name `alert`; backend also sends `alert_type` for compatibility.
- UE sends `move` with `"drone_id":"1"` and pause/resume with
  `"drone_ids":["1", ...]`.

The backend accepts both numeric ids (`1`) and string ids (`d1`) for HTTP debug
and management endpoints where applicable.

## Test Commands

Run from `F:\UE5DroneControl\BackEnd`.

### Build

```powershell
cmake --build build --config Release --target DroneBackend DroneBackend_tests
```

Expected result:

- `build\Release\DroneBackend.exe`
- `build\Release\DroneBackend_tests.exe`

### Unit Tests

```powershell
.\build\Release\DroneBackend_tests.exe
```

Latest verified result:

```text
[  PASSED  ] 28 tests.
```

Covered areas:

- NED/UE coordinate conversion
- Quaternion yaw conversion
- GPS anchor manager
- Command queue behavior
- Connection state machine

### Week 1-3 Integration Test

```powershell
python .\tests\integration_week3.py
```

Latest verified result:

```text
week1-3 backend integration: PASS
```

This script starts `DroneBackend.exe` with a temporary config, then verifies:

- REST health and `GET /api/drones`
- registration success and duplicate slot rejection
- real UE-compatible drone list fields
- debug telemetry injection
- WS `power_on` event with GPS anchor
- WS `telemetry` position, yaw, speed, armed/offboard fields
- `GET /api/drones/{id}/anchor`
- WS `move` command queueing and UE cm -> NED m conversion
- WS `pause` / `resume`
- WS error response for an invalid drone id
- low battery alert
- lost connection event and alert
- reconnect event and anchor update
- assembly progress and completion
- debug batch array and `assembly_timeout`
- numeric id deletion

The script uses only temporary storage/log files and cleans up the backend
process when finished.

## Mock UE Client Test

Start the real backend:

```powershell
.\build\Release\DroneBackend.exe
```

Register and inject a test drone:

```powershell
$body = @{
  name = "mock-cli-d1"
  model = "PX4"
  slot = 1
  ip = "127.0.0.1"
  port = 8889
} | ConvertTo-Json

Invoke-RestMethod `
  -Uri http://127.0.0.1:8080/api/drones `
  -Method Post `
  -Body $body `
  -ContentType "application/json"

$tel = @{
  position = @(1.0, 2.0, -3.0)
  q = @(0.965925826, 0, 0, 0.258819045)
  velocity = @(3.0, 4.0, 0.0)
  battery = 88
  gps_lat = 39.9
  gps_lon = 116.3
  gps_alt = 50.0
  arming_state = 2
  nav_state = 14
} | ConvertTo-Json

Invoke-RestMethod `
  -Uri http://127.0.0.1:8080/api/debug/drone/1/inject `
  -Method Post `
  -Body $tel `
  -ContentType "application/json"
```

Run the mock UE shell:

```powershell
cd .\tests\mock_ue
python -m mock_ue.main --ws-url ws://127.0.0.1:8081/ws shell
```

Useful shell commands:

```text
poll
list
telemetry
events
move d1 1200 3400 600
pause d1
resume d1
quit
```

Verify the command queue:

```powershell
cd F:\UE5DroneControl\BackEnd
Invoke-RestMethod http://127.0.0.1:8080/api/debug/drone/1/queue
```

Expected queue command for `move d1 1200 3400 600`:

```json
{
  "x": 12,
  "y": 34,
  "z": -6,
  "mode": 1
}
```

## Manual Real UE Smoke Test

1. Start `DroneBackend.exe`.
2. Register a drone with `POST /api/drones`.
3. Start UE with HTTP base `http://127.0.0.1:8080` and WS URL
   `ws://127.0.0.1:8081/ws` when running on the same machine.
4. Confirm UE polls `GET /api/drones` and sees numeric `id`.
5. Inject telemetry through `/api/debug/drone/1/inject`.
6. Confirm UE receives:
   - `event: power_on` with GPS fields
   - `telemetry` with numeric `drone_id`, cm offsets, yaw, speed, battery
7. Send a UE move command.
8. Confirm `/api/debug/drone/1/queue` contains the converted NED command.
9. Send UE pause/resume for selected drones.
10. Confirm `/api/debug/drone/1/state` toggles `queue_paused`.

For LAN deployment, replace `127.0.0.1` in UE settings with the backend
machine LAN IP. The backend listens on `0.0.0.0`.

## Known Boundaries After Week 3

These are outside the week 1-3 acceptance target or only partially implemented:

- Full multi-stage recon/patrol/attack waypoint execution is not complete.
- Full realtime obstacle avoidance is not complete.
- Real ROS2 battery topic subscription is not implemented; battery is accepted
  from YAML/debug telemetry for current testing.
- Real PX4/Jetson end-to-end flight must still be validated with UDP capture and
  hardware/SITL.
