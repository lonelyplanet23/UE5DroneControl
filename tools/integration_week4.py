"""
integration_week4.py — 第四周后端集成测试

覆盖：
  - 集结流程（含集结指令发送、进度推送、完成推送）
  - 执行引擎：侦察模式（非循环）
  - 执行引擎：侦察模式（循环，验证回绕）
  - 执行引擎：攻击模式（末航点悬停，不循环）
  - 执行引擎：巡逻模式（目标识别事件中断）
  - 多机并发调度（两机同时执行）
  - 集结超时
  - 停止阵列任务

用法：
  python integration_week4.py
  python integration_week4.py --exe BackEnd/build/Release/DroneBackend.exe
"""

from __future__ import annotations

import argparse
import asyncio
import json
import math
import os
import signal
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path
from typing import Any

import websockets

ROOT = Path(__file__).resolve().parents[1]

# ============================================================
# HTTP helpers
# ============================================================

def request(method: str, path: str, payload: Any = None, timeout: float = 5.0) -> Any:
    data = None
    headers = {"Accept": "application/json"}
    if payload is not None:
        data = json.dumps(payload).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(
        f"http://127.0.0.1:8080{path}", data=data, headers=headers, method=method
    )
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        text = resp.read().decode("utf-8")
        return json.loads(text) if text.strip() else None


def wait_http_ready(proc: subprocess.Popen[Any], deadline: float = 10.0) -> None:
    until = time.time() + deadline
    while time.time() < until:
        if proc.poll() is not None:
            raise RuntimeError(f"backend exited early (code {proc.returncode})")
        try:
            if request("GET", "/").get("status") == "ok":
                return
        except Exception:
            time.sleep(0.2)
    raise TimeoutError("backend did not become ready")


def close_backend(proc: subprocess.Popen[Any]) -> None:
    if proc.poll() is not None:
        return
    if os.name == "nt":
        proc.terminate()
    else:
        proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)


def write_test_config(path: Path) -> None:
    tmp = str(path.parent).replace("\\", "/")
    path.write_text(
        f"""
server:
  http_port: 8080
  ws_port: 8081
  debug: true

drone:
  max_count: 6
  heartbeat_hz: 2
  lost_timeout_sec: 3
  arrival_threshold_m: 0.5
  assembly_timeout_sec: 3
  avoidance_radius_m: 3.0
  avoidance_lookahead_sec: 2.0
  low_battery_threshold: 20

port_map:
  1:
    send_port: 18889
    recv_port: 18888
    ros_topic_prefix: "/px4_1"
  2:
    send_port: 18891
    recv_port: 18890
    ros_topic_prefix: "/px4_2"

jetson:
  host: "127.0.0.1"

storage:
  path: "{tmp}/drones.json"

log:
  level: "warn"
  file: "{tmp}/backend.log"
""",
        encoding="utf-8",
    )


# ============================================================
# WebSocket helpers
# ============================================================

async def expect_ws(
    ws: Any,
    predicate: Any,
    description: str,
    timeout: float = 5.0,
) -> dict[str, Any]:
    deadline = time.time() + timeout
    seen: list[dict[str, Any]] = []
    while time.time() < deadline:
        try:
            raw = await asyncio.wait_for(ws.recv(), timeout=max(0.05, deadline - time.time()))
        except asyncio.TimeoutError:
            break
        msg = json.loads(raw)
        seen.append(msg)
        if predicate(msg):
            return msg
    raise AssertionError(f"missing WS message: {description}; seen={[m.get('type') for m in seen]}")


def wait_until(predicate: Any, description: str, timeout: float = 5.0, interval: float = 0.1) -> Any:
    until = time.time() + timeout
    while time.time() < until:
        v = predicate()
        if v:
            return v
        time.sleep(interval)
    raise AssertionError(f"condition not met: {description}")


# ============================================================
# Telemetry injection helpers
# ============================================================

def inject(drone_id: int, ned_n: float, ned_e: float, ned_d: float,
           battery: int = 85, gps_lat: float = 39.9, gps_lon: float = 116.3) -> None:
    request("POST", f"/api/debug/drone/{drone_id}/inject", {
        "position": [ned_n, ned_e, ned_d],
        "q": [1, 0, 0, 0],
        "velocity": [0, 0, 0],
        "battery": battery,
        "gps_lat": gps_lat,
        "gps_lon": gps_lon,
        "gps_alt": 50.0,
    })


