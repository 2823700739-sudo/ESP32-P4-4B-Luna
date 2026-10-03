from __future__ import annotations

from pathlib import Path
import io
import json
import queue
from types import SimpleNamespace
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
            return {"codex": {"available": True, "windows": [{"usedPercent": 25}]}}

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
                snapshot["codex"]["windows"][0]["usedPercent"] = 100
                self.assertEqual(adapter.snapshot()["codex"]["windows"][0]["usedPercent"], 25)
            finally:
                release.set()
                adapter.close()

    def test_expired_or_failed_refresh_never_returns_old_quota_as_available(self) -> None:
        adapter = CodexAdapter("unused-test-codex.exe")
        good = {"codex": {"available": True, "windows": [{"usedPercent": 25}]}}
        with patch.object(adapter, "_refresh", return_value=good):
            adapter._refresh_once()
        with patch("codex_adapter.time.monotonic", return_value=adapter._cached_at + 31):
            self.assertFalse(adapter.snapshot()["codex"]["available"])
        with patch.object(adapter, "_refresh", side_effect=TimeoutError("App Server is slow")):
            adapter._refresh_once()
        failed = adapter.snapshot()["codex"]
        self.assertFalse(failed["available"])
        self.assertEqual(failed["windows"], [])

    def test_refresh_is_quota_only_with_no_thread_list_or_legacy_fields(self):
        adapter = CodexAdapter("unused-test-codex.exe")
        windows = {"primary": {"usedPercent": 25, "windowDurationMins": 300},
                   "secondary": {"usedPercent": 40, "windowDurationMins": 10080}}
        with patch.object(adapter, "_request", return_value={"rateLimitsByLimitId": {"codex": windows}}) as request:
            snapshot = adapter._refresh()
        request.assert_called_once_with("account/rateLimits/read")
        self.assertEqual(set(snapshot), {"codex"})
        self.assertEqual(snapshot["codex"]["windows"], [windows["primary"], windows["secondary"]])
        self.assertEqual(set(snapshot["codex"]), {"available", "windows"})

    def test_reader_ignores_notifications_and_malformed_or_nonobject_json(self):
        adapter = CodexAdapter("unused-test-codex.exe")
        raw = b'[]\ninvalid\n' + b'{"method":"notice","params":{}}\n'*10000
        raw += json.dumps({"id": 7, "result": {"ok": True}}).encode() + b'\n'
        responses = queue.Queue(maxsize=32)
        adapter._reader(SimpleNamespace(stdout=io.BytesIO(raw)), responses)
        self.assertEqual(responses.qsize(), 2)
        self.assertEqual(responses.get_nowait()["id"], 7)
        self.assertIsNone(responses.get_nowait())

    def test_late_response_flood_is_bounded_and_eof_still_delivered(self):
        adapter = CodexAdapter("unused-test-codex.exe")
        raw = b''.join(json.dumps({"id": n, "result": {}}).encode()+b'\n' for n in range(1000))
        adapter._reader(SimpleNamespace(stdout=io.BytesIO(raw)), adapter._responses)
        self.assertEqual(adapter._responses.qsize(), 32)
        replies = [adapter._responses.get_nowait() for _ in range(32)]
        self.assertEqual(replies[-2]["id"], 999)
        self.assertIsNone(replies[-1])
