import asyncio
from contextlib import asynccontextmanager
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import luna_ble_link as link
from luna_ble_protocol import SERVICE_UUID


def arguments(**kwargs):
    return SimpleNamespace(**dict(dict(scan_seconds=1, address=None, heartbeat_seconds=.001,
                                     sync_seconds=60, run_seconds=.01), **kwargs))


class LinkTests(unittest.IsolatedAsyncioTestCase):
    async def test_dashboard_negotiation_and_music_action_priority(self):
        stop=asyncio.Event();requests=[];calls=0
        class Session:
            att_payload=253
            async def request(self,kind,**fields):
                requests.append(kind)
                if kind=='hello':return {'features':['music','dashboard']}
                if kind=='dashboard_snapshot':stop.set();return {'accepted':True}
        class Music:
            async def step(self,session):
                nonlocal calls
                calls+=1;requests.append('music_action' if calls==1 else 'music_poll')
                return calls==1
        class Dashboard:
            def snapshot(self):return {'computer':{},'quota':{},'project':{}}
        @asynccontextmanager
        async def connect(_device,**kwargs):yield Session()
        async def discover(_args):return object()
        async def wait(_stop,_seconds):pass
        with patch.object(link,'discover_device',discover),patch.object(link,'connected_session',connect),patch.object(link,'wait_stop',wait):
            await link.supervise(arguments(music_service=Music(),dashboard_service=Dashboard()),stop)
        self.assertEqual(requests,['hello','time_sync','music_action','music_poll','dashboard_snapshot'])

    async def test_local_dashboard_snapshot_failure_keeps_authenticated_link(self):
        stop = asyncio.Event()
        now = [100.0]
        requests, connections = [], []

        class Session:
            att_payload = 253
            async def request(self, kind, **fields):
                requests.append(kind)
                if kind == 'hello':
                    return {'features': ['dashboard']}
                if kind == 'dashboard_snapshot':
                    stop.set()
                    return {'accepted': True}

        class Dashboard:
            attempts = 0
            def snapshot(self):
                self.attempts += 1
                if self.attempts == 1:
                    raise OSError('local collector unavailable')
                return {'computer': {}, 'quota': {}, 'project': {}}

        dashboard = Dashboard()
        @asynccontextmanager
        async def connect(_device):
            connections.append(True)
            yield Session()
        async def discover(_args): return object()
        async def wait(_stop, _seconds): now[0] += 2
        clock = SimpleNamespace(monotonic=lambda: now[0], time=link.time.time)
        with patch.object(link, 'discover_device', discover), patch.object(link, 'connected_session', connect), \
             patch.object(link, 'wait_stop', wait), patch.object(link, 'time', clock):
            with self.assertLogs('luna.ble.link', level='WARNING') as logs:
                await link.supervise(arguments(dashboard_service=dashboard), stop)
        self.assertEqual(connections, [True])
        self.assertEqual(dashboard.attempts, 2)
        self.assertEqual(requests, ['hello', 'time_sync', 'ping', 'ping', 'dashboard_snapshot'])
        self.assertIn('Dashboard snapshot unavailable: OSError', logs.output[0])

    async def test_stays_connected_until_stop_not_after_first_ping(self):
        stop = asyncio.Event()
        requests, closed = [], []
        class Session:
            att_payload = 20
            async def request(self, kind, **fields):
                requests.append((kind, fields))
                if sum(k == "ping" for k, _ in requests) == 3: stop.set()
        @asynccontextmanager
        async def connect(_device):
            try: yield Session()
            finally: closed.append(True)
        async def discover(_args): return object()
        with patch.object(link, "discover_device", discover), patch.object(link, "connected_session", connect):
            await link.supervise(arguments(), stop)
        self.assertEqual([k for k, _ in requests], ["hello", "time_sync", "ping", "ping", "ping"])
        self.assertEqual(closed, [True])
        self.assertEqual(requests[1][1]["timezone_offset_minutes"],
                         int(link.datetime.now().astimezone().utcoffset().total_seconds() // 60))

    async def test_reconnect_has_new_handshake_no_old_request_replay(self):
        stop = asyncio.Event()
        connections, waits = [], []
        class Session:
            att_payload = 20
            def __init__(self): self.requests = []
            async def request(self, kind, **fields):
                self.requests.append(kind)
                if kind == "ping":
                    if len(connections) == 1: raise ConnectionError("lost")
                    stop.set()
        @asynccontextmanager
        async def connect(_device):
            session = Session(); connections.append(session)
            yield session
        async def discover(_args): return object()
        async def wait(event, seconds): waits.append(seconds)
        with patch.object(link, "discover_device", discover), patch.object(link, "connected_session", connect), \
             patch.object(link, "wait_stop", wait):
            await link.supervise(arguments(), stop)
        self.assertEqual(len(connections), 2)
        self.assertTrue(all(s.requests == ["hello", "time_sync", "ping"] for s in connections))
        self.assertEqual(waits[0], 2)

    async def test_discovery_failure_bounded_backoff_and_interrupt(self):
        stop = asyncio.Event(); waits = []
        async def discover(_args): raise ConnectionError("off")
        async def wait(event, seconds):
            waits.append(seconds)
            if len(waits) == 7: stop.set()
        with patch.object(link, "discover_device", discover), patch.object(link, "wait_stop", wait):
            await link.supervise(arguments(), stop)
        self.assertEqual(waits, [2, 4, 8, 16, 30, 30, 30])

    async def test_periodic_sync_remains_same_connection(self):
        stop = asyncio.Event(); kinds = []
        class Session:
            att_payload = 20
            async def request(self, kind, **fields):
                kinds.append(kind)
                if kinds.count("time_sync") == 2: stop.set()
        @asynccontextmanager
        async def connect(_device): yield Session()
        async def discover(_args): return object()
        with patch.object(link, "discover_device", discover), patch.object(link, "connected_session", connect):
            await link.supervise(arguments(sync_seconds=0), stop)
        self.assertEqual(kinds, ["hello", "time_sync", "ping", "time_sync"])

    async def test_bounded_run_cancels_and_closes_context(self):
        closed = []
        class Session:
            att_payload = 20
            async def request(self, kind, **fields): return {}
        @asynccontextmanager
        async def connect(_device):
            try: yield Session()
            finally: closed.append(True)
        async def discover(_args): return object()
        with patch.object(link, "discover_device", discover), patch.object(link, "connected_session", connect):
            await link.run(arguments(run_seconds=.02))
        self.assertEqual(closed, [True])

    async def test_multiple_devices_never_chosen_arbitrarily(self):
        adv = SimpleNamespace(local_name="Luna", service_uuids=[SERVICE_UUID])
        d1, d2 = SimpleNamespace(address="one"), SimpleNamespace(address="two")
        async def discover(**kwargs): return {"1": (d1, adv), "2": (d2, adv)}
        with patch.dict(sys.modules, {"bleak": SimpleNamespace(BleakScanner=SimpleNamespace(discover=discover))}):
            with self.assertRaises(RuntimeError): await link.discover_device(arguments())
            self.assertIs(await link.discover_device(arguments(address="TWO")), d2)


if __name__ == "__main__": unittest.main()
