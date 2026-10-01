from __future__ import annotations

from pathlib import Path
import sys
import threading
import time
import unittest
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from codex_adapter import CodexAdapter  # noqa: E402


class CodexAdapterTests(unittest.TestCase):
    def test_slow_refresh_does_not_block_state_reads(self) -> None:
        adapter = CodexAdapter("unused-test-codex.exe")
        entered = threading.Event()
        release = threading.Event()

        def slow_refresh() -> dict:
            entered.set()
            if not release.wait(timeout=3):
                raise TimeoutError("test refresh was not released")
            return {"codex": {"available": True, "remaining_percent": 75}, "recent_projects": []}

        with patch.object(adapter, "_refresh", side_effect=slow_refresh):
            try:
                adapter.start()
                self.assertTrue(entered.wait(timeout=1))
                started = time.perf_counter()
                pending = adapter.snapshot()
                self.assertLess(time.perf_counter() - started, 0.1)
                self.assertFalse(pending["codex"]["available"])
                release.set()
                deadline = time.monotonic() + 1
                while not adapter.snapshot()["codex"]["available"] and time.monotonic() < deadline:
                    time.sleep(0.01)
                snapshot = adapter.snapshot()
                self.assertTrue(snapshot["codex"]["available"])
                snapshot["codex"]["remaining_percent"] = 0
                self.assertEqual(adapter.snapshot()["codex"]["remaining_percent"], 75)
            finally:
                release.set()
                adapter.close()

    def test_expired_or_failed_refresh_never_returns_old_quota_as_available(self) -> None:
        adapter = CodexAdapter("unused-test-codex.exe")
        good = {"codex": {"available": True, "remaining_percent": 75}, "recent_projects": []}
        with patch.object(adapter, "_refresh", return_value=good):
            adapter._refresh_once()
        with patch("codex_adapter.time.monotonic", return_value=adapter._cached_at + 16):
            self.assertFalse(adapter.snapshot()["codex"]["available"])
        with patch.object(adapter, "_refresh", side_effect=TimeoutError("App Server is slow")):
            adapter._refresh_once()
        failed = adapter.snapshot()["codex"]
        self.assertFalse(failed["available"])
        self.assertEqual(failed["remaining_percent"], -1)
