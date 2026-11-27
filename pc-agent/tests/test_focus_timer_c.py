"""Real session timer C: delayed ticks, pause precision and one-shot completion."""
import ctypes as C
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
class Focus(C.Structure):
    _fields_=[('state',C.c_int),('duration_seconds',C.c_uint),
              ('remaining_us',C.c_int64),('deadline_us',C.c_int64),('notice',C.c_bool)]

@unittest.skipUnless(shutil.which('gcc'),'existing GCC required')
class FocusTimerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='luna-focus-')
        dll=Path(cls.temp.name)/'focus.dll'
        subprocess.run(['gcc','-std=c11','-shared','-O1','-Wall','-Wextra','-Werror',
            str(ROOT/'firmware/luna-panel/main/luna_focus.c'),'-o',str(dll)],check=True,capture_output=True)
        cls.lib=C.CDLL(str(dll))
        for name in ['init','end','ack']:
            getattr(cls.lib,'luna_focus_'+name).argtypes=[C.POINTER(Focus)]
        for name in ['start','pause','tick','remaining']:
            getattr(cls.lib,'luna_focus_'+name).argtypes=[C.POINTER(Focus),C.c_int64]
            getattr(cls.lib,'luna_focus_'+name).restype=C.c_uint if name=='remaining' else C.c_bool
        cls.lib.luna_focus_select.argtypes=[C.POINTER(Focus),C.c_uint]
        cls.lib.luna_focus_select.restype=C.c_bool
    @classmethod
    def tearDownClass(cls):
        if sys.platform=='win32':
            C.windll.kernel32.FreeLibrary.argtypes=[C.c_void_p];C.windll.kernel32.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()
    def setUp(self):
        self.focus=Focus();self.ptr=C.byref(self.focus);self.lib.luna_focus_init(self.ptr)
    def test_presets_and_running_duplicate_start_do_not_reset_deadline(self):
        self.assertFalse(self.lib.luna_focus_select(self.ptr,0))
        self.assertTrue(self.lib.luna_focus_select(self.ptr,60))
        self.assertTrue(self.lib.luna_focus_start(self.ptr,1000000))
        deadline=self.focus.deadline_us
        self.assertFalse(self.lib.luna_focus_start(self.ptr,5000000))
        self.assertFalse(self.lib.luna_focus_select(self.ptr,25))
        self.assertEqual(self.focus.deadline_us,deadline)
        self.assertEqual(self.lib.luna_focus_remaining(self.ptr,1000000),3600)
    def test_delayed_render_tick_counts_elapsed_time_and_finishes_once(self):
        self.lib.luna_focus_start(self.ptr,1000000)
        self.assertEqual(self.lib.luna_focus_remaining(self.ptr,125500000),1376)
        self.assertTrue(self.lib.luna_focus_tick(self.ptr,1800000000))
        self.assertEqual(self.focus.state,3);self.assertTrue(self.focus.notice)
        self.assertEqual(self.lib.luna_focus_remaining(self.ptr,1800000000),0)
        self.lib.luna_focus_ack(self.ptr)
        self.assertFalse(self.lib.luna_focus_tick(self.ptr,3000000000))
        self.assertFalse(self.focus.notice)
    def test_pause_retains_subsecond_precision_across_long_inactivity(self):
        self.lib.luna_focus_start(self.ptr,1000000)
        self.assertTrue(self.lib.luna_focus_pause(self.ptr,2250000))
        self.assertEqual(self.focus.remaining_us,1498750000)
        self.assertFalse(self.lib.luna_focus_select(self.ptr,45))
        self.assertEqual(self.lib.luna_focus_remaining(self.ptr,4000000000),1499)
        self.lib.luna_focus_start(self.ptr,4000000000)
        self.assertEqual(self.focus.deadline_us,5498750000)
        self.assertFalse(self.lib.luna_focus_tick(self.ptr,5498749999))
        self.assertTrue(self.lib.luna_focus_tick(self.ptr,5498750000))
    def test_pause_at_deadline_completes_instead_of_freezing_zero(self):
        self.lib.luna_focus_start(self.ptr,1000000)
        self.assertFalse(self.lib.luna_focus_pause(self.ptr,1501000000))
        self.assertEqual(self.focus.state,3);self.assertTrue(self.focus.notice)
        self.lib.luna_focus_start(self.ptr,2000000000)
        self.assertEqual(self.focus.state,1);self.assertFalse(self.focus.notice)
    def test_end_and_reboot_reset_without_carrying_old_deadlines(self):
        self.lib.luna_focus_select(self.ptr,45);self.lib.luna_focus_start(self.ptr,1000000)
        self.lib.luna_focus_end(self.ptr)
        self.assertEqual(self.focus.state,0);self.assertEqual(self.focus.deadline_us,0)
        self.assertEqual(self.lib.luna_focus_remaining(self.ptr,5000000000),2700)
        self.lib.luna_focus_init(self.ptr)
        self.assertEqual(self.focus.duration_seconds,1500);self.assertFalse(self.focus.notice)
    def test_custom_minute_bounds_and_long_duration_deadline(self):
        for value in [0,1000,4294967295]:
            self.assertFalse(self.lib.luna_focus_select(self.ptr,value))
            self.assertEqual(self.focus.duration_seconds,1500)
        for value in [1,40,65,999]:
            self.assertTrue(self.lib.luna_focus_select(self.ptr,value))
            self.assertEqual(self.lib.luna_focus_remaining(self.ptr,0),value*60)
        self.lib.luna_focus_start(self.ptr,1000000)
        self.assertEqual(self.focus.deadline_us,59941000000)
        self.assertFalse(self.lib.luna_focus_select(self.ptr,40))
        self.assertTrue(self.lib.luna_focus_tick(self.ptr,59941000000))
