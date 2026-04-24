from __future__ import annotations

import asyncio
import json
import threading
from typing import Any, Callable, Optional

import websockets


StatusCallback = Callable[[str], None]
MessageCallback = Callable[[dict[str, Any]], None]


class WebSocketEventClient:
    def __init__(
        self,
        url: str,
        on_status: StatusCallback,
        on_message: MessageCallback,
        reconnect_delay: float = 2.0,
    ) -> None:
        self.url = url
        self.on_status = on_status
        self.on_message = on_message
        self.reconnect_delay = reconnect_delay

        self._stop_event = threading.Event()
        self._connected = threading.Event()
        self._thread: Optional[threading.Thread] = None
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._send_queue: Optional[asyncio.Queue[Optional[dict[str, Any]]]] = None

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._stop_event.clear()
        self._thread = threading.Thread(target=self._thread_main, name="mock-ue-ws", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop_event.set()
        if self._loop is not None and self._send_queue is not None:
            self._loop.call_soon_threadsafe(self._send_queue.put_nowait, None)
        if self._thread is not None:
            self._thread.join(timeout=2.0)

    def is_connected(self) -> bool:
        return self._connected.is_set()

    def send_json(self, payload: dict[str, Any]) -> None:
        if not self.is_connected() or self._loop is None or self._send_queue is None:
            raise RuntimeError("WebSocket 未连接")
        self._loop.call_soon_threadsafe(self._send_queue.put_nowait, payload)

    def _thread_main(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self._loop = loop
        self._send_queue = asyncio.Queue()
        try:
            loop.run_until_complete(self._run())
        finally:
            pending = asyncio.all_tasks(loop)
            for task in pending:
                task.cancel()
            if pending:
                loop.run_until_complete(asyncio.gather(*pending, return_exceptions=True))
            loop.close()

    async def _run(self) -> None:
        while not self._stop_event.is_set():
            try:
                self.on_status(f"正在连接 {self.url}")
                async with websockets.connect(
                    self.url,
                    ping_interval=20,
                    ping_timeout=20,
                    close_timeout=3,
                ) as websocket:
                    self._connected.set()
                    self.on_status("WebSocket 已连接")
                    receiver = asyncio.create_task(self._receiver_loop(websocket))
                    sender = asyncio.create_task(self._sender_loop(websocket))
                    done, pending = await asyncio.wait(
                        [receiver, sender],
                        return_when=asyncio.FIRST_EXCEPTION,
                    )
                    for task in pending:
                        task.cancel()
                    for task in done:
                        exception = task.exception()
                        if exception is not None:
                            raise exception
            except Exception as error:
                self._connected.clear()
                if not self._stop_event.is_set():
                    self.on_status(f"WebSocket 断开: {error}")
                    await asyncio.sleep(self.reconnect_delay)
            finally:
                self._connected.clear()

    async def _receiver_loop(self, websocket: websockets.WebSocketClientProtocol) -> None:
        async for message in websocket:
            try:
                payload = json.loads(message)
                if isinstance(payload, dict):
                    self.on_message(payload)
            except json.JSONDecodeError:
                self.on_status(f"收到无法解析的 WS 消息: {message}")

    async def _sender_loop(self, websocket: websockets.WebSocketClientProtocol) -> None:
        assert self._send_queue is not None
        while not self._stop_event.is_set():
            payload = await self._send_queue.get()
            if payload is None:
                return
            await websocket.send(json.dumps(payload, ensure_ascii=False))
