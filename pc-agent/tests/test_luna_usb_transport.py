from __future__ import annotations

import json
from pathlib import Path
import sys
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from luna_link_protocol import Frame, FrameDecoder, MessageType  # noqa: E402
from luna_usb_transport import LunaUsbTransport  # noqa: E402


class FakeAgent:
    def __init__(self) -> None:
        self.actions: list[tuple[str, str, object]] = []

    def snapshot(self) -> dict:
        return {"protocol_version": 1, "sequence": 3, "pc": {"online": True}}

    def execute(self, request_id: str, action: str, value: object = None) -> dict:
        self.actions.append((request_id, action, value))
        return {"ok": True, "action": action, "duplicate": False}


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


if __name__ == "__main__":
    unittest.main()

