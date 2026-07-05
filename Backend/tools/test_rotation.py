"""
旋转中心测试脚本
================
向 mock_server 注入姿态数据，在 UE 中直观验证旋转轴是否在无人机机体中心。

前置条件：
  1. 运行 mock_server.py
  2. UE 已启动并连接到 mock_server（ws://127.0.0.1:8081/ws）
  3. UE 场景中有对应 drone_id 的 ARealTimeDroneReceiver Actor

用法：
  pip install requests
  python test_rotation.py [--drone-id 1] [--z 500] [--host 127.0.0.1]

参数：
  --drone-id   目标无人机 ID，默认 1
  --z          悬停高度偏移（cm），默认 500（锚点上方 5m），建议 300-1000
  --host       mock_server 地址，默认 127.0.0.1

每阶段控制台会提示观察要点，结束后按 Ctrl+C 或等待自动退出。
"""

import argparse
import sys
import time
import requests

# --------------------------------------------------------------------------- #

def _post(base: str, path: str, payload: dict) -> bool:
    try:
        r = requests.post(f"{base}{path}", json=payload, timeout=2.0)
        return r.ok
    except requests.exceptions.ConnectionError:
        print(f"\n  [ERROR] 无法连接 {base}，请确认 mock_server.py 已启动")
        sys.exit(1)
    except Exception as e:
        print(f"\n  [WARN] {path}: {e}")
        return False


def set_telem(base: str, drone_id: int, *, x=0.0, y=0.0, z=0.0,
              pitch=0.0, yaw=0.0, roll=0.0, speed=0.0, battery=100):
    """设置 mock_server 的 telemetry override，10Hz 后台推送会自动使用此值。"""
    return _post(base, "/test/telemetry", {
        "drone_id": drone_id,
        "x": x, "y": y, "z": z,
        "pitch": pitch, "yaw": yaw, "roll": roll,
        "speed": speed, "battery": battery,
    })


def power_on(base: str, drone_id: int, lat=39.9, lon=116.3, alt=50.0):
    return _post(base, "/test/event", {
        "drone_id": drone_id, "event": "power_on",
        "gps_lat": lat, "gps_lon": lon, "gps_alt": alt,
    })


def clear_telem(base: str, drone_id: int):
    _post(base, "/test/telemetry_clear", {"drone_id": drone_id})


# --------------------------------------------------------------------------- #

def run_stage(label: str, hint: str, frames, delay=0.15):
    """
    运行一个测试阶段。
    label  - 阶段名（控制台标题）
    hint   - 观察要点提示
    frames - 可迭代，每项是 (kwargs_for_set_telem, display_str)
    delay  - 每帧间隔（秒）
    """
    print(f"\n{'─'*60}")
    print(f"  {label}")
    print(f"  → 观察：{hint}")
    print(f"{'─'*60}")
    for kwargs, display in frames:
        sys.stdout.write(f"\r  {display}    ")
        sys.stdout.flush()
        yield kwargs
        time.sleep(delay)
    print()


# --------------------------------------------------------------------------- #

