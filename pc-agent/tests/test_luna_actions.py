from __future__ import annotations

import json
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from luna_actions import ActionHistory, parse_action_request  # noqa: E402


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

    def test_malformed_action_objects_are_rejected_without_coercion(self) -> None:
        bad = [None, [], 42, {"request_id": None, "action": "music.play"},
               {"request_id": 42, "action": "music.play"},
               {"request_id": "a", "action": []},
               {"request_id": "a" * 81, "action": "music.play"},
               {"request_id": "a", "action": "b" * 65}]
        for body in bad:
            with self.subTest(body=body), self.assertRaises(ValueError):
                parse_action_request(json.dumps(body).encode())
        self.assertEqual(parse_action_request(
            b'{"request_id":"a","action":"music.volume_set","value":50}'),
            ("a", "music.volume_set", 50))

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

    def test_oversized_or_deeply_nested_payload_is_rejected(self) -> None:
        for payload in (b" " * 4097, b"[" * 1500 + b"0" + b"]" * 1500):
            with self.subTest(size=len(payload)), self.assertRaises(ValueError):
                parse_action_request(payload)
