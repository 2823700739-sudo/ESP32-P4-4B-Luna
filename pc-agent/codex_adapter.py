"""Read and cache quota only. Project names belong to the VS Code collector."""

from __future__ import annotations

import copy
import json
import math
import logging
import os
import queue
import shutil
import subprocess
import threading
import time
from pathlib import Path
from typing import Any


def _resolve_codex_executable(configured: str) -> str:
    """Find the desktop Codex CLI even when a new shell has no Codex PATH entry."""
    if configured:
        expanded = Path(os.path.expandvars(configured)).expanduser()
        if expanded.is_file():
            return str(expanded)
        resolved = shutil.which(configured)
        if resolved:
            return resolved
        return str(expanded)

    for command in ("codex.exe", "codex"):
        resolved = shutil.which(command)
        if resolved:
            return resolved

    local_app_data = os.environ.get("LOCALAPPDATA", "")
    if local_app_data:
        codex_bin = Path(local_app_data) / "OpenAI" / "Codex" / "bin"
        candidates = list(codex_bin.glob("*/codex.exe"))
        candidates.extend(codex_bin.glob("codex.exe"))
        candidates = [candidate for candidate in candidates if candidate.is_file()]
        if candidates:
            return str(max(candidates, key=lambda candidate: candidate.stat().st_mtime))

    # Keep the Windows agent available for music and other cards. The first
    # refresh will report an explicit Codex-unavailable state if this fails.
    return "codex"


def _remaining_percent(window: Any) -> int:
    if not isinstance(window, dict):
        return -1
    used = window.get("usedPercent")
    if type(used) not in (int, float) or not math.isfinite(used) or not 0 <= used <= 100:
        return -1
    return max(0, min(100, round(100.0 - float(used))))


