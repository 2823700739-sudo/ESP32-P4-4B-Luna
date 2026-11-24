from __future__ import annotations

from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from luna_actions import ActionHistory  # noqa: E402


class ActionHistoryTests(unittest.TestCase):
    def test_completed_history_is_bounded_and_oldest_is_evicted(self) -> None:
        history = ActionHistory(capacity=2)
        calls = []

        def operation() -> dict:
            calls.append(True)
            return {"ok": True}

        for request in ("one", "two", "three", "two"):
            history.execute(request, "music.play", None, operation)
        self.assertEqual(len(calls), 3)
        self.assertEqual(history.diagnostics()["cached"], 2)
        history.execute("one", "music.play", None, operation)
        self.assertEqual(len(calls), 4)

    def test_full_pending_history_is_not_evicted_or_reexecuted(self) -> None:
        history = ActionHistory(capacity=1)
        entered, release = threading.Event(), threading.Event()

        def operation() -> dict:
            entered.set()
            release.wait(timeout=2)
            return {"ok": True}

        thread = threading.Thread(target=lambda: history.execute("one", "music.play", None,
                                                                 operation))
        thread.start()
        try:
            self.assertTrue(entered.wait(timeout=1))
            self.assertEqual(history.execute("two", "music.pause", None, operation)["error"],
                             "action_history_busy")
            self.assertEqual(history.diagnostics()["pending"], 1)
        finally:
            release.set()
            thread.join(timeout=2)
        self.assertFalse(thread.is_alive())
        self.assertTrue(history.execute("one", "music.play", None, operation)["duplicate"])

    def test_retired_actions_are_not_executable(self) -> None:
        history = ActionHistory()
        for action in ("music.play_pause", "music.volume_up", "music.volume_down"):
            self.assertEqual(history.execute(action, action, None, lambda: self.fail("retired command executed"))["error"], "unsupported_action")

    def test_validation_and_history_stats_are_copied(self) -> None:
        history = ActionHistory()
        self.assertEqual(history.execute(None, "music.play", None, lambda: {})["error"],
                         "invalid_request_id")
        self.assertEqual(history.execute("a", "music.play", True, lambda: {})["error"],
                         "invalid_action_value")
        snapshot = history.diagnostics()
        snapshot["rejected"] = 0
        self.assertEqual(history.diagnostics()["rejected"], 2)

    def test_execution_time_counts_only_new_execution(self) -> None:
        history = ActionHistory()
        with patch("luna_actions.time.perf_counter", side_effect=[10.0, 10.125, 20.0, 20.05]):
            history.execute("a", "music.play", None, lambda: {"ok": True})
            history.execute("a", "music.play", None, lambda: {"ok": False})
            history.execute("b", "music.pause", None, lambda: {"ok": False})
        stats = history.diagnostics()
        self.assertEqual(stats["last_execution_ms"], 50.0)
        self.assertEqual(stats["max_execution_ms"], 125.0)
        self.assertEqual(stats["succeeded"], 1)
        self.assertEqual(stats["failed"], 1)
        self.assertEqual(stats["executed"], 2)
