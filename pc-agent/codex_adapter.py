"""Read Codex quota and recent workspaces through the local App Server."""

from __future__ import annotations

import copy
import json
import os
import queue
import shutil
import subprocess
import threading
import time
from datetime import datetime, timezone
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


def _utc_iso(timestamp: Any) -> str:
    if not isinstance(timestamp, (int, float)):
        return ""
    return datetime.fromtimestamp(timestamp, timezone.utc).isoformat(timespec="seconds").replace(
        "+00:00", "Z"
    )


def _remaining_percent(window: Any) -> int:
    if not isinstance(window, dict):
        return -1
    used = window.get("usedPercent")
    if not isinstance(used, (int, float)):
        return -1
    return max(0, min(100, round(100.0 - float(used))))


def _window_name(window: dict[str, Any], fallback: str) -> str:
    minutes = window.get("windowDurationMins")
    if isinstance(minutes, (int, float)):
        minutes = int(minutes)
        if minutes % 1440 == 0:
            return f"{minutes // 1440}d"
        if minutes % 60 == 0:
            return f"{minutes // 60}h"
        return f"{minutes}m"
    return fallback


def _reset_line(window: Any, fallback: str) -> str:
    if not isinstance(window, dict):
        return ""
    timestamp = window.get("resetsAt")
    if not isinstance(timestamp, (int, float)):
        return ""
    reset = datetime.fromtimestamp(timestamp).astimezone()
    return f"{_window_name(window, fallback)} reset {reset:%a %H:%M}"


class CodexAdapter:
    """Maintain one local Codex App Server process and cache read-only state."""

    def __init__(self, executable: str = "", refresh_seconds: int = 15) -> None:
        self.executable = _resolve_codex_executable(executable)
        self.refresh_seconds = max(5, refresh_seconds)
        self._process: subprocess.Popen[bytes] | None = None
        self._responses: queue.Queue[dict[str, Any] | None] = queue.Queue()
        self._request_lock = threading.Lock()
        self._cache_lock = threading.Lock()
        self._stop_event = threading.Event()
        self._refresh_thread: threading.Thread | None = None
        self._next_id = 1
        self._cached_at = 0.0
        self._cached = self._empty_snapshot()

    @staticmethod
    def _empty_snapshot(error: str = "Codex data unavailable") -> dict[str, Any]:
        return {
            "codex": {
                "available": False,
                "remaining_percent": -1,
                "weekly_remaining_percent": -1,
                "reset_at": "",
                "weekly_reset_at": "",
                "reset_text": "",
                "summary": "Codex data unavailable",
                "source": "Codex App Server",
                "error": error,
            },
            "recent_projects": [],
        }

    def _reader(self, process: subprocess.Popen[bytes], responses: queue.Queue) -> None:
        assert process.stdout is not None
        try:
            for raw_line in iter(process.stdout.readline, b""):
                try:
                    responses.put(json.loads(raw_line.decode("utf-8")))
                except (UnicodeDecodeError, json.JSONDecodeError):
                    continue
        finally:
            responses.put(None)

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
        self._responses = queue.Queue()
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

    @staticmethod
    def _projects(thread_result: dict[str, Any]) -> list[dict[str, Any]]:
        projects: list[dict[str, Any]] = []
        seen: set[str] = set()
        for thread in thread_result.get("data", []):
            if not isinstance(thread, dict):
                continue
            cwd = thread.get("cwd")
            if not isinstance(cwd, str) or not cwd:
                continue
            key = os.path.normcase(os.path.normpath(cwd))
            if key in seen:
                continue
            seen.add(key)
            path = Path(cwd)
            projects.append(
                {
                    "name": path.name or cwd,
                    "path": cwd,
                    "updated_at": _utc_iso(thread.get("updatedAt")),
                }
            )
            if len(projects) == 3:
                break
        return projects

    def _refresh(self) -> dict[str, Any]:
        limits_result = self._request("account/rateLimits/read")
        thread_result = self._request(
            "thread/list",
            {
                "limit": 25,
                "sortKey": "updated_at",
                "sortDirection": "desc",
                "sourceKinds": [],
            },
        )

        by_id = limits_result.get("rateLimitsByLimitId")
        limits = by_id.get("codex") if isinstance(by_id, dict) else None
        if not isinstance(limits, dict):
            limits = limits_result.get("rateLimits")
        if not isinstance(limits, dict):
            limits = {}
        primary = limits.get("primary")
        secondary = limits.get("secondary")
        primary_remaining = _remaining_percent(primary)
        secondary_remaining = _remaining_percent(secondary)
        reset_lines = [
            line
            for line in (_reset_line(primary, "Primary"), _reset_line(secondary, "Secondary"))
            if line
        ]
        summary_lines = []
        if primary_remaining >= 0:
            summary_lines.append(f"{_window_name(primary, 'Primary')} remaining")
        if secondary_remaining >= 0:
            summary_lines.append(
                f"{_window_name(secondary, 'Secondary')} {secondary_remaining}% remaining"
            )
        return {
            "codex": {
                "available": primary_remaining >= 0,
                "remaining_percent": primary_remaining,
                "weekly_remaining_percent": secondary_remaining,
                "reset_at": _utc_iso(primary.get("resetsAt")) if isinstance(primary, dict) else "",
                "weekly_reset_at": (
                    _utc_iso(secondary.get("resetsAt")) if isinstance(secondary, dict) else ""
                ),
                "reset_text": "\n".join(reset_lines),
                "summary": "\n".join(summary_lines),
                "source": "Codex App Server",
                "plan_type": limits.get("planType"),
            },
            "recent_projects": self._projects(thread_result),
        }

    def _refresh_once(self) -> None:
        try:
            snapshot = self._refresh()
        except Exception as error:
            if not self._stop_event.is_set():
                print(f"Codex state refresh failed: {error}")
            snapshot = self._empty_snapshot(str(error))
        with self._cache_lock:
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
        # USB/HTTP readers never wait for App Server I/O or its request lock.
        with self._cache_lock:
            if self._cached_at == 0 or time.monotonic() - self._cached_at >= self.refresh_seconds:
                return self._empty_snapshot("Codex refresh pending")
            return copy.deepcopy(self._cached)

    def close(self) -> None:
        self._stop_event.set()
        with self._request_lock:
            self._stop()
        if self._refresh_thread is not None:
            self._refresh_thread.join(timeout=2)
