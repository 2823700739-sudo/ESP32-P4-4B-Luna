import asyncio
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from luna_ble_music import MusicService, MusicSession, bounded_text, validate_action
from luna_ble_protocol import ProtocolError, Reassembler, encode_fragments

BOOT = "0123456789abcdef"
def action(name="music.play", value=None, index=1):
    return {"request_id": f"{BOOT}-{index:08x}", "action": name, "value": value}

class FakeMedia:
    def __init__(self): self.calls=[]; self.playing=False; self.raise_error=False
    def snapshot(self):
        return {"available": True, "controllable": True, "playing": self.playing,
                "title": "夜空" * 200, "artist": "Luna", "cover_id": "forbidden"}
    def execute(self, name):
        self.calls.append(name)
        if self.raise_error: raise TimeoutError("unknown action result")
        self.playing = name == "music.play"
        return True

class FakeVolume:
    def __init__(self): self.percent=25; self.muted=False; self.calls=[]
    def snapshot(self): return {"available": True, "percent": self.percent, "muted": self.muted}
    def set_percent(self, value): self.calls.append(value); self.percent=value; return self.snapshot()
    def toggle_mute(self): self.calls.append("mute"); self.muted=not self.muted; return self.snapshot()

class WireClient:
    mtu_size=256
    def __init__(self): self.receiver=Reassembler(); self.action=None; self.override={}; self.requests=[]
    async def write_gatt_char(self, uuid, data, response):
        m=self.receiver.feed(data)
        if m is None: return
        req=json.loads(m.payload); self.requests.append(req)
        reply={"v": 1, "session": req["session"], "boot_id": BOOT,
               "type": MusicSession.REQUEST_TYPES[req["type"]], "name": "Luna",
               "max_message": 4096, "att_payload": 253, "features": ["music"],
               "accepted": True, "action_request": self.action, **self.override}
        for data in encode_fragments(json.dumps(reply).encode(),m.message_id,253):
            self.session.notification(None,bytearray(data))

class ValidationTests(unittest.TestCase):
    def test_whitelist_id_and_types(self):
        self.assertEqual(validate_action(action(),BOOT),action())
        self.assertIsNone(validate_action(None,BOOT))
        for bad in ({},action("music.play_pause"),action("music.volume_set",True),
                    action("music.volume_set",101),action("music.play",1),
                    {**action(),"request_id":"old-00000001"},{**action(),"cover":True}):
            with self.assertRaises(ProtocolError): validate_action(bad,BOOT)

    def test_utf8_safe_text_bound_and_newlines(self):
        self.assertEqual(bounded_text(" a\n b\t c ",255),"a b c")
        self.assertLessEqual(len(bounded_text("猫"*200,255).encode()),255)
        self.assertEqual(bounded_text(None,255),"")

class BusinessTests(unittest.IsolatedAsyncioTestCase):
    def setUp(self):
        self.media=FakeMedia(); self.volume=FakeVolume(); self.service=MusicService(self.media,self.volume)
        self.client=WireClient(); self.session=MusicSession(self.client); self.client.session=self.session

    async def test_snapshot_has_no_cover_and_fits_bound(self):
        await self.session.request("hello"); await self.service.step(self.session)
        wire=self.client.requests[-1]
        self.assertNotIn("cover",json.dumps(wire))
        self.assertLess(len(json.dumps(wire,ensure_ascii=False).encode()),4096)
        self.assertEqual(self.media.calls,[])

    async def test_two_rapid_commands_keep_explicit_order(self):
        await self.session.request("hello")
        for cmd in (action("music.play"),action("music.pause",index=2)):
            self.client.action=cmd; await self.service.step(self.session)
        self.assertEqual(self.media.calls,["music.play","music.pause"])
        self.assertFalse(self.media.playing)
        self.assertEqual([r["type"] for r in self.client.requests],
                         ["hello","state_snapshot","action_result","state_snapshot","action_result"])

    async def test_duplicate_known_unknown_and_conflict_never_reexecute(self):
        first=await self.service.execute(action()); self.assertTrue(first["ok"])
        await self.service.execute(action()); self.assertEqual(len(self.media.calls),1)
        conflict=await self.service.execute(action("music.pause")); self.assertFalse(conflict["ok"])
        self.media.raise_error=True
        first=await self.service.execute(action(index=2)); self.assertFalse(first["result_known"])
        await self.service.execute(action(index=2)); self.assertEqual(len(self.media.calls),2)

    async def test_volume_and_mute_use_only_coreaudio(self):
        await self.service.execute(action("music.volume_set",50))
        await self.service.execute(action("music.mute",index=2))
        self.assertEqual(self.volume.calls,[50,"mute"]); self.assertEqual(self.media.calls,[])

    async def test_b0_capability_is_rejected(self):
        self.client.override={"features":["diagnostic"]}
        with self.assertRaises(ProtocolError): await self.session.request("hello")
        self.assertTrue(self.session.failed)

    async def test_old_boot_and_bad_action_fail_before_execution(self):
        for bad in ({"boot_id":"other"},{"action_request":action("music.play_pause")}):
            self.setUp(); await self.session.request("hello"); self.client.override=bad
            with self.assertRaises(ProtocolError): await self.service.step(self.session)
            self.assertEqual(self.media.calls,[])

    async def test_ack_failure_is_terminal_not_replay(self):
        await self.session.request("hello"); self.client.action=action()
        original=self.client.write_gatt_char
        async def fail_ack(uuid,data,response):
            # Raise before second business message begins, after operation.
            if len(self.client.requests)>=2: raise ConnectionError("ack lost")
            await original(uuid,data,response)
        self.client.write_gatt_char=fail_ack
        with self.assertRaises(ConnectionError): await self.service.step(self.session)
        with self.assertRaises(ConnectionError): await self.service.step(self.session)
        self.assertEqual(self.media.calls,["music.play"])

    async def test_read_only_dashboard_ack_requires_same_boot_and_acceptance(self):
        await self.session.request('hello')
        result=await self.session.request('dashboard_snapshot',computer={},quota={},project={})
        self.assertTrue(result['accepted']);self.assertEqual(self.media.calls,[])
        for bad in ({'boot_id':'fedcba9876543210'},{'accepted':False}):
            self.setUp();await self.session.request('hello');self.client.override=bad
            with self.assertRaises(ProtocolError):await self.session.request('dashboard_snapshot')
            self.assertEqual(self.media.calls,[])

if __name__ == "__main__": unittest.main()
