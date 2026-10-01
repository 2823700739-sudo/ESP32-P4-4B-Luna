"""USB transport that exposes the Windows agent over Luna Link."""

from __future__ import annotations

import json
import hashlib
import struct
import threading
import time
from copy import deepcopy
from datetime import datetime, timezone
from typing import Any, Protocol

import serial

from luna_device_diagnostics import parse_device_diagnostics
from luna_actions import parse_action_request
from luna_link_protocol import Frame, FrameDecoder, MAX_PAYLOAD, MessageType, encode_frame
from luna_usb_probe import open_luna, read_frames


class AgentApi(Protocol):
    media: Any

    def snapshot(self) -> dict[str, Any]: ...

    def execute(self, request_id: str, action: str, value: Any = None) -> dict[str, Any]: ...


def encode_json(body: dict[str, Any]) -> bytes:
    encoded = json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(encoded) > MAX_PAYLOAD:
        raise ValueError(f"USB JSON payload exceeds {MAX_PAYLOAD} bytes")
    return encoded


class LunaUsbTransport:
    def __init__(self, agent: AgentApi, retry_seconds: float = 1.0,
                 idle_timeout_seconds: float = 120.0) -> None:
        self.agent = agent
        self.retry_seconds = retry_seconds
        self.idle_timeout_seconds = idle_timeout_seconds
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._stats_lock = threading.Lock()
        self._connected = False
        self._port: str | None = None
        self._firmware: str | None = None
        self._connected_since: float | None = None
        self._connections = 0
        self._disconnects = 0
        self._errors_total = 0
        self._consecutive_errors = 0
        self._last_connected_at: str | None = None
        self._last_disconnected_at: str | None = None
        self._last_error: str | None = None
        self._last_error_at: str | None = None
        self._last_snapshot_at: str | None = None
        self._last_frame_at: str | None = None
        self._last_frame_monotonic: float | None = None
        self._state_requests = 0
        self._action_requests = 0
        self._cover_info_requests = 0
        self._cover_requests = 0
        self._device: dict[str, Any] | None = None
        self._device_received_at: str | None = None
        self._device_received_monotonic: float | None = None
        self._device_connection: int | None = None
        self._device_boot_changes = 0
        self._device_diagnostics_received = 0
        self._device_diagnostics_rejected = 0

    @staticmethod
    def _utc_now() -> str:
        return datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")

    def diagnostics(self) -> dict[str, Any]:
        with self._stats_lock:
            duration = (round(time.monotonic() - self._connected_since, 1)
                        if self._connected_since is not None else None)
            frame_age = (round(time.monotonic() - self._last_frame_monotonic, 1)
                         if self._connected and self._last_frame_monotonic is not None else None)
            device_age = (round(time.monotonic() - self._device_received_monotonic, 1)
                          if self._device_received_monotonic is not None else None)
            device_stale = (not self._connected or device_age is None or
                            device_age >= self.idle_timeout_seconds or
                            self._device_connection != self._connections)
            return {
                "connected": self._connected,
                "port": self._port,
                "firmware": self._firmware,
                "connected_seconds": duration,
                "connections": self._connections,
                "disconnects": self._disconnects,
                "errors_total": self._errors_total,
                "consecutive_errors": self._consecutive_errors,
                "last_connected_at": self._last_connected_at,
                "last_disconnected_at": self._last_disconnected_at,
                "last_error": self._last_error,
                "last_error_at": self._last_error_at,
                "last_snapshot_at": self._last_snapshot_at,
                "last_frame_at": self._last_frame_at,
                "last_frame_age_seconds": frame_age,
                "idle_timeout_seconds": self.idle_timeout_seconds,
                "state_requests": self._state_requests,
                "action_requests": self._action_requests,
                "cover_info_requests": self._cover_info_requests,
                "cover_chunk_requests": self._cover_requests,
                "device": deepcopy(self._device),
                "device_received_at": self._device_received_at,
                "device_age_seconds": device_age,
                "device_stale": device_stale,
                "device_boot_changes": self._device_boot_changes,
                "device_diagnostics_received": self._device_diagnostics_received,
                "device_diagnostics_rejected": self._device_diagnostics_rejected,
            }

    def start(self) -> None:
        if self._thread is not None:
            return
        self._thread = threading.Thread(target=self._run, name="luna-usb", daemon=True)
        self._thread.start()

    def close(self) -> None:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=2.0)
            self._thread = None

    @staticmethod
    def _send_json(port: serial.Serial, message_type: MessageType,
                   request_id: int, body: dict[str, Any]) -> None:
        frame = encode_frame(message_type, request_id, encode_json(body))
        if port.write(frame) != len(frame):
            raise serial.SerialTimeoutException("short USB write")
        port.flush()

    def _handle_frame(self, port: serial.Serial, frame: Frame) -> None:
        if frame.message_type == MessageType.DEVICE_DIAGNOSTICS:
            try:
                device = parse_device_diagnostics(frame.payload)
            except (UnicodeDecodeError, ValueError) as error:
                with self._stats_lock:
                    self._device_diagnostics_rejected += 1
                    rejected = self._device_diagnostics_rejected
                if rejected == 1 or rejected % 20 == 0:
                    print(f"Luna device diagnostics rejected: {error} count={rejected}")
                return
            with self._stats_lock:
                boot_changed = self._device is not None and self._device["boot_id"] != device["boot_id"]
                if boot_changed:
                    self._device_boot_changes += 1
                self._device = device
                self._device_received_at = self._utc_now()
                self._device_received_monotonic = time.monotonic()
                self._device_connection = self._connections
                self._device_diagnostics_received += 1
            if boot_changed:
                print(f"Luna device boot changed: reason={device['reset_reason_name']}")
            return

        if frame.message_type == MessageType.STATE_REQUEST:
            snapshot = self.agent.snapshot()
            self._send_json(
                port, MessageType.STATE_SNAPSHOT, frame.request_id, snapshot
            )
            with self._stats_lock:
                self._state_requests += 1
                self._last_snapshot_at = self._utc_now()
                count = self._state_requests
            if count == 1 or count % 20 == 0:
                print(
                    f"Luna USB state snapshot served: sequence={snapshot.get('sequence')} "
                    f"count={count}"
                )
            return

        if frame.message_type == MessageType.COVER_INFO_REQUEST:
            data, _content_type, cover_id = self.agent.media.cover()
            if (len(frame.payload) != 16 or
                    frame.payload != cover_id.encode("ascii") or
                    not data or len(data) > 256 * 1024):
                payload = bytes(36)
            else:
                payload = struct.pack("<I", len(data)) + hashlib.sha256(data).digest()
            self._send_bytes(port, MessageType.COVER_INFO, frame.request_id, payload)
            with self._stats_lock:
                self._cover_info_requests += 1
            return

        if frame.message_type == MessageType.COVER_CHUNK_REQUEST:
            payload = b""
            if len(frame.payload) == 22:
                requested_id = frame.payload[:16]
                offset, chunk_size = struct.unpack_from("<IH", frame.payload, 16)
                data, _content_type, cover_id = self.agent.media.cover()
                if (requested_id == cover_id.encode("ascii") and
                        0 < chunk_size <= 2048 and offset < len(data)):
                    payload = struct.pack("<I", offset) + data[offset:offset + chunk_size]
            self._send_bytes(port, MessageType.COVER_CHUNK, frame.request_id, payload)
            with self._stats_lock:
                self._cover_requests += 1
                count = self._cover_requests
            if count == 1 or count % 100 == 0:
                print(f"Luna USB cover chunks served: count={count}")
            return

        if frame.message_type == MessageType.ACTION_REQUEST:
            try:
                request_id, action, value = parse_action_request(frame.payload)
                result = self.agent.execute(request_id, action, value)
            except (KeyError, UnicodeDecodeError, json.JSONDecodeError, ValueError) as error:
                result = {"ok": False, "error": str(error)}
            self._send_json(port, MessageType.ACTION_RESULT, frame.request_id, result)
            with self._stats_lock:
                self._action_requests += 1
                count = self._action_requests
            print(
                f"Luna USB action result: action={result.get('action', 'invalid')} "
                f"ok={result.get('ok', False)} count={count}"
            )
            return

        if frame.message_type == MessageType.TOUCH_TEST:
            print(f"Luna USB touch: {frame.payload.decode('utf-8', errors='replace')}")

    @staticmethod
    def _send_bytes(port: serial.Serial, message_type: MessageType,
                    request_id: int, payload: bytes) -> None:
        frame = encode_frame(message_type, request_id, payload)
        if port.write(frame) != len(frame):
            raise serial.SerialTimeoutException("short USB write")
        port.flush()

    def _serve(self, port: serial.Serial, decoder: FrameDecoder) -> None:
        last_frame_time = time.monotonic()
        while not self._stop.is_set():
            deadline = time.monotonic() + 0.25
            for frame in read_frames(port, decoder, deadline):
                self._handle_frame(port, frame)
                last_frame_time = time.monotonic()
                with self._stats_lock:
                    self._last_frame_monotonic = last_frame_time
                    self._last_frame_at = self._utc_now()
            if time.monotonic() - last_frame_time >= self.idle_timeout_seconds:
                raise TimeoutError(f"no Luna Link frames for {self.idle_timeout_seconds:g}s")

    def _run(self) -> None:
        last_error = ""
        next_error_log = 0.0
        while not self._stop.is_set():
            port: serial.Serial | None = None
            try:
                port, decoder, identity = open_luna()
                last_error = ""
                with self._stats_lock:
                    self._connected = True
                    self._port = str(port.port)
                    self._firmware = str(identity.get("firmware", "unknown"))
                    self._connected_since = time.monotonic()
                    self._last_frame_monotonic = None
                    self._connections += 1
                    self._consecutive_errors = 0
                    self._last_connected_at = self._utc_now()
                print(
                    f"Luna USB agent connected on {port.port}: "
                    f"firmware={identity.get('firmware', 'unknown')}"
                )
                self._serve(port, decoder)
            except Exception as error:
                if not self._stop.is_set():
                    message = f"{type(error).__name__}: {error}"
                    now = time.monotonic()
                    with self._stats_lock:
                        self._errors_total += 1
                        self._consecutive_errors += 1
                        self._last_error = message[:240]
                        self._last_error_at = self._utc_now()
                    if message != last_error or now >= next_error_log:
                        print(f"Luna USB agent waiting: {message}")
                        last_error = message
                        next_error_log = now + 10.0
            finally:
                if port is not None:
                    with self._stats_lock:
                        if self._connected:
                            self._connected = False
                            self._connected_since = None
                            self._last_frame_monotonic = None
                            self._disconnects += 1
                            self._last_disconnected_at = self._utc_now()
                    try:
                        port.close()
                    except (OSError, serial.SerialException):
                        pass
            self._stop.wait(max(self.retry_seconds, 0.1))
