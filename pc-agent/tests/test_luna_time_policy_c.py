import ctypes
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
class Policy(ctypes.Structure):
    _fields_=[("valid",ctypes.c_bool),("ble_seen",ctypes.c_bool),("source",ctypes.c_int),
              ("last_sync_us",ctypes.c_int64),("last_ble_us",ctypes.c_int64)]
@unittest.skipUnless(shutil.which("gcc"),"existing native gcc required")
class TimePolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix="luna-time-policy-"); dll=Path(cls.temp.name)/"policy.dll"
        subprocess.run(["gcc","-std=c11","-shared","-O2","-Wall","-Wextra","-Werror",
            str(ROOT/"firmware/luna-panel/main/luna_time_policy.c"),"-o",str(dll)],check=True,capture_output=True)
        cls.lib=ctypes.CDLL(str(dll))
        cls.lib.luna_time_epoch_valid.argtypes=[ctypes.c_int64]; cls.lib.luna_time_epoch_valid.restype=ctypes.c_bool
        cls.lib.luna_time_may_sync.argtypes=[ctypes.POINTER(Policy),ctypes.c_int,ctypes.c_int64]; cls.lib.luna_time_may_sync.restype=ctypes.c_bool
        cls.lib.luna_time_did_sync.argtypes=[ctypes.POINTER(Policy),ctypes.c_int,ctypes.c_int64]
    @classmethod
    def tearDownClass(cls):
        if sys.platform=="win32":
            ctypes.windll.kernel32.FreeLibrary.argtypes=[ctypes.c_void_p]
            ctypes.windll.kernel32.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()
    def test_boot_waits_for_real_time_not_saved_cache(self):
        p=Policy(); self.assertFalse(p.valid); self.assertEqual(p.source,0)
        self.assertTrue(self.lib.luna_time_may_sync(ctypes.byref(p),2,0))
    def test_ble_priority_including_boot_at_zero(self):
        p=Policy(); self.lib.luna_time_did_sync(ctypes.byref(p),1,0)
        self.assertFalse(self.lib.luna_time_may_sync(ctypes.byref(p),2,119999999))
        self.assertTrue(self.lib.luna_time_may_sync(ctypes.byref(p),2,120000000))
        self.assertTrue(self.lib.luna_time_may_sync(ctypes.byref(p),1,1))
    def test_ntp_fallback_then_ble_reclaims_and_extends_guard(self):
        p=Policy(); self.lib.luna_time_did_sync(ctypes.byref(p),2,1)
        self.assertEqual(p.source,2); self.assertTrue(p.valid)
        self.lib.luna_time_did_sync(ctypes.byref(p),1,100000000)
        self.assertEqual(p.source,1); self.assertFalse(self.lib.luna_time_may_sync(ctypes.byref(p),2,120000000))
        self.lib.luna_time_did_sync(ctypes.byref(p),1,160000000)
        self.assertFalse(self.lib.luna_time_may_sync(ctypes.byref(p),2,270000000))
    def test_epoch_bounds_and_unknown_sources_reject(self):
        for ms in (0,1699999999999,4102444800000,-1): self.assertFalse(self.lib.luna_time_epoch_valid(ms))
        self.assertTrue(self.lib.luna_time_epoch_valid(1790900000000))
        p=Policy()
        self.assertFalse(self.lib.luna_time_may_sync(ctypes.byref(p),0,1))
        self.assertFalse(self.lib.luna_time_may_sync(ctypes.byref(p),2,-1))
if __name__=="__main__": unittest.main()
