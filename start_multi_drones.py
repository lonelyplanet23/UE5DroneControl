"""
多无人机数据桥接启动器
从配置文件读取多个无人机的配置，并为每个无人机启动独立的 drone_data_bridge.py 进程
"""
import os
import sys
import yaml
import subprocess
import time
from pathlib import Path

def load_config(config_file: str = "multi_drone_config.yaml"):
    """加载配置文件"""
    with open(config_file, 'r', encoding='utf-8') as f:
        return yaml.safe_load(f)

def start_drone_bridge(drone_config: dict, drone_index: int):
    """启动单个无人机的数据桥接"""
    name = drone_config.get('name', f'Drone{drone_index}')

    cmd = [
        sys.executable,  # Python 解释器
        "drone_data_bridge.py",
        "--ue-host", drone_config['ue_host'],
        "--ue-port", str(drone_config['ue_port']),
        "--ssh-host", drone_config['ssh_host'],
        "--ssh-user", drone_config['ssh_user'],
        "--ssh-password", drone_config['ssh_password'],
        "--ros-topic", drone_config['ros_topic']
    ]

    print(f"[{name}] 启动数据桥接...")
    print(f"[{name}] SSH: {drone_config['ssh_user']}@{drone_config['ssh_host']}")
    print(f"[{name}] 话题: {drone_config['ros_topic']}")
    print(f"[{name}] UE5: {drone_config['ue_host']}:{drone_config['ue_port']}")
    print(f"[{name}] 命令: {' '.join(cmd)}")
    print()

    # 在新窗口启动进程
    if os.name == 'nt':  # Windows
        # 使用 start 命令在新窗口启动
        subprocess.Popen(
            ['start', 'cmd', '/k'] + cmd,
            shell=True,
            creationflags=subprocess.CREATE_NEW_CONSOLE
        )
    else:  # Linux/Mac
        subprocess.Popen(
            ['gnome-terminal', '--', 'python3'] + cmd[1:],
            shell=False
        )

def main():
    """主函数"""
    print("=" * 60)
    print("多无人机数据桥接启动器")
    print("=" * 60)
    print()

    # 加载配置
    config_file = "multi_drone_config.yaml"
    if not os.path.exists(config_file):
        print(f"[ERROR] 配置文件不存在: {config_file}")
        print("请先创建配置文件")
        return False

    config = load_config(config_file)
    drones = config.get('drones', [])

    if not drones:
        print("[ERROR] 配置文件中没有无人机配置")
        return False

    print(f"发现 {len(drones)} 架无人机配置:")
    for i, drone in enumerate(drones, 1):
        name = drone.get('name', f'Drone{i}')
        print(f"  {i}. {name} - {drone['ssh_host']}:{drone['ue_port']}")
    print()

    # 询问启动哪些无人机
    print("请选择要启动的无人机:")
    print("  1. 启动所有无人机")
    print("  2. 选择特定无人机")
    print("  3. 只启动第一架（UAV1）")

    choice = input("请输入选项 (1/2/3): ").strip()

    drones_to_start = []

    if choice == '1':
        drones_to_start = drones
    elif choice == '2':
        indices = input(f"请输入无人机编号 (1-{len(drones)})，用逗号分隔: ").strip()
        for idx in indices.split(','):
            try:
                i = int(idx.strip()) - 1
                if 0 <= i < len(drones):
                    drones_to_start.append(drones[i])
            except:
                pass
    elif choice == '3':
        drones_to_start = [drones[0]]
    else:
        print("[ERROR] 无效的选项")
        return False

    if not drones_to_start:
        print("[ERROR] 没有选择任何无人机")
        return False

    print()
    print("=" * 60)
    print(f"将启动 {len(drones_to_start)} 架无人机的数据桥接")
    print("=" * 60)
    print()

    # 启动每个无人机
    for i, drone in enumerate(drones_to_start, 1):
        start_drone_bridge(drone, i)
        time.sleep(2)  # 间隔2秒启动下一个

    print()
    print("=" * 60)
    print("所有无人机数据桥接已启动！")
    print("=" * 60)
    print()
    print("提示:")
    print("  - 每个无人机会在独立的终端窗口运行")
    print("  - 请在各自的终端中输入 SSH 密码")
    print("  - 使用 Ctrl+C 停止各个桥接进程")
    print()

    return True

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n[OK] 已取消启动")
    except Exception as e:
        print(f"[ERROR] 发生错误: {e}")
        import traceback
        traceback.print_exc()
