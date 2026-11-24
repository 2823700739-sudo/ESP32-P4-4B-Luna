"""Real strict C dashboard parser + cJSON, deterministic mutex/time stubs."""
import copy
import ctypes as C
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
ROOT=Path(__file__).resolve().parents[2]

class State(C.Structure):
    _fields_=[(k,C.c_bool) for k in ('online','computer_fresh','quota_fresh')]+[(k,C.c_double) for k in
        ('cpu','gpu','cpu_temp','gpu_temp','ram_used_gb','ram_total_gb','vram_used_gb','vram_total_gb')]+[
        ('primary_remaining',C.c_int),('weekly_remaining',C.c_int),('received_us',C.c_int64),('quota_sampled_ms',C.c_int64),
        ('computer_age_ms',C.c_uint),('gpu_name',C.c_char*128),('project_name',C.c_char*192)]

@unittest.skipUnless(shutil.which('gcc'),'existing native gcc required')
class DashboardCoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='luna-dashboard-')
        native=ROOT/'pc-agent/tests/native_music';cjson=ROOT/'firmware/luna-panel/managed_components/espressif__cjson/cJSON'
        dll=Path(cls.temp.name)/'dashboard.dll'
        subprocess.run(['gcc','-std=c11','-shared','-O2','-Wall','-Wextra','-Werror','-DCJSON_HIDE_SYMBOLS',
            '-Dstrlcpy=luna_test_strlcpy','-include',str(native/'runtime.h'),'-I',str(native),'-I',str(cjson),
            str(ROOT/'firmware/luna-panel/main/luna_dashboard.c'),str(native/'runtime.c'),str(cjson/'cJSON.c'),'-o',str(dll)],check=True,capture_output=True)
        cls.lib=C.CDLL(str(dll));cls.lib.cJSON_Parse.argtypes=[C.c_char_p];cls.lib.cJSON_Parse.restype=C.c_void_p
        cls.lib.cJSON_Delete.argtypes=[C.c_void_p]
        cls.lib.luna_dashboard_message.argtypes=[C.c_void_p,C.c_char_p,C.c_char_p,C.c_void_p,C.c_size_t]
        cls.lib.luna_dashboard_snapshot.argtypes=[C.POINTER(State)];cls.lib.luna_dashboard_snapshot.restype=C.c_bool
        cls.lib.luna_test_clock.argtypes=[C.c_int64]
        assert cls.lib.luna_dashboard_init()==0
    @classmethod
    def tearDownClass(cls):
        if sys.platform=='win32':
            C.windll.kernel32.FreeLibrary.argtypes=[C.c_void_p];C.windll.kernel32.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()
    def setUp(self):
        self.lib.luna_test_clock(1000000)
        self.body={'computer':{'cpu':37,'gpu':24,'cpu_temp':None,'gpu_temp':None,'ram_used_gb':11.1,'ram_total_gb':15.8,
            'vram_used_gb':1.3,'vram_total_gb':8,'name':'AMD Radeon RX 6600','sample_age_ms':0},
            'quota':{'primary_remaining':44,'weekly_remaining':60,'sampled_at_ms':int(time.time()*1000)},'project':{'name':'Luna'}}
    def send(self,body):
        obj=self.lib.cJSON_Parse(json.dumps(body,ensure_ascii=False).encode());reply=C.create_string_buffer(512)
        try:rc=self.lib.luna_dashboard_message(obj,b'session',b'0123456789abcdef',reply,512)
        finally:self.lib.cJSON_Delete(obj)
        return rc,json.loads(reply.value) if reply.value else None
    def view(self):
        s=State();assert self.lib.luna_dashboard_snapshot(C.byref(s));return s
    def test_actual_data_ack_and_nullable_sensor(self):
        rc,reply=self.send(self.body);self.assertEqual(rc,0);self.assertTrue(reply['accepted'])
        s=self.view();self.assertTrue(s.computer_fresh and s.quota_fresh);self.assertEqual(s.cpu_temp,-1)
        self.assertEqual((s.primary_remaining,s.weekly_remaining),(44,60))
    def test_each_missing_window_is_independent(self):
        self.body['quota']['primary_remaining']=None;self.send(self.body)
        self.assertEqual((self.view().primary_remaining,self.view().weekly_remaining),(-1,60))
    def test_bad_snapshot_is_atomic(self):
        self.send(self.body)
        for path,value in [(('computer','cpu'),True),(('computer','gpu'),101),(('computer','ram_total_gb'),0),
            (('computer','vram_used_gb'),9),
            (('quota','primary_remaining'),1.5),(('project','name'),'猫'*100),(('computer','sample_age_ms'),None)]:
            body=copy.deepcopy(self.body);body[path[0]][path[1]]=value
            self.assertNotEqual(self.send(body)[0],0);self.assertEqual(self.view().cpu,37)
    def test_disconnected_and_stale_data_is_retained_but_marked(self):
        self.send(self.body);self.lib.luna_test_clock(7000000);s=self.view()
        self.assertFalse(s.online or s.computer_fresh or s.quota_fresh);self.assertEqual(s.gpu,24)
        self.lib.luna_dashboard_disconnect();self.assertFalse(self.view().online)
