import asyncio
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from luna_ble_protocol import SERVICE_UUID, RX_UUID, ProtocolError, Reassembler, encode_fragments
from luna_ble_probe import ProbeSession, is_luna, run, wait_security_ready, connected_session


class FakeClient:
    def __init__(self):
        self.rx = Reassembler(); self.session = None; self.writes = []; self.requests = []
        self.drop = False; self.hang = False; self.boot_id = "boot1"; self.accept_time = True
        self.server_cap = 512; self.agreed = 20; self.reply_override = {}

    async def write_gatt_char(self, uuid, packet, response):
        self.writes.append((uuid, packet, response))
        if self.hang: await asyncio.Event().wait()
        message = self.rx.feed(packet)
        if not message: return
        request = json.loads(message.payload); self.requests.append(request)
        if self.drop: return
        if request["type"] == "hello": self.agreed = min(request["att_payload"], self.server_cap)
        result = {"v": 1, "type": {"hello": "hello", "ping": "pong", "time_sync": "time_sync_result", "diagnostic": "diagnostic", "time_status": "time_status"}[request['type']],
                  "session": request['session'], "name": "Luna", "boot_id": self.boot_id,
                  "max_message": 4096, "accepted": self.accept_time, "att_payload": self.agreed,
                  "internal_free_bytes": 10000, "internal_min_bytes": 9000,
                  "host_stack_free_bytes": 2000, "tx_stack_free_bytes": 1000,
                  "valid": True, "source": "ntp", "last_sync_age_ms": 1234,
                  "epoch_ms": 1790880000000, **self.reply_override}
        for p in encode_fragments(json.dumps(result).encode(), message.message_id, self.agreed):
            self.session.notification(None, bytearray(p))


class DiscoveryTests(unittest.TestCase):
    def test_name_and_service_both_required(self):
        self.assertTrue(is_luna(SimpleNamespace(local_name="Luna", service_uuids=[SERVICE_UUID.upper()])))
        self.assertFalse(is_luna(SimpleNamespace(local_name="Luna", service_uuids=[])))
        self.assertFalse(is_luna(SimpleNamespace(local_name="Luna-test", service_uuids=[SERVICE_UUID])))


class SessionTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.client = FakeClient(); self.session = ProbeSession(self.client, request_timeout=.05)
        self.client.session = self.session

    async def test_hello_ping_time_sync_and_mtu23(self):
        await self.session.request("hello")
        await self.session.request("ping")
        await self.session.request("time_sync", epoch_ms=1790880000000, timezone_offset_minutes=480)
        self.assertEqual(len(self.client.requests), 3)
        self.assertTrue(all(uuid == RX_UUID and response and len(packet) <= 20
                            for uuid, packet, response in self.client.writes))
        self.assertFalse(self.session.pending)

    async def test_timeout_does_not_replay_or_allow_new_request(self):
        self.client.drop = True
        with self.assertRaises(asyncio.TimeoutError): await self.session.request("hello")
        self.assertEqual(len(self.client.requests), 1)
        with self.assertRaises(ConnectionError): await self.session.request("hello")
        self.assertFalse(self.session.pending)

    async def test_write_hang_is_also_bounded(self):
        self.client.hang = True
        with self.assertRaises(asyncio.TimeoutError): await self.session.request("hello")
        self.assertEqual(len(self.client.writes), 1)
        self.assertTrue(self.session.failed)

    async def test_new_boot_requires_handshake(self):
        await self.session.request("hello"); self.client.boot_id = "boot2"
        with self.assertRaises(ValueError): await self.session.request("ping")

    async def test_rejected_time_is_not_success(self):
        await self.session.request("hello"); self.client.accept_time = False
        with self.assertRaises(ValueError): await self.session.request("time_sync", epoch_ms=1790880000000)

    async def test_time_status_accepts_real_sources_and_waiting(self):
        await self.session.request("hello")
        for fields in ({"source": "ntp"}, {"source": "ble"},
                       {"valid": False, "source": "waiting", "last_sync_age_ms": -1, "epoch_ms": 0}):
            self.client.reply_override = fields
            status = await self.session.request("time_status")
            self.assertEqual(status["source"], fields["source"])

    async def test_time_status_rejects_malformed_or_inconsistent_fields(self):
        for fields in ({"valid": 1}, {"source": "cache"}, {"last_sync_age_ms": True},
                       {"epoch_ms": True}, {"last_sync_age_ms": -1}, {"epoch_ms": 0},
                       {"valid": False}, {"source": "waiting"},
                       {"valid": False, "source": "waiting", "last_sync_age_ms": 0}):
            with self.subTest(fields=fields):
                self.setUp()
                await self.session.request("hello")
                self.client.reply_override = fields
                with self.assertRaises(ValueError): await self.session.request("time_status")
                self.assertTrue(self.session.failed)

    async def test_time_status_rejects_boot_change(self):
        await self.session.request("hello")
        self.client.boot_id = "boot2"
        with self.assertRaises(ValueError): await self.session.request("time_status")

    async def test_business_actions_and_reserved_fields_never_send(self):
        for kind, fields in (("music.play", {}), ("hello", {"session": "wrong"}), ("hello", {"v": 2})):
            with self.assertRaises(ValueError): await self.session.request(kind, **fields)
        self.assertFalse(self.client.writes)

    async def test_wrong_session_reply_is_rejected(self):
        self.client.drop = True
        task = asyncio.create_task(self.session.request("hello"))
        await asyncio.sleep(.005)
        for p in encode_fragments(b'{"v":1,"session":"wrong","type":"hello"}', 1):
            self.session.notification(None, bytearray(p))
        with self.assertRaises(ValueError): await task

    async def test_disconnect_cancels_pending_and_clears_assembly(self):
        self.client.drop = True
        task = asyncio.create_task(self.session.request("hello"))
        await asyncio.sleep(.005)
        self.session.disconnected()
        with self.assertRaises(ConnectionError): await task
        self.assertFalse(self.session.pending)

    async def test_real_entry_disables_auto_pairing(self):
        options = {}
        class Client(FakeClient):
            def __init__(self, device, **kwargs):
                super().__init__(); options.update(kwargs)
                self.services = SimpleNamespace(get_characteristic=lambda _uuid: object())
                self.mtu_size = 23; self.is_connected = True
            async def __aenter__(self): return self
            async def __aexit__(self, *args): self.is_connected = False
            async def start_notify(self, uuid, callback):
                self.session = SimpleNamespace(notification=callback)
            async def stop_notify(self, uuid): pass
            async def read_gatt_char(self, uuid, **kwargs): return b"\x01"
        async def discover(**kwargs):
            return {"one": (object(), SimpleNamespace(local_name="Luna", service_uuids=[SERVICE_UUID]))}
        fake_bleak = SimpleNamespace(BleakClient=Client, BleakScanner=SimpleNamespace(discover=discover))
        args = SimpleNamespace(scan_seconds=1, address=None, connect=True, pings=1, time_sync=False)
        with patch.dict(sys.modules, {"bleak": fake_bleak}):
            self.assertEqual(await run(args), 0)
        self.assertIs(options["pair"], False)
        self.assertIs(options["winrt"]["use_cached_services"], False)

    async def test_security_wait_is_uncached_and_precedes_messages(self):
        calls = []
        class Client:
            is_connected = True
            async def read_gatt_char(self, uuid, **kwargs):
                calls.append(kwargs)
                return b"\x00" if len(calls) == 1 else b"\x01"
        await wait_security_ready(Client(), 1)
        self.assertEqual(calls, [{"use_cached": False}, {"use_cached": False}])

    async def test_security_wait_timeout_and_disconnect(self):
        class Client:
            is_connected = True
            async def read_gatt_char(self, uuid, **kwargs): return b"\x00"
        client = Client()
        with self.assertRaises(asyncio.TimeoutError): await wait_security_ready(client, .01)
        client.is_connected = False
        with self.assertRaises(ConnectionError): await wait_security_ready(client, .01)

    async def test_invalid_security_readiness_is_not_ready(self):
        class Client:
            is_connected = True
            async def read_gatt_char(self, uuid, **kwargs): return b"\x02"
        with self.assertRaises(ProtocolError): await wait_security_ready(Client(), .01)

    async def test_large_mtu_negotiates_after_small_hello(self):
        self.client.mtu_size = 256
        self.session = ProbeSession(self.client); self.client.session = self.session
        await self.session.request("hello")
        self.assertTrue(all(len(p) <= 20 for _, p, _ in self.client.writes))
        self.assertEqual(self.session.att_payload, 253)
        self.client.writes.clear()
        await self.session.request("ping")
        self.assertEqual(len(self.client.writes), 1)

    async def test_forced_cap_and_legacy_peer_stay_small(self):
        for cap, server_cap in ((20, 512), (512, 20)):
            self.client = FakeClient(); self.client.mtu_size = 256; self.client.server_cap = server_cap
            self.session = ProbeSession(self.client, att_payload_cap=cap); self.client.session = self.session
            await self.session.request("hello"); await self.session.request("ping", padding="p" * 300)
            self.assertEqual(self.session.att_payload, 20)
            self.assertTrue(all(len(p) <= 20 for _, p, _ in self.client.writes))

    async def test_bad_negotiation_never_enables_large_writes(self):
        for limit in (19, 254, 513, True, 20.0, "20", None):
            self.client = FakeClient(); self.client.mtu_size = 256
            self.client.reply_override = {"att_payload": limit}
            self.session = ProbeSession(self.client); self.client.session = self.session
            with self.assertRaises(ProtocolError): await self.session.request("hello")
            self.assertTrue(self.session.failed)
            self.assertEqual(self.session.att_payload, 20)

    async def test_large_synthetic_ping_and_valid_diagnostics(self):
        self.client.mtu_size = 256
        self.session = ProbeSession(self.client); self.client.session = self.session
        await self.session.request("hello")
        await self.session.request("ping", padding="p" * 3800)
        result = await self.session.request("diagnostic")
        self.assertEqual(result["host_stack_free_bytes"], 2000)
        self.assertEqual(self.client.requests[1]["padding"], "p" * 3800)
        self.assertTrue(all(len(p) <= 253 for _, p, _ in self.client.writes))

    async def test_invalid_diagnostic_value_is_not_success(self):
        await self.session.request("hello")
        self.client.reply_override = {"host_stack_free_bytes": True}
        with self.assertRaises(ProtocolError): await self.session.request("diagnostic")

    async def test_auth_timeout_closes_client_without_notify_or_write(self):
        calls = []
        class Client:
            is_connected = True
            services = SimpleNamespace(get_characteristic=lambda _u: object())
            def __init__(self, *args, **kwargs): pass
            async def __aenter__(self): return self
            async def __aexit__(self, *args): calls.append("close")
            async def read_gatt_char(self, *args, **kwargs): return b"\x00"
            async def start_notify(self, *args): calls.append("notify")
        with patch.dict(sys.modules, {"bleak": SimpleNamespace(BleakClient=Client)}):
            with self.assertRaises(asyncio.TimeoutError):
                async with connected_session(object(), auth_timeout=.01): calls.append("write")
        self.assertEqual(calls, ["close"])

    async def test_security_read_hang_is_bounded(self):
        class Client:
            is_connected = True
            async def read_gatt_char(self, *args, **kwargs): await asyncio.Event().wait()
        with self.assertRaises(asyncio.TimeoutError): await wait_security_ready(Client(), .01)


if __name__ == "__main__": unittest.main()
