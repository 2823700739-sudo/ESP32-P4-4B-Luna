"""Actual power controller with mocked SDK operations; never writes to a board."""
import ctypes as C
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]

@unittest.skipUnless(shutil.which('gcc'),'existing native gcc required')
class PowerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='luna-power-')
        dll=Path(cls.temp.name)/'power.dll';native=ROOT/'pc-agent/tests/native_power'
        subprocess.run(['gcc','-std=c11','-shared','-O2','-Wall','-Wextra','-Werror',
            '-I',str(native),'-I',str(ROOT/'pc-agent/tests/native_music'),
            str(ROOT/'firmware/luna-panel/main/luna_power.c'),str(native/'runtime.c'),'-o',str(dll)],check=True,capture_output=True)
        cls.lib=C.CDLL(str(dll));cls.lib.luna_power_set_idle.argtypes=[C.c_bool]
        cls.lib.luna_test_fail.argtypes=[C.c_int];cls.lib.luna_test_calls.restype=C.c_uint
    @classmethod
    def tearDownClass(cls):
        if sys.platform=='win32':
            C.windll.kernel32.FreeLibrary.argtypes=[C.c_void_p];C.windll.kernel32.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()
    def setUp(self):
        self.lib.luna_test_fail(0);self.assertEqual(self.lib.luna_power_init(),0)
    def assert_profile(self,mhz):
        self.assertEqual(self.lib.luna_test_max(),mhz);self.assertEqual(self.lib.luna_test_min(),mhz)
        self.assertEqual(self.lib.luna_test_sleep(),0)
    def test_boot_full_performance(self):self.assert_profile(360)
    def test_idle_and_wake(self):
        self.assertEqual(self.lib.luna_power_set_idle(True),0);self.assert_profile(360)
        self.assertEqual(self.lib.luna_power_set_idle(False),0);self.assert_profile(360)
    def test_idempotent_repeated_transitions(self):
        for idle in (False,True):
            self.lib.luna_power_set_idle(idle);count=self.lib.luna_test_calls()
            self.lib.luna_power_set_idle(idle);self.assertEqual(self.lib.luna_test_calls(),count)
    def test_entry_never_reconfigures_live_display_clock(self):
        count=self.lib.luna_test_calls();self.lib.luna_test_fail(258)
        self.assertEqual(self.lib.luna_power_set_idle(True),0);self.assert_profile(360)
        self.assertEqual(self.lib.luna_test_calls(),count)
    def test_wake_never_reconfigures_live_display_clock(self):
        self.lib.luna_power_set_idle(True);count=self.lib.luna_test_calls();self.lib.luna_test_fail(258)
        self.assertEqual(self.lib.luna_power_set_idle(False),0);self.assert_profile(360)
        self.assertEqual(self.lib.luna_test_calls(),count)
    def test_failed_init_blocks_profile_writes(self):
        self.lib.luna_test_fail(258);self.assertEqual(self.lib.luna_power_init(),258)
        count=self.lib.luna_test_calls();self.assertEqual(self.lib.luna_power_set_idle(True),259)
        self.assertEqual(self.lib.luna_test_calls(),count)
    def test_never_changes_brightness_or_radio(self):
        source=(ROOT/'firmware/luna-panel/main/luna_power.c').read_text(encoding='utf-8')
        for call in ('bsp_display_brightness_set(', 'esp_wifi_set_ps(', 'esp_deep_sleep_start(', 'esp_light_sleep_start(', 'esp_hosted_'):
            self.assertNotIn(call,source)

if __name__=='__main__':unittest.main()
