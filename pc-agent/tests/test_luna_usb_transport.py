from __future__ import annotations

import json
import hashlib
from pathlib import Path
import struct
import sys
import time
import unittest
from unittest.mock import patch


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from luna_link_protocol import Frame, FrameDecoder, MessageType  # noqa: E402
from luna_usb_transport import LunaUsbTransport  # noqa: E402


class FakeAgent:
    def __init__(self) -> None:
        self.actions: list[tuple[str, str, object]] = []
        self.media = FakeMedia()

    def snapshot(self) -> dict:
        return {"protocol_version": 1, "sequence": 3, "pc": {"online": True}}

    def execute(self, request_id: str, action: str, value: object = None) -> dict:
        self.actions.append((request_id, action, value))
        return {"ok": True, "action": action, "duplicate": False}


class FakeMedia:
    def __init__(self) -> None:
        self.data = b"\xff\xd8" + bytes(range(256)) * 12 + b"\xff\xd9"
        self.cover_id = hashlib.sha256(self.data).hexdigest()[:16]

    def cover(self) -> tuple[bytes, str, str]:
        return self.data, "image/jpeg", self.cover_id


class FakePort:
    def __init__(self) -> None:
        self.port = "COM-TEST"
        self.closed = False
        self.writes: list[bytes] = []

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)

    def flush(self) -> None:
        pass

    def close(self) -> None:
        self.closed = True


def decode_write(port: FakePort) -> Frame:
    frames = FrameDecoder().feed(port.writes[-1])
    if len(frames) != 1:
        raise AssertionError("expected exactly one response frame")
    return frames[0]


