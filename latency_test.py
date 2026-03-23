#!/usr/bin/env python3
"""
延时测试脚本 - 测量 UE5 ↔ PX4 双向通讯延时

模式:
  ue2px4  - 测试 UE→PX4 方向延时（本地 echo RTT）
  px42ue  - 测试 PX4→UE 方向延时（监听 8888 端口，统计包间隔/抖动）
  both    - 同时运行两个测试

用法:
  python latency_test.py --mode ue2px4
  python latency_test.py --mode px42ue --duration 30
  python latency_test.py --mode both --count 200 --rate 20
"""

import socket
import struct
import time
import threading
import argparse
import statistics
import yaml
from datetime import datetime

# 与 test_ue_sender.py 保持一致的数据包格式
# struct FDroneSocketData { double Timestamp; float X, Y, Z; int32 Mode; }
STRUCT_FORMAT = "dfffI"
DATA_SIZE = struct.calcsize(STRUCT_FORMAT)  # 24 字节

RESPONSE_PORT = 18889  # echo 回包端口（避免与正式端口冲突）


# ==================== 统计工具 ====================

class LatencyStats:
    """延时统计计算"""

    def __init__(self, name: str):
        self.name = name
        self.samples = []

    def add(self, value_ms: float):
        self.samples.append(value_ms)

    def report(self) -> str:
        if not self.samples:
            return f"[{self.name}] 无数据"
        n = len(self.samples)
        avg = statistics.mean(self.samples)
        mn = min(self.samples)
        mx = max(self.samples)
        std = statistics.stdev(self.samples) if n > 1 else 0.0
        p95 = sorted(self.samples)[int(n * 0.95)]
        return (
            f"[{self.name}]\n"
            f"  样本数: {n}\n"
            f"  平均:   {avg:.3f} ms\n"
            f"  最小:   {mn:.3f} ms\n"
            f"  最大:   {mx:.3f} ms\n"
            f"  标准差: {std:.3f} ms\n"
            f"  P95:    {p95:.3f} ms"
        )


# ==================== 模式 1：UE→PX4 RTT 测试 ====================

class UEToPX4Tester:
    """
    本地 echo 测试 UE→Bridge 延时。
    启动一个 echo 服务器监听 ue_port（模拟桥接接收端），
    发送方发包后等待 echo 回包，计算 RTT/2 作为单向延时估计。
    """

    def __init__(self, ue_port: int = 8889, response_port: int = RESPONSE_PORT):
        self.ue_port = ue_port
        self.response_port = response_port
        self._echo_running = False
        self._echo_thread = None

    def _echo_server(self):
        """Echo 服务器：收到包后立即原样回包到 response_port"""
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.settimeout(1.0)
        try:
            sock.bind(("127.0.0.1", self.ue_port))
            print(f"  [Echo服务器] 监听 127.0.0.1:{self.ue_port}")
            while self._echo_running:
                try:
                    data, addr = sock.recvfrom(1024)
                    # 立即回包到 response_port
                    sock.sendto(data, ("127.0.0.1", self.response_port))
                except socket.timeout:
                    continue
        except OSError as e:
            print(f"  [Echo服务器] 绑定失败: {e}")
            print(f"  提示: 端口 {self.ue_port} 可能已被占用（桥接脚本正在运行？）")
        finally:
            sock.close()

    def run(self, count: int = 100, rate: float = 10.0, timeout_ms: float = 500.0):
        """
        运行 RTT 测试。

        Args:
            count: 发送包数
            rate: 发送频率 (Hz)
            timeout_ms: 等待回包超时 (ms)
        """
        print("=" * 60)
        print(f"[UE→PX4] RTT 延时测试")
        print(f"  目标端口: {self.ue_port}  回包端口: {self.response_port}")
        print(f"  发包数: {count}  频率: {rate}Hz  超时: {timeout_ms}ms")
        print("=" * 60)

        # 启动 echo 服务器
        self._echo_running = True
        self._echo_thread = threading.Thread(target=self._echo_server, daemon=True)
        self._echo_thread.start()
        time.sleep(0.1)  # 等待服务器就绪

        # 发送端 socket
        send_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        recv_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        recv_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        recv_sock.settimeout(timeout_ms / 1000.0)
        recv_sock.bind(("127.0.0.1", self.response_port))

        rtt_stats = LatencyStats("UE→PX4 RTT")
        one_way_stats = LatencyStats("UE→PX4 单向估计 (RTT/2)")
        lost = 0
        interval = 1.0 / rate

        try:
            for seq in range(count):
                send_time = time.perf_counter()
                data = struct.pack(STRUCT_FORMAT, send_time, 0.0, 0.0, 100.0, 1)
                send_sock.sendto(data, ("127.0.0.1", self.ue_port))

                try:
                    echo_data, _ = recv_sock.recvfrom(1024)
                    recv_time = time.perf_counter()
                    rtt_ms = (recv_time - send_time) * 1000.0
                    rtt_stats.add(rtt_ms)
                    one_way_stats.add(rtt_ms / 2.0)

                    if (seq + 1) % 20 == 0:
                        ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]
                        print(f"  [{ts}] #{seq+1:4d}  RTT={rtt_ms:.3f}ms")
                except socket.timeout:
                    lost += 1
                    print(f"  [超时] #{seq+1} 丢包")

                # 控制发送频率
                elapsed = time.perf_counter() - send_time
                sleep_time = interval - elapsed
                if sleep_time > 0:
                    time.sleep(sleep_time)

        finally:
            self._echo_running = False
            send_sock.close()
            recv_sock.close()

        print()
        print(rtt_stats.report())
        print()
        print(one_way_stats.report())
        print(f"\n  丢包: {lost}/{count} ({lost/count*100:.1f}%)")
        return rtt_stats, lost


