"""Luna Windows companion HTTP service.

The service exposes small JSON snapshots to the panel and accepts a strict
allow-list of media actions. Windows GSMTC provides the authoritative playback
state so rapid taps cannot make the panel drift away from the real player.
"""

from __future__ import annotations

import ctypes
import json
import platform
import socket
import sys
import threading
import time
from datetime import datetime, timezone
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any

from agent_instance import single_agent_instance
from codex_adapter import CodexAdapter
from luna_actions import ActionHistory, parse_action_request
from luna_usb_transport import LunaUsbTransport
from weather_adapter import WeatherAdapter
from windows_media import WindowsMediaAdapter
from windows_volume import WindowsVolumeAdapter


ROOT = Path(__file__).resolve().parent
CONFIG_PATH = ROOT / "config.local.json"
PROTOCOL_VERSION = 1
MAX_BODY_BYTES = 4096

MEDIA_KEYS = {
    "music.previous": 0xB1,
    "music.next": 0xB0,
    "music.play_pause": 0xB3,
    "music.volume_down": 0xAE,
    "music.volume_up": 0xAF,
}


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def load_config() -> dict[str, Any]:
    if not CONFIG_PATH.exists():
        raise FileNotFoundError(
            f"Missing {CONFIG_PATH}. Copy config.example.json to config.local.json first."
        )
    config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    token = str(config.get("token", ""))
    if len(token) < 20:
        raise ValueError("config.local.json token must contain at least 20 characters")
    return config


def send_media_key(vk_code: int) -> None:
    if platform.system() != "Windows":
        raise RuntimeError("Media keys are only available on Windows")
    key_up = 0x0002
    user32 = ctypes.windll.user32
    user32.keybd_event(vk_code, 0, 0, 0)
    user32.keybd_event(vk_code, 0, key_up, 0)


class AgentState:
    def __init__(
        self,
        config: dict[str, Any],
        media: WindowsMediaAdapter,
        codex: CodexAdapter,
        weather: WeatherAdapter,
        volume: WindowsVolumeAdapter,
    ) -> None:
        self.config = config
        self.media = media
        self.codex = codex
        self.weather = weather
        self.volume = volume
        self.started_at = utc_now()
        self._sequence = 0
        self._lock = threading.Lock()
        self._actions = ActionHistory()

    def snapshot(self) -> dict[str, Any]:
        with self._lock:
            self._sequence += 1
            sequence = self._sequence

        codex_snapshot = self.codex.snapshot()
        projects = codex_snapshot["recent_projects"]
        if not projects:
            projects = self.config.get("recent_projects", [])[:3]
        music = self.media.snapshot()
        return {
            "protocol_version": PROTOCOL_VERSION,
            "sequence": sequence,
            "generated_at": utc_now(),
            "pc": {
                "online": True,
                "name": socket.gethostname(),
                "started_at": self.started_at,
            },
            "music": music,
            "volume": self.volume.snapshot(),
            "codex": codex_snapshot["codex"],
            "recent_projects": projects,
            "weather": self.weather.snapshot(),
        }

    def execute(self, request_id: str, action: str, value: Any = None) -> dict[str, Any]:
        return self._actions.execute(
            request_id, action, value, lambda: self._execute_action(action, value)
        )

    def action_diagnostics(self) -> dict[str, Any]:
        return self._actions.diagnostics()

    def _execute_action(self, action: str, value: Any) -> dict[str, Any]:
        if action in {"music.volume_set", "music.mute"}:
            volume = (self.volume.set_percent(value) if action == "music.volume_set"
                      else self.volume.toggle_mute())
            return {"ok": True, "duplicate": False, "action": action, "volume": volume}

        accepted = False
        if action in {
            "music.previous",
            "music.play",
            "music.pause",
            "music.play_pause",
            "music.next",
        }:
            accepted = self.media.execute(action)
        if not accepted and action in {"music.play", "music.pause"}:
            return {"ok": False, "error": "media_session_unavailable", "action": action}
        key = MEDIA_KEYS.get(action)
        if not accepted and key is not None:
            send_media_key(key)

        return {
            "ok": True,
            "duplicate": False,
            "action": action,
            "session_controlled": accepted,
            "music": self.media.snapshot(),
            "volume": self.volume.snapshot(),
        }