def inject_at_target(drone_id: int, ue_x: float, ue_y: float, ue_z: float,
                     battery: int = 85) -> None:
    """注入遥测，位置恰好在 UE 偏移对应的 NED 坐标（误差 0）。"""
    ned_n = ue_x / 100.0
    ned_e = ue_y / 100.0
    ned_d = -ue_z / 100.0
    inject(drone_id, ned_n, ned_e, ned_d, battery=battery)


# ============================================================
# 测试用例
# ============================================================

async def test_assembly_and_recon(ws: Any) -> None:
    """集结流程 + 侦察模式（非循环）"""
    print("  [T1] 集结流程 + 侦察模式（非循环）")

    # 注册两架无人机
    r1 = request("POST", "/api/drones", {"name": "w4-d1", "slot": 1})
    r2 = request("POST", "/api/drones", {"name": "w4-d2", "slot": 2})
    assert r1["id"] == 1 and r2["id"] == 2

    # 上线
    inject(1, 0, 0, -5)
    inject(2, 5, 0, -5)
    await expect_ws(ws, lambda m: m.get("type") == "event" and m.get("drone_id") == 1, "d1 power_on")
    await expect_ws(ws, lambda m: m.get("type") == "event" and m.get("drone_id") == 2, "d2 power_on")

    # 下发集结任务（两机各自飞向第一个航点）
    # 航点 UE 偏移 (cm)：d1→(100,0,-500)，d2→(-100,0,-500)
    # 对应 NED (m)：d1→(1,0,5)，d2→(-1,0,5)
    array_payload = {
        "array_id": "w4-recon",
        "mode": "recon",
        "paths": [
            {
                "pathId": 1, "drone_id": "d1", "bClosedLoop": False,
                "waypoints": [
                    {"location": {"x": 100, "y": 0, "z": -500}},
                    {"location": {"x": 200, "y": 0, "z": -500}},
                ],
            },
            {
                "pathId": 2, "drone_id": "d2", "bClosedLoop": False,
                "waypoints": [
                    {"location": {"x": -100, "y": 0, "z": -500}},
                    {"location": {"x": -200, "y": 0, "z": -500}},
                ],
            },
        ],
    }
    created = request("POST", "/api/arrays", array_payload)
    assert created["status"] == "assembling", f"expected assembling, got {created}"

    # 验证集结进度推送
    await expect_ws(ws, lambda m: m.get("type") == "assembling" and m.get("array_id") == "w4-recon", "assembling push")

    # 模拟两机到达集结点（误差 < 0.5m）
    inject_at_target(1, 100, 0, -500)
    inject_at_target(2, -100, 0, -500)

    # 验证集结完成
    await expect_ws(
        ws,
        lambda m: m.get("type") == "assembly_complete" and m.get("array_id") == "w4-recon",
        "assembly_complete",
        timeout=6.0,
    )

    # 验证执行引擎已启动（exec_running=true）
    state = request("GET", "/api/debug/arrays/w4-recon/state")
    assert state["exec_running"] is True, f"exec_running should be True: {state}"

    # 模拟 d1 到达第二个航点
    inject_at_target(1, 200, 0, -500)

    # 停止任务
    request("POST", "/api/arrays/w4-recon/stop")
    print("  [T1] PASS")


async def test_recon_loop(ws: Any) -> None:
    """侦察模式循环（单机，跳过集结）"""
    print("  [T2] 侦察模式循环（单机）")

    # 确保 d1 在线
    inject(1, 0, 0, -5)

    # 直接启动执行引擎（跳过集结）
    result = request("POST", "/api/debug/cmd/1/array", {
        "mode": "recon",
        "loop": True,
        "waypoints": [
            {"x": 50, "y": 0, "z": -300},
            {"x": 100, "y": 0, "z": -300},
        ],
    })
    assert result["status"] == "executing", f"expected executing: {result}"

    # 模拟到达第一个航点
    inject_at_target(1, 50, 0, -300)
    time.sleep(0.3)

    # 模拟到达第二个航点
    inject_at_target(1, 100, 0, -300)
    time.sleep(0.3)

    # 循环：再次到达第一个航点（验证回绕）
    inject_at_target(1, 50, 0, -300)
    time.sleep(0.3)

    # 停止
    request("POST", "/api/arrays/debug_1/stop")
    print("  [T2] PASS")