class LunaUsbTransportTests(unittest.TestCase):
    def test_default_idle_timeout_exceeds_maximum_panel_poll_interval(self) -> None:
        self.assertGreater(LunaUsbTransport(FakeAgent()).idle_timeout_seconds, 60)

    def test_state_request_returns_snapshot(self) -> None:
        agent = FakeAgent()
        port = FakePort()
        transport = LunaUsbTransport(agent)

        transport._handle_frame(port, Frame(MessageType.STATE_REQUEST, 17, b""))

        response = decode_write(port)
        self.assertEqual(response.message_type, MessageType.STATE_SNAPSHOT)
        self.assertEqual(response.request_id, 17)
        self.assertEqual(json.loads(response.payload)["sequence"], 3)
        self.assertEqual(transport.diagnostics()["state_requests"], 1)
        self.assertIsNotNone(transport.diagnostics()["last_snapshot_at"])

    def test_action_request_executes_allowlisted_agent_path(self) -> None:
        agent = FakeAgent()
        port = FakePort()
        transport = LunaUsbTransport(agent)
        payload = json.dumps(
            {"request_id": "p4-99", "action": "music.volume_set", "value": 42}
        ).encode("utf-8")

        transport._handle_frame(port, Frame(MessageType.ACTION_REQUEST, 23, payload))

        response = decode_write(port)
        self.assertEqual(response.message_type, MessageType.ACTION_RESULT)
        self.assertEqual(response.request_id, 23)
        self.assertTrue(json.loads(response.payload)["ok"])
        self.assertEqual(agent.actions, [("p4-99", "music.volume_set", 42)])
        self.assertEqual(transport.diagnostics()["action_requests"], 1)

    def test_invalid_action_json_returns_error(self) -> None:
        port = FakePort()
        transport = LunaUsbTransport(FakeAgent())

        transport._handle_frame(port, Frame(MessageType.ACTION_REQUEST, 5, b"{"))

        response = decode_write(port)
        self.assertFalse(json.loads(response.payload)["ok"])

    def test_cover_info_and_chunks_match_digest(self) -> None:
        agent = FakeAgent()
        port = FakePort()
        transport = LunaUsbTransport(agent)
        cover_id = agent.media.cover_id.encode("ascii")

        transport._handle_frame(port, Frame(MessageType.COVER_INFO_REQUEST, 1, cover_id))
        info = decode_write(port)
        self.assertEqual(info.message_type, MessageType.COVER_INFO)
        self.assertEqual(struct.unpack_from("<I", info.payload)[0], len(agent.media.data))
        self.assertEqual(info.payload[4:], hashlib.sha256(agent.media.data).digest())

        received = bytearray()
        for offset in range(0, len(agent.media.data), 2048):
            request = cover_id + struct.pack("<IH", offset, 2048)
            transport._handle_frame(port, Frame(MessageType.COVER_CHUNK_REQUEST, offset + 2,
                                                request))
            chunk = decode_write(port)
            self.assertEqual(chunk.message_type, MessageType.COVER_CHUNK)
            self.assertEqual(struct.unpack_from("<I", chunk.payload)[0], offset)
            received.extend(chunk.payload[4:])
        self.assertEqual(bytes(received), agent.media.data)
        self.assertEqual(transport.diagnostics()["cover_info_requests"], 1)
        self.assertEqual(transport.diagnostics()["cover_chunk_requests"], 2)

    def test_stale_cover_id_never_returns_new_cover(self) -> None:
        port = FakePort()
        transport = LunaUsbTransport(FakeAgent())
        stale_id = b"0000000000000000"

        transport._handle_frame(port, Frame(MessageType.COVER_INFO_REQUEST, 1, stale_id))
        self.assertEqual(decode_write(port).payload, bytes(36))

        request = stale_id + struct.pack("<IH", 0, 2048)
        transport._handle_frame(port, Frame(MessageType.COVER_CHUNK_REQUEST, 2, request))
        self.assertEqual(decode_write(port).payload, b"")

    def test_unexpected_failure_is_counted_and_retried(self) -> None:
        transport = LunaUsbTransport(FakeAgent(), retry_seconds=0.01)
        calls = 0

        def fail_then_stop() -> None:
            nonlocal calls
            calls += 1
            if calls == 2:
                transport._stop.set()
            raise RuntimeError("temporary snapshot failure")

        with patch("luna_usb_transport.open_luna", side_effect=fail_then_stop):
            transport._run()

        self.assertEqual(calls, 2)
        stats = transport.diagnostics()
        self.assertFalse(stats["connected"])
        self.assertEqual(stats["errors_total"], 1)
        self.assertIn("RuntimeError", stats["last_error"])

    def test_connected_session_is_closed_after_serve_failure(self) -> None:
        transport = LunaUsbTransport(FakeAgent(), retry_seconds=0.01)
        port = FakePort()

        def fail_serve(_port: FakePort, _decoder: FrameDecoder) -> None:
            self.assertTrue(transport.diagnostics()["connected"])
            raise RuntimeError("snapshot failed")

        def stop_after_retry(_seconds: float) -> None:
            transport._stop.set()

        with patch("luna_usb_transport.open_luna", return_value=(port, FrameDecoder(),
                                                                 {"firmware": "test"})), \
             patch.object(transport, "_serve", side_effect=fail_serve), \
             patch.object(transport._stop, "wait", side_effect=stop_after_retry):
            transport._run()

        stats = transport.diagnostics()
        self.assertFalse(stats["connected"])
        self.assertEqual(stats["port"], "COM-TEST")
        self.assertEqual(stats["connections"], 1)
        self.assertEqual(stats["disconnects"], 1)
        self.assertEqual(stats["errors_total"], 1)
        self.assertTrue(port.closed)

    def test_idle_link_raises_timeout_for_reconnect(self) -> None:
        transport = LunaUsbTransport(FakeAgent(), idle_timeout_seconds=0.03)
        port = FakePort()

        def wait_for_frame(_port: FakePort, _decoder: FrameDecoder,
                           _deadline: float) -> list[Frame]:
            time.sleep(0.01)
            return []

        with patch("luna_usb_transport.read_frames", side_effect=wait_for_frame):
            with self.assertRaisesRegex(TimeoutError, "no Luna Link frames"):
                transport._serve(port, FrameDecoder())

    def test_valid_frame_resets_idle_timer(self) -> None:
        transport = LunaUsbTransport(FakeAgent(), idle_timeout_seconds=0.03)
        port = FakePort()
        frames = [Frame(MessageType.TOUCH_TEST, 1, b"touch")]

        def feed_then_stop(_port: FakePort, _decoder: FrameDecoder,
                           _deadline: float) -> list[Frame]:
            transport._stop.set()
            return frames

        with patch("luna_usb_transport.read_frames", side_effect=feed_then_stop):
            transport._serve(port, FrameDecoder())

        self.assertIsNotNone(transport.diagnostics()["last_frame_at"])


if __name__ == "__main__":
    unittest.main()
