from __future__ import annotations

import argparse
import cmd
import shlex
import threading
import time
from pathlib import Path
from typing import Any

from .app import MockUEApp


class SafePrinter:
    def __init__(self) -> None:
        self._lock = threading.Lock()

    def __call__(self, message: str) -> None:
        with self._lock:
            print(f"\n[{time.strftime('%H:%M:%S')}] {message}")


class MockUEShell(cmd.Cmd):
    intro = (
        "Mock UE 已启动。它会持续轮询 /api/drones，并连接 WebSocket 接收后端推送。\n"
        "输入 help 查看命令。"
    )
    prompt = "mock-ue> "

    def __init__(self, app: MockUEApp, project_dir: Path) -> None:
        super().__init__()
        self.app = app
        self.project_dir = project_dir

    def default(self, line: str) -> None:
        print(f"未知命令: {line}")

    def emptyline(self) -> None:
        return

    def _tokens(self, line: str) -> list[str]:
        return shlex.split(line)

    def _pairs(self, tokens: list[str]) -> dict[str, str]:
        result: dict[str, str] = {}
        for token in tokens:
            if "=" not in token:
                raise ValueError(f"参数格式错误: {token}，需要 key=value")
            key, value = token.split("=", 1)
            result[key] = value
        return result

    def _resolve_path(self, raw: str) -> str:
        path = Path(raw)
        if path.is_absolute():
            return str(path)
        return str((self.project_dir / path).resolve())

    def do_status(self, line: str) -> None:
        print(f"HTTP: {self.app.http_base}")
        print(f"WS:   {self.app.ws_url}")
        print(f"WS connected: {self.app.ws.is_connected()}")
        print(f"Selected: {', '.join(self.app.get_selected()) if self.app.get_selected() else '(空)'}")
        try:
            print(f"Health: {self.app.health()}")
        except Exception as error:
            print(f"Health check failed: {error}")

    def do_list(self, line: str) -> None:
        print(self.app.list_drones())

    def do_poll(self, line: str) -> None:
        self.app.force_sync(log_result=True)
        print(self.app.list_drones())

    def do_anchors(self, line: str) -> None:
        print(self.app.list_anchors())

    def do_anchor(self, line: str) -> None:
        tokens = self._tokens(line)
        if len(tokens) != 1:
            print("用法: anchor <drone_id>")
            return
        self.app.get_anchor(tokens[0])

    def do_telemetry(self, line: str) -> None:
        tokens = self._tokens(line)
        drone_id = tokens[0] if tokens else None
        print(self.app.list_telemetry(drone_id))

    def do_alerts(self, line: str) -> None:
        print(self.app.list_alerts())

    def do_arrays(self, line: str) -> None:
        print(self.app.list_arrays())

    def do_events(self, line: str) -> None:
        print(self.app.list_events())

    def do_select(self, line: str) -> None:
        tokens = self._tokens(line)
        self.app.set_selected(tokens)

    def do_selected(self, line: str) -> None:
        selected = self.app.get_selected()
        print(", ".join(selected) if selected else "(空)")

    def do_clear(self, line: str) -> None:
        self.app.clear_selected()

    def do_register(self, line: str) -> None:
        try:
            args = self._pairs(self._tokens(line))
            payload: dict[str, Any] = {
                "name": args["name"],
                "model": args.get("model", "PX4-SITL"),
                "slot": int(args["slot"]),
                "ip": args["ip"],
                "port": int(args["port"]),
                "video_url": args.get("video_url", ""),
            }
            self.app.register_drone(payload)
        except Exception as error:
            print(f"register 失败: {error}")

    def do_update(self, line: str) -> None:
        try:
            tokens = self._tokens(line)
            if len(tokens) < 2:
                print("用法: update <drone_id> key=value ...")
                return
            drone_id = tokens[0]
            args = self._pairs(tokens[1:])
            payload: dict[str, Any] = {}
            for key, value in args.items():
                if key in {"slot", "port"}:
                    payload[key] = int(value)
                else:
                    payload[key] = value
            self.app.update_drone(drone_id, payload)
        except Exception as error:
            print(f"update 失败: {error}")

    def do_delete(self, line: str) -> None:
        tokens = self._tokens(line)
        if len(tokens) != 1:
            print("用法: delete <drone_id>")
            return
        try:
            self.app.delete_drone(tokens[0])
        except Exception as error:
            print(f"delete 失败: {error}")

    def do_move(self, line: str) -> None:
        tokens = self._tokens(line)
        if len(tokens) != 4:
            print("用法: move <drone_id> <x_cm> <y_cm> <z_cm>")
            return
        try:
            self.app.move(tokens[0], float(tokens[1]), float(tokens[2]), float(tokens[3]))
        except Exception as error:
            print(f"move 失败: {error}")

    def do_move_selected(self, line: str) -> None:
        tokens = self._tokens(line)
        if len(tokens) != 3:
            print("用法: move-selected <x_cm> <y_cm> <z_cm>")
            return
        try:
            self.app.move_selected(float(tokens[0]), float(tokens[1]), float(tokens[2]))
        except Exception as error:
            print(f"move-selected 失败: {error}")

    def do_pause(self, line: str) -> None:
        try:
            tokens = self._tokens(line)
            self.app.pause(tokens or None)
        except Exception as error:
            print(f"pause 失败: {error}")

    def do_resume(self, line: str) -> None:
        try:
            tokens = self._tokens(line)
            self.app.resume(tokens or None)
        except Exception as error:
            print(f"resume 失败: {error}")

    def do_array(self, line: str) -> None:
        tokens = self._tokens(line)
        if len(tokens) != 1:
            print("用法: array <json_file>")
            return
        try:
            self.app.submit_array(self._resolve_path(tokens[0]))
        except Exception as error:
            print(f"array 失败: {error}")

    def do_stop(self, line: str) -> None:
        tokens = self._tokens(line)
        if len(tokens) != 1:
            print("用法: stop <array_id>")
            return
        try:
            self.app.stop_array(tokens[0])
        except Exception as error:
            print(f"stop 失败: {error}")

    def do_samples(self, line: str) -> None:
        sample_dir = self.project_dir / "samples"
        for path in sorted(sample_dir.glob("*.json")):
            print(path.relative_to(self.project_dir))

    def do_script(self, line: str) -> None:
        tokens = self._tokens(line)
        if len(tokens) != 1:
            print("用法: script <txt_file>")
            return
        try:
            self.app.run_script(self._resolve_path(tokens[0]), self.onecmd)
        except Exception as error:
            print(f"script 失败: {error}")

    def do_quit(self, line: str) -> bool:
        return True

    def do_exit(self, line: str) -> bool:
        return True

    def do_EOF(self, line: str) -> bool:
        print()
        return True


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Mock UE client for UE5DroneControl backend")
    parser.add_argument("--http-base", default="http://127.0.0.1:8080", help="后端 HTTP 基地址")
    parser.add_argument("--ws-url", default="ws://127.0.0.1:8081/", help="后端 WebSocket 地址")
    parser.add_argument("--poll-interval", type=float, default=2.0, help="无人机列表轮询间隔（秒）")
    parser.add_argument("--reconnect-delay", type=float, default=2.0, help="WebSocket 重连间隔（秒）")
    parser.add_argument("--verbose-telemetry", action="store_true", help="打印每一帧 telemetry")
    parser.add_argument("mode", nargs="?", default="shell", choices=["shell"], help="运行模式")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    project_dir = Path(__file__).resolve().parent.parent
    printer = SafePrinter()
    app = MockUEApp(
        http_base=args.http_base,
        ws_url=args.ws_url,
        printer=printer,
        poll_interval=args.poll_interval,
        reconnect_delay=args.reconnect_delay,
        verbose_telemetry=args.verbose_telemetry,
    )

    app.start()
    try:
        shell = MockUEShell(app, project_dir=project_dir)
        shell.cmdloop()
    finally:
        app.stop()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
