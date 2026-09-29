from __future__ import annotations

import json
import hashlib
from pathlib import Path
import struct
import sys
import unittest


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
        self.writes: list[bytes] = []

    def write(self, data: bytes) -> int:
        self.writes.append(data)
        return len(data)

    def flush(self) -> None:
        pass


def decode_write(port: FakePort) -> Frame:
    frames = FrameDecoder().feed(port.writes[-1])
    if len(frames) != 1:
        raise AssertionError("expected exactly one response frame")
    return frames[0]


class LunaUsbTransportTests(unittest.TestCase):
    def test_state_request_returns_snapshot(self) -> None:
        agent = FakeAgent()
        port = FakePort()
        transport = LunaUsbTransport(agent)

        transport._handle_frame(port, Frame(MessageType.STATE_REQUEST, 17, b""))

        response = decode_write(port)
        self.assertEqual(response.message_type, MessageType.STATE_SNAPSHOT)
        self.assertEqual(response.request_id, 17)
        self.assertEqual(json.loads(response.payload)["sequence"], 3)

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

    def test_stale_cover_id_never_returns_new_cover(self) -> None:
        port = FakePort()
        transport = LunaUsbTransport(FakeAgent())
        stale_id = b"0000000000000000"

        transport._handle_frame(port, Frame(MessageType.COVER_INFO_REQUEST, 1, stale_id))
        self.assertEqual(decode_write(port).payload, bytes(36))

        request = stale_id + struct.pack("<IH", 0, 2048)
        transport._handle_frame(port, Frame(MessageType.COVER_CHUNK_REQUEST, 2, request))
        self.assertEqual(decode_write(port).payload, b"")


if __name__ == "__main__":
    unittest.main()
