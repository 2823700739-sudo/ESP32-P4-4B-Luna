"""Bounded action history for Luna BLE; never replay an uncertain command."""

from __future__ import annotations

from collections import OrderedDict
from copy import deepcopy
import threading
import time
from typing import Any, Callable


SUPPORTED_ACTIONS = frozenset({
    "music.previous", "music.play", "music.pause", "music.next",
    "music.volume_set", "music.mute",
})


class ActionHistory:
    def __init__(self, capacity: int = 128) -> None:
        if capacity < 1:
            raise ValueError("action history capacity must be positive")
        self.capacity = capacity
        self._lock = threading.Lock()
        # None results represent commands currently executing, and cannot be evicted.
        self._entries: OrderedDict[str, tuple[tuple[str, Any], dict | None]] = OrderedDict()
        self._counts = dict.fromkeys((
            "received", "executed", "succeeded", "failed", "unknown", "duplicates",
            "in_progress", "conflicts", "rejected", "busy",
        ), 0)
        self._last_action: str | None = None
        self._last_result: str | None = None
        self._last_execution_ms: float | None = None
        self._max_execution_ms: float | None = None

    def diagnostics(self) -> dict[str, Any]:
        with self._lock:
            return {
                **self._counts, "capacity": self.capacity, "cached": len(self._entries),
                "pending": sum(result is None for _, result in self._entries.values()),
                "last_action": self._last_action, "last_result": self._last_result,
                "last_execution_ms": self._last_execution_ms,
                "max_execution_ms": self._max_execution_ms,
            }

    def execute(self, request_id: str, action: str, value: Any,
                operation: Callable[[], dict[str, Any]]) -> dict[str, Any]:
        with self._lock:
            self._counts["received"] += 1
            error = None
            if not isinstance(request_id, str) or not request_id or len(request_id) > 80:
                error = "invalid_request_id"
            elif not isinstance(action, str) or action not in SUPPORTED_ACTIONS:
                error = "unsupported_action"
            elif action == "music.volume_set":
                if type(value) is not int or not 0 <= value <= 100:
                    error = "invalid_volume"
            elif value is not None:
                error = "invalid_action_value"
            if error:
                self._counts["rejected"] += 1
                return {"ok": False, "error": error}

            fingerprint = (action, value)
            if request_id in self._entries:
                previous, result = self._entries[request_id]
                if previous != fingerprint:
                    self._counts["conflicts"] += 1
                    return {"ok": False, "error": "request_id_conflict", "action": action}
                self._counts["duplicates"] += 1
                if result is None:
                    self._counts["in_progress"] += 1
                    return {"ok": False, "duplicate": True, "error": "action_in_progress",
                            "action": action}
                return {**deepcopy(result), "duplicate": True}

            if len(self._entries) >= self.capacity:
                completed = next((key for key, (_, result) in self._entries.items()
                                  if result is not None), None)
                if completed is None:
                    self._counts["busy"] += 1
                    return {"ok": False, "error": "action_history_busy", "action": action}
                del self._entries[completed]
            self._entries[request_id] = (fingerprint, None)
            self._counts["executed"] += 1

        started = time.perf_counter()
        try:
            result = operation()
        except Exception as error:
            # The Windows operation may have happened before the adapter raised.
            # Keep this terminal result, without a media-key fallback or retry.
            print(f"Luna action result uncertain: action={action} type={type(error).__name__}")
            result = {"ok": False, "error": "action_result_unknown", "result_known": False}
        result = {**result, "action": action, "duplicate": False}
        elapsed_ms = round((time.perf_counter() - started) * 1000, 2)
        outcome = ("succeeded" if result.get("ok") else
                   "unknown" if result.get("result_known") is False else "failed")
        with self._lock:
            self._entries[request_id] = (fingerprint, deepcopy(result))
            self._counts[outcome] += 1
            self._last_action, self._last_result = action, outcome
            self._last_execution_ms = elapsed_ms
            self._max_execution_ms = max(self._max_execution_ms or 0, elapsed_ms)
        return result
