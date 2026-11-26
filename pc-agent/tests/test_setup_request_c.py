"""Real C request parser/origin policy; this does not exercise an ESP HTTP socket."""
import ctypes as C
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

@unittest.skipUnless(shutil.which('gcc'),'existing GCC required')
class SetupRequestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='luna-setup-request-');folder=Path(cls.temp.name)
        main=ROOT/'firmware/luna-panel/main';cjson=ROOT/'firmware/luna-panel/managed_components/espressif__cjson/cJSON'
        dll=folder/'request.dll'
        result=subprocess.run(['gcc','-std=c11','-shared','-O1','-Wall','-Wextra','-Werror','-DCJSON_HIDE_SYMBOLS',
            '-I',str(main),'-I',str(cjson),str(main/'luna_setup_request.c'),str(main/'luna_weather_config.c'),
            str(cjson/'cJSON.c'),'-o',str(dll)],capture_output=True)
        if result.returncode:raise AssertionError(result.stderr.decode(errors='replace'))
        cls.lib=C.CDLL(str(dll))
        cls.lib.luna_setup_request_allowed.argtypes=[C.c_char_p]*4;cls.lib.luna_setup_request_allowed.restype=C.c_bool
        cls.lib.luna_setup_request_parse.argtypes=[C.c_char_p,C.c_size_t];cls.lib.luna_setup_request_parse.restype=C.c_void_p
        cls.lib.luna_setup_request_dispose.argtypes=[C.c_void_p]
    @classmethod
    def tearDownClass(cls):
        if sys.platform=='win32':
            C.windll.kernel32.FreeLibrary.argtypes=[C.c_void_p];C.windll.kernel32.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()
    def parsed(self,data):
        pointer=self.lib.luna_setup_request_parse(data,len(data))
        if pointer:self.lib.luna_setup_request_dispose(pointer)
        return bool(pointer)
    def test_local_host_and_session_token_required_for_api(self):
        allowed=self.lib.luna_setup_request_allowed
        self.assertTrue(allowed(b'192.168.4.1',b'http://192.168.4.1',b'token',b'token'))
        self.assertTrue(allowed(b'192.168.4.1:80',None,b'token',b'token'))
        for host,origin,provided in [(b'evil.example',None,b'token'),(b'192.168.4.1',b'https://evil.example',b'token'),
                                     (b'192.168.4.1',b'null',b'token'),(b'192.168.4.1',None,b'wrong'),(None,None,b'token')]:
            self.assertFalse(allowed(host,origin,provided,b'token'))
        self.assertFalse(allowed(b'192.168.4.1',None,b'',b''))
    def test_flat_utf8_credentials_parse_but_nested_or_duplicate_fields_do_not(self):
        self.assertTrue(self.parsed('{"ssid":"示例网络","password":"example-only"}'.encode()))
        self.assertTrue(self.parsed(b'{"ssid":"braces { and [ inside a string","latitude":0}'))
        for data in [b'{"ssid":"a","ssid":"b"}',b'{"x":{}}',b'{"x":[]}',b'{"x":true}',b'[]',
                     b'{"x":1} garbage',b'{"x":"a\\u0000b"}',b'{"x":"a\0b"}',b'{"x":1,}']:
            self.assertFalse(self.parsed(data),data)
    def test_body_limit_and_deep_nesting_rejected_before_recursive_parser(self):
        self.assertFalse(self.parsed(b'{"x":"'+b'a'*800+b'"}'))
        self.assertFalse(self.parsed(b'{"x":'*120+b'0'+b'}'*120))
        self.assertFalse(self.parsed(b'{"a":1,"b":2,"c":3,"d":4,"e":5,"f":6,"g":7,"h":8,"i":9}'))
        self.assertFalse(self.parsed(b''))

if __name__=='__main__':unittest.main()
