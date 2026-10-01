from __future__ import annotations

import http.client
import json
from pathlib import Path
import sys
import threading
import unittest


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from luna_agent import LunaServer  # noqa: E402


class FakeAgent:
    config = {"token": "test-token-with-at-least-20-chars"}


class FakeTransport:
    def diagnostics(self) -> dict:
        return {"connected": True, "connections": 2, "state_requests": 12}


class AgentDiagnosticsTests(unittest.TestCase):
    def test_endpoint_requires_token_and_returns_usb_counters(self) -> None:
        server = LunaServer(("127.0.0.1", 0), FakeAgent(), FakeTransport())
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            def request(headers: dict[str, str]) -> tuple[int, dict]:
                connection = http.client.HTTPConnection("127.0.0.1", server.server_port,
                                                        timeout=2)
                try:
                    connection.request("GET", "/api/v1/diagnostics", headers=headers)
                    response = connection.getresponse()
                    return response.status, json.loads(response.read())
                finally:
                    connection.close()

            status, body = request({})
            self.assertEqual(status, 401)
            self.assertEqual(body["error"], "unauthorized")

            status, body = request({"X-Luna-Token": FakeAgent.config["token"]})
            self.assertEqual(status, 200)
            self.assertEqual(body["usb"]["connections"], 2)
            self.assertEqual(body["usb"]["state_requests"], 12)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
