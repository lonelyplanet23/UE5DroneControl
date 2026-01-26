import socket
import struct
import time
import math
import threading
import sys
import os

# ================= 端口配置 =================
# 发送目标 (UE5 监听端口)
UE5_IP =  "10.192.200.83"
UE5_PORT = 9999

# 本机接收端口 (UE5 发送目标)
PYTHON_LISTEN_PORT = 8888
# ============================================

# 协议: Timestamp(d), X(f), Y(f), Z(f), Mode(i)
STRUCT_FORMAT = '<dfffi' 
PACKET_SIZE = struct.calcsize(STRUCT_FORMAT)

# 全局共享状态
shared_state = {
    "current_pos": [0.0, 0.0, 0.0],  # 物理模拟位置
    "target_pos": [0.0, 0.0, 0.0],   # 目标位置
    "ue5_feedback": None,            # 记录最后一次从 UE5 收到的数据
    "running": True,
    "mode": 1,
    "show_logs": False               # <--- 默认关闭刷屏日志
}

def receive_thread(sock):
    """
    🎧 监听线程：默默收数据，只更新变量，不随便 Print 抢话筒
    """
    print(f"🎧 [后台] 监听端口 {PYTHON_LISTEN_PORT} 启动...")
    
    while shared_state["running"]:
        try:
            data, addr = sock.recvfrom(1024)
            
            if len(data) == PACKET_SIZE:
                unpacked = struct.unpack(STRUCT_FORMAT, data)
                ts, x, y, z, mode = unpacked
                
                # 更新状态
                shared_state["ue5_feedback"] = (x, y, z, mode)
                
                # 只有打开了日志开关，才往屏幕上喷数据
                if shared_state["show_logs"]:
                    sys.stdout.write(f"\r📩 [UE5反馈] Pos:({x:.0f}, {y:.0f}, {z:.0f}) | Mode:{mode}   ")
                    sys.stdout.flush()
        
        except (ConnectionResetError, socket.timeout):
            pass
        except OSError as e:
            if e.errno == 10054: pass # 忽略远程关闭
            elif shared_state["running"]: print(f"\n❌ 接收异常: {e}")
        except Exception:
            pass

def logic_and_send_loop(sock):
    """
    🚀 发送线程：物理模拟 + 发送
    """
    last_time = time.time()
    sim_speed = 300.0 # 飞行速度

    while shared_state["running"]:
        now = time.time()
        dt = now - last_time
        last_time = now

        # --- 物理模拟 飞行 平滑移动 ---
        curr = shared_state["current_pos"]
        tgt = shared_state["target_pos"]
        dist = math.sqrt((tgt[0]-curr[0])**2 + (tgt[1]-curr[1])**2 + (tgt[2]-curr[2])**2)
        
        if dist > 1.0:
            step = sim_speed * dt
            ratio = min(step / dist, 1.0) if dist > 0 else 0
            curr[0] += (tgt[0] - curr[0]) * ratio
            curr[1] += (tgt[1] - curr[1]) * ratio
            curr[2] += (tgt[2] - curr[2]) * ratio

        # --- 发送 ---
        try:
            send_data = struct.pack(STRUCT_FORMAT, time.time(), curr[0], curr[1], curr[2], shared_state["mode"])
            sock.sendto(send_data, (UE5_IP, UE5_PORT))
        except:
            print(f"❌ 发送失败")
            pass # 发送失败通常是因为 UE5 没开，保持安静

        time.sleep(0.016) # 60Hz

def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind(('0.0.0.0', PYTHON_LISTEN_PORT))
        sock.settimeout(1.0)
    except Exception as e:
        print(f"❌ 端口 {PYTHON_LISTEN_PORT} 被占用！请关闭其他 Python 窗口。")
        return

    t_recv = threading.Thread(target=receive_thread, args=(sock,), daemon=True)
    t_send = threading.Thread(target=logic_and_send_loop, args=(sock,), daemon=True)
    t_recv.start()
    t_send.start()

    print("\n" + "="*40)
    print("🚁 交互式无人机控制台")
    print("="*40)
    print(" [指令指南]")
    print("  x y z   -> 飞向坐标 (例: 500 0 200)")
    print("  status  -> 查看当前状态 (不刷屏)")
    print("  logs    -> 开启/关闭 实时数据刷屏")
    print("  q       -> 退出")
    print("="*40 + "\n")
    
    try:
        while True:
            # input() 会阻塞主线程，这是你打字的地方，不会被干扰了
            user_input = input("指令 > ")
            
            cmd = user_input.lower().strip()
            
            if cmd == 'q':
                break
            
            elif cmd == 'logs':
                shared_state["show_logs"] = not shared_state["show_logs"]
                state = "开启" if shared_state["show_logs"] else "关闭"
                print(f"📺 实时日志已{state} (再输 logs 切换)")
            
            elif cmd == 'status':
                # 手动查询一次
                curr = shared_state["current_pos"]
                tgt = shared_state["target_pos"]
                fb = shared_state["ue5_feedback"]
                
                print(f"\n📊 --- 系统状态 ---")
                print(f"   当前位置 (Python模拟): ({curr[0]:.1f}, {curr[1]:.1f}, {curr[2]:.1f})")
                print(f"   目标指令: ({tgt[0]:.1f}, {tgt[1]:.1f}, {tgt[2]:.1f})")
                if fb:
                    print(f"   UE5反馈:  ({fb[0]:.1f}, {fb[1]:.1f}, {fb[2]:.1f}) Mode:{fb[3]}")
                else:
                    print(f"   UE5反馈:  (暂无数据 - 请检查 UE5 是否运行)")
                print("-" * 20 + "\n")

            else:
                try:
                    parts = cmd.split()
                    if len(parts) == 3:
                        x, y, z = map(float, parts)
                        shared_state["target_pos"] = [x, y, z]
                        print(f"✅ 指令已更新: 目标设为 ({x}, {y}, {z})")
                    elif cmd:
                        print("❌ 未知指令，请输入 'x y z', 'status' 或 'logs'")
                except:
                    print("❌ 输入格式错误")
                    
    except KeyboardInterrupt:
        pass
    finally:
        shared_state["running"] = False
        sock.close()
        print("\n再见!")

if __name__ == "__main__":
    main()