# ==================== 模式 2：PX4→UE 延时测试 ====================

class PX4ToUETester:
    """
    监听端口 8888，接收桥接脚本发来的 YAML 数据，
    统计包间隔、抖动（jitter）、接收频率。
    注意：PX4 时钟与 Windows 不同步，无法直接算绝对延时，
    改为统计包间隔和 PX4 timestamp 递增量。
    """

    def __init__(self, px4_port: int = 8888):
        self.px4_port = px4_port

    def run(self, duration: float = 30.0):
        """
        运行监听测试。

        Args:
            duration: 监听时长 (秒)
        """
        print("=" * 60)
        print(f"[PX4→UE] 包间隔/抖动测试")
        print(f"  监听端口: {self.px4_port}  时长: {duration}s")
        print("  等待桥接脚本发送数据（需要 drone_data_bridge.py 在运行）...")
        print("=" * 60)

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.settimeout(2.0)

        try:
            sock.bind(("0.0.0.0", self.px4_port))
        except OSError as e:
            print(f"  [错误] 绑定端口 {self.px4_port} 失败: {e}")
            print(f"  提示: UE5 可能正在占用此端口")
            return None, None

        interval_stats = LatencyStats("PX4→UE 包间隔")
        jitter_stats = LatencyStats("PX4→UE 抖动 (Jitter)")
        px4_interval_stats = LatencyStats("PX4 timestamp 递增量")

        recv_count = 0
        parse_errors = 0
        last_recv_time = None
        last_interval = None
        last_px4_ts = None
        start_time = time.time()

        print(f"  开始监听，按 Ctrl+C 提前结束...")

        try:
            while time.time() - start_time < duration:
                try:
                    data, addr = sock.recvfrom(65535)
                    recv_time = time.perf_counter()
                    wall_time = time.time()

                    # 计算包间隔
                    if last_recv_time is not None:
                        interval_ms = (recv_time - last_recv_time) * 1000.0
                        interval_stats.add(interval_ms)

                        # 抖动 = 相邻包间隔之差的绝对值
                        if last_interval is not None:
                            jitter_ms = abs(interval_ms - last_interval)
                            jitter_stats.add(jitter_ms)
                        last_interval = interval_ms

                    last_recv_time = recv_time
                    recv_count += 1

                    # 解析 YAML 提取 PX4 timestamp
                    try:
                        text = data.decode('utf-8', errors='replace')
                        parsed = yaml.safe_load(text)
                        if parsed and isinstance(parsed, dict):
                            px4_ts = parsed.get('timestamp')
                            if px4_ts and last_px4_ts is not None:
                                px4_delta_ms = (px4_ts - last_px4_ts) / 1000.0  # 微秒→毫秒
                                if 0 < px4_delta_ms < 1000:  # 过滤异常值
                                    px4_interval_stats.add(px4_delta_ms)
                            last_px4_ts = px4_ts
                    except Exception:
                        parse_errors += 1

                    # 每 50 包打印一次进度
                    if recv_count % 50 == 0:
                        elapsed = wall_time - start_time
                        freq = recv_count / elapsed if elapsed > 0 else 0
                        ts = datetime.now().strftime('%H:%M:%S.%f')[:-3]
                        avg_interval = statistics.mean(interval_stats.samples) if interval_stats.samples else 0
                        print(f"  [{ts}] 已收 {recv_count} 包  频率={freq:.1f}Hz  "
                              f"平均间隔={avg_interval:.2f}ms")

                except socket.timeout:
                    if recv_count == 0:
                        elapsed = time.time() - start_time
                        print(f"  [等待中] {elapsed:.0f}s 未收到数据...")
                    continue

        except KeyboardInterrupt:
            print("\n  [中断] 用户提前结束")
        finally:
            sock.close()

        elapsed = time.time() - start_time
        freq = recv_count / elapsed if elapsed > 0 else 0

        print()
        print(f"[PX4→UE] 测试结果 (共 {elapsed:.1f}s)")
        print(f"  接收包数: {recv_count}  频率: {freq:.2f}Hz  解析错误: {parse_errors}")
        print()
        if interval_stats.samples:
            print(interval_stats.report())
            print()
        if jitter_stats.samples:
            print(jitter_stats.report())
            print()
        if px4_interval_stats.samples:
            print(px4_interval_stats.report())
            print()

        return interval_stats, jitter_stats


