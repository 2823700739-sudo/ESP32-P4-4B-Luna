"""B1 music-only BLE business. No serial/HTTP/cover/media-key fallback."""
from __future__ import annotations

import asyncio
import logging
import re

from luna_actions import ActionHistory
from luna_ble_probe import ProbeSession
from luna_ble_protocol import ProtocolError


MUSIC_ACTIONS = frozenset({"music.play", "music.pause", "music.previous", "music.next", "music.volume_set", "music.mute"})
LOG = logging.getLogger("luna.ble.link")


def bounded_text(value, limit: int) -> str:
    if not isinstance(value, str): return ""
    value = " ".join(value.split())
    return value.encode("utf-8")[:limit].decode("utf-8", "ignore")


def validate_action(action, boot_id: str) -> dict | None:
    if action is None: return None
    if not isinstance(action, dict) or set(action) != {"request_id", "action", "value"}:
        raise ProtocolError("invalid music action")
    rid, name, value = action["request_id"], action["action"], action["value"]
    if not isinstance(rid, str) or re.fullmatch(re.escape(boot_id) + r"-[0-9a-f]{8}", rid) is None:
        raise ProtocolError("action belongs to another boot")
    if not isinstance(name, str) or name not in MUSIC_ACTIONS:
        raise ProtocolError("unsupported music action")
    if name == "music.volume_set":
        if type(value) is not int or not 0 <= value <= 100: raise ProtocolError("invalid volume action")
    elif value is not None: raise ProtocolError("invalid music action value")
    return dict(action)


class MusicSession(ProbeSession):
    REQUEST_TYPES = {**ProbeSession.REQUEST_TYPES, "state_snapshot": "state_snapshot_result", "action_result": "action_result_ack",
                     "dashboard_snapshot": "dashboard_snapshot_result"}

    def validate_reply(self, kind, reply):
        if kind == "hello":
            features, boot = reply.get("features"), reply.get("boot_id")
            if not isinstance(features, list) or not all(isinstance(f, str) for f in features) or "music" not in features:
                raise ProtocolError("B1 music firmware required; diagnostic B0 has no music business")
            if not isinstance(boot, str) or re.fullmatch(r"[0-9a-f]{16}", boot) is None:
                raise ProtocolError("invalid B1 boot ID")
        if kind in ("state_snapshot", "action_result", "dashboard_snapshot"):
            if reply.get("boot_id") != self.boot_id: raise ProtocolError("device restarted; drop old business")
            if kind == "state_snapshot":
                if reply.get("accepted") is not True or "action_request" not in reply:
                    raise ProtocolError("music snapshot rejected")
                validate_action(reply["action_request"], self.boot_id)
            elif reply.get("accepted") is not True:
                raise ProtocolError("action result not acknowledged")


class MusicService:
    def __init__(self, media, volume):
        self.media, self.volume = media, volume
        self.history = ActionHistory()
        self.volume_state = {"available": False, "percent": 0, "muted": False}

    async def snapshot(self):
        # CoreAudio is synchronous COM. Keep it off the BLE event loop.
        self.volume_state = await asyncio.to_thread(self.volume.snapshot)
        state = self.media.snapshot()
        return {
            "music": {k: state.get(k) is True for k in ("available", "controllable", "playing")} | {
                "title": bounded_text(state.get("title"), 255),
                "artist": bounded_text(state.get("artist"), 127),
            },
            "volume": {"available": self.volume_state.get("available") is True,
                       "percent": self.volume_state.get("percent", 0),
                       "muted": self.volume_state.get("muted") is True},
        }

    async def execute(self, action):
        def operation():
            name, value = action["action"], action["value"]
            if name == "music.volume_set":
                result = self.volume.set_percent(value)
                return {"ok": result.get("available") is True}
            if name == "music.mute":
                result = self.volume.toggle_mute()
                return {"ok": result.get("available") is True}
            return {"ok": self.media.execute(name)}
        result = await asyncio.to_thread(self.history.execute, action["request_id"], action["action"], action["value"], operation)
        LOG.info("ACTION: %s; accepted=%s; result_known=%s; duplicate=%s", action["action"],
                 result.get("ok") is True, result.get("result_known", True) is True, result.get("duplicate") is True)
        return {"request_id": action["request_id"], "ok": result.get("ok") is True,
                "result_known": result.get("result_known", True) is True}

    async def step(self, session):
        reply = await session.request("state_snapshot", **await self.snapshot())
        action = validate_action(reply["action_request"], session.boot_id)
        if action is not None:
            if session.failed or not getattr(session.client, "is_connected", True):
                raise ConnectionError("disconnected before action dispatch; discard click")
            result = await self.execute(action)
            # Never retry a failed/uncertain operation or acknowledgement.
            await session.request("action_result", **result)
        return action is not None