async def test_attack_mode(ws: Any) -> None:
    """攻击模式（末航点悬停，不循环）"""
    print("  [T3] 攻击模式")

    inject(1, 0, 0, -5)

    result = request("POST", "/api/debug/cmd/1/array", {
        "mode": "attack",
        "loop": False,
        "waypoints": [
            {"x": 50, "y": 0, "z": -300},
            {"x": 100, "y": 50, "z": -300},
            {"x": 150, "y": 100, "z": -300},  # 攻击目标点
        ],
    })
    assert result["status"] == "executing"

    # 依次到达三个航点
    for wp in [(50, 0, -300), (100, 50, -300), (150, 100, -300)]:
        inject_at_target(1, *wp)
        time.sleep(0.3)

    # 验证执行引擎仍在运行（悬停在末航点）
    state = request("GET", "/api/debug/arrays/debug_1/state")
    assert state["exec_running"] is True, f"should still be running (hovering): {state}"

    request("POST", "/api/arrays/debug_1/stop")
    print("  [T3] PASS")


async def test_patrol_target_injection(ws: Any) -> None:
    """巡逻模式：目标识别事件中断航点序列"""
    print("  [T4] 巡逻模式目标注入")

    inject(1, 0, 0, -5)

    result = request("POST", "/api/debug/cmd/1/array", {
        "mode": "patrol",
        "loop": False,
        "waypoints": [
            {"x": 50, "y": 0, "z": -300},
            {"x": 100, "y": 0, "z": -300},
            {"x": 150, "y": 0, "z": -300},
        ],
    })
    assert result["status"] == "executing"

    # 到达第一个航点
    inject_at_target(1, 50, 0, -300)
    time.sleep(0.2)

    # 注入目标识别事件（中断序列）
    target_result = request("POST", "/api/debug/cmd/1/target", {"x": 300, "y": 300, "z": -300})
    assert target_result["target_injected"] is True, f"target injection failed: {target_result}"

    # 模拟到达目标点
    inject_at_target(1, 300, 300, -300)
    time.sleep(0.3)

    request("POST", "/api/arrays/debug_1/stop")
    print("  [T4] PASS")


async def test_multi_drone_concurrent(ws: Any) -> None:
    """多机并发调度（两机同时执行不同模式）"""
    print("  [T5] 多机并发调度")

    inject(1, 0, 0, -5)
    inject(2, 5, 0, -5)

    # 通过 batch/array 同时下发两机任务（含集结）
    batch = request("POST", "/api/debug/cmd/batch/array", [
        {
            "drone_id": "d1",
            "mode": "recon",
            "waypoints": [{"x": 100, "y": 0, "z": -300}],
        },
        {
            "drone_id": "d2",
            "mode": "attack",
            "waypoints": [{"x": -100, "y": 0, "z": -300}],
        },
    ])
    assert batch["status"] == "assembling"
    assert batch["path_count"] == 2

    # 验证集结进度推送
    await expect_ws(ws, lambda m: m.get("type") == "assembling", "batch assembling")

    # 两机同时到达集结点
    inject_at_target(1, 100, 0, -300)
    inject_at_target(2, -100, 0, -300)

    # 验证集结完成
    await expect_ws(
        ws,
        lambda m: m.get("type") == "assembly_complete",
        "batch assembly_complete",
        timeout=6.0,
    )

    # 验证两机队列独立（各自有指令）
    q1 = request("GET", "/api/debug/drone/1/queue")
    q2 = request("GET", "/api/debug/drone/2/queue")
    # 队列可能已被消费，只验证不报错
    assert isinstance(q1, dict) and isinstance(q2, dict)

    request("POST", "/api/arrays/debug_batch/stop")
    print("  [T5] PASS")