def main():
    parser = argparse.ArgumentParser(description="无人机旋转中心测试")
    parser.add_argument("--drone-id", type=int, default=1, metavar="N",
                        help="目标无人机 ID（默认 1）")
    parser.add_argument("--z", type=float, default=500.0, metavar="CM",
                        help="悬停高度偏移 cm（默认 500 = 锚点上方 5m）")
    parser.add_argument("--host", default="127.0.0.1",
                        help="mock_server 地址（默认 127.0.0.1）")
    args = parser.parse_args()

    base = f"http://{args.host}:8080"
    did  = args.drone_id
    z    = args.z

    print("=" * 60)
    print("  无人机旋转中心测试")
    print(f"  drone_id={did}  z={z:.0f}cm  server={base}")
    print("=" * 60)

    # ------------------------------------------------------------------ #
    # 0. 发送 power_on（建立 GPS 锚点）
    # ------------------------------------------------------------------ #
    print("\n[准备] 发送 power_on 事件，建立 GPS 锚点...")
    power_on(base, did)
    print("  等待 1.5s 让 UE 完成锚点绑定...")
    time.sleep(1.5)

    try:
        # ------------------------------------------------------------------ #
        # 阶段 1: 悬停（基准，无姿态偏转）
        # ------------------------------------------------------------------ #
        print("\n[1/5] 悬停基准（3s）")
        print(f"  → 无人机应出现在锚点正上方 {z:.0f}cm 处，姿态水平")
        set_telem(base, did, z=z)
        time.sleep(3.0)

        # ------------------------------------------------------------------ #
        # 阶段 2: Yaw 旋转（水平绕 Z 轴自转一圈）
        # ------------------------------------------------------------------ #
        hint = "无人机原地自转，机体中心不移动（若旋转中心偏低，模型会绕下方点公转）"
        steps = 72  # 每步 5°，共 360°，约 10s
        print(f"\n[2/5] Yaw 自转 0°→360° ({steps * 0.14:.0f}s)")
        print(f"  → {hint}")
        for i in range(steps + 1):
            yaw = (i / steps) * 360.0
            set_telem(base, did, z=z, yaw=yaw)
            sys.stdout.write(f"\r  Yaw = {yaw:6.1f}°")
            sys.stdout.flush()
            time.sleep(0.14)
        print()

        # 回零停顿
        set_telem(base, did, z=z)
        time.sleep(1.0)

        # ------------------------------------------------------------------ #
        # 阶段 3: Pitch 俯仰（绕 Y 轴）
        # ------------------------------------------------------------------ #
        hint = "机头上下俯仰，旋转轴在机体中心——整体不应升降或前后平移"
        pitches = (
            [(i * 2, f"Pitch = {i*2:+5.1f}°") for i in range(16)]    # 0→30
          + [(30 - i * 2, f"Pitch = {30-i*2:+5.1f}°") for i in range(31)] # 30→-30
          + [(-30 + i * 2, f"Pitch = {-30+i*2:+5.1f}°") for i in range(16)] # -30→0
        )
        print(f"\n[3/5] Pitch 俯仰 0°→+30°→-30°→0° ({len(pitches) * 0.13:.0f}s)")
        print(f"  → {hint}")
        for p, label in pitches:
            set_telem(base, did, z=z, pitch=float(p))
            sys.stdout.write(f"\r  {label}")
            sys.stdout.flush()
            time.sleep(0.13)
        print()

        set_telem(base, did, z=z)
        time.sleep(1.0)

        # ------------------------------------------------------------------ #
        # 阶段 4: Roll 滚转（绕 X 轴）
        # ------------------------------------------------------------------ #
        hint = "机体左右侧倾，旋转轴在机体中心——整体不应左右平移"
        rolls = (
            [(i * 3, f"Roll = {i*3:+5.1f}°") for i in range(16)]     # 0→45
          + [(45 - i * 3, f"Roll = {45-i*3:+5.1f}°") for i in range(31)]  # 45→-45
          + [(-45 + i * 3, f"Roll = {-45+i*3:+5.1f}°") for i in range(16)] # -45→0
        )
        print(f"\n[4/5] Roll 滚转 0°→+45°→-45°→0° ({len(rolls) * 0.13:.0f}s)")
        print(f"  → {hint}")
        for r, label in rolls:
            set_telem(base, did, z=z, roll=float(r))
            sys.stdout.write(f"\r  {label}")
            sys.stdout.flush()
            time.sleep(0.13)
        print()

        set_telem(base, did, z=z)
        time.sleep(1.0)

        # ------------------------------------------------------------------ #
        # 阶段 5: 复合姿态 + 慢速 Yaw（综合验证）
        # ------------------------------------------------------------------ #
        hint = "复合倾斜下缓慢自转——机体中心应固定，无抖动或漂移"
        steps5 = 60  # 360° over ~9s
        print(f"\n[5/5] 复合姿态旋转（pitch=20°, roll=15°, yaw 0→360°, {steps5 * 0.15:.0f}s）")
        print(f"  → {hint}")
        for i in range(steps5 + 1):
            yaw = (i / steps5) * 360.0
            set_telem(base, did, z=z, pitch=20.0, roll=15.0, yaw=yaw)
            sys.stdout.write(f"\r  pitch=20°  roll=15°  Yaw={yaw:6.1f}°")
            sys.stdout.flush()
            time.sleep(0.15)
        print()

        # ------------------------------------------------------------------ #
        # 收尾：回到水平悬停，清除 override
        # ------------------------------------------------------------------ #
        print("\n[完成] 恢复水平悬停，清除 override...")
        set_telem(base, did, z=z)
        time.sleep(1.5)
        clear_telem(base, did)
        print("  override 已清除，无人机将回到锚点零点（z=0）")

    except KeyboardInterrupt:
        print("\n\n[中断] 收到 Ctrl+C，清除 override...")
        clear_telem(base, did)
        print("  已清除。")

    print("\n测试完成。")


if __name__ == "__main__":
    main()
