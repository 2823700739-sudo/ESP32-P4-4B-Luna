from __future__ import annotations

import http.client
import json
from pathlib import Path
import sys
import threading
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from luna_agent import LunaServer  # noqa: E402
from luna_link_protocol import Frame, MessageType  # noqa: E402
from luna_usb_transport import LunaUsbTransport  # noqa: E402
from test_agent_state import make_agent  # noqa: E402
from test_luna_usb_transport import FakePort, decode_write  # noqa: E402


class ActionTransportTests(unittest.TestCase):
    def setUp(self) -> None:
        self.agent, self.volume = make_agent()
        self.agent.config["token"] = "local-test-token-at-least-20-chars"
        self.usb = LunaUsbTransport(self.agent)
        self.server = LunaServer(("127.0.0.1", 0), self.agent, self.usb)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self) -> None:
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)

    def post(self, body: object) -> tuple[int, dict]:
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=2)
        try:
            connection.request("POST", "/api/v1/actions", json.dumps(body),
                               {"X-Luna-Token": self.agent.config["token"]})
            response = connection.getresponse()
            return response.status, json.loads(response.read())
        finally:
            connection.close()

    def send_usb(self, body: object) -> dict:
        port = FakePort()
        self.usb._handle_frame(port, Frame(MessageType.ACTION_REQUEST, 42,
                                          json.dumps(body).encode()))
        frame = decode_write(port)
        self.assertEqual(frame.message_type, MessageType.ACTION_RESULT)
        self.assertEqual(frame.request_id, 42)
        return json.loads(frame.payload)

    def test_usb_and_http_share_results_and_reject_conflicting_values(self) -> None:
        request = {"request_id": "shared", "action": "music.volume_set", "value": 55}
        status, first = self.post(request)
        repeat = self.send_usb(request)
        conflict = self.send_usb({**request, "value": 60})
        self.assertEqual(status, 200)
        self.assertTrue(first["ok"])
        self.assertTrue(repeat["duplicate"])
        self.assertEqual(repeat["volume"]["percent"], 55)
        self.assertEqual(conflict["error"], "request_id_conflict")
        self.assertEqual(self.volume.set_calls, 1)

    def test_uncertain_usb_result_is_replayed_as_uncertain_on_http(self) -> None:
        request = {"request_id": "unknown", "action": "music.next"}
        with patch.object(self.agent.media, "execute", side_effect=TimeoutError()) as execute, \
             patch("luna_agent.send_media_key") as key:
            first = self.send_usb(request)
            status, repeat = self.post(request)
        self.assertEqual(status, 400)
        self.assertEqual(first["error"], "action_result_unknown")
        self.assertEqual(repeat["error"], "action_result_unknown")
        self.assertTrue(repeat["duplicate"])
        execute.assert_called_once()
        key.assert_not_called()

    def test_malformed_objects_get_error_responses_on_both_transports(self) -> None:
        for body in ([], None, 42, {"request_id": None, "action": "music.play"},
                     {"request_id": "a", "action": []}):
            with self.subTest(body=body):
                status, http_result = self.post(body)
                usb_result = self.send_usb(body)
                self.assertEqual(status, 400)
                self.assertFalse(http_result["ok"])
                self.assertFalse(usb_result["ok"])
        self.assertEqual(self.agent.action_diagnostics()["executed"], 0)
