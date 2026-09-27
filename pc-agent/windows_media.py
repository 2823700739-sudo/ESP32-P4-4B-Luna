"""Windows Global System Media Transport Controls adapter for Luna."""

from __future__ import annotations

import asyncio
import hashlib
import threading
from dataclasses import asdict, dataclass
from typing import Any

from winrt.windows.media.control import (
    GlobalSystemMediaTransportControlsSession as MediaSession,
    GlobalSystemMediaTransportControlsSessionManager as MediaSessionManager,
    GlobalSystemMediaTransportControlsSessionPlaybackStatus as PlaybackStatus,
)
from winrt.windows.storage.streams import DataReader


MAX_COVER_BYTES = 256 * 1024


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
    cover_available: bool = False
    cover_id: str = ""
    cover_content_type: str = ""


class WindowsMediaAdapter:
    """Owns a WinRT event loop and serializes media commands."""

    def __init__(self, preferred_source: str = "cloudmusic.exe") -> None:
        self._preferred_source = preferred_source.casefold()
        self._snapshot = MediaSnapshot()
        self._snapshot_lock = threading.Lock()
        self._media_key = ""
        self._cover_data = b""
        self._cover_content_type = ""
        self._cover_id = ""
        self._ready = threading.Event()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._manager: MediaSessionManager | None = None
        self._action_lock: asyncio.Lock | None = None
        self._startup_error: BaseException | None = None
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

    def cover(self) -> tuple[bytes, str, str]:
        with self._snapshot_lock:
            return self._cover_data, self._cover_content_type, self._cover_id

    def execute(self, action: str, timeout_seconds: float = 3.0) -> bool:
        loop = self._loop
        if loop is None or not loop.is_running():
            return False
        future = asyncio.run_coroutine_threadsafe(self._execute(action), loop)
        return bool(future.result(timeout=timeout_seconds))

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
            with self._snapshot_lock:
                self._media_key = ""
                self._cover_data = b""
                self._cover_content_type = ""
                self._cover_id = ""
            return MediaSnapshot()

        playback = session.get_playback_info()
        status = playback.playback_status
        controls = playback.controls
        properties = await session.try_get_media_properties_async()
        title = properties.title.strip() if properties.title else "NetEase Cloud Music"
        artist = properties.artist.strip() if properties.artist else session.source_app_user_model_id
        source = session.source_app_user_model_id
        media_key = "\0".join((source, title, artist))
        with self._snapshot_lock:
            cover_data = self._cover_data
            cover_content_type = self._cover_content_type
            cover_id = self._cover_id
            previous_media_key = self._media_key

        if media_key != previous_media_key:
            cover_data, cover_content_type = await self._read_cover(properties.thumbnail)
            cover_id = hashlib.sha256(cover_data).hexdigest()[:16] if cover_data else ""
            with self._snapshot_lock:
                self._media_key = media_key
                self._cover_data = cover_data
                self._cover_content_type = cover_content_type
                self._cover_id = cover_id

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
            cover_available=bool(cover_data),
            cover_id=cover_id,
            cover_content_type=cover_content_type,
        )

    async def _read_cover(self, thumbnail: Any) -> tuple[bytes, str]:
        if thumbnail is None:
            return b"", ""
        stream = await thumbnail.open_read_async()
        try:
            size = int(stream.size)
            if size <= 0 or size > MAX_COVER_BYTES:
                return b"", ""
            reader = DataReader(stream.get_input_stream_at(0))
            try:
                loaded = await reader.load_async(size)
                data = bytearray(loaded)
                reader.read_bytes(data)
            finally:
                reader.close()
            content_type = str(stream.content_type).split(",", 1)[0].strip().casefold()
            if not data.startswith(b"\xff\xd8"):
                return b"", ""
            return bytes(data), content_type or "image/jpeg"
        finally:
            stream.close()

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

    async def _execute(self, action: str) -> bool:
        if self._action_lock is None:
            return False
        async with self._action_lock:
            session = self._choose_session()
            if session is None:
                return False

            playback = session.get_playback_info()
            controls = playback.controls
            if action == "music.play":
                if playback.playback_status == PlaybackStatus.PLAYING:
                    accepted = True
                elif controls.is_play_enabled:
                    accepted = await session.try_play_async()
                else:
                    return False
            elif action == "music.pause":
                if playback.playback_status != PlaybackStatus.PLAYING:
                    accepted = True
                elif controls.is_pause_enabled:
                    accepted = await session.try_pause_async()
                else:
                    return False
            elif action == "music.play_pause":
                if playback.playback_status == PlaybackStatus.PLAYING:
                    accepted = await session.try_pause_async()
                else:
                    accepted = await session.try_play_async()
            elif action == "music.previous" and controls.is_previous_enabled:
                accepted = await session.try_skip_previous_async()
            elif action == "music.next" and controls.is_next_enabled:
                accepted = await session.try_skip_next_async()
            else:
                return False

            await asyncio.sleep(0.2)
            await self._refresh()
            return bool(accepted)
