from __future__ import annotations

import asyncio
from concurrent.futures import TimeoutError as FutureTimeoutError
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from unittest.mock import Mock


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
    def test_timed_out_queued_action_is_cancelled_not_left_to_run_later(self):
        adapter = WindowsMediaAdapter()
        adapter._loop = Mock()
        adapter._loop.is_running.return_value = True
        future = Mock()
        future.result.side_effect = FutureTimeoutError("queued")
        def submit(coro, loop):
            coro.close()
            return future
        with patch("windows_media.asyncio.run_coroutine_threadsafe", side_effect=submit):
            with self.assertRaises(FutureTimeoutError): adapter.execute("music.play")
        future.cancel.assert_called_once_with()

    def test_stalled_metadata_is_bounded_and_returns_unavailable(self):
        adapter = WindowsMediaAdapter()
        async def stalled():
            await asyncio.Event().wait()
        class Session:
            def get_playback_info(self):
                return SimpleNamespace(playback_status=PlaybackStatus.PAUSED, controls=object())
            def try_get_media_properties_async(self): return stalled()
        adapter._choose_session = lambda: Session()
        original_wait = asyncio.wait_for
        async def quick_timeout(awaitable, timeout):
            self.assertEqual(timeout, 2)
            return await original_wait(awaitable, .01)
        with patch("windows_media.asyncio.wait_for", side_effect=quick_timeout):
            self.assertFalse(asyncio.run(adapter._refresh()).available)

    def test_ble_mode_never_accesses_thumbnail_stream(self) -> None:
        class Properties:
            title, artist = "Song", "Artist"
            @property
            def thumbnail(self): raise AssertionError("cover stream was accessed")
        class Session:
            source_app_user_model_id = "player"
            def get_playback_info(self):
                return SimpleNamespace(playback_status=PlaybackStatus.PAUSED,
                    controls=SimpleNamespace(is_play_enabled=True, is_pause_enabled=True,
                                             is_play_pause_toggle_enabled=True))
            async def try_get_media_properties_async(self): return Properties()
        adapter = WindowsMediaAdapter()
        snapshot = asyncio.run(adapter._read_snapshot(Session()))
        self.assertEqual(snapshot.title, "Song")
        self.assertFalse(hasattr(snapshot,"cover_available"))
        self.assertFalse(hasattr(adapter,"cover"))

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