class CodexAdapter:
    """Maintain one local Codex App Server process and cache read-only state."""

    def __init__(self, executable: str = "", refresh_seconds: int = 15) -> None:
        self.executable = _resolve_codex_executable(executable)
        self.refresh_seconds = max(5, refresh_seconds)
        self._process: subprocess.Popen[bytes] | None = None
        self._responses: queue.Queue[dict[str, Any] | None] = queue.Queue(maxsize=32)
        self._request_lock = threading.Lock()
        self._cache_lock = threading.Lock()
        self._stop_event = threading.Event()
        self._refresh_thread: threading.Thread | None = None
        self._next_id = 1
        self._cached_at = 0.0
        self._cached = self._empty_snapshot()

    @staticmethod
    def _empty_snapshot(error: str = "Codex data unavailable") -> dict[str, Any]:
        return {"codex": {"available": False, "windows": [], "error": error}}

    @staticmethod
    def _deliver(responses: queue.Queue, message: dict | None) -> None:
        # One serialized request is outstanding. Never let late replies or
        # unsolicited notifications grow an unbounded long-running queue.
        while True:
            try:
                responses.put_nowait(message)
                return
            except queue.Full:
                try: responses.get_nowait()
                except queue.Empty: pass

    def _reader(self, process: subprocess.Popen[bytes], responses: queue.Queue) -> None:
        assert process.stdout is not None
        try:
            for raw_line in iter(process.stdout.readline, b""):
                try:
                    message = json.loads(raw_line.decode("utf-8"))
                    if isinstance(message, dict) and "id" in message and ("result" in message or "error" in message):
                        self._deliver(responses, message)
                except (UnicodeDecodeError, json.JSONDecodeError):
                    continue
        finally:
            self._deliver(responses, None)

    def _write(self, message: dict[str, Any]) -> None:
        if self._process is None or self._process.stdin is None:
            raise RuntimeError("Codex App Server is not running")
        encoded = (json.dumps(message, ensure_ascii=False, separators=(",", ":")) + "\n").encode(
            "utf-8"
        )
        self._process.stdin.write(encoded)
        self._process.stdin.flush()

    def _wait_for(self, request_id: int, timeout: float = 8.0) -> dict[str, Any]:
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Codex App Server response timed out")
            try:
                message = self._responses.get(timeout=remaining)
            except queue.Empty as error:
                raise TimeoutError("Codex App Server response timed out") from error
            if message is None:
                raise RuntimeError("Codex App Server stopped")
            if message.get("id") != request_id:
                continue
            if "error" in message:
                raise RuntimeError(str(message["error"]))
            result = message.get("result")
            return result if isinstance(result, dict) else {}

    def _start(self) -> None:
        self._stop()
        creation_flags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
        self._responses = queue.Queue(maxsize=32)
        self._process = subprocess.Popen(
            [self.executable, "app-server"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            creationflags=creation_flags,
        )
        threading.Thread(
            target=self._reader,
            args=(self._process, self._responses),
            name="codex-app-server-reader",
            daemon=True,
        ).start()
        initialize_id = self._next_id
        self._next_id += 1
        self._write(
            {
                "method": "initialize",
                "id": initialize_id,
                "params": {
                    "clientInfo": {
                        "name": "luna_desktop_companion",
                        "title": "Luna Desktop Companion",
                        "version": "0.2.0",
                    }
                },
            }
        )
        self._wait_for(initialize_id)
        self._write({"method": "initialized", "params": {}})

    def _stop(self) -> None:
        process = self._process
        self._process = None
        if process is None:
            return
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill()
        for stream in (process.stdin, process.stdout):
            if stream is not None:
                stream.close()

    def _request(self, method: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
        with self._request_lock:
            if self._stop_event.is_set():
                raise RuntimeError("Codex adapter is stopping")
            try:
                if self._process is None or self._process.poll() is not None:
                    self._start()
                request_id = self._next_id
                self._next_id += 1
                message: dict[str, Any] = {"method": method, "id": request_id}
                if params is not None:
                    message["params"] = params
                self._write(message)
                return self._wait_for(request_id)
            except Exception:
                self._stop()
                raise

    def _refresh(self) -> dict[str, Any]:
        limits_result = self._request("account/rateLimits/read")

        by_id = limits_result.get("rateLimitsByLimitId")
        limits = by_id.get("codex") if isinstance(by_id, dict) else None
        if not isinstance(limits, dict):
            limits = limits_result.get("rateLimits")
        if not isinstance(limits, dict):
            limits = {}
        primary = limits.get("primary")
        secondary = limits.get("secondary")
        windows = [w for w in (primary, secondary) if isinstance(w, dict)]
        return {
            "codex": {
                "available": any(_remaining_percent(w) >= 0 for w in windows),
                "windows": windows,
            },
        }

    def _refresh_once(self) -> None:
        try:
            snapshot = self._refresh()
        except Exception as error:
            if not self._stop_event.is_set():
                logging.getLogger("luna.ble.link").warning("Codex state refresh failed: %s", type(error).__name__)
            snapshot = self._empty_snapshot(type(error).__name__)
        with self._cache_lock:
            snapshot["sampled_at_ms"] = int(time.time() * 1000)
            self._cached = snapshot
            self._cached_at = time.monotonic()

    def _refresh_loop(self) -> None:
        while not self._stop_event.is_set():
            self._refresh_once()
            self._stop_event.wait(self.refresh_seconds)

    def start(self) -> None:
        if self._refresh_thread is None and not self._stop_event.is_set():
            self._refresh_thread = threading.Thread(
                target=self._refresh_loop, name="codex-refresh", daemon=True,
            )
            self._refresh_thread.start()

    def snapshot(self) -> dict[str, Any]:
        # UI snapshot readers never wait for App Server I/O or its request lock.
        with self._cache_lock:
            if self._cached_at == 0 or time.monotonic() - self._cached_at >= self.refresh_seconds * 2:
                return self._empty_snapshot("Codex refresh pending")
            return copy.deepcopy(self._cached)

    def close(self) -> None:
        self._stop_event.set()
        with self._request_lock:
            self._stop()
        if self._refresh_thread is not None:
            self._refresh_thread.join(timeout=2)