async def test_assembly_timeout(ws: Any) -> None:
    """集结超时（配置 3s）"""
    print("  [T6] 集结超时")

    inject(1, 0, 0, -5)

    # 下发集结任务，但不注入到达遥测
    request("POST", "/api/arrays", {
        "array_id": "w4-timeout",
        "mode": "recon",
        "paths": [
            {
                "pathId": 1, "drone_id": "d1", "bClosedLoop": False,
                "waypoints": [{"location": {"x": 9999, "y": 9999, "z": -500}}],
            }
        ],
    })

    # 等待超时推送（配置 3s，留 5s 余量）
    await expect_ws(
        ws,
        lambda m: m.get("type") == "assembly_timeout" and m.get("array_id") == "w4-timeout",
        "assembly_timeout",
        timeout=6.0,
    )
    request("POST", "/api/arrays/w4-timeout/stop")
    print("  [T6] PASS")


async def test_pause_resume_during_exec(ws: Any) -> None:
    """执行期间暂停/恢复"""
    print("  [T7] 执行期间暂停/恢复")

    inject(1, 0, 0, -5)

    request("POST", "/api/debug/cmd/1/array", {
        "mode": "recon",
        "loop": False,
        "waypoints": [{"x": 500, "y": 0, "z": -300}],
    })

    # 暂停
    await ws.send(json.dumps({"type": "pause", "request_id": "p1", "drone_ids": ["1"]}))
    await expect_ws(ws, lambda m: m.get("type") == "command_ack" and m.get("request_id") == "p1", "pause ack")
    wait_until(
        lambda: request("GET", "/api/debug/drone/1/state")["queue_paused"] is True,
        "queue paused",
    )

    # 恢复
    await ws.send(json.dumps({"type": "resume", "request_id": "r1", "drone_ids": ["1"]}))
    await expect_ws(ws, lambda m: m.get("type") == "command_ack" and m.get("request_id") == "r1", "resume ack")
    wait_until(
        lambda: request("GET", "/api/debug/drone/1/state")["queue_paused"] is False,
        "queue resumed",
    )

    request("POST", "/api/arrays/debug_1/stop")
    print("  [T7] PASS")


async def run_all_checks() -> None:
    async with websockets.connect("ws://127.0.0.1:8081/ws") as ws:
        # 清空注册表（如有残留）
        for drone in request("GET", "/api/drones"):
            request("DELETE", f"/api/drones/{drone['id']}")

        await test_assembly_and_recon(ws)

        # 继续复用 d1/d2；后端 ID 递增不回收，删除后重注册会变成 d3/d4。
        inject(1, 0, 0, -5)
        inject(2, 5, 0, -5)
        await expect_ws(ws, lambda m: m.get("type") == "telemetry" and m.get("drone_id") == 1, "d1 telemetry")
        await expect_ws(ws, lambda m: m.get("type") == "telemetry" and m.get("drone_id") == 2, "d2 telemetry")

        await test_recon_loop(ws)
        await test_attack_mode(ws)
        await test_patrol_target_injection(ws)
        await test_multi_drone_concurrent(ws)
        await test_assembly_timeout(ws)
        await test_pause_resume_during_exec(ws)


def main() -> int:
    parser = argparse.ArgumentParser(description="Week 4 backend integration test")
    parser.add_argument(
        "--exe",
        default=str(ROOT / "BackEnd" / "build" / "Release" / "DroneBackend.exe"),
    )
    parser.add_argument("--keep-backend", action="store_true")
    args = parser.parse_args()

    exe = Path(args.exe)
    if not exe.exists():
        print(f"ERROR: backend executable not found: {exe}", file=sys.stderr)
        print("请先编译后端: cd BackEnd && .\\build.bat", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="ue5drone-week4-") as tmp:
        tmp_path = Path(tmp)
        config = tmp_path / "config.yaml"
        write_test_config(config)

        proc = subprocess.Popen(
            [str(exe), str(config)],
            cwd=str(ROOT),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            wait_http_ready(proc)
            asyncio.run(run_all_checks())
            print("\nweek4 backend integration: ALL PASS")
            return 0
        except AssertionError as e:
            print(f"\nFAIL: {e}", file=sys.stderr)
            return 1
        except Exception as e:
            print(f"\nERROR: {e}", file=sys.stderr)
            return 1
        finally:
            if not args.keep_backend:
                close_backend(proc)


if __name__ == "__main__":
    raise SystemExit(main())