# ==================== 主函数 ====================

def main():
    parser = argparse.ArgumentParser(
        description="UE5 ↔ PX4 双向通讯延时测试",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python latency_test.py --mode ue2px4              # 测试 UE→PX4 延时（无需无人机）
  python latency_test.py --mode px42ue --duration 30 # 测试 PX4→UE 延时（需桥接脚本运行）
  python latency_test.py --mode both --count 200     # 同时测试两个方向
        """
    )
    parser.add_argument('--mode', choices=['ue2px4', 'px42ue', 'both'],
                        default='both', help='测试模式 (默认: both)')
    parser.add_argument('--count', type=int, default=100,
                        help='UE→PX4 发包数量 (默认: 100)')
    parser.add_argument('--rate', type=float, default=10.0,
                        help='UE→PX4 发送频率 Hz (默认: 10)')
    parser.add_argument('--duration', type=float, default=30.0,
                        help='PX4→UE 监听时长秒 (默认: 30)')
    parser.add_argument('--ue-port', type=int, default=8889,
                        help='UE→PX4 端口 (默认: 8889)')
    parser.add_argument('--px4-port', type=int, default=8888,
                        help='PX4→UE 端口 (默认: 8888)')
    parser.add_argument('--timeout', type=float, default=500.0,
                        help='RTT 等待超时 ms (默认: 500)')

    args = parser.parse_args()

    print("=" * 60)
    print("UE5 ↔ PX4 通讯延时测试工具")
    print(f"时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 60)

    if args.mode in ('ue2px4', 'both'):
        tester = UEToPX4Tester(ue_port=args.ue_port, response_port=RESPONSE_PORT)
        tester.run(count=args.count, rate=args.rate, timeout_ms=args.timeout)
        print()

    if args.mode in ('px42ue', 'both'):
        tester = PX4ToUETester(px4_port=args.px4_port)
        tester.run(duration=args.duration)

    print("=" * 60)
    print("测试完成")
    print("=" * 60)


if __name__ == "__main__":
    main()