class LunaRequestHandler(BaseHTTPRequestHandler):
    server_version = "LunaAgent/0.1"

    @property
    def agent(self) -> AgentState:
        return self.server.agent  # type: ignore[attr-defined]

    @property
    def usb_transport(self) -> LunaUsbTransport:
        return self.server.usb_transport  # type: ignore[attr-defined]

    def log_message(self, message: str, *args: object) -> None:
        sys.stdout.write("[%s] %s\n" % (self.log_date_time_string(), message % args))
        sys.stdout.flush()

    def _authorized(self) -> bool:
        expected = str(self.agent.config["token"])
        return self.headers.get("X-Luna-Token", "") == expected

    def _send_json(self, status: HTTPStatus, body: dict[str, Any]) -> None:
        encoded = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(encoded)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(encoded)

    def _send_bytes(self, content_type: str, data: bytes, cover_id: str) -> None:
        self.send_response(HTTPStatus.OK)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "private, max-age=3600")
        self.send_header("ETag", f'"{cover_id}"')
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self) -> None:
        if self.path == "/health":
            self._send_json(HTTPStatus.OK, {"ok": True, "protocol_version": PROTOCOL_VERSION})
            return
        if self.path == "/api/v1/diagnostics":
            if not self._authorized():
                self._send_json(HTTPStatus.UNAUTHORIZED, {"ok": False, "error": "unauthorized"})
                return
            self._send_json(HTTPStatus.OK, {
                "ok": True,
                "agent_started_at": self.agent.started_at,
                "usb": self.usb_transport.diagnostics(),
                "actions": self.agent.action_diagnostics(),
            })
            return
        if self.path == "/api/v1/music/cover":
            if not self._authorized():
                self._send_json(HTTPStatus.UNAUTHORIZED, {"ok": False, "error": "unauthorized"})
                return
            data, content_type, cover_id = self.agent.media.cover()
            if not data:
                self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "cover_unavailable"})
                return
            self._send_bytes(content_type or "image/jpeg", data, cover_id)
            return
        if self.path != "/api/v1/state":
            self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not_found"})
            return
        if not self._authorized():
            self._send_json(HTTPStatus.UNAUTHORIZED, {"ok": False, "error": "unauthorized"})
            return
        self._send_json(HTTPStatus.OK, self.agent.snapshot())

    def do_POST(self) -> None:
        if self.path != "/api/v1/actions":
            self._send_json(HTTPStatus.NOT_FOUND, {"ok": False, "error": "not_found"})
            return
        if not self._authorized():
            self._send_json(HTTPStatus.UNAUTHORIZED, {"ok": False, "error": "unauthorized"})
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
            if length <= 0 or length > MAX_BODY_BYTES:
                raise ValueError("invalid body length")
            request_id, action, value = parse_action_request(self.rfile.read(length))
            result = self.agent.execute(request_id, action, value)
            status = HTTPStatus.OK if result["ok"] else HTTPStatus.BAD_REQUEST
            self._send_json(status, result)
        except (KeyError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
            self._send_json(HTTPStatus.BAD_REQUEST, {"ok": False, "error": str(error)})


class LunaServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address: tuple[str, int], agent: AgentState,
                 usb_transport: LunaUsbTransport) -> None:
        self.agent = agent
        self.usb_transport = usb_transport
        super().__init__(address, LunaRequestHandler)


def run_agent() -> int:
    config = load_config()
    host = str(config.get("listen_host", "0.0.0.0"))
    port = int(config.get("port", 8765))
    media = WindowsMediaAdapter(str(config.get("preferred_media_source", "cloudmusic.exe")))
    volume = WindowsVolumeAdapter()
    codex = CodexAdapter(
        str(config.get("codex_executable") or ""),
        int(config.get("codex_refresh_seconds") or 15),
    )
    weather_config = config.get("weather")
    weather = WeatherAdapter(
        weather_config if isinstance(weather_config, dict) else {},
        ROOT / "weather-cache.json",
    )
    media.start()
    weather.start()
    codex.start()
    print(f"Codex executable: {codex.executable}")
    agent = AgentState(config, media, codex, weather, volume)
    usb_transport = LunaUsbTransport(agent)
    server = LunaServer((host, port), agent, usb_transport)
    if bool(config.get("usb_enabled", True)):
        usb_transport.start()
    print(f"Luna agent listening on http://{host}:{port}")
    print("Luna USB transport is enabled." if bool(config.get("usb_enabled", True))
          else "Luna USB transport is disabled.")
    print("Windows media session synchronization is active.")
    print("Codex App Server synchronization is starting in the background.")
    if weather.configured:
        print("Open-Meteo weather synchronization is active.")
    else:
        print("Weather location is not configured.")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever(poll_interval=0.5)
    except KeyboardInterrupt:
        print("Stopping Luna agent...")
    finally:
        server.server_close()
        usb_transport.close()
        media.close()
        codex.close()
        weather.close()
    return 0


def main() -> int:
    with single_agent_instance() as primary:
        if not primary:
            print("Luna agent is already running in this Windows session.")
            return 0
        return run_agent()


if __name__ == "__main__":
    raise SystemExit(main())
