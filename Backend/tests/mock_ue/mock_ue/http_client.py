from __future__ import annotations

import json
import urllib.error
import urllib.request
from dataclasses import dataclass
from typing import Any, Optional


@dataclass
class BackendHttpError(RuntimeError):
    status_code: int
    message: str
    response_body: str = ""

    def __str__(self) -> str:
        return f"HTTP {self.status_code}: {self.message}"


class BackendHttpClient:
    def __init__(self, base_url: str, timeout: float = 5.0) -> None:
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout

    def _url(self, path: str) -> str:
        return f"{self.base_url}/{path.lstrip('/')}"

    def _request(self, method: str, path: str, payload: Optional[dict[str, Any]] = None) -> Any:
        data: Optional[bytes] = None
        headers = {
            "Accept": "application/json",
        }
        if payload is not None:
            data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
            headers["Content-Type"] = "application/json"

        request = urllib.request.Request(
            self._url(path),
            data=data,
            headers=headers,
            method=method,
        )

        try:
            with urllib.request.urlopen(request, timeout=self.timeout) as response:
                text = response.read().decode("utf-8")
                if not text.strip():
                    return None
                return json.loads(text)
        except urllib.error.HTTPError as error:
            body = error.read().decode("utf-8", errors="replace")
            message = body
            try:
                parsed = json.loads(body)
                if isinstance(parsed, dict):
                    message = parsed.get("detail", body)
            except json.JSONDecodeError:
                pass
            raise BackendHttpError(error.code, message, body) from error
        except urllib.error.URLError as error:
            raise RuntimeError(f"failed to connect to backend: {error.reason}") from error

    def health(self) -> dict[str, Any]:
        result = self._request("GET", "/")
        return result if isinstance(result, dict) else {}

    def get_drones(self) -> dict[str, Any]:
        result = self._request("GET", "/api/drones")
        if isinstance(result, list):
            return {"drones": result}
        return result if isinstance(result, dict) else {"drones": []}

    def register_drone(self, payload: dict[str, Any]) -> dict[str, Any]:
        result = self._request("POST", "/api/drones", payload)
        return result if isinstance(result, dict) else {}

    def update_drone(self, drone_id: str, payload: dict[str, Any]) -> dict[str, Any]:
        result = self._request("PUT", f"/api/drones/{drone_id}", payload)
        return result if isinstance(result, dict) else {}

    def delete_drone(self, drone_id: str) -> dict[str, Any]:
        result = self._request("DELETE", f"/api/drones/{drone_id}")
        return result if isinstance(result, dict) else {}

    def get_anchor(self, drone_id: str) -> dict[str, Any]:
        result = self._request("GET", f"/api/drones/{drone_id}/anchor")
        return result if isinstance(result, dict) else {}

    def submit_array(self, payload: dict[str, Any]) -> dict[str, Any]:
        result = self._request("POST", "/api/arrays", payload)
        return result if isinstance(result, dict) else {}

    def stop_array(self, array_id: str) -> dict[str, Any]:
        result = self._request("POST", f"/api/arrays/{array_id}/stop")
        return result if isinstance(result, dict) else {}
