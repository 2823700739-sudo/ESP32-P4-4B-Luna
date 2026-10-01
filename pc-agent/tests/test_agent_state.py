from __future__ import annotations

import json
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from luna_agent import AgentState  # noqa: E402
from luna_link_protocol import MAX_PAYLOAD  # noqa: E402


class FakeMedia:
    def snapshot(self) -> dict:
        return {
            "controllable": True,
            "metadata_available": True,
            "playing": False,
            "cover_available": False,
            "title": "Test track",
            "artist": "Test artist",
            "cover_id": "",
            "cover_content_type": "",
        }

    def execute(self, action: str) -> bool:
        return True


class FakeCodex:
    def snapshot(self) -> dict:
        return {
            "codex": {
                "available": True,
                "remaining_percent": 75,
                "weekly_remaining_percent": 60,
                "reset_at": "",
                "weekly_reset_at": "",
                "summary": "75% remaining",
                "reset_text": "",
            },
            "recent_projects": [{"name": "Luna", "path": "D:\\Luna"}],
        }


class FakeWeather:
    def snapshot(self) -> dict:
        return {
            "configured": True,
            "available": True,
            "stale": False,
            "latitude": 22.551,
            "longitude": 114.111,
            "temperature_c": 23,
            "location": "Test",
            "condition": "Clear",
            "observed_at": "",
            "summary": "Clear 23 C",
            "details": "",
        }


class FakeVolume:
    def __init__(self) -> None:
        self.set_calls = 0

    def snapshot(self) -> dict:
        return {"available": True, "muted": False, "percent": 40}

    def set_percent(self, value: int) -> dict:
        self.set_calls += 1
        return {"available": True, "muted": False, "percent": value}

    def toggle_mute(self) -> dict:
        return {"available": True, "muted": True, "percent": 40}


def make_agent() -> tuple[AgentState, FakeVolume]:
    volume = FakeVolume()
    agent = AgentState(
        {"recent_projects": []},
        FakeMedia(),  # type: ignore[arg-type]
        FakeCodex(),  # type: ignore[arg-type]
        FakeWeather(),  # type: ignore[arg-type]
        volume,  # type: ignore[arg-type]
    )
    return agent, volume


