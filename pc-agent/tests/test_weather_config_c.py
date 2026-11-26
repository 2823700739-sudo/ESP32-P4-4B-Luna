import ctypes as C
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'pc-agent'))
from luna_weather_setup import make_settings

class Config(C.Structure):
    _fields_=[('version',C.c_uint32),('setup_id',C.c_char*33),('ssid',C.c_char*33),
              ('password',C.c_char*65),('location',C.c_char*48),('latitude',C.c_double),('longitude',C.c_double)]

@unittest.skipUnless(shutil.which('gcc'),'existing GCC required')
class ConfigTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='luna-weather-setup-')
        cjson=ROOT/'firmware/luna-panel/managed_components/espressif__cjson/cJSON'
        dll=Path(cls.temp.name)/'weatherconfig.dll'
        subprocess.run(['gcc','-std=c11','-shared','-O2','-Wall','-Wextra','-Werror','-DCJSON_HIDE_SYMBOLS','-I',str(cjson),
                        str(ROOT/'firmware/luna-panel/main/luna_weather_config.c'),str(cjson/'cJSON.c'),'-o',str(dll)],check=True,capture_output=True)
        cls.lib=C.CDLL(str(dll));cls.lib.cJSON_Parse.argtypes=[C.c_char_p];cls.lib.cJSON_Parse.restype=C.c_void_p
        cls.lib.cJSON_Delete.argtypes=[C.c_void_p]
        cls.lib.luna_weather_config_parse.argtypes=[C.c_void_p,C.POINTER(Config)];cls.lib.luna_weather_config_parse.restype=C.c_bool
        cls.lib.luna_weather_config_valid.argtypes=[C.POINTER(Config)];cls.lib.luna_weather_config_valid.restype=C.c_bool
        cls.lib.luna_weather_config_clear.argtypes=[C.c_void_p,C.c_uint]
    @classmethod
    def tearDownClass(cls):
        if sys.platform=='win32':
            C.windll.kernel32.FreeLibrary.argtypes=[C.c_void_p];C.windll.kernel32.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()
    def parse(self,body):
        obj=self.lib.cJSON_Parse(json.dumps(body,ensure_ascii=False).encode());out=Config()
        try:return self.lib.luna_weather_config_parse(obj,C.byref(out)),out
        finally:self.lib.cJSON_Delete(obj)
    def test_actual_c_accepts_utf8_and_full_length_ssid_without_truncation(self):
        body=make_settings('猫'*10,'a'*64,'上海',31.2,121.5,'1'*32)
        valid,out=self.parse(body);self.assertTrue(valid);self.assertEqual(out.ssid.decode(),'猫'*10)
        valid,out=self.parse(body|{'ssid':'x'*32});self.assertTrue(valid);self.assertEqual(len(out.ssid),32)
    def test_invalid_inputs_rejected_before_settings_change(self):
        body=make_settings('Network','example-only','City',1,2,'1'*32)
        for change in ({'ssid':'猫'*11},{'password':'short'},{'password':'g'*64},{'location':'猫'*16},
                       {'latitude':True},{'longitude':181},{'setup_id':'wrong'},{'ssid':'a\nb'}, {'password':'中文密码1234'}):
            with self.subTest(change=change):self.assertFalse(self.parse(body|change)[0])
    def test_persisted_version_termination_and_explicit_memory_clear(self):
        valid,out=self.parse(make_settings('Network','example-only','City',1,2,'1'*32));self.assertTrue(valid)
        out.version=99;self.assertFalse(self.lib.luna_weather_config_valid(C.byref(out)))
        self.lib.luna_weather_config_clear(C.byref(out),C.sizeof(out))
        self.assertEqual(bytes(out),bytes(C.sizeof(out)))
