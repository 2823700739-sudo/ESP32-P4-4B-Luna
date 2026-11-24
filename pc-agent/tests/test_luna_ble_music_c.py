"""Compile the real B1 C core; stub time/mutex, not transport or cJSON."""
import ctypes
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOOT = "0123456789abcdef"
class State(ctypes.Structure):
    _fields_ = [(key, ctypes.c_bool) for key in (
        "online", "available", "controllable", "playing", "volume_available", "muted",
        "pending", "result_unknown", "last_ok", "has_result")] + [
        ("volume",ctypes.c_uint),("updated_us",ctypes.c_int64),
        ("title",ctypes.c_char*256),("artist",ctypes.c_char*128)]

@unittest.skipUnless(shutil.which("gcc"), "existing native gcc required")
class MusicCoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="luna-music-core-")
        native=ROOT/"pc-agent/tests/native_music"
        cjson=ROOT/"firmware/luna-panel/managed_components/espressif__cjson/cJSON"
        dll=Path(cls.temp.name)/"music.dll"
        subprocess.run(["gcc","-std=c11","-shared","-O2","-Wall","-Wextra","-Werror",
            "-DCJSON_HIDE_SYMBOLS","-Dstrlcpy=luna_test_strlcpy","-include",str(native/"runtime.h"),
            "-I",str(native),"-I",str(cjson),
            str(ROOT/"firmware/luna-panel/main/luna_ble_music.c"),str(native/"runtime.c"),
            str(cjson/"cJSON.c"),"-o",str(dll)],check=True,capture_output=True)
        cls.lib=ctypes.CDLL(str(dll))
        cls.lib.cJSON_Parse.argtypes=[ctypes.c_char_p]; cls.lib.cJSON_Parse.restype=ctypes.c_void_p
        cls.lib.cJSON_Delete.argtypes=[ctypes.c_void_p]
        cls.lib.luna_music_set_boot_id.argtypes=[ctypes.c_char_p]
        cls.lib.luna_music_enqueue.argtypes=[ctypes.c_char_p,ctypes.c_int]; cls.lib.luna_music_enqueue.restype=ctypes.c_bool
        cls.lib.luna_music_get_state.argtypes=[ctypes.POINTER(State)]; cls.lib.luna_music_get_state.restype=ctypes.c_bool
        cls.lib.luna_music_message.argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_char_p,ctypes.c_void_p,ctypes.c_size_t]
        cls.lib.luna_test_clock.argtypes=[ctypes.c_int64]
        assert cls.lib.luna_music_init()==0

    @classmethod
    def tearDownClass(cls):
        if sys.platform=="win32":
            ctypes.windll.kernel32.FreeLibrary.argtypes=[ctypes.c_void_p]
            ctypes.windll.kernel32.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()

    def setUp(self):
        self.lib.luna_music_reset_session(); self.lib.luna_music_set_boot_id(BOOT.encode()); self.clock(1000000)

    def clock(self,value): self.lib.luna_test_clock(value)
    def view(self):
        out=State(); self.assertTrue(self.lib.luna_music_get_state(ctypes.byref(out))); return out
    def enqueue(self,action,value=0): return self.lib.luna_music_enqueue(action.encode(),value)
    def message(self,kind,body):
        obj=self.lib.cJSON_Parse(json.dumps(body,ensure_ascii=False).encode()); output=ctypes.create_string_buffer(512)
        try: rc=self.lib.luna_music_message(kind.encode(),obj,b"session",output,512)
        finally: self.lib.cJSON_Delete(obj)
        return rc,json.loads(output.value) if output.value else None
    def snapshot(self,**music):
        return self.message("state_snapshot",{"music":dict({
            "available":True,"controllable":True,"playing":False,"title":"夜空","artist":"Luna"},**music),
            "volume":{"available":True,"percent":25,"muted":False}})
    def ack(self,action,ok=True,known=True):
        return self.message("action_result",{"request_id":action["request_id"],"ok":ok,"result_known":known})

    def test_offline_and_unknown_actions_never_enqueue(self):
        self.assertFalse(self.enqueue("music.play")); self.snapshot()
        self.assertFalse(self.enqueue("music.play_pause")); self.assertFalse(self.enqueue("music.volume_set",101))
        self.assertIsNone(self.snapshot()[1]["action_request"])

    def test_two_clicks_are_fifo_and_optimistic_pause_wins(self):
        self.snapshot(); self.assertTrue(self.enqueue("music.play")); self.assertTrue(self.view().playing)
        self.assertTrue(self.enqueue("music.pause")); self.assertFalse(self.view().playing)
        first=self.snapshot()[1]["action_request"]; self.assertEqual(first["action"],"music.play")
        self.ack(first); self.assertTrue(self.view().pending)
        second=self.snapshot()[1]["action_request"]; self.assertEqual(second["action"],"music.pause")
        self.ack(second); self.assertFalse(self.view().pending)
        self.assertNotEqual(first["request_id"],second["request_id"])

    def test_stale_authoritative_playing_does_not_override_newer_pause(self):
        self.snapshot(); self.enqueue("music.play"); self.enqueue("music.pause")
        first=self.snapshot()[1]["action_request"]; self.ack(first)
        second=self.snapshot(playing=True)[1]["action_request"]
        self.assertFalse(self.view().playing); self.ack(second)
        self.snapshot(playing=True); self.assertFalse(self.view().playing)
        self.clock(5000000); self.snapshot(playing=True)
        self.assertTrue(self.view().playing)  # Bounded reconciliation, no permanent fiction.

    def test_taken_action_is_never_reoffered_without_ack(self):
        self.snapshot(); self.enqueue("music.next"); first=self.snapshot()[1]["action_request"]
        self.assertIsNotNone(first); self.assertIsNone(self.snapshot()[1]["action_request"])

    def test_volume_feedback_is_not_overwritten_by_stale_snapshot(self):
        self.snapshot(); self.enqueue("music.volume_set",65)
        a=self.snapshot()[1]["action_request"]
        self.assertEqual(a["value"],65); self.assertEqual(self.view().volume,65)
        self.ack(a); self.snapshot(); self.assertEqual(self.view().volume,65)
        self.clock(5000000); self.snapshot(); self.assertEqual(self.view().volume,25)

    def test_fifo_is_bounded_at_eight(self):
        self.snapshot()
        self.assertTrue(all(self.enqueue("music.next") for _ in range(8)))
        self.assertFalse(self.enqueue("music.next"))
        ids=[self.snapshot()[1]["action_request"]["request_id"] for _ in range(8)]
        self.assertEqual(len(set(ids)),8); self.assertIsNone(self.snapshot()[1]["action_request"])

    def test_disconnect_clears_queue_and_online_but_ids_are_not_reused(self):
        self.snapshot(); self.enqueue("music.next"); first=self.snapshot()[1]["action_request"]
        self.enqueue("music.next"); self.lib.luna_music_reset_session()
        self.assertFalse(self.view().online); self.assertIsNone(self.snapshot()[1]["action_request"])
        self.enqueue("music.next"); second=self.snapshot()[1]["action_request"]
        self.assertNotEqual(first["request_id"],second["request_id"])

    def test_expired_click_is_unknown_and_never_delivered(self):
        self.snapshot(); self.enqueue("music.play"); self.clock(8000000)
        self.assertFalse(self.view().online); self.assertTrue(self.view().result_unknown)
        self.assertIsNone(self.snapshot()[1]["action_request"])

    def test_bad_schema_is_rejected_without_marking_online(self):
        for music in ({"available":1},{"title":"x"*256}):
            body={"music":{"available":True,"controllable":True,"playing":False,"title":"Song","artist":"Singer",**music},
                  "volume":{"available":True,"percent":25,"muted":False}}
            self.assertNotEqual(self.message("state_snapshot",body)[0],0)
            self.assertFalse(self.view().online)

if __name__=="__main__": unittest.main()
