from __future__ import annotations

import json
from pathlib import Path
import sys
import unittest


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
        self.assertEqual(volume.set_calls, 1)

    def test_state_snapshot_fits_one_luna_link_frame(self) -> None:
        agent, _volume = make_agent()
        encoded = json.dumps(
            agent.snapshot(), ensure_ascii=False, separators=(",", ":")
        ).encode("utf-8")
        self.assertLessEqual(len(encoded), MAX_PAYLOAD)


if __name__ == "__main__":
    unittest.main()

