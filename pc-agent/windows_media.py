"""Windows Global System Media Transport Controls adapter for Luna."""

from __future__ import annotations

import asyncio
from concurrent.futures import TimeoutError as FutureTimeoutError
import threading
from dataclasses import asdict, dataclass
from typing import Any

from winrt.windows.media.control import (
    GlobalSystemMediaTransportControlsSession as MediaSession,
    GlobalSystemMediaTransportControlsSessionManager as MediaSessionManager,
    GlobalSystemMediaTransportControlsSessionPlaybackStatus as PlaybackStatus,
)


PLAYBACK_RECONCILE_SECONDS = 2.5
PLAYBACK_RETRY_SECONDS = 0.4


@dataclass(frozen=True)
class MediaSnapshot:
    available: bool = False
    controllable: bool = False
    metadata_available: bool = False
    playing: bool = False
    status: str = "unavailable"
    source: str = ""
    title: str = "NetEase Cloud Music"
    artist: str = "No active media session"


class WindowsMediaAdapter:
    """Owns a WinRT event loop and serializes media commands."""

    def __init__(self, preferred_source: str = "cloudmusic.exe") -> None:
        self._preferred_source = preferred_source.casefold()
        self._snapshot = MediaSnapshot()
        self._snapshot_lock = threading.Lock()
        self._ready = threading.Event()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._manager: MediaSessionManager | None = None
        self._action_lock: asyncio.Lock | None = None
        self._startup_error: BaseException | None = None
        self._playback_generation = 0
        self._playback_reconcile_task: asyncio.Task[None] | None = None
        self._thread = threading.Thread(
            target=self._thread_main,
            name="luna-windows-media",
            daemon=True,
        )

    def start(self, timeout_seconds: float = 5.0) -> None:
        self._thread.start()
        if not self._ready.wait(timeout_seconds):
            raise TimeoutError("Timed out while starting the Windows media adapter")
        if self._startup_error is not None:
            raise RuntimeError("Unable to start the Windows media adapter") from self._startup_error

    def close(self) -> None:
        loop = self._loop
        if loop is not None and loop.is_running():
            loop.call_soon_threadsafe(loop.stop)

    def snapshot(self) -> dict[str, Any]:
        with self._snapshot_lock:
            return asdict(self._snapshot)

    def execute(self, action: str, timeout_seconds: float = 3.0) -> bool:
        loop = self._loop
        if loop is None or not loop.is_running():
            return False
        future = asyncio.run_coroutine_threadsafe(self._execute(action), loop)
        try:
            return bool(future.result(timeout=timeout_seconds))
        except (TimeoutError, FutureTimeoutError):
            # Prevent a command still queued behind the action lock from firing
            # long after its caller reported an uncertain result. An already
            # submitted Windows operation cannot be undone or safely replayed.
            future.cancel()
            raise

    def _thread_main(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self._loop = loop
        try:
            self._manager = loop.run_until_complete(MediaSessionManager.request_async())
            self._action_lock = asyncio.Lock()
            loop.create_task(self._poll_forever())
        except BaseException as error:
            self._startup_error = error
            self._ready.set()
            loop.close()
            return

        self._ready.set()
        try:
            loop.run_forever()
        finally:
            pending = asyncio.all_tasks(loop)
            for task in pending:
                task.cancel()
            if pending:
                loop.run_until_complete(asyncio.gather(*pending, return_exceptions=True))
            loop.close()

    def _choose_session(self) -> MediaSession | None:
        manager = self._manager
        if manager is None:
            return None
        sessions = list(manager.get_sessions())
        if self._preferred_source:
            for session in sessions:
                if self._preferred_source in session.source_app_user_model_id.casefold():
                    return session
        current = manager.get_current_session()
        return current if current is not None else (sessions[0] if sessions else None)

    async def _read_snapshot(self, session: MediaSession | None) -> MediaSnapshot:
        if session is None:
            return MediaSnapshot()

        playback = session.get_playback_info()
        status = playback.playback_status
        controls = playback.controls
        properties = await asyncio.wait_for(session.try_get_media_properties_async(), timeout=2)
        title = properties.title.strip() if properties.title else "NetEase Cloud Music"
        artist = properties.artist.strip() if properties.artist else session.source_app_user_model_id
        source = session.source_app_user_model_id
        return MediaSnapshot(
            available=True,
            controllable=(
                controls.is_play_enabled
                or controls.is_pause_enabled
                or controls.is_play_pause_toggle_enabled
            ),
            metadata_available=bool(properties.title or properties.artist),
            playing=status == PlaybackStatus.PLAYING,
            status=status.name.casefold(),
            source=source,
            title=title,
            artist=artist,
        )

    async def _refresh(self) -> MediaSnapshot:
        try:
            snapshot = await self._read_snapshot(self._choose_session())
        except Exception:
            snapshot = MediaSnapshot()
        with self._snapshot_lock:
            self._snapshot = snapshot
        return snapshot

    async def _poll_forever(self) -> None:
        while True:
            await self._refresh()
            await asyncio.sleep(0.5)

    async def _reconcile_playback(self, session: MediaSession, desired: bool,
                                  generation: int) -> None:
        """Keep the latest explicit command effective while the player catches up."""
        loop = asyncio.get_running_loop()
        deadline = loop.time() + PLAYBACK_RECONCILE_SECONDS
        last_sent = loop.time()
        retries = 0
        while loop.time() < deadline and retries < 3:
            await asyncio.sleep(0.1)
            if generation != self._playback_generation:
                return
            try:
                playing = session.get_playback_info().playback_status == PlaybackStatus.PLAYING
            except Exception:
                return
            if playing == desired or loop.time() - last_sent < PLAYBACK_RETRY_SECONDS:
                continue
            if self._action_lock is None:
                return
            async with self._action_lock:
                if generation != self._playback_generation:
                    return
                try:
                    accepted = (await session.try_play_async() if desired
                                else await session.try_pause_async())
                except Exception:
                    return
                last_sent = loop.time()
                retries += 1
                print(f"Luna media reconcile: {'play' if desired else 'pause'} "
                      f"retry={retries} accepted={bool(accepted)}")
                if accepted:
                    await self._refresh()

    async def _execute(self, action: str) -> bool:
        if self._action_lock is None:
            return False
        async with self._action_lock:
            session = self._choose_session()
            if session is None:
                return False

            self._playback_generation += 1
            generation = self._playback_generation
            if self._playback_reconcile_task is not None:
                self._playback_reconcile_task.cancel()
                self._playback_reconcile_task = None

            playback = session.get_playback_info()
            controls = playback.controls
            if action == "music.play":
                accepted = await session.try_play_async()
                if not accepted:
                    accepted = session.get_playback_info().playback_status == PlaybackStatus.PLAYING
            elif action == "music.pause":
                accepted = await session.try_pause_async()
                if not accepted:
                    accepted = session.get_playback_info().playback_status != PlaybackStatus.PLAYING
            elif action == "music.previous" and controls.is_previous_enabled:
                accepted = await session.try_skip_previous_async()
            elif action == "music.next" and controls.is_next_enabled:
                accepted = await session.try_skip_next_async()
            else:
                return False

            if accepted and action in {"music.play", "music.pause"}:
                self._playback_reconcile_task = asyncio.create_task(
                    self._reconcile_playback(session, action == "music.play", generation)
                )

            await asyncio.sleep(0.2)
            await self._refresh()
            return bool(accepted)
