"""Actual device weather parser, deterministic provider fixtures; no HTTP calls."""
import ctypes
from copy import deepcopy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
class Weather(ctypes.Structure):
    _fields_=[(k,ctypes.c_bool) for k in ("configured","available","stale","coordinates_valid")]+[
        ("latitude",ctypes.c_double),("longitude",ctypes.c_double),("temperature_c",ctypes.c_int),
        ("location",ctypes.c_char*48),("condition",ctypes.c_char*48),("observed_at",ctypes.c_char*48),
        ("summary",ctypes.c_char*128),("details",ctypes.c_char*128)]
class Metrics(ctypes.Structure):
    _fields_=[("available",ctypes.c_bool),("rain_available",ctypes.c_bool)]+[(k,ctypes.c_int) for k in (
        "apparent_c","humidity_percent","wind_decims_ms","high_c","low_c","rain_percent","weather_code")]

def forecast():
    return {"current_units":{"temperature_2m":"°C","apparent_temperature":"°C",
        "relative_humidity_2m":"%","wind_speed_10m":"m/s"},
        "daily_units":{"temperature_2m_max":"°C","temperature_2m_min":"°C","precipitation_probability_max":"%"},
        "current":{"time":"2026-10-02T15:00","temperature_2m":21.4,"apparent_temperature":20.2,
                   "relative_humidity_2m":65,"wind_speed_10m":2.7,"weather_code":2},
        "daily":{"temperature_2m_max":[25.4],"temperature_2m_min":[16.6],"precipitation_probability_max":[10]}}

@unittest.skipUnless(shutil.which("gcc"),"existing native gcc required")
class WeatherParserTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix="luna-weather-parser-")
        native=ROOT/"pc-agent/tests/native_music"; cjson=ROOT/"firmware/luna-panel/managed_components/espressif__cjson/cJSON"
        dll=Path(cls.temp.name)/"weather.dll"
        subprocess.run(["gcc","-std=c11","-shared","-O2","-Wall","-Wextra","-Werror",
            "-DCJSON_HIDE_SYMBOLS","-Dstrlcpy=luna_test_strlcpy","-include",str(native/"runtime.h"),
            "-I",str(native),"-I",str(cjson),str(ROOT/"firmware/luna-panel/main/luna_weather_parse.c"),
            str(native/"runtime.c"),str(cjson/"cJSON.c"),"-o",str(dll)],check=True,capture_output=True)
        cls.lib=ctypes.CDLL(str(dll)); cls.lib.luna_weather_parse_forecast.argtypes=[
            ctypes.c_char_p,ctypes.POINTER(Weather),ctypes.POINTER(Metrics)]
    @classmethod
    def tearDownClass(cls):
        if sys.platform=="win32":
            ctypes.windll.kernel32.FreeLibrary.argtypes=[ctypes.c_void_p]
            ctypes.windll.kernel32.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()
    def setUp(self):
        self.w=Weather(configured=True,available=True,stale=True,coordinates_valid=True,latitude=30,longitude=120,
                       temperature_c=9,location="测试地点".encode(),observed_at=b"old")
        self.m=Metrics(available=True,apparent_c=8)
    def parse(self,data):
        payload=data if isinstance(data,bytes) else json.dumps(data,ensure_ascii=False).encode()
        return self.lib.luna_weather_parse_forecast(payload,ctypes.byref(self.w),ctypes.byref(self.m))
    def rejected(self,data):
        old=(bytes(self.w),bytes(self.m)); self.assertNotEqual(self.parse(data),0)
        self.assertEqual((bytes(self.w),bytes(self.m)),old)
    def test_valid_units_readings_and_cache_abi(self):
        self.assertEqual(ctypes.sizeof(Weather),432)
        self.assertEqual(self.parse(forecast()),0); self.assertFalse(self.w.stale)
        self.assertEqual(self.w.temperature_c,21); self.assertEqual(self.w.condition.decode(),"多云")
        self.assertEqual((self.m.apparent_c,self.m.humidity_percent,self.m.wind_decims_ms),(20,65,27))
        self.assertEqual((self.m.high_c,self.m.low_c,self.m.rain_percent),(25,17,10))
        self.assertIn(b"Wind 10 km/h",self.w.details)  # Legacy text keeps km/h semantics.
    def test_null_boolean_range_and_fractional_code_reject(self):
        for key,value in (("temperature_2m",None),("temperature_2m",True),("temperature_2m",90),
                          ("relative_humidity_2m",101),("wind_speed_10m",-1),("weather_code",2.5)):
            data=forecast(); data["current"][key]=value; self.rejected(data)
    def test_wrong_missing_units_reject(self):
        for group,key,unit in (("current_units","wind_speed_10m","km/h"),("current_units","temperature_2m","°F"),
                              ("daily_units","temperature_2m_min",None)):
            data=forecast(); data[group][key]=unit; self.rejected(data)
        data=forecast(); del data["current_units"]; self.rejected(data)
    def test_daily_missing_and_reversed_range_reject(self):
        data=forecast(); data["daily"]["temperature_2m_min"]=[]; self.rejected(data)
        data=forecast(); data["daily"]["temperature_2m_min"]=[40]; self.rejected(data)
    def test_optional_rain_is_unknown_not_zero(self):
        data=forecast(); del data["daily"]["precipitation_probability_max"]
        self.assertEqual(self.parse(data),0); self.assertFalse(self.m.rain_available)
        self.assertIn(b"Rain --",self.w.details)
    def test_bad_time_json_and_trailing_garbage_reject(self):
        for time in ("today","2026/10/02T15:00","2026-10-02T15:00"+"x"*40):
            data=forecast(); data["current"]["time"]=time; self.rejected(data)
        for data in (b"[]",b"null",b"{",json.dumps(forecast()).encode()+b" garbage"):
            self.rejected(data)
    def test_negative_temperature_and_unknown_code_are_not_clear_sky(self):
        data=forecast(); data["current"].update(temperature_2m=-5.6,weather_code=4)
        self.assertEqual(self.parse(data),0); self.assertEqual(self.w.temperature_c,-6)
        self.assertEqual(self.w.condition.decode(),"天气变化")

if __name__=="__main__": unittest.main()