class AgentStateTests(unittest.TestCase):
    def test_duplicate_action_is_not_executed_twice(self) -> None:
        agent, volume = make_agent()

        first = agent.execute("p4-123", "music.volume_set", 55)
        duplicate = agent.execute("p4-123", "music.volume_set", 55)

        self.assertTrue(first["ok"])
        self.assertFalse(first["duplicate"])
        self.assertTrue(duplicate["duplicate"])
        self.assertEqual(duplicate["volume"]["percent"], 55)
        self.assertEqual(volume.set_calls, 1)

    def test_failed_action_duplicate_is_not_reported_as_success(self) -> None:
        agent, _volume = make_agent()
        with patch.object(agent.media, "execute", return_value=False) as execute:
            first = agent.execute("failed-play", "music.play")
            repeat = agent.execute("failed-play", "music.play")
        self.assertFalse(first["ok"])
        self.assertFalse(repeat["ok"])
        self.assertTrue(repeat["duplicate"])
        self.assertEqual(repeat["error"], "media_session_unavailable")
        execute.assert_called_once()
        self.assertEqual(agent.action_diagnostics()["failed"], 1)

    def test_conflicting_action_or_value_does_not_execute(self) -> None:
        agent, volume = make_agent()
        agent.execute("same-id", "music.volume_set", 55)
        for action, value in (("music.volume_set", 60), ("music.mute", None)):
            result = agent.execute("same-id", action, value)
            self.assertEqual(result["error"], "request_id_conflict")
        self.assertEqual(volume.set_calls, 1)
        self.assertEqual(agent.action_diagnostics()["conflicts"], 2)

    def test_uncertain_media_action_does_not_fall_back_or_retry(self) -> None:
        agent, _volume = make_agent()
        with patch.object(agent.media, "execute", side_effect=TimeoutError("late response")) as execute, \
             patch("luna_agent.send_media_key") as key:
            first = agent.execute("uncertain-next", "music.next")
            repeat = agent.execute("uncertain-next", "music.next")
        self.assertFalse(first["ok"])
        self.assertFalse(first["result_known"])
        self.assertEqual(repeat["error"], "action_result_unknown")
        self.assertTrue(repeat["duplicate"])
        execute.assert_called_once()
        key.assert_not_called()
        self.assertEqual(agent.action_diagnostics()["unknown"], 1)

    def test_duplicate_in_progress_never_waits_or_claims_success(self) -> None:
        agent, _volume = make_agent()
        entered, release = threading.Event(), threading.Event()
        results = []

        def slow_action(_action: str) -> bool:
            entered.set()
            if not release.wait(timeout=2):
                raise TimeoutError("test release missing")
            return True

        with patch.object(agent.media, "execute", side_effect=slow_action) as execute:
            worker = threading.Thread(target=lambda: results.append(
                agent.execute("pending", "music.play")))
            worker.start()
            try:
                self.assertTrue(entered.wait(timeout=1))
                pending = agent.execute("pending", "music.play")
                self.assertFalse(pending["ok"])
                self.assertEqual(pending["error"], "action_in_progress")
                self.assertEqual(agent.action_diagnostics()["pending"], 1)
            finally:
                release.set()
                worker.join(timeout=2)
            self.assertFalse(worker.is_alive())
            repeat = agent.execute("pending", "music.play")
            self.assertTrue(repeat["ok"])
            execute.assert_called_once()
        self.assertEqual(agent.action_diagnostics()["in_progress"], 1)

    def test_action_cache_is_isolated_from_returned_snapshots(self) -> None:
        agent, _volume = make_agent()
        first = agent.execute("copy", "music.volume_set", 55)
        first["volume"]["percent"] = 0
        repeat = agent.execute("copy", "music.volume_set", 55)
        self.assertEqual(repeat["volume"]["percent"], 55)
        repeat["volume"]["percent"] = 1
        self.assertEqual(agent.execute("copy", "music.volume_set", 55)["volume"]["percent"], 55)

    def test_explicit_session_rejection_keeps_existing_media_key_fallback(self) -> None:
        agent, _volume = make_agent()
        with patch.object(agent.media, "execute", return_value=False), \
             patch("luna_agent.send_media_key") as key:
            first = agent.execute("fallback", "music.next")
            repeat = agent.execute("fallback", "music.next")
        self.assertTrue(first["ok"])
        self.assertTrue(repeat["duplicate"])
        key.assert_called_once()

    def test_invalid_volume_and_action_never_run_or_claim_success(self) -> None:
        agent, volume = make_agent()
        for value in (True, -1, 101, 55.0, "55"):
            self.assertEqual(agent.execute("bad-volume", "music.volume_set", value)["error"],
                             "invalid_volume")
        for _ in range(2):
            self.assertFalse(agent.execute("bad-action", "shell.run")["ok"])
        self.assertEqual(volume.set_calls, 0)
        self.assertEqual(agent.action_diagnostics()["executed"], 0)

    def test_uncertain_volume_write_is_cached_without_retry(self) -> None:
        agent, volume = make_agent()
        with patch.object(volume, "set_percent", side_effect=OSError("readback failed")) as write:
            first = agent.execute("unknown-volume", "music.volume_set", 55)
            repeat = agent.execute("unknown-volume", "music.volume_set", 55)
        self.assertFalse(first["result_known"])
        self.assertEqual(repeat["error"], "action_result_unknown")
        write.assert_called_once()

    def test_state_snapshot_fits_one_luna_link_frame(self) -> None:
        agent, _volume = make_agent()
        encoded = json.dumps(
            agent.snapshot(), ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")
        self.assertLessEqual(len(encoded), MAX_PAYLOAD)

    def test_state_provides_location_for_device_weather(self) -> None:
        agent, _volume = make_agent()
        weather = agent.snapshot()["weather"]

        self.assertTrue(weather["configured"])
        self.assertEqual(weather["latitude"], 22.551)
        self.assertEqual(weather["longitude"], 114.111)


if __name__ == "__main__":
    unittest.main()
