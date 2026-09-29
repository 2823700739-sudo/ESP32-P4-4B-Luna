from __future__ import annotations

import asyncio
from pathlib import Path
import sys
import unittest
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from windows_media import (  # noqa: E402
    MediaSnapshot,
    PlaybackStatus,
    WindowsMediaAdapter,
)


class FakePlaybackInfo:
    def __init__(self, status: PlaybackStatus) -> None:
        self.playback_status = status
        self.controls = object()


class FakeSession:
    def __init__(self, status: PlaybackStatus) -> None:
        self.status = status
        self.play_calls = 0
        self.pause_calls = 0
        self.delay_first_play = False
        self.reject_pause_while_paused = False

    def get_playback_info(self) -> FakePlaybackInfo:
        return FakePlaybackInfo(self.status)

    async def try_play_async(self) -> bool:
        self.play_calls += 1
        if self.delay_first_play and self.play_calls == 1:
            asyncio.get_running_loop().call_later(
                0.33, setattr, self, "status", PlaybackStatus.PLAYING
            )
        else:
            self.status = PlaybackStatus.PLAYING
        return True

    async def try_pause_async(self) -> bool:
        self.pause_calls += 1
        if self.reject_pause_while_paused and self.status == PlaybackStatus.PAUSED:
            return False
        self.status = PlaybackStatus.PAUSED
        return True


async def run_actions(session: FakeSession, *actions: str,
                      settle_seconds: float = 0.0) -> None:
    adapter = WindowsMediaAdapter()
    adapter._action_lock = asyncio.Lock()
    adapter._choose_session = lambda: session  # type: ignore[method-assign]

    async def refresh() -> MediaSnapshot:
        return MediaSnapshot(playing=session.status == PlaybackStatus.PLAYING)

    adapter._refresh = refresh  # type: ignore[method-assign]
    for action in actions:
        if not await adapter._execute(action):
            raise AssertionError(f"action rejected: {action}")
    if settle_seconds:
        await asyncio.sleep(settle_seconds)


class WindowsMediaCommandTests(unittest.TestCase):
    def test_explicit_commands_are_sent_even_when_reported_state_matches(self) -> None:
        paused = FakeSession(PlaybackStatus.PAUSED)
        playing = FakeSession(PlaybackStatus.PLAYING)

        asyncio.run(run_actions(paused, "music.pause"))
        asyncio.run(run_actions(playing, "music.play"))

        self.assertEqual(paused.pause_calls, 1)
        self.assertEqual(playing.play_calls, 1)

    def test_rapid_play_then_pause_overrides_late_playback_update(self) -> None:
        session = FakeSession(PlaybackStatus.PAUSED)
        session.delay_first_play = True

        with patch("windows_media.PLAYBACK_RECONCILE_SECONDS", 0.9), patch(
            "windows_media.PLAYBACK_RETRY_SECONDS", 0.35
        ):
            asyncio.run(run_actions(session, "music.play", "music.pause",
                                    settle_seconds=0.7))

        self.assertEqual(session.status, PlaybackStatus.PAUSED)
        self.assertGreaterEqual(session.pause_calls, 2)

    def test_rapid_pause_still_wins_if_player_rejects_stale_noop(self) -> None:
        session = FakeSession(PlaybackStatus.PAUSED)
        session.delay_first_play = True
        session.reject_pause_while_paused = True

        with patch("windows_media.PLAYBACK_RECONCILE_SECONDS", 0.9), patch(
            "windows_media.PLAYBACK_RETRY_SECONDS", 0.35
        ):
            asyncio.run(run_actions(session, "music.play", "music.pause",
                                    settle_seconds=0.7))

        self.assertEqual(session.status, PlaybackStatus.PAUSED)
        self.assertGreaterEqual(session.pause_calls, 2)


if __name__ == "__main__":
    unittest.main()